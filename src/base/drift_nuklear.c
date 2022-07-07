#include <limits.h>

#include "tina/tina_jobs.h"
#include "SDL.h"

#include "drift_types.h"
#include "drift_util.h"
#include "drift_math.h"
#include "drift_mem.h"
#include "drift_gfx.h"
#include "drift_app.h"
#include "drift_sprite.h"
#include "drift_draw.h"

#define NK_IMPLEMENTATION
#include "drift_nuklear.h"

typedef struct {
	DriftVec2 position, uv;
	DriftRGBA8 color;
} DriftNuklearVertex;

static const uint font_scale = 1;
static const uint font_w = 5, font_h = 10, font_baseline = 2;
static const uint glyph_w = 8, glyph_h = 16;

float text_width(nk_handle handle, float height, const char *text, int len){
	return font_scale*len*(font_w + 1);
}

void query_glyph(nk_handle handle, float font_height, struct nk_user_font_glyph *glyph, nk_rune codepoint, nk_rune next_codepoint){
	// your_font_type *type = handle.ptr;
	
	// TODO Magic numbers.
	uint x = ((codepoint*glyph_w) & 0xFF);
	uint y = ((codepoint/2      ) & 0xF0);
	
	(*glyph) = (struct nk_user_font_glyph){
		.width = font_scale*font_w, .height = font_scale*font_h, .xadvance = font_scale*(font_w + 1),
		.uv = {{x/256.0f, (y + font_h)/256.0f}, {(x + font_w)/256.0f, y/256.0f}},
		.offset.y = font_scale*font_baseline,
	};
}

DriftNuklear* DriftNuklearNew(void){
	DriftNuklear* ctx = DriftAlloc(DriftSystemMem, sizeof(*ctx));
	
	nk_init_default(&ctx->nk, 0);
	nk_buffer_init_default(&ctx->commands);
	
	nk_style_from_table(&ctx->nk, (struct nk_color[NK_COLOR_COUNT]){
		[NK_COLOR_TEXT] = nk_rgba(190, 190, 190, 255),
		[NK_COLOR_WINDOW] = nk_rgba(30, 33, 40, 215),
		[NK_COLOR_HEADER] = nk_rgba(181, 45, 69, 220),
		[NK_COLOR_BORDER] = nk_rgba(51, 55, 67, 255),
		[NK_COLOR_BUTTON] = nk_rgba(181, 45, 69, 255),
		[NK_COLOR_BUTTON_HOVER] = nk_rgba(190, 50, 70, 255),
		[NK_COLOR_BUTTON_ACTIVE] = nk_rgba(195, 55, 75, 255),
		[NK_COLOR_TOGGLE] = nk_rgba(51, 55, 67, 255),
		[NK_COLOR_TOGGLE_HOVER] = nk_rgba(45, 60, 60, 255),
		[NK_COLOR_TOGGLE_CURSOR] = nk_rgba(181, 45, 69, 255),
		[NK_COLOR_SELECT] = nk_rgba(51, 55, 67, 255),
		[NK_COLOR_SELECT_ACTIVE] = nk_rgba(181, 45, 69, 255),
		[NK_COLOR_SLIDER] = nk_rgba(51, 55, 67, 255),
		[NK_COLOR_SLIDER_CURSOR] = nk_rgba(181, 45, 69, 255),
		[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgba(186, 50, 74, 255),
		[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgba(191, 55, 79, 255),
		[NK_COLOR_PROPERTY] = nk_rgba(51, 55, 67, 255),
		[NK_COLOR_EDIT] = nk_rgba(51, 55, 67, 225),
		[NK_COLOR_EDIT_CURSOR] = nk_rgba(190, 190, 190, 255),
		[NK_COLOR_COMBO] = nk_rgba(51, 55, 67, 255),
		[NK_COLOR_CHART] = nk_rgba(51, 55, 67, 255),
		[NK_COLOR_CHART_COLOR] = nk_rgba(170, 40, 60, 255),
		[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_rgba( 255, 0, 0, 255),
		[NK_COLOR_SCROLLBAR] = nk_rgba(30, 33, 40, 255),
		[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgba(64, 84, 95, 255),
		[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgba(70, 90, 100, 255),
		[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(75, 95, 105, 255),
		[NK_COLOR_TAB_HEADER] = nk_rgba(181, 45, 69, 220),
	});
	
	return ctx;
}

void DriftNuklearSetupGFX(DriftNuklear* ctx, DriftDrawShared* draw_shared){
	const DriftGfxDriver* driver = draw_shared->driver;
	
	ctx->font = (struct nk_user_font){
		// .userdata.ptr = &your_font_class_or_struct,
		.width = text_width, .height = font_scale*font_h,
		.query = query_glyph, .texture.ptr = draw_shared->atlas_texture,
	};
	nk_style_set_font(&ctx->nk, &ctx->font);
	
	static const DriftGfxShaderDesc nk_pipeline_desc = {
		.vertex[0] = {.type = DRIFT_TYPE_FLOAT32_2, .offset = offsetof(DriftNuklearVertex, position)},
		.vertex[1] = {.type = DRIFT_TYPE_FLOAT32_2, .offset = offsetof(DriftNuklearVertex, uv)},
		.vertex[2] = {.type = DRIFT_TYPE_UNORM8_4, .offset = offsetof(DriftNuklearVertex, color)},
		.vertex_stride = sizeof(DriftNuklearVertex),
		.uniform[0] = "DriftGlobals",
		.sampler[0] = "DriftNearest",
		.texture[1] = "Texture",
	};
	DriftGfxShader* nk_shader = driver->load_shader(driver, "nuklear", &nk_pipeline_desc);
	ctx->pipeline = driver->new_pipeline(driver, (DriftGfxPipelineOptions){.shader = nk_shader, .blend = &DriftGfxBlendModeAlpha});
	
	static struct nk_draw_vertex_layout_element layout[] = {
		{NK_VERTEX_POSITION, NK_FORMAT_FLOAT, offsetof(DriftNuklearVertex, position)},
		{NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, offsetof(DriftNuklearVertex, uv)},
		{NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, offsetof(DriftNuklearVertex, color)},
		{NK_VERTEX_LAYOUT_END}
	};
	
	ctx->convert_config = (struct nk_convert_config){
		.vertex_layout = layout, .vertex_size = sizeof(DriftNuklearVertex), .vertex_alignment = NK_ALIGNOF(DriftNuklearVertex),
		.circle_segment_count = 22, .curve_segment_count = 22, .arc_segment_count = 22,
		.null = {.texture.ptr = draw_shared->atlas_texture, .uv = {}}, .global_alpha = 1.0f, .shape_AA = true, .line_AA = true,
	};
}

void DriftNuklearFree(DriftNuklear* ctx){
	// TODO doesn't free the gfx resources, but also not production code?
	nk_buffer_free(&ctx->commands);
	DriftDealloc(DriftSystemMem, ctx, sizeof(*ctx));
}

int DriftNuklearHandleEvent(DriftNuklear* nk, SDL_Event *event){
	struct nk_context *ctx = &nk->nk;
	switch(event->type){
		case SDL_KEYUP:
		case SDL_KEYDOWN:{
			bool down = event->type == SDL_KEYDOWN;
			bool lctrl = SDL_GetKeyboardState(NULL)[SDL_SCANCODE_LCTRL];
			switch(event->key.keysym.sym){
				case SDLK_RSHIFT:
				case SDLK_LSHIFT: nk_input_key(ctx, NK_KEY_SHIFT, down); break;
				case SDLK_DELETE: nk_input_key(ctx, NK_KEY_DEL, down); break;
				case SDLK_RETURN: nk_input_key(ctx, NK_KEY_ENTER, down); break;
				case SDLK_TAB: nk_input_key(ctx, NK_KEY_TAB, down); break;
				case SDLK_BACKSPACE: nk_input_key(ctx, NK_KEY_BACKSPACE, down); break;
				case SDLK_r: nk_input_key(ctx, NK_KEY_TEXT_REDO, down && lctrl); break;
				case SDLK_c: nk_input_key(ctx, NK_KEY_COPY, down && lctrl); break;
				case SDLK_v: nk_input_key(ctx, NK_KEY_PASTE, down && lctrl); break;
				case SDLK_x: nk_input_key(ctx, NK_KEY_CUT, down && lctrl); break;
				case SDLK_z: nk_input_key(ctx, NK_KEY_TEXT_UNDO, down && lctrl); break;
				case SDLK_b: nk_input_key(ctx, NK_KEY_TEXT_LINE_START, down && lctrl); break;
				case SDLK_e: nk_input_key(ctx, NK_KEY_TEXT_LINE_END, down && lctrl); break;
				case SDLK_LEFT: nk_input_key(ctx, lctrl ? NK_KEY_TEXT_WORD_LEFT : NK_KEY_LEFT, down); break;
				case SDLK_RIGHT: nk_input_key(ctx, lctrl ? NK_KEY_TEXT_WORD_RIGHT : NK_KEY_RIGHT, down); break;
				case SDLK_UP: nk_input_key(ctx, NK_KEY_UP, down); break;
				case SDLK_DOWN: nk_input_key(ctx, NK_KEY_DOWN, down); break;
				case SDLK_PAGEUP: nk_input_key(ctx, NK_KEY_SCROLL_UP, down); break;
				case SDLK_PAGEDOWN: nk_input_key(ctx, NK_KEY_SCROLL_DOWN, down); break;
				case SDLK_HOME: {
					nk_input_key(ctx, NK_KEY_TEXT_START, down);
					nk_input_key(ctx, NK_KEY_SCROLL_START, down);
				} break;
				case SDLK_END: {
					nk_input_key(ctx, NK_KEY_TEXT_END, down);
					nk_input_key(ctx, NK_KEY_SCROLL_END, down);
				} break;
				default: return false;
			}
		} break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP: {
			bool down = event->type == SDL_MOUSEBUTTONDOWN;
			int x = event->button.x, y = event->button.y;
			switch(event->button.button){
				case SDL_BUTTON_LEFT:{
					nk_input_button(ctx, NK_BUTTON_LEFT, x, y, down);
					if(event->button.clicks == 2) nk_input_button(ctx, NK_BUTTON_DOUBLE, x, y, down);
				} break;
				case SDL_BUTTON_MIDDLE: nk_input_button(ctx, NK_BUTTON_MIDDLE, x, y, down); break;
				case SDL_BUTTON_RIGHT: nk_input_button(ctx, NK_BUTTON_RIGHT, x, y, down); break;
			}
		} break;
		case SDL_MOUSEMOTION: nk_input_motion(ctx, event->motion.x, event->motion.y); break;
		case SDL_TEXTINPUT: {
			nk_glyph glyph;
			memcpy(glyph, event->text.text, NK_UTF_SIZE);
			nk_input_glyph(ctx, glyph);
		} break;
		case SDL_MOUSEWHEEL: nk_input_scroll(ctx, nk_vec2(event->wheel.x, event->wheel.y)); break;
	}
	
	return true;
}

void DriftNuklearDraw(DriftNuklear* ctx, DriftDraw* draw){
	size_t max_vertex_buffer =  512*1024, max_element_buffer = 128*1024;
	
	DriftGfxRenderer* renderer = draw->renderer;
	DriftGfxBufferSlice vertexes = DriftGfxRendererPushGeometry(renderer, NULL, max_vertex_buffer);
	DriftGfxBufferSlice indexes = DriftGfxRendererPushIndexes(renderer, NULL, max_vertex_buffer);
	
	nk_buffer_init_fixed(&ctx->vertexes, vertexes.ptr, max_vertex_buffer);
	nk_buffer_init_fixed(&ctx->indexes, indexes.ptr, max_element_buffer);
	nk_convert(&ctx->nk, &ctx->commands, &ctx->vertexes, &ctx->indexes, &ctx->convert_config);
	
	const struct nk_draw_command *cmd;
	nk_draw_foreach(cmd, &ctx->nk, &ctx->commands){
		if(!cmd->elem_count) continue;
		
		DRIFT_ASSERT(cmd->texture.ptr, "No texture?");
		DriftGfxPipelineBindings* bindings = DriftGfxRendererPushBindPipelineCommand(renderer, ctx->pipeline);
		bindings->uniforms[0] = draw->globals_binding,
		bindings->samplers[0] = draw->shared->nearest_sampler;
		bindings->textures[1] = cmd->texture.ptr;
		bindings->vertex = vertexes.binding;
		
		struct nk_rect rect = cmd->clip_rect;
		DriftGfxRendererPushScissorCommand(renderer, (DriftAABB2){
			.l = rect.x, .b = rect.y, .r = rect.x + rect.w, .t = rect.y + rect.h,
		});
		
		DriftGfxRendererPushDrawIndexedCommand(renderer, indexes.binding, cmd->elem_count, 1);
		indexes.binding.offset += cmd->elem_count*sizeof(nk_draw_index);
	}
	DriftGfxRendererPushScissorCommand(renderer, DRIFT_AABB2_ALL);
	
	nk_clear(&ctx->nk);
	nk_buffer_clear(&ctx->commands);
}

#if DRIFT_DEBUG
	#include "nuklear/demo/overview.c"
#else
	static void overview(struct nk_context *ctx){}
#endif

void DriftNuklearOverview(struct nk_context *ctx){overview(ctx);}
