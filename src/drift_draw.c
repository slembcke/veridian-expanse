#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "drift_game.h"

#include "qoi/qoi.h"

typedef void AtlasLoaderFunc(tina_job* job, const DriftGfxDriver* driver, DriftGfxTexture* atlas, uint layer);
static AtlasLoaderFunc LoadTexture;
static AtlasLoaderFunc LoadTextureG;
static AtlasLoaderFunc LoadTextureN;

static const struct {
	AtlasLoaderFunc* loader;
	void* ctx;
} ATLAS_LAYERS[_DRIFT_ATLAS_COUNT] = {
	[DRIFT_ATLAS_TEXT] = {LoadTexture, "gfx/ATLAS_TEXT.qoi"},
	[DRIFT_ATLAS_INPUT] = {LoadTexture, "gfx/ATLAS_INPUT.qoi"},
	[DRIFT_ATLAS_LIGHTS] = {LoadTexture, "gfx/ATLAS_LIGHTS.qoi"},
	[DRIFT_ATLAS_PLAYER] = {LoadTexture, "gfx/ATLAS_PLAYER.qoi"},
	[DRIFT_ATLAS_PLAYERG] = {LoadTextureG, "gfx/ATLAS_PLAYER_FX.qoi"},
	[DRIFT_ATLAS_LIGHT ] = {LoadTexture, "gfx/ATLAS_BG_LIGHT.qoi"},
	[DRIFT_ATLAS_LIGHTG] = {LoadTexture, "gfx/ATLAS_BG_LIGHT_FX.qoi"},
	[DRIFT_ATLAS_RADIO ] = {LoadTexture, "gfx/ATLAS_BG_RADIO.qoi"},
	[DRIFT_ATLAS_RADIOG] = {LoadTexture, "gfx/ATLAS_BG_RADIO_FX.qoi"},
	[DRIFT_ATLAS_CRYO ] = {LoadTexture, "gfx/ATLAS_BG_CRYO.qoi"},
	[DRIFT_ATLAS_CRYOG] = {LoadTexture, "gfx/ATLAS_BG_CRYO_FX.qoi"},
	[DRIFT_ATLAS0] = {LoadTexture, "gfx/ATLAS0.qoi"},
	[DRIFT_ATLAS0_FX] = {LoadTexture, "gfx/ATLAS0_FX.qoi"},
	[DRIFT_ATLAS1] = {LoadTexture, "gfx/ATLAS1.qoi"},
	[DRIFT_ATLAS1_FX] = {LoadTexture, "gfx/ATLAS1_FX.qoi"},
};

static void LoadAtlasLayer(tina_job* job){
	DriftDrawShared* draw_shared = tina_job_get_description(job)->user_data;
	unsigned layer = tina_job_get_description(job)->user_idx;
	
	AtlasLoaderFunc* f = ATLAS_LAYERS[layer].loader;
	if(f) f(job, draw_shared->driver, draw_shared->atlas_texture, layer);
}

static void LoadTexture(tina_job* job, const DriftGfxDriver* driver, DriftGfxTexture* atlas, uint layer){
	const char* name = ATLAS_LAYERS[layer].ctx;
	const DriftConstData* buffer = DriftAssetGet(name);
	DRIFT_ASSERT_HARD(buffer, "Resource not found: %s", name);
	
	int w, h, n;
	u8* pixels = qoi_decode(buffer->data, buffer->length, &w, &h, 4);
	DRIFT_ASSERT_HARD(pixels, "Failed to load image: %s", name);
	
	tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
	driver->load_texture_layer(driver, atlas, layer, pixels);
	free(pixels);
}

static void LoadTextureG(tina_job* job, const DriftGfxDriver* driver, DriftGfxTexture* atlas, uint layer){
	const char* name = ATLAS_LAYERS[layer].ctx;
	const DriftConstData* buffer = DriftAssetGet(name);
	DRIFT_ASSERT_HARD(buffer, "Resource not found: %s", name);
	
	int w, h, n;
	u8* pixels = qoi_decode(buffer->data, buffer->length, &w, &h, 4);
	DRIFT_ASSERT_HARD(pixels, "Failed to load image: %s", name);
	
	DriftGradientMap(pixels, w, h, layer);
	
	// Temporary fix for hatch normals.
	if(layer == DRIFT_ATLAS_PLAYERG){
		for(uint i = 0; i < 16; i++){
			u8* dst = pixels + 4*(256*(0x90 + i) + 0x30);
			u8* src = pixels + 4*(256*(0x90 + i) + 0x10);
			memcpy(dst, src, 4*16);
		}
	}
	
	tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
	driver->load_texture_layer(driver, atlas, layer, pixels);
	free(pixels);
}

static void DriftDrawSharedMakeBuffers(DriftDrawShared* draw_shared, DriftVec2 size){
	const DriftGfxDriver* driver = draw_shared->driver;
	draw_shared->color_buffer_size = size;
	
	for(uint i = 0; i < 2; i++){
		draw_shared->color_buffer[i] = driver->new_texture(driver, (uint)size.x, (uint)size.y, (DriftGfxTextureOptions){
			.name = "color_buffer", .type = DRIFT_GFX_TEXTURE_TYPE_2D, .format = DRIFT_GFX_TEXTURE_FORMAT_RGBA16F, .render_target = true,
		});
		
		draw_shared->color_target[i] = driver->new_target(driver, (DriftGfxRenderTargetOptions){
			.name = "color_target", .load = DRIFT_GFX_LOAD_ACTION_CLEAR, .store = DRIFT_GFX_STORE_ACTION_STORE,
			.bindings[0] = {.texture = draw_shared->color_buffer[i]},
		});
	}
	
	draw_shared->lightfield_buffer = driver->new_texture(driver, (uint)size.x/DRIFT_LIGHTFIELD_SCALE, (uint)size.y/DRIFT_LIGHTFIELD_SCALE, (DriftGfxTextureOptions){
		.name = "lightfield_buffer", .type = DRIFT_GFX_TEXTURE_TYPE_2D_ARRAY, .format = DRIFT_GFX_TEXTURE_FORMAT_RGBA16F, .layers = 5, .render_target = true,
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
	
	draw_shared->shadowfield_buffer = driver->new_texture(driver, (uint)size.x/DRIFT_SHADOWFIELD_SCALE, (uint)size.y/DRIFT_SHADOWFIELD_SCALE, (DriftGfxTextureOptions){
		.name = "shadowfield_buffer", .type = DRIFT_GFX_TEXTURE_TYPE_2D_ARRAY, .format = DRIFT_GFX_TEXTURE_FORMAT_RGBA16F, .layers = 5, .render_target = true,
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

DriftDrawShared* DriftDrawSharedNew(DriftApp* app, tina_job* job){
	tina_group jobs = {};
	const DriftGfxDriver* driver = app->gfx_driver;
	
	DriftDrawShared* draw_shared = DriftAlloc(DriftSystemMem, sizeof(*draw_shared));
	draw_shared->app = app;
	draw_shared->driver = driver;
	
	draw_shared->atlas_texture = driver->new_texture(driver, DRIFT_ATLAS_SIZE, DRIFT_ATLAS_SIZE, (DriftGfxTextureOptions){
		.name = "atlas", .type = DRIFT_GFX_TEXTURE_TYPE_2D_ARRAY, .format = DRIFT_GFX_TEXTURE_FORMAT_RGBA8, .layers = _DRIFT_ATLAS_COUNT,
	});
	
	// TODO This can consume all the fibers once more atlases are added!
	tina_scheduler* sched = tina_job_get_scheduler(job);
	for(uint i = 0; i < _DRIFT_ATLAS_COUNT; i++){
		tina_scheduler_enqueue(sched, "JobLoadAtlasLayer", LoadAtlasLayer, draw_shared, i, DRIFT_JOB_QUEUE_WORK, &jobs);
	}
	
	draw_shared->terrain_tiles = driver->new_texture(driver, DRIFT_TERRAIN_TILE_SIZE, DRIFT_TERRAIN_TILE_SIZE, (DriftGfxTextureOptions){
		.name = "terrain_tiles", .type = DRIFT_GFX_TEXTURE_TYPE_2D_ARRAY, .format = DRIFT_GFX_TEXTURE_FORMAT_RGBA8, .layers = DRIFT_TERRAIN_TILECACHE_SIZE,
	});
	
	DriftDrawSharedMakeBuffers(draw_shared, DriftDrawBufferExtent((DriftVec2){app->window_w, app->window_h}));
	draw_shared->color_buffer_index = 0;
	
	draw_shared->nearest_sampler = driver->new_sampler(driver, (DriftGfxSamplerOptions){});
	draw_shared->linear_sampler = driver->new_sampler(driver, (DriftGfxSamplerOptions){.min_filter = DRIFT_GFX_FILTER_LINEAR, .mag_filter = DRIFT_GFX_FILTER_LINEAR});
	draw_shared->terrain_sampler = driver->new_sampler(driver, (DriftGfxSamplerOptions){.address_x = DRIFT_GFX_ADDRESS_MODE_REPEAT, .address_y = DRIFT_GFX_ADDRESS_MODE_REPEAT});
	
	static const DriftGfxShaderDesc light_desc = {
		.vertex[0] = {.type = DRIFT_TYPE_FLOAT32_2},
		.vertex_stride = sizeof(DriftVec2),
		.vertex[1] = {.type = DRIFT_TYPE_FLOAT32_4, .offset = offsetof(DriftLight, matrix.a), .instanced = true},
		.vertex[2] = {.type = DRIFT_TYPE_FLOAT32_2, .offset = offsetof(DriftLight, matrix.x), .instanced = true},
		.vertex[3] = {.type = DRIFT_TYPE_FLOAT32_4, .offset = offsetof(DriftLight, color), .instanced = true},
		.vertex[4] = {.type = DRIFT_TYPE_U8_4, .offset = offsetof(DriftLight, frame.bounds), .instanced = true},
		.vertex[5] = {.type = DRIFT_TYPE_U8_2, .offset = offsetof(DriftLight, frame.anchor), .instanced = true},
		.vertex[6] = {.type = DRIFT_TYPE_U16, .offset = offsetof(DriftLight, frame.layer), .instanced = true},
		.instance_stride = sizeof(DriftLight),
		DRIFT_GLOBAL_BINDINGS,
	};
	draw_shared->light_pipeline[0] = MakePipeline(driver, "light0", &light_desc, &DriftGfxBlendModeAdd, draw_shared->lightfield_target[0], DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->light_pipeline[1] = MakePipeline(driver, "light1", &light_desc, &DriftGfxBlendModeAdd, draw_shared->lightfield_target[1], DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxShaderDesc light_blit_desc = {
		.vertex[0] = {.type = DRIFT_TYPE_FLOAT32_2},
		.vertex_stride = sizeof(DriftVec2),
		DRIFT_GLOBAL_BINDINGS,
	};
	draw_shared->light_blit_pipeline[0] = MakePipeline(driver, "light_blit0", &light_blit_desc, NULL, draw_shared->shadowfield_target[0], DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->light_blit_pipeline[1] = MakePipeline(driver, "light_blit1", &light_blit_desc, NULL, draw_shared->shadowfield_target[1], DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxShaderDesc shadow_mask_desc = {
		.vertex[0] = {.type = DRIFT_TYPE_FLOAT32_2},
		.vertex_stride = sizeof(DriftVec2),
		.vertex[1] = {.type = DRIFT_TYPE_FLOAT32_4, .instanced = true},
		.instance_stride = sizeof(DriftVec4),
		DRIFT_GLOBAL_BINDINGS,
		.uniform[1] = "Locals",
	};
	
	static const DriftGfxBlendMode shadow_mask_blend = {
		.color_op = DRIFT_GFX_BLEND_OP_ADD, .color_src_factor = DRIFT_GFX_BLEND_FACTOR_ZERO, .color_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE,
		.alpha_op = DRIFT_GFX_BLEND_OP_ADD, .alpha_src_factor = DRIFT_GFX_BLEND_FACTOR_ONE, .alpha_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE,
	};
	
	draw_shared->shadow_mask_pipeline[0] = MakePipeline(driver, "shadow_mask0", &shadow_mask_desc, &shadow_mask_blend, draw_shared->shadowfield_target[0], DRIFT_GFX_CULL_MODE_FRONT);
	draw_shared->shadow_mask_pipeline[1] = MakePipeline(driver, "shadow_mask1", &shadow_mask_desc, &shadow_mask_blend, draw_shared->shadowfield_target[1], DRIFT_GFX_CULL_MODE_FRONT);
	
	static const DriftGfxShaderDesc primitive_desc = {
		.vertex[0] = {.type = DRIFT_TYPE_FLOAT32_2},
		.vertex_stride = sizeof(DriftVec2),
		.vertex[1] = {.type = DRIFT_TYPE_FLOAT32_4, .offset = offsetof(DriftPrimitive, p0), .instanced = true},
		.vertex[2] = {.type = DRIFT_TYPE_FLOAT32_2, .offset = offsetof(DriftPrimitive, radii), .instanced = true},
		.vertex[3] = {.type = DRIFT_TYPE_UNORM8_4, .offset = offsetof(DriftPrimitive, color), .instanced = true},
		.instance_stride = sizeof(DriftPrimitive),
		DRIFT_GLOBAL_BINDINGS,
		.uniform[1] = "Locals",
	};
	
	static const DriftGfxBlendMode prim_blend = {
		.color_op = DRIFT_GFX_BLEND_OP_ADD, .color_src_factor = DRIFT_GFX_BLEND_FACTOR_ONE, .color_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.alpha_op = DRIFT_GFX_BLEND_OP_ADD, .alpha_src_factor = DRIFT_GFX_BLEND_FACTOR_ZERO, .alpha_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE,
	};
	
	draw_shared->primitive_pipeline = MakePipeline(driver, "primitive", &primitive_desc, &prim_blend, draw_shared->color_target[0], DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxBlendMode shadow_blend = {
		.color_op = DRIFT_GFX_BLEND_OP_ADD, .color_src_factor = DRIFT_GFX_BLEND_FACTOR_SRC_ALPHA_SATURATE, .color_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE,
		.alpha_op = DRIFT_GFX_BLEND_OP_ADD, .alpha_src_factor = DRIFT_GFX_BLEND_FACTOR_ONE, .alpha_dst_factor = DRIFT_GFX_BLEND_FACTOR_ZERO,
	};
	
	draw_shared->shadow_pipeline[0] = MakePipeline(driver, "shadow0", &light_desc, &shadow_blend, draw_shared->shadowfield_target[0], DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->shadow_pipeline[1] = MakePipeline(driver, "shadow1", &light_desc, &shadow_blend, draw_shared->shadowfield_target[1], DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxShaderDesc terrain_desc = {
		.vertex[0] = {.type = DRIFT_TYPE_FLOAT32_2},
		.vertex_stride = sizeof(DriftVec2),
		.vertex[1] = {.type = DRIFT_TYPE_FLOAT32_4, .instanced = true},
		.instance_stride = sizeof(DriftTerrainChunk),
		DRIFT_GLOBAL_BINDINGS,
		.sampler[2] = "_repeat",
		.texture[1] = "_tiles",
	};
	draw_shared->terrain_pipeline = MakePipeline(driver, "terrain", &terrain_desc, NULL, draw_shared->color_target[0], DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->debug_terrain_pipeline = MakePipeline(driver, "debug_terrain", &terrain_desc, NULL, draw_shared->color_target[0], DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxShaderDesc sprite_desc = {
		.vertex[0] = {.type = DRIFT_TYPE_FLOAT32_2,},
		.vertex_stride = sizeof(DriftVec2),
		.vertex[1] = {.type = DRIFT_TYPE_FLOAT32_4, .offset = offsetof(DriftSprite, matrix.a), .instanced = true},
		.vertex[2] = {.type = DRIFT_TYPE_FLOAT32_4, .offset = offsetof(DriftSprite, matrix.x), .instanced = true},
		.vertex[3] = {.type = DRIFT_TYPE_UNORM8_4, .offset = offsetof(DriftSprite, color), .instanced = true},
		.vertex[4] = {.type = DRIFT_TYPE_U8_4, .offset = offsetof(DriftSprite, frame.bounds), .instanced = true},
		.vertex[5] = {.type = DRIFT_TYPE_U8_2, .offset = offsetof(DriftSprite, frame.anchor), .instanced = true},
		.vertex[6] = {.type = DRIFT_TYPE_U16, .offset = offsetof(DriftSprite, frame.layer), .instanced = true},
		.instance_stride = sizeof(DriftSprite),
		DRIFT_GLOBAL_BINDINGS,
	};
	
	static const DriftGfxBlendMode premultiplied = {
		.color_op = DRIFT_GFX_BLEND_OP_ADD, .color_src_factor = DRIFT_GFX_BLEND_FACTOR_ONE, .color_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.alpha_op = DRIFT_GFX_BLEND_OP_ADD, .alpha_src_factor = DRIFT_GFX_BLEND_FACTOR_ZERO, .alpha_dst_factor = DRIFT_GFX_BLEND_FACTOR_ONE,
	};
	
	draw_shared->sprite_pipeline = MakePipeline(driver, "sprite", &sprite_desc, &premultiplied, draw_shared->color_target[0], DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->overlay_sprite_pipeline = MakePipeline(driver, "overlay_sprite", &sprite_desc, &premultiplied, draw_shared->color_target[0], DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxShaderDesc debug_lightfield_desc = {
		.vertex[0] = {.type = DRIFT_TYPE_FLOAT32_2},
		.vertex_stride = sizeof(DriftVec2),
		DRIFT_GLOBAL_BINDINGS,
	};
	draw_shared->debug_lightfield_pipeline = MakePipeline(driver, "debug_lightfield", &debug_lightfield_desc, NULL, draw_shared->color_target[0], DRIFT_GFX_CULL_MODE_NONE);
	
	static const DriftGfxShaderDesc present_desc = {
		.vertex[0] = {.type = DRIFT_TYPE_FLOAT32_2},
		.vertex_stride = sizeof(DriftVec2),
		DRIFT_GLOBAL_BINDINGS,
		.texture[1] = "_texture",
	};
	draw_shared->present_pipeline = MakePipeline(driver, "present", &present_desc, NULL, NULL, DRIFT_GFX_CULL_MODE_NONE);
	draw_shared->debug_delta_pipeline = MakePipeline(driver, "debug_delta", &present_desc, &DriftGfxBlendModePremultipliedAlpha, NULL, DRIFT_GFX_CULL_MODE_NONE);
	
	tina_job_wait(job, &jobs, 0);
	return draw_shared;
}

void DriftDrawSharedFree(DriftDrawShared* draw_shared){
	DriftDealloc(DriftSystemMem, draw_shared, sizeof(*draw_shared));
}

DriftDraw* DriftDrawCreate(DriftGameContext* ctx, tina_job* job){
	DriftZoneMem* zone = DriftZoneMemAquire(ctx->app->zone_heap, "DrawMem");
	DriftMem* mem = DriftZoneMemWrap(zone);
	
	DriftGfxRenderer* renderer = DriftAppBeginFrame(ctx->app, zone);
	DriftVec2 pixel_extent = DriftGfxRendererDefaultExtent(renderer);
	DriftVec2 screen_extent = DriftDrawScreenExtent(pixel_extent);
	DriftVec2 buffer_extent = DriftDrawBufferExtent(screen_extent);
	
	DriftDraw* draw = DriftZoneMemAlloc(zone, sizeof(*draw));
	(*draw) = (DriftDraw){
		.ctx = ctx, .state = &ctx->state, .shared = ctx->draw_shared, .job = job,
		.zone = zone, .mem = mem, .renderer = renderer,
		.pixel_extent = pixel_extent, .screen_extent = screen_extent, .buffer_extent = buffer_extent,
		
		.terrain_chunks = DRIFT_ARRAY_NEW(mem, 64, DriftTerrainChunk),
		.lights = DRIFT_ARRAY_NEW(mem, 2048, DriftLight),
		.shadow_masks = DRIFT_ARRAY_NEW(mem, 2048, DriftSegment),
		.bg_sprites = DRIFT_ARRAY_NEW(mem, 2048, DriftSprite),
		.bg_prims = DRIFT_ARRAY_NEW(mem, 2048, DriftPrimitive),
		.fg_sprites = DRIFT_ARRAY_NEW(mem, 2048, DriftSprite),
		.overlay_sprites = DRIFT_ARRAY_NEW(mem, 2048, DriftSprite),
		.overlay_prims = DRIFT_ARRAY_NEW(mem, 2048, DriftPrimitive),
		.hud_sprites = DRIFT_ARRAY_NEW(mem, 2048, DriftSprite),
		.input_icons = DRIFT_INPUT_ICON_SETS[ctx->input.icon_type],
	};
	
	DriftDrawResizeBuffers(draw, buffer_extent);
	
	return draw;
}

DriftVec2 DriftDrawScreenExtent(DriftVec2 pixel_extent){
	float scale = floorf(fmaxf(1.0f, fmaxf(pixel_extent.x/DEFAULT_EXTENT.x, pixel_extent.y/DEFAULT_EXTENT.y)));
	return (DriftVec2){pixel_extent.x/scale, pixel_extent.y/scale};
}

DriftVec2 DriftDrawBufferExtent(DriftVec2 screen_extent){
	return (DriftVec2){-(-(uint)screen_extent.x & -DRIFT_LIGHTFIELD_SCALE), -(-(uint)screen_extent.y & -DRIFT_LIGHTFIELD_SCALE)};
}

void DriftDrawResizeBuffers(DriftDraw* draw, DriftVec2 extent){
	DriftDrawShared* draw_shared = draw->shared;
	if(extent.x != draw_shared->color_buffer_size.x || extent.y != draw_shared->color_buffer_size.y){
		void* objects[] = {
			draw_shared->color_target[0], draw_shared->color_target[1], draw_shared->color_buffer[0], draw_shared->color_buffer[1],
			draw_shared->lightfield_target[0], draw_shared->lightfield_target[1], draw_shared->lightfield_buffer,
			draw_shared->shadowfield_target[0], draw_shared->shadowfield_target[1], draw_shared->shadowfield_buffer,
		};
		
		uint queue = tina_job_switch_queue(draw->job, DRIFT_JOB_QUEUE_GFX);
		draw_shared->driver->free_objects(draw_shared->driver, objects, sizeof(objects)/sizeof(*objects));
		DriftDrawSharedMakeBuffers(draw_shared, extent);
		tina_job_switch_queue(draw->job, queue);
	}
}

static void DriftDrawSetDefaultBindings(DriftDraw* draw, DriftGfxPipelineBindings* bindings){
	DriftDrawShared* draw_shared = draw->shared;
	bindings->uniforms[0] = draw->globals_binding;
	bindings->samplers[0] = draw_shared->nearest_sampler;
	bindings->samplers[1] = draw_shared->linear_sampler;
	bindings->textures[0] = draw_shared->atlas_texture;
	bindings->textures[5] = draw->color_buffer_prev;
	bindings->textures[6] = draw_shared->lightfield_buffer;
	bindings->textures[7] = draw_shared->shadowfield_buffer;
}

DriftGfxPipelineBindings* DriftDrawQuads(DriftDraw* draw, DriftGfxPipeline* pipeline, u32 count){
	DriftGfxRenderer* renderer = draw->renderer;
	DriftGfxPipelineBindings* bindings = DriftGfxRendererPushBindPipelineCommand(renderer, pipeline);
	DriftGfxRendererPushDrawIndexedCommand(renderer, draw->quad_index_binding, 6, count);
	bindings->vertex = draw->quad_vertex_binding;
	DriftDrawSetDefaultBindings(draw, bindings);
	return bindings;
}

DriftAffine DriftDrawText(DriftDraw* draw, DRIFT_ARRAY(DriftSprite)* array, DriftAffine matrix, const char* string){
	uint font_w = 5, font_h = 11;
	uint glyph_w = 5, glyph_h = 10;
	uint baseline = 2;
	
	DriftAffine t = matrix;
	DriftVec2 line_origin = {t.x, t.y};
	
	size_t length = strlen(string);
	DriftSprite* instances = DRIFT_ARRAY_RANGE(*array, length);
	
	DriftRGBA8 color = DRIFT_RGBA8_WHITE;
	const char* cursor = string;
	while(*cursor){
		char c = *(cursor++);
		switch(c){
			default: {
				// TODO Magic numbers.
				uint x = ((c*8) & 0xFF);
				uint y = ((c/2) & 0xF0);
				DriftSpriteFrame frame = {.bounds = {x, y, x + (font_w - 1), y + (glyph_h - 1)}, .anchor.y = baseline, .layer = DRIFT_ATLAS_TEXT};
				*instances++ = (DriftSprite){.frame = frame, .color = color, .matrix = t};
			} goto advance;
			case ' ': advance: {
				t.x += (font_w + 1)*matrix.a;
				t.y += (font_w + 1)*matrix.b;
			} break;
			case '\n': {
				t.x = (line_origin.x -= font_h*matrix.c);
				t.y = (line_origin.y -= font_h*matrix.d);
			} break;
			
			case '{': {
				c = *cursor;
				switch(c){
					case '{': continue;
					case '#': {
						cursor++;
						DRIFT_ASSERT_HARD(strlen(cursor) > 8, "Truncated color tag.");
						DRIFT_ASSERT(cursor[8] == '}', "Color tag not closed.");
						
						static const u8 HEXN[256] = {
							['0'] = 0x0, ['1'] = 0x1, ['2'] = 0x2, ['3'] = 0x3, ['4'] = 0x4, ['5'] = 0x5, ['6'] = 0x6, ['7'] = 0x7,
							['8'] = 0x8, ['9'] = 0x9, ['A'] = 0xA, ['B'] = 0xB, ['C'] = 0xC, ['D'] = 0xD, ['E'] = 0xE, ['F'] = 0xF,
						};
						
						color.r = 16*HEXN[(uint)cursor[0]] + HEXN[(uint)cursor[1]];
						color.g = 16*HEXN[(uint)cursor[2]] + HEXN[(uint)cursor[3]];
						color.b = 16*HEXN[(uint)cursor[4]] + HEXN[(uint)cursor[5]];
						color.a = 16*HEXN[(uint)cursor[6]] + HEXN[(uint)cursor[7]];
						cursor += 9;
					} break;
					case '@': {
						const DriftInputIcon* icons = draw->input_icons;
						const DriftInputIcon* icon = DriftInputIconFind(icons, cursor + 1);
						DRIFT_ASSERT(icon, "Icon '%s' not found.", cursor +1);
						cursor += strlen(icon->label) + 2;
						
						while(true){
							DriftSpriteFrame frame = DRIFT_SPRITE_FRAMES[icon->frame];
							
							*instances++ = (DriftSprite){frame, DRIFT_RGBA8_WHITE, t};
							t.x += icon->advance*(font_w + 1)*matrix.a;
							t.y += icon->advance*(font_w + 1)*matrix.b;
							
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
	
	DriftArrayRangeCommit(*array, instances);
	return t;
}

DriftAffine DriftDrawTextF(DriftDraw* draw, DRIFT_ARRAY(DriftSprite)* array, DriftAffine matrix, const char* format, ...){
	va_list args;
	va_start(args, format);
	
	char str[1024];
	vsnprintf(str, sizeof(str), format, args);
	
	va_end(args);
	return DriftDrawText(draw, array, matrix, str);
}
