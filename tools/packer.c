#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb/stb_rect_pack.h"

#define QOI_IMPLEMENTATION
#include "qoi/qoi.h"

#include "lua/lauxlib.h"
#include "lua/lualib.h"

#define LDBG_IMPLEMENTATION
#include "ldbg.h"


#define ATLAS_SIZE 256

typedef struct {uint8_t r, g, b, a;} RGBA8;

typedef struct {
	int w, h;
	RGBA8* px;
} Image;

typedef struct {
	int x, y, w, h;
} Frame;

static void blit(Image dst, int dx, int dy, Image src, Frame frame){
	RGBA8* dst_px = dst.px + dx + dy*dst.w;
	RGBA8* src_px = src.px + frame.x + frame.y*src.w;
	for(int i = 0; i < frame.h; i++){
		memcpy(dst_px, src_px, frame.w*sizeof(RGBA8));
		dst_px += dst.w; src_px += src.w;
	}
}

typedef struct {
	Image albedo, normal;
} Input;

static void write_image(const char* name_format, Image img, int atlas_index){
	char filename[1024];
	
	// snprintf(filename, sizeof(filename), "resources/gfx/ATLAS%d.%s", atlas_index, "png");
	// stbi_write_png(filename, img.w, img.h, 4, img.px, 0);
	
	RGBA8 flip[img.w*img.h];
	for(int i = 0; i < img.h; i++){
		memcpy(flip + (img.h - i - 1)*img.w, img.px + i*img.w, sizeof(RGBA8)*img.w);
	}
	
	snprintf(filename, sizeof(filename), name_format, atlas_index, "qoi");
	if(!qoi_write(filename, flip, img.w, img.h, 4)){
		fprintf(stderr, "PACKING ERROR: '%s' could not be written.\n", filename);
		abort();
	}
}

static int gen_atlases(lua_State *lua){
	const int ARG_IMAGES = 1;
	int input_count = luaL_len(lua, ARG_IMAGES);
	Image input_albedo[input_count + 1];
	Image input_normal[input_count + 1];
	
	for(int input_idx = 1; input_idx <= input_count; input_idx++){
		lua_geti(lua, ARG_IMAGES, input_idx);
		const char* input_name = lua_tostring(lua, -1);
		lua_pop(lua, 1);
		
		char filename[1024];
		int w, h;
		
		{ // Read in albedo image.
			snprintf(filename, sizeof(filename), "%s.png", input_name);
			Image img = {.px = (RGBA8*)stbi_load(filename, &img.w, &img.h, NULL, 4)};
			input_albedo[input_idx] = img;
			if(img.px == NULL){
				fprintf(stderr, "PACKING ERROR: '%s' not found.\n", filename);
				abort();
			} else {
				// printf("loaded '%s'\n", filename);
			}
			w = img.w, h = img.h;
			
			// Premultiply
			for(int i = 0; i < img.w*img.h; i++){
				img.px[i].r = img.px[i].r*img.px[i].a/255;
				img.px[i].g = img.px[i].g*img.px[i].a/255;
				img.px[i].b = img.px[i].b*img.px[i].a/255;
			}
		}
		
		{ // Read in normals/glow.
			snprintf(filename, sizeof(filename), "%s_n.png", input_name);
			Image img = {.px = (RGBA8*)stbi_load(filename, &img.w, &img.h, NULL, 4)};
			input_normal[input_idx] = img;
			if(img.px == NULL){
				fprintf(stderr, "PACKING ERROR: '%s' not found.\n", filename);
				abort();
			} else {
				// printf("loaded '%s'\n", filename);
			}
			
			snprintf(filename, sizeof(filename), "%s_e.png", input_name);
			Image glow = {.px = (RGBA8*)stbi_load(filename, &glow.w, &glow.h, NULL, 4)};
			if(glow.px == NULL){
				fprintf(stderr, "PACKING ERROR: '%s' not found.\n", filename);
				abort();
			} else {
				// printf("loaded '%s'\n", filename);
			}
			
			if(img.w != w || img.h != h || glow.w != w || glow.h != h){
				fprintf(stderr, "PACKING ERROR: '%s' size does not match.\n", input_name);
				abort();
			}
			// Convert to gradient + glow
			for(int i = 0; i < img.w*img.h; i++){
				float nx = 2*img.px[i].r/255.0 - 1;
				float ny = 2*img.px[i].g/255.0 - 1;
				float nz = 2*img.px[i].b/255.0 - 1;
				img.px[i].r = 255*fmaxf(0, fminf(0.5 - 0.5*nx/nz, 1));
				img.px[i].g = 255*fmaxf(0, fminf(0.5 - 0.5*ny/nz, 1));
				img.px[i].b = glow.px[i].b;
				img.px[i].a = 255;
			}
		}
	}
	
	const int ARG_FRAMES = 2;
	int rect_count = luaL_len(lua, ARG_FRAMES);
	stbrp_rect rects[rect_count + 1];
	
	int pack_count = 0;
	for(int rect_idx = 1; rect_idx <= rect_count; rect_idx++){
		lua_geti(lua, ARG_FRAMES, rect_idx);
		int frame = lua_gettop(lua);
		lua_getfield(lua, frame, "pack");
		bool pack = lua_toboolean(lua, -1);
		lua_getfield(lua, frame, "w");
		int w = lua_tointeger(lua, -1);
		lua_getfield(lua, frame, "h");
		int h = lua_tointeger(lua, -1);
		if(pack) rects[++pack_count] = (stbrp_rect){.id = rect_idx, .w = w, .h = h};
		lua_pop(lua, 4);
	}
	
	int atlas_index = 0;
	while(pack_count){
		RGBA8 albedo_px[ATLAS_SIZE*ATLAS_SIZE] = {}, normal_px[ATLAS_SIZE*ATLAS_SIZE] = {};
		Image albedo = {.w = ATLAS_SIZE, .h = ATLAS_SIZE, .px = albedo_px};
		Image normal = {.w = ATLAS_SIZE, .h = ATLAS_SIZE, .px = normal_px};
		for(int i = 0; i < ATLAS_SIZE*ATLAS_SIZE; i++) normal.px[i] = (RGBA8){128, 128, 0, 255};
		
		stbrp_context rpack; stbrp_node nodes[ATLAS_SIZE];
		stbrp_init_target(&rpack, ATLAS_SIZE, ATLAS_SIZE, nodes, ATLAS_SIZE);
		stbrp_pack_rects(&rpack, rects + 1, pack_count);
		
		int packed = 0;
		stbrp_rect* cursor = rects + 1;
		for(int i = 1; i <= pack_count; i++){
			stbrp_rect r = rects[i];
			*cursor = r;
			
			if(r.was_packed){
				lua_geti(lua, ARG_FRAMES, r.id);
				int frame = lua_gettop(lua);
				lua_getfield(lua, frame, "x");
				int sx = lua_tointeger(lua, -1);
				lua_getfield(lua, frame, "y");
				int sy = lua_tointeger(lua, -1);
				lua_getfield(lua, frame, "image_idx");
				int image_idx = lua_tointeger(lua, -1);
				// push updated rect origin
				lua_pushinteger(lua, r.x);
				lua_setfield(lua, frame, "x");
				lua_pushinteger(lua, r.y);
				lua_setfield(lua, frame, "y");
				lua_pushfstring(lua, "ATLAS%d", atlas_index);
				lua_setfield(lua, frame, "atlas");
				// cleanup
				lua_pop(lua, 4);
				
				blit(albedo, r.x, r.y, input_albedo[image_idx], (Frame){sx, sy, r.w, r.h});
				blit(normal, r.x, r.y, input_normal[image_idx], (Frame){sx, sy, r.w, r.h});
				packed++;
			} else {
				cursor++;
			}
		}
		
		// printf("packed %d rects into atlas %d\n", packed, atlas_index);
		pack_count -= packed;
		packed = 0;
		
		write_image("resources/gfx/ATLAS%d.%s", albedo, atlas_index);
		write_image("resources/gfx/ATLAS%d_FX.%s", normal, atlas_index);
		// char filename[1024];
		// { // Write albedo
		// 	snprintf(filename, sizeof(filename), "resources/gfx/ATLAS%d.qoi", atlas_index);
		// 	// stbi_write_png(filename, albedo.w, albedo.h, 4, albedo.px, 0);
		// 	qoi_write(filename, albedo.px, albedo.w, albedo.h, 4);
		// }
		// { // Write normal
		// 	snprintf(filename, sizeof(filename), "resources/gfx/ATLAS%d_FX.qoi", atlas_index);
		// 	// stbi_write_png(filename, normal.w, normal.h, 4, normal.px, 0);
		// 	qoi_write(filename, normal.px, normal.w, normal.h, 4);
		// }
		
		// TODO push onto return list
		// lua_pushfstring(lua, "ATLAS%d", atlas_index);
		atlas_index++;
	}
	
	return 0;
}

int main(int argc, const char* argv[]){
	lua_State *lua = luaL_newstate();
	luaL_openlibs(lua);
	// dbg_setup(lua, "debugger", "dbg", NULL, NULL);

	lua_createtable(lua, argc, 0);
	for(int i = 1; i < argc; i++){
		lua_pushstring(lua, argv[i]);
		lua_rawseti(lua, -2, i);
	}
  lua_setglobal(lua, "arg");
	
	lua_pushcfunction(lua, gen_atlases);
  lua_setglobal(lua, "gen_atlases");
	
	// char filename[1024];
	// snprintf(filename, sizeof(filename), "%s/tools/packer.lua", getenv("VPATH"));
	const char* filename = "packer.lua";
	
	if(luaL_loadfile(lua, filename) || lua_pcall(lua, 0, LUA_MULTRET, 0)){
		fprintf(stderr, "Lua Error: %s\n", lua_tostring(lua, -1));
		abort();
	}
	
	return EXIT_SUCCESS;
}
