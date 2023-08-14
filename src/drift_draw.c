/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "drift_game.h"

typedef DriftImage AtlasLoaderFunc(tina_job* job, DriftMem* mem, uint layer, const char* name);
static AtlasLoaderFunc LoadTexture;
static AtlasLoaderFunc LoadTextureG;
static AtlasLoaderFunc LoadTextureBin128;

static const struct {
	AtlasLoaderFunc* loader;
	const char* name;
} ATLAS_LAYERS[_DRIFT_ATLAS_COUNT] = {
	[DRIFT_ATLAS_UI] = {LoadTexture, "gfx/ATLAS_UI.qoi"},
	[DRIFT_ATLAS_INPUT] = {LoadTexture, "gfx/ATLAS_INPUT.qoi"},
	[DRIFT_ATLAS_LIGHTS] = {LoadTexture, "gfx/ATLAS_LIGHTS.qoi"},
	[DRIFT_ATLAS_MISC] = {LoadTexture, "gfx/ATLAS_MISC.qoi"},
	[DRIFT_ATLAS_MISCG] = {LoadTextureG, "gfx/ATLAS_MISC_FX.qoi"},
	[DRIFT_ATLAS_LIGHT ] = {LoadTexture, "gfx/ATLAS_BG_LIGHT.qoi"},
	[DRIFT_ATLAS_LIGHTG] = {LoadTexture, "gfx/ATLAS_BG_LIGHT_FX.qoi"},
	[DRIFT_ATLAS_RADIO ] = {LoadTexture, "gfx/ATLAS_BG_RADIO.qoi"},
	[DRIFT_ATLAS_RADIOG] = {LoadTexture, "gfx/ATLAS_BG_RADIO_FX.qoi"},
	[DRIFT_ATLAS_CRYO ] = {LoadTexture, "gfx/ATLAS_BG_CRYO.qoi"},
	[DRIFT_ATLAS_CRYOG] = {LoadTexture, "gfx/ATLAS_BG_CRYO_FX.qoi"},
	[DRIFT_ATLAS_DARK] = {LoadTexture, "gfx/ATLAS_BG_DARK.qoi"},
	[DRIFT_ATLAS_DARKG] = {LoadTexture, "gfx/ATLAS_BG_DARK_FX.qoi"},
	[DRIFT_ATLAS_BLUE] = {LoadTextureBin128, "bin/blue_noise.bin"},
	#include "atlas_defs.inc"
};

static void LoadAtlasLayer(tina_job* job){
	DriftImage* images = tina_job_get_description(job)->user_data;
	unsigned layer = tina_job_get_description(job)->user_idx;
	AtlasLoaderFunc* f = ATLAS_LAYERS[layer].loader;
	if(f) images[layer] = f(job, DriftSystemMem, layer, ATLAS_LAYERS[layer].name);
}

static DriftImage LoadTexture(tina_job* job, DriftMem* mem, uint layer, const char* name){
	return DriftAssetLoadImage(mem, name);
}

static DriftImage LoadTextureG(tina_job* job, DriftMem* mem, uint layer, const char* name){
	DriftImage img = DriftAssetLoadImage(mem, name);
	DriftGradientMap(img.pixels, img.w, img.h, layer);
	
	// Temporary fix for hatch normals.
	if(layer == DRIFT_ATLAS_MISCG){
		for(uint i = 0; i < 16; i++){
			u8* dst = img.pixels + 4*(256*(0x90 + i) + 0x20);
			u8* src = img.pixels + 4*(256*(0x90 + i) + 0x10);
			memcpy(dst, src, 4*16);
		}
	}
	
	return img;
}

static DriftImage LoadTextureBin128(tina_job* job, DriftMem* mem, uint layer, const char* name){
	DriftData data = DriftAssetLoad(mem, name);
	void* pixels = DriftAlloc(mem, DRIFT_ATLAS_SIZE*DRIFT_ATLAS_SIZE*4);
	
	for(uint i = 0; i < 128; i++){
		void* dst = pixels + i*256*4;
		void* src = data.ptr + i*128*4;
		memcpy(dst + 0x0000*4, src, 128*4);
		memcpy(dst + 0x0080*4, src, 128*4);
		memcpy(dst + 0x8000*4, src, 128*4);
		memcpy(dst + 0x8080*4, src, 128*4);
	}
	DriftDealloc(mem, data.ptr, data.size);
	
	return (DriftImage){.w = DRIFT_ATLAS_SIZE, .h = DRIFT_ATLAS_SIZE, .pixels = pixels};
}

static void DriftDrawSharedMakeBuffers(DriftDrawShared* draw_shared, DriftVec2 size, float hires, float lightfield_scale){
	const DriftGfxDriver* driver = draw_shared->driver;
	draw_shared->color_buffer_size = size;
	draw_shared->lightfield_scale = lightfield_scale;
	draw_shared->hires = hires;
	
	DriftVec2 lightfield_size = DriftVec2Mul(size, 1/(lightfield_scale));
	draw_shared->lightfield_buffer = driver->new_texture(driver, (uint)(lightfield_size.x/2), (uint)(lightfield_size.y/2), (DriftGfxTextureOptions){
		.name = "lightfield_buffer", .type = DRIFT_GFX_TEXTURE_2D_ARRAY, .format = DRIFT_GFX_TEXTURE_FORMAT_RGBA16F, .layers = 5, .render_target = true,
	});
	
	draw_shared->lightfield_target[0] = driver->new_target(driver, (DriftGfxRenderTargetOptions){
		.name = "lightfield_target[0]", .load = DRIFT_GFX_LOAD_ACTION_CLEAR, .store = DRIFT_GFX_STORE_ACTION_STORE,
		.bindings[0] = {.texture = draw_shared->lightfield_buffer, .layer = 0},
	});
	
	draw_shared->lightfield_target[1] = driver->new_target(driver, (DriftGfxRenderTargetOptions){
		.name = "lightfield_target[1]", .load = DRIFT_GFX_LOAD_ACTION_CLEAR, .store = DRIFT_GFX_STORE_ACTION_STORE,
		.bindings[0] = {.texture = draw_shared->lightfield_buffer, .layer = 1},
		.bindings[1] = {.texture = draw_shared->lightfield_buffer, .layer = 2},
		.bindings[2] = {.texture = draw_shared->lightfield_buffer, .layer = 3},
		.bindings[3] = {.texture = draw_shared->lightfield_buffer, .layer = 4},
	});
	
	draw_shared->shadowfield_buffer = driver->new_texture(driver, (uint)(lightfield_size.x), (uint)(lightfield_size.y), (DriftGfxTextureOptions){
		.name = "shadowfield_buffer", .type = DRIFT_GFX_TEXTURE_2D_ARRAY, .format = DRIFT_GFX_TEXTURE_FORMAT_RGBA16F, .layers = 5, .render_target = true,
	});
	
	draw_shared->shadowfield_target[0] = driver->new_target(driver, (DriftGfxRenderTargetOptions){
		.name = "shadowfield_target[0]", .load = DRIFT_GFX_LOAD_ACTION_DONT_CARE, .store = DRIFT_GFX_STORE_ACTION_STORE,
		.bindings[0] = {.texture = draw_shared->shadowfield_buffer, .layer = 0},
	});
	
	draw_shared->shadowfield_target[1] = driver->new_target(driver, (DriftGfxRenderTargetOptions){
		.name = "shadowfield_target[1]", .load = DRIFT_GFX_LOAD_ACTION_DONT_CARE, .store = DRIFT_GFX_STORE_ACTION_STORE,
		.bindings[0] = {.texture = draw_shared->shadowfield_buffer, .layer = 1},
		.bindings[1] = {.texture = draw_shared->shadowfield_buffer, .layer = 2},
		.bindings[2] = {.texture = draw_shared->shadowfield_buffer, .layer = 3},
		.bindings[3] = {.texture = draw_shared->shadowfield_buffer, .layer = 4},
	});
	
	DriftVec2 buffer_size = DriftVec2Mul(size, hires);
	for(uint i = 0; i < 2; i++){
		draw_shared->color_buffer[i] = driver->new_texture(driver, (uint)buffer_size.x, (uint)buffer_size.y, (DriftGfxTextureOptions){
			.name = "color_buffer", .type = DRIFT_GFX_TEXTURE_2D, .format = DRIFT_GFX_TEXTURE_FORMAT_RGBA16F, .render_target = true,
		});
		
		draw_shared->color_target[i] = driver->new_target(driver, (DriftGfxRenderTargetOptions){
			.name = "color_target", .load = DRIFT_GFX_LOAD_ACTION_CLEAR, .store = DRIFT_GFX_STORE_ACTION_STORE,
			.bindings[0] = {.texture = draw_shared->color_buffer[i]},
		});
	}
	
	draw_shared->resolve_buffer = driver->new_texture(driver, (uint)buffer_size.x, (uint)buffer_size.y, (DriftGfxTextureOptions){
		.name = "resolve_buffer", .type = DRIFT_GFX_TEXTURE_2D, .format = DRIFT_GFX_TEXTURE_FORMAT_RGBA8, .render_target = true,
	});
	
	draw_shared->resolve_target = driver->new_target(driver, (DriftGfxRenderTargetOptions){
		.name = "resolve_target", .load = DRIFT_GFX_LOAD_ACTION_CLEAR, .store = DRIFT_GFX_STORE_ACTION_STORE,
		.bindings[0] = {.texture = draw_shared->resolve_buffer, .layer = 0},
	});
}

static const DriftVec2 DEFAULT_EXTENT = {640, 360};
#define DRIFT_GLOBAL_BINDINGS \
	.uniform[0] = "DriftGlobals", \
	.sampler[0] = "DriftNearest", \
	.sampler[1] = "DriftLinear", \
	.texture[0] = "DriftAtlas", \
	.texture[5] = "DriftPrevFrame", \
	.texture[6] = "DriftLightfield", \
	.texture[7] = "DriftShadowfield"

static DriftGfxPipeline* MakePipeline(
	const DriftGfxDriver* driver,
	const char* name, const DriftGfxShaderDesc* desc,
	const DriftGfxBlendMode* blend, const DriftGfxRenderTarget* target, DriftGfxCullMode cull_mode
){
	DriftGfxShader* shader = driver->load_shader(driver, name, desc);
	return driver->new_pipeline(driver, (DriftGfxPipelineOptions){.shader = shader, .blend = blend, .target = target, .cull_mode = cull_mode});
}

static float calculate_virtual_scaling_factor(DriftVec2 pixel_extent){
	return floorf(fmaxf(1.0f, fmaxf(pixel_extent.x/DEFAULT_EXTENT.x, pixel_extent.y/DEFAULT_EXTENT.y)));
}

static DriftVec2 calculate_internal_extents(DriftVec2 virtual_extent, float lightfield_scale){
	float scale = 2*lightfield_scale;
	return (DriftVec2){ceilf(virtual_extent.x/scale)*scale, ceilf(virtual_extent.y/scale)*scale};
}

DriftDrawShared* DriftDrawSharedNew(tina_job* job, float lightfield_scale){
	const DriftGfxDriver* driver = APP->gfx_driver;
	DriftDrawShared* draw_shared = DRIFT_COPY(DriftSystemMem, ((DriftDrawShared){.driver = driver}));
	
	draw_shared->atlas_texture = driver->new_texture(driver, DRIFT_ATLAS_SIZE, DRIFT_ATLAS_SIZE, (DriftGfxTextureOptions){
		.name = "atlas", .type = DRIFT_GFX_TEXTURE_2D_ARRAY, .format = DRIFT_GFX_TEXTURE_FORMAT_RGBA8, .layers = _DRIFT_ATLAS_COUNT,
	});
	
	tina_scheduler* sched = tina_job_get_scheduler(job);
	tina_group atlas_jobs = {};
	DriftImage atlas_images[_DRIFT_ATLAS_COUNT] = {};
	for(uint i = 0; i < _DRIFT_ATLAS_COUNT; i++) tina_scheduler_enqueue(sched, LoadAtlasLayer, atlas_images, i, DRIFT_JOB_QUEUE_WORK, &atlas_jobs);
	
	draw_shared->terrain_tiles = driver->new_texture(driver, DRIFT_TERRAIN_TILE_SIZE, DRIFT_TERRAIN_TILE_SIZE, (DriftGfxTextureOptions){
		.name = "terrain_tiles", .type = DRIFT_GFX_TEXTURE_2D_ARRAY, .format = DRIFT_GFX_TEXTURE_FORMAT_RGBA8, .layers = DRIFT_TERRAIN_TILECACHE_SIZE,
	});
	
	// Need to make a set of small throwaway buffers for the shaders to compile against.
	DriftDrawSharedMakeBuffers(draw_shared, (DriftVec2){2, 2}, 1, 1);
	draw_shared->nearest_sampler = driver->new_sampler(driver, (DriftGfxSamplerOptions){});
	draw_shared->linear_sampler = driver->new_sampler(driver, (DriftGfxSamplerOptions){.min_filter = DRIFT_GFX_FILTER_LINEAR, .mag_filter = DRIFT_GFX_FILTER_LINEAR});
	draw_shared->repeat_sampler = driver->new_sampler(driver, (DriftGfxSamplerOptions){.address_x = DRIFT_GFX_ADDRESS_MODE_REPEAT, .address_y = DRIFT_GFX_ADDRESS_MODE_REPEAT});
	
	DriftGfxRenderTarget* color_target = draw_shared->color_target[0];
	
	static const DriftGfxShaderDesc basic_quad_desc = {
		.vertex[0] = {.type = DRIFT_GFX_TYPE_FLOAT32_2},
		.vertex_stride = sizeof(DriftVec2),
		DRIFT_GLOBAL_BINDINGS,
	};
	
	static const DriftGfxShaderDesc light_desc = {
		.vertex[0] = {.type = DRIFT_GFX_TYPE_FLOAT32_2},
		.vertex_stride = sizeof(DriftVec2),
		.vertex[1] = {.type = DRIFT_GFX_TYPE_FLOAT32_4, .offset = offsetof(DriftLight, matrix.a), .instanced = true},
		.vertex[2] = {.type = DRIFT_GFX_TYPE_FLOAT32_2, .offset = offsetof(DriftLight, matrix.x), .instanced = true},
		.vertex[3] = {.type = DRIFT_GFX_TYPE_FLOAT32_4, .offset = offsetof(DriftLight, color), .instanced = true},
		.vertex[4] = {.type = DRIFT_GFX_TYPE_U8_4, .offset = offsetof(DriftLight, frame.bounds), .instanced = true},
		.vertex[5] = {.type = DRIFT_GFX_TYPE_U8_2, .offset = offsetof(DriftLight, frame.anchor), .instanced = true},
		.vertex[6] = {.type = DRIFT_GFX_TYPE_U16, .offset = offsetof(DriftLight, frame.layer), .instanced = true},
		.instance_stride = sizeof(DriftLight),
		DRIFT_GLOBAL_BINDINGS,
	};
	draw_shared->light_pipeline[0] = MakePipeline(driver, "light0", &light_desc, &DriftGfxBlendModeAdd, draw_shared->lightfield_target[0], DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->light_pipeline[1] = MakePipeline(driver, "light1", &light_desc, &DriftGfxBlendModeAdd, draw_shared->lightfield_target[1], DRIFT_GFX_CULL_MODE_NONE);
	
	draw_shared->light_blit_pipeline[0] = MakePipeline(driver, "light_blit0", &basic_quad_desc, NULL, draw_shared->shadowfield_target[0], DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->light_blit_pipeline[1] = MakePipeline(driver, "light_blit1", &basic_quad_desc, NULL, draw_shared->shadowfield_target[1], DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxShaderDesc shadow_mask_desc = {
		.vertex[0] = {.type = DRIFT_GFX_TYPE_FLOAT32_2},
		.vertex_stride = sizeof(DriftVec2),
		.vertex[1] = {.type = DRIFT_GFX_TYPE_FLOAT32_4, .instanced = true},
		.instance_stride = sizeof(DriftVec4),
		.uniform[1] = "Locals",
		DRIFT_GLOBAL_BINDINGS,
	};
	
	static const DriftGfxBlendMode shadow_mask_blend = {
		.color_src_factor = DRIFT_GFX_BLEND_FACTOR_ZERO, .color_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE,
		.alpha_src_factor = DRIFT_GFX_BLEND_FACTOR_ONE, .alpha_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE,
	};
	
	draw_shared->shadow_mask_pipeline[0] = MakePipeline(driver, "shadow_mask0", &shadow_mask_desc, &shadow_mask_blend, draw_shared->shadowfield_target[0], DRIFT_GFX_CULL_MODE_FRONT);
	draw_shared->shadow_mask_pipeline[1] = MakePipeline(driver, "shadow_mask1", &shadow_mask_desc, &shadow_mask_blend, draw_shared->shadowfield_target[1], DRIFT_GFX_CULL_MODE_FRONT);
	
	static const DriftGfxShaderDesc primitive_desc = {
		.vertex[0] = {.type = DRIFT_GFX_TYPE_FLOAT32_2},
		.vertex_stride = sizeof(DriftVec2),
		.vertex[1] = {.type = DRIFT_GFX_TYPE_FLOAT32_4, .offset = offsetof(DriftPrimitive, p0), .instanced = true},
		.vertex[2] = {.type = DRIFT_GFX_TYPE_FLOAT32_2, .offset = offsetof(DriftPrimitive, radii), .instanced = true},
		.vertex[3] = {.type = DRIFT_GFX_TYPE_UNORM8_4, .offset = offsetof(DriftPrimitive, color), .instanced = true},
		.instance_stride = sizeof(DriftPrimitive),
		.uniform[1] = "Locals",
		DRIFT_GLOBAL_BINDINGS,
	};
	
	static const DriftGfxBlendMode prim_blend = {
		.color_src_factor = DRIFT_GFX_BLEND_FACTOR_ONE, .color_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.alpha_src_factor = DRIFT_GFX_BLEND_FACTOR_ZERO, .alpha_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE,
	};
	
	draw_shared->overlay_primitive_pipeline = MakePipeline(driver, "primitive_overlay", &primitive_desc, &prim_blend, draw_shared->resolve_target, DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->linear_primitive_pipeline = MakePipeline(driver, "primitive_linear", &primitive_desc, &prim_blend, color_target, DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxShaderDesc plasma_desc = {
		.vertex[0] = {.type = DRIFT_GFX_TYPE_FLOAT32_2, .offset = offsetof(DriftPlasmaVert, pos)},
		.vertex[1] = {.type = DRIFT_GFX_TYPE_FLOAT32  , .offset = offsetof(DriftPlasmaVert, value)},
		.vertex_stride = sizeof(DriftPlasmaVert),
		.vertex[2] = {.type = DRIFT_GFX_TYPE_FLOAT32_4, .instanced = true},
		.instance_stride = sizeof(DriftSegment),
		DRIFT_GLOBAL_BINDINGS,
	};
	
	draw_shared->plasma_pipeline = MakePipeline(driver, "plasma", &plasma_desc, &prim_blend, color_target, DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxBlendMode shadow_blend = {
		.color_src_factor = DRIFT_GFX_BLEND_FACTOR_SRC_ALPHA_SATURATE, .color_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE,
		.alpha_src_factor = DRIFT_GFX_BLEND_FACTOR_ONE, .alpha_dst_factor = DRIFT_GFX_BLEND_FACTOR_ZERO,
	};
	
	draw_shared->shadow_pipeline[0] = MakePipeline(driver, "shadow0", &light_desc, &shadow_blend, draw_shared->shadowfield_target[0], DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->shadow_pipeline[1] = MakePipeline(driver, "shadow1", &light_desc, &shadow_blend, draw_shared->shadowfield_target[1], DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxShaderDesc terrain_desc = {
		.vertex[0] = {.type = DRIFT_GFX_TYPE_FLOAT32_2},
		.vertex_stride = sizeof(DriftVec2),
		.vertex[1] = {.type = DRIFT_GFX_TYPE_FLOAT32_4, .instanced = true},
		.instance_stride = sizeof(DriftTerrainChunk),
		.sampler[2] = "_repeat",
		.texture[1] = "_tiles",
		DRIFT_GLOBAL_BINDINGS,
	};
	draw_shared->terrain_pipeline = MakePipeline(driver, "terrain", &terrain_desc, NULL, color_target, DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->terrain_map_pipeline = MakePipeline(driver, "terrain_map", &terrain_desc, NULL, draw_shared->resolve_target, DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->debug_terrain_pipeline = MakePipeline(driver, "debug_terrain", &terrain_desc, NULL, color_target, DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxShaderDesc sprite_desc = {
		.vertex[0] = {.type = DRIFT_GFX_TYPE_FLOAT32_2,},
		.vertex_stride = sizeof(DriftVec2),
		.vertex[1] = {.type = DRIFT_GFX_TYPE_FLOAT32_4, .offset = offsetof(DriftSprite, matrix.a), .instanced = true},
		.vertex[2] = {.type = DRIFT_GFX_TYPE_FLOAT32_2, .offset = offsetof(DriftSprite, matrix.x), .instanced = true},
		.vertex[3] = {.type = DRIFT_GFX_TYPE_UNORM8_4, .offset = offsetof(DriftSprite, color), .instanced = true},
		.vertex[4] = {.type = DRIFT_GFX_TYPE_U8_4, .offset = offsetof(DriftSprite, frame.bounds), .instanced = true},
		.vertex[5] = {.type = DRIFT_GFX_TYPE_U8_4, .offset = offsetof(DriftSprite, frame.anchor), .instanced = true},
		.vertex[6] = {.type = DRIFT_GFX_TYPE_U8_2, .offset = offsetof(DriftSprite, z), .instanced = true},
		.instance_stride = sizeof(DriftSprite),
		DRIFT_GLOBAL_BINDINGS,
	};
	
	static const DriftGfxBlendMode premultiplied = {
		.color_src_factor = DRIFT_GFX_BLEND_FACTOR_ONE, .color_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.alpha_src_factor = DRIFT_GFX_BLEND_FACTOR_ZERO, .alpha_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE,
	};
	
	draw_shared->sprite_pipeline = MakePipeline(driver, "sprite", &sprite_desc, &premultiplied, color_target, DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->flash_sprite_pipeline = MakePipeline(driver, "sprite_flash", &sprite_desc, &premultiplied, color_target, DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->overlay_sprite_pipeline = MakePipeline(driver, "sprite_overlay", &sprite_desc, &premultiplied, draw_shared->resolve_target, DRIFT_GFX_CULL_MODE_NONE);
	
	draw_shared->debug_lightfield_pipeline = MakePipeline(driver, "debug_lightfield", &basic_quad_desc, &DriftGfxBlendModePremultipliedAlpha, color_target, DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxShaderDesc resolve_desc = {
		.vertex[0] = {.type = DRIFT_GFX_TYPE_FLOAT32_2},
		.vertex_stride = sizeof(DriftVec2),
		.texture[1] = "_texture",
		.sampler[2] = "_repeat",
		.uniform[1] = "Locals",
		DRIFT_GLOBAL_BINDINGS,
	};
	draw_shared->resolve_pipeline = MakePipeline(driver, "resolve", &resolve_desc, NULL, draw_shared->resolve_target, DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxShaderDesc image_blit_desc = {
		.vertex[0] = {.type = DRIFT_GFX_TYPE_FLOAT32_2},
		.vertex[1] = {.type = DRIFT_GFX_TYPE_FLOAT32_4, .instanced = true},
		.vertex_stride = sizeof(DriftVec2), .instance_stride = sizeof(DriftVec4),
		.texture[1] = "_texture",
		.sampler[2] = "_repeat",
		DRIFT_GLOBAL_BINDINGS,
	};
	draw_shared->image_blit_pipeline = MakePipeline(driver, "image_blit", &image_blit_desc, NULL, draw_shared->resolve_target, DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->pause_blit_pipeline = MakePipeline(driver, "pause_blit", &image_blit_desc, NULL, draw_shared->resolve_target, DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxShaderDesc map_blit_desc = {
		.vertex[0] = {.type = DRIFT_GFX_TYPE_FLOAT32_2},
		.vertex[1] = {.type = DRIFT_GFX_TYPE_FLOAT32_4, .offset = offsetof(DriftGPUMatrix, m[0]), .instanced = true},
		.vertex[2] = {.type = DRIFT_GFX_TYPE_FLOAT32_4, .offset = offsetof(DriftGPUMatrix, m[4]), .instanced = true},
		.vertex_stride = sizeof(DriftVec2), .instance_stride = sizeof(DriftGPUMatrix),
		.texture[1] = "_texture",
		DRIFT_GLOBAL_BINDINGS,
	};
	draw_shared->map_blit_pipeline = MakePipeline(driver, "map_blit", &map_blit_desc, &DriftGfxBlendModeAlpha, draw_shared->resolve_target, DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxShaderDesc present_desc = {
		.vertex[0] = {.type = DRIFT_GFX_TYPE_FLOAT32_2},
		.vertex_stride = sizeof(DriftVec2),
		.texture[1] = "_texture",
		DRIFT_GLOBAL_BINDINGS,
	};
	draw_shared->present_pipeline = MakePipeline(driver, "present", &present_desc, NULL, NULL, DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->debug_delta_pipeline = MakePipeline(driver, "debug_delta", &present_desc, &DriftGfxBlendModePremultipliedAlpha, NULL, DRIFT_GFX_CULL_MODE_NONE);
	
	tina_job_wait(job, &atlas_jobs, 0);
	for(uint i = 0; i < _DRIFT_ATLAS_COUNT; i++){
		DriftImage img = atlas_images[i];
		if(img.pixels) driver->load_texture_layer(driver, draw_shared->atlas_texture, i, img.pixels);
		DriftImageFree(DriftSystemMem, img);
	}
	return draw_shared;
}

void DriftDrawSharedFree(DriftDrawShared* draw_shared){
	DriftDealloc(DriftSystemMem, draw_shared, sizeof(*draw_shared));
}

static bool DriftDrawResizeBuffersIfChanged(DriftDrawShared* draw_shared, tina_job* job, DriftVec2 extent, float hires, float lightfield_scale){
	DriftVec2 buff_size = draw_shared->color_buffer_size;
	if(extent.x != buff_size.x || extent.y != buff_size.y || lightfield_scale != draw_shared->lightfield_scale || hires != draw_shared->hires){
		void* objects[] = {
			draw_shared->color_target[0], draw_shared->color_target[1], draw_shared->color_buffer[0], draw_shared->color_buffer[1],
			draw_shared->lightfield_target[0], draw_shared->lightfield_target[1], draw_shared->lightfield_buffer,
			draw_shared->shadowfield_target[0], draw_shared->shadowfield_target[1], draw_shared->shadowfield_buffer,
		};
		uint obj_count = sizeof(objects)/sizeof(*objects);
		
		uint queue = tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
		if(buff_size.x*buff_size.y > 0) draw_shared->driver->free_objects(draw_shared->driver, objects, obj_count);
		DriftDrawSharedMakeBuffers(draw_shared, extent, hires, lightfield_scale);
		tina_job_switch_queue(job, queue);
		
		return true;
	} else {
		return false;
	}
}

DriftDraw* DriftDrawBeginBase(tina_job* job, DriftGameContext* ctx, DriftAffine v_matrix, DriftAffine prev_vp_matrix){
	DriftMem* mem = DriftZoneMemAquire(APP->zone_heap, "DrawMem");
	DriftGfxRenderer* renderer = DriftAppBeginFrame(mem);
	DriftVec2 raw_extent = DriftGfxRendererDefaultExtent(renderer);
	DriftDrawShared* draw_shared = ctx->draw_shared;
	
	// TODO kinda seems like a bad place for this?
	float virtual_scale = calculate_virtual_scaling_factor(raw_extent);
	float lightfield_scale = APP->prefs.lightfield_scale;
	float hires = APP->prefs.hires ? virtual_scale : 1;
	DriftVec2 virtual_extent = DriftVec2Mul(raw_extent, 1/virtual_scale);
	DriftVec2 internal_extent = calculate_internal_extents(virtual_extent, lightfield_scale);
	bool prev_buffer_invalid = DriftDrawResizeBuffersIfChanged(draw_shared, job, internal_extent, hires, lightfield_scale);
	APP->scaling_factor = virtual_scale;
	
	DriftAffine p_matrix = DriftAffineOrtho(-0.5f*internal_extent.x, 0.5f*internal_extent.x, -0.5f*internal_extent.y, 0.5f*internal_extent.y);
	DriftAffine vp_matrix = DriftAffineMul(p_matrix, v_matrix);
	if(prev_buffer_invalid) prev_vp_matrix = vp_matrix;
	DriftAffine vp_inverse = DriftAffineInverse(vp_matrix);
	
	uint curr_buffer_idx = (ctx->current_frame + 0) % 2;
	uint prev_buffer_idx = (ctx->current_frame + 1) % 2;
	
	return DRIFT_COPY(mem, ((DriftDraw){
		.job = job, .mem = mem, .renderer = renderer,
		.ctx = ctx, .state = ctx->state, .shared = ctx->draw_shared,
		.frame = ctx->current_frame, .tick = ctx->current_tick, .nanos = ctx->update_nanos,
		.raw_extent = raw_extent, .virtual_extent = virtual_extent, .internal_extent = internal_extent,
		.p_matrix = p_matrix, .v_matrix = v_matrix, .vp_matrix = vp_matrix, .vp_inverse = vp_inverse,
		.ui_matrix = DriftAffineOrtho(0, internal_extent.x, 0, internal_extent.y),
		.reproj_matrix = DriftAffineMul(prev_vp_matrix, vp_inverse),
		
		.screen_tint = DRIFT_VEC4_WHITE,
		.color_target_curr = draw_shared->color_target[curr_buffer_idx],
		.color_target_prev = draw_shared->color_target[prev_buffer_idx],
		.color_buffer_curr = draw_shared->color_buffer[curr_buffer_idx],
		.color_buffer_prev = draw_shared->color_buffer[prev_buffer_idx],
		.prev_buffer_invalid = prev_buffer_invalid,
		
		.terrain_chunks = DRIFT_ARRAY_NEW(mem, 64, DriftTerrainChunk),
		.lights = DRIFT_ARRAY_NEW(mem, 2048, DriftLight),
		.shadow_masks = DRIFT_ARRAY_NEW(mem, 2048, DriftSegment),
		.bg_sprites = DRIFT_ARRAY_NEW(mem, 2048, DriftSprite),
		.bg_prims = DRIFT_ARRAY_NEW(mem, 2048, DriftPrimitive),
		.plasma_strands = DRIFT_ARRAY_NEW(mem, 64, DriftSegment),
		.fg_sprites = DRIFT_ARRAY_NEW(mem, 2048, DriftSprite),
		.flash_sprites = DRIFT_ARRAY_NEW(mem, 2048, DriftSprite),
		.bullet_sprites = DRIFT_ARRAY_NEW(mem, 64, DriftSprite),
		.overlay_sprites = DRIFT_ARRAY_NEW(mem, 2048, DriftSprite),
		.overlay_prims = DRIFT_ARRAY_NEW(mem, 2048, DriftPrimitive),
		.hud_sprites = DRIFT_ARRAY_NEW(mem, 2048, DriftSprite),
	}));
}

DriftDraw* DriftDrawBegin(DriftUpdate* update, float dt_since_tick, DriftAffine v_matrix, DriftAffine prev_vp_matrix){
	DriftGameContext* ctx = update->ctx;
	DriftDraw* draw = DriftDrawBeginBase(update->job, ctx, v_matrix, prev_vp_matrix);
	draw->dt = update->dt;
	draw->dt_since_tick = dt_since_tick;
	
	return draw;
}

void DriftDrawBindGlobals(DriftDraw* draw){
	DriftAffine terrain_matrix = draw->state ? draw->state->terra->map_to_world : DRIFT_AFFINE_IDENTITY;
	
	DriftGlobalUniforms global_uniforms = {
		.v_matrix = DriftAffineToGPU(draw->v_matrix),
		.p_matrix = DriftAffineToGPU(draw->p_matrix),
		.terrain_matrix = DriftAffineToGPU(terrain_matrix),
		.vp_matrix = DriftAffineToGPU(draw->vp_matrix),
		.vp_inverse = DriftAffineToGPU(draw->vp_inverse),
		.reproj_matrix = DriftAffineToGPU(draw->reproj_matrix),
		.jitter = DriftNoiseR2(draw->frame),
		.raw_extent = draw->raw_extent,
		.virtual_extent = draw->virtual_extent,
		.internal_extent = draw->internal_extent,
		.atlas_size = DRIFT_ATLAS_SIZE, .sharpening = APP->prefs.sharpening, .gradmul = draw->shared->hires,
		.biome_layer = DRIFT_ATLAS_BIOME, .visibility_layer = DRIFT_ATLAS_VISIBILITY,
	};
	
	DriftGlobalUniforms ui_uniforms = global_uniforms;
	ui_uniforms.v_matrix = DriftAffineToGPU(DRIFT_AFFINE_IDENTITY);
	ui_uniforms.p_matrix = DriftAffineToGPU(draw->ui_matrix);
	ui_uniforms.vp_matrix = DriftAffineToGPU(draw->ui_matrix);
	ui_uniforms.vp_inverse = DriftAffineToGPU(DriftAffineInverse(draw->ui_matrix));
	
	static const DriftVec2 QUAD_UVS[] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
	static const u16 QUAD_INDICES[] = {0, 1, 2, 3, 2, 1};
	draw->quad_vertex_binding = DriftGfxRendererPushGeometry(draw->renderer, QUAD_UVS, sizeof(QUAD_UVS)).binding;
	draw->quad_index_binding = DriftGfxRendererPushIndexes(draw->renderer, QUAD_INDICES, sizeof(QUAD_INDICES)).binding;
	draw->globals_binding = DriftGfxRendererPushUniforms(draw->renderer, &global_uniforms, sizeof(global_uniforms)).binding;
	draw->ui_binding = DriftGfxRendererPushUniforms(draw->renderer, &ui_uniforms, sizeof(ui_uniforms)).binding;
	
	DriftDrawShared* draw_shared = draw->shared;
	draw->default_bindings = (DriftGfxPipelineBindings){
		.vertex = draw->quad_vertex_binding,
		.uniforms[0] = draw->globals_binding,
		.samplers[0] = draw_shared->nearest_sampler,
		.samplers[1] = draw_shared->linear_sampler,
		.textures[0] = draw_shared->atlas_texture,
		.textures[5] = draw->color_buffer_prev,
		.textures[6] = draw_shared->lightfield_buffer,
		.textures[7] = draw_shared->shadowfield_buffer,
	};
}

DriftGfxPipelineBindings* DriftDrawQuads(DriftDraw* draw, DriftGfxPipeline* pipeline, u32 count){
	DriftGfxRenderer* renderer = draw->renderer;
	DriftGfxPipelineBindings* bindings = DriftGfxRendererPushBindPipelineCommand(renderer, pipeline);
	DriftGfxRendererPushDrawIndexedCommand(renderer, draw->quad_index_binding, 6, count);
	*bindings = draw->default_bindings;
	return bindings;
}

static const uint FONT_W = 5, FONT_H = 11;
static const uint GLYPH_W = 5, GLYPH_H = 10;
static const uint GLYPH_BASELINE = 2, GLYPH_ADVANCE = 6;

DriftAABB2 DriftDrawTextBounds(const char* string, size_t n){
	const DriftInputIcon* icons = DRIFT_INPUT_ICON_SETS[INPUT->icon_type];
	if(n == 0) n = strlen(string);
	
	DriftVec2 origin = DRIFT_VEC2_ZERO;
	DriftAABB2 bounds = {0, -(float)GLYPH_BASELINE, 0, GLYPH_H - GLYPH_BASELINE};
	
	const char* cursor = string;
	while(*cursor && cursor < string + n){
		char c = *cursor++;
		switch(c){
			default: {
				bounds.r = fmaxf(bounds.r, origin.x + GLYPH_W);
				bounds.b = fminf(bounds.b, origin.y - GLYPH_BASELINE);
				origin.x += GLYPH_ADVANCE;
			} break;
			case '\t':{
				origin.x += 2*GLYPH_ADVANCE;
			} break;
			case '\n': {
				origin.x = 0;
				origin.y -= FONT_H;
			} break;
			
			case '{': {
				switch(*cursor){
					case '{': cursor++; continue;
					case '#': {
						cursor++;
						DRIFT_ASSERT_HARD(strlen(cursor) > 8, "Truncated color tag.");
						DRIFT_ASSERT(cursor[8] == '}', "Color tag not closed.");
						cursor += 9;
					} break;
					case '+':
					case '!': {
						cursor++;
						while((uint)*cursor++ - '0' < 10){}
					} break;
					case '@': {
						const DriftInputIcon* icon = DriftInputIconFind(icons, cursor + 1);
						DRIFT_ASSERT(icon, "Icon '%s' not found.", cursor +1);
						cursor += strlen(icon->label) + 2;
						
						while(true){
							bounds.r = fmaxf(bounds.r, origin.x + icon->advance*GLYPH_W);
							bounds.b = fminf(bounds.b, origin.y - GLYPH_BASELINE);
							origin.x += icon->advance*GLYPH_ADVANCE;
							
							if(icon[1].label == DRIFT_INPUT_ICON_MULTI){
								// The next icon continues the sequence.
								icon++;
							} else {
								// Icon sequence complete.
								break;
							}
						}
					} break;
				}
			}
		}
	}
	
	return bounds;
}

static inline void output_glyph(DriftSprite** glyphs, char c, DriftRGBA8 color, DriftAffine t){
	// TODO Magic numbers for glyph indexing
	uint x = ((c*8) & 0xFF);
	uint y = ((c/2) & 0xF0) + 4*16;
	DriftFrame frame = {.bounds = {x, y, x + (FONT_W - 1), y + (GLYPH_H - 1)}, .anchor.y = GLYPH_BASELINE, .layer = DRIFT_ATLAS_UI};
	*(*glyphs)++ = (DriftSprite){.frame = frame, .color = color, .matrix = t};
}

DriftAffine DriftDrawTextFull(DriftDraw* draw, DRIFT_ARRAY(DriftSprite)* array, const char* string, DriftTextOptions opts){
	DriftAffine t = opts.matrix;
	DriftVec2 origin = {t.x, t.y};
	
	size_t length = strlen(string);
	DriftSprite* glyphs = DRIFT_ARRAY_RANGE(*array, length);
	if(opts.pause) *opts.pause = false;
	if(opts.active) *opts.active = false;
	uint glyph_count = 0;
	float text_width = 0;
	float line_spacing = FONT_H + opts.spacing_adjust;
	
	const char* cursor = string;
	const char* word_end = cursor;
	
	DriftRGBA8 color = DriftRGBA8FromColor(opts.tint);
	while(*cursor){
		if(opts.glyph_limit && glyph_count >= opts.glyph_limit){
			if(opts.active) *opts.active = true;
			break;
		}
		
		if(opts.max_width && cursor >= word_end){
			// Move past leading whitespace.
			while(*word_end == ' ' || *word_end == '\n') word_end++;
			// Find next word boundary.
			while(*word_end != ' ' && *word_end != '\n' && *word_end != '\0') word_end++;
			// Calculate the advance.
			float adv = DriftDrawTextSize(cursor, word_end - cursor).x;
			if(adv > 0) text_width += adv + 1;
			
			if(text_width > opts.max_width){
				// strip leading whitespace.
				while(*cursor == ' ' || *cursor == '\n') cursor++;
				if(cursor != word_end){
					// Add a line break.
					t.x = (origin.x -= line_spacing*opts.matrix.c);
					t.y = (origin.y -= line_spacing*opts.matrix.d);
					text_width = DriftDrawTextSize(cursor, word_end - cursor).x + 1;
				}
				
				continue;
			}
		}
		
		char c = *cursor++;
		switch(c){
			default: {
				output_glyph(&glyphs, c, color, t);
				t.x += GLYPH_ADVANCE*opts.matrix.a;
				t.y += GLYPH_ADVANCE*opts.matrix.b;
				glyph_count++;
			} break;
			case ' ': {
				t.x += GLYPH_ADVANCE*opts.matrix.a;
				t.y += GLYPH_ADVANCE*opts.matrix.b;
				glyph_count++;
			} break;
			case '\t':{
				t.x += 2*GLYPH_ADVANCE*opts.matrix.a;
				t.y += 2*GLYPH_ADVANCE*opts.matrix.b;
			} break;
			case '\n': {
				t.x = (origin.x -= line_spacing*opts.matrix.c);
				t.y = (origin.y -= line_spacing*opts.matrix.d);
				text_width = 0;
			} break;
			
			case '{': {
				char peek = *cursor;
				switch(peek){
					case '{':{
						output_glyph(&glyphs, peek, color, t);
						t.x += GLYPH_ADVANCE*opts.matrix.a;
						t.y += GLYPH_ADVANCE*opts.matrix.b;
						cursor++;
						glyph_count++;
					} break;
					case '+':{
						cursor++;
						uint count = 0;
						while(true){
							uint digit = *cursor++ - '0';
							if(digit < 10){
								count = 10*count + digit;
							} else {
								glyph_count += count;
								if(opts.pause && glyph_count > opts.glyph_limit) *opts.pause = true;
								break;
							}
						}
					} break;
					case '#': { // set color
						cursor++;
						DRIFT_ASSERT_HARD(strlen(cursor) > 8, "Truncated color tag.");
						DRIFT_ASSERT(cursor[8] == '}', "Color tag not closed.");
						
						static const u8 HEXN[256] = {
							['0'] = 0x0, ['1'] = 0x1, ['2'] = 0x2, ['3'] = 0x3, ['4'] = 0x4, ['5'] = 0x5, ['6'] = 0x6, ['7'] = 0x7,
							['8'] = 0x8, ['9'] = 0x9, ['A'] = 0xA, ['B'] = 0xB, ['C'] = 0xC, ['D'] = 0xD, ['E'] = 0xE, ['F'] = 0xF,
						};
						
						color.r = (u8)(opts.tint.r*(16*HEXN[(u8)cursor[0]] + HEXN[(u8)cursor[1]]));
						color.g = (u8)(opts.tint.g*(16*HEXN[(u8)cursor[2]] + HEXN[(u8)cursor[3]]));
						color.b = (u8)(opts.tint.b*(16*HEXN[(u8)cursor[4]] + HEXN[(u8)cursor[5]]));
						color.a = (u8)(opts.tint.a*(16*HEXN[(u8)cursor[6]] + HEXN[(u8)cursor[7]]));
						cursor += 9;
					} break;
					case '!':{ // draw sprite
						cursor++;
						uint frame = 0;
						while(true){
							uint digit = *cursor++ - '0';
							if(digit < 10){
								frame = 10*frame + digit;
							} else {
								*glyphs++ = (DriftSprite){DRIFT_FRAMES[frame], color, t};
								t.x += GLYPH_ADVANCE*opts.matrix.a;
								t.y += GLYPH_ADVANCE*opts.matrix.b;
								glyph_count++;
								break;
							}
						}
					} break;
					case '@': { // draw input prompt
						const DriftInputIcon* icons = DRIFT_INPUT_ICON_SETS[INPUT->icon_type];
						const DriftInputIcon* icon = DriftInputIconFind(icons, cursor + 1);
						DRIFT_ASSERT(icon, "Icon '%s' not found.", cursor +1);
						cursor += strlen(icon->label) + 2;
						
						while(true){
							DriftFrame frame = DRIFT_FRAMES[icon->frame];
							
							*glyphs++ = (DriftSprite){frame, DriftRGBA8FromColor(opts.tint), t};
							t.x += icon->advance*GLYPH_ADVANCE*opts.matrix.a;
							t.y += icon->advance*GLYPH_ADVANCE*opts.matrix.b;
							glyph_count++;
							
							if(icon[1].label == DRIFT_INPUT_ICON_MULTI){
								// The next icon continues the sequence.
								icon++;
							} else {
								// Icon sequence complete.
								break;
							}
						}
					} break;
				}
			}
		}
	}
	
	DriftArrayRangeCommit(*array, glyphs);
	return t;
}

DriftVec2 DriftDrawText(DriftDraw* draw, DRIFT_ARRAY(DriftSprite)* array, DriftVec2 origin, const char* string){
	DriftAffine t = DriftDrawTextFull(draw, array, string, (DriftTextOptions){.matrix = {1, 0, 0, 1, origin.x, origin.y}, .tint = DRIFT_VEC4_WHITE});
	return DriftAffineOrigin(t);
}

DriftVec2 DriftDrawTextF(DriftDraw* draw, DRIFT_ARRAY(DriftSprite)* array, DriftVec2 origin, const char* format, ...){
	va_list args;
	va_start(args, format);
	
	char str[1024];
	vsnprintf(str, sizeof(str), format, args);
	
	va_end(args);
	DriftAffine t = DriftDrawTextFull(draw, array, str, (DriftTextOptions){.matrix = {1, 0, 0, 1, origin.x, origin.y}, .tint = DRIFT_VEC4_WHITE});
	return DriftAffineOrigin(t);
}

void DriftDrawBatches(DriftDraw* draw, DriftDrawBatch batches[]){
	for(uint i = 0; batches[i].arr; i++){
		DriftDrawBatch* batch = batches + i;
		DriftArray* header = DriftArrayHeader(batch->arr);
		
		DriftGfxPipelineBindings* bindings = DriftGfxRendererPushBindPipelineCommand(draw->renderer, batch->pipeline);
		*bindings = *batch->bindings;
		bindings->instance = DriftGfxRendererPushGeometry(draw->renderer, batch->arr, header->count*header->elt_size).binding;
		DriftGfxRendererPushDrawIndexedCommand(draw->renderer, draw->quad_index_binding, 6, header->count);
	}
}
