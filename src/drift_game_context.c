/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <string.h>

#include "tina/tina.h"
#include <SDL.h>
#include "tracy/TracyC.h"

#include "drift_game.h"
#include "base/drift_nuklear.h"

static void DriftGameStateIO(DriftIO* io){
	DriftGameState* state = io->user_ptr;
	
	// Handle ECS data.
	DriftIOBlock(io, "entities", &state->entities, sizeof(state->entities));
	DRIFT_ARRAY_FOREACH(state->components, component) DriftComponentIO(*component, io);
	DriftIOBlock(io, "player", &state->player, sizeof(state->player));
	
	// Handle terrain.
	// TODO this saves density mips
	DriftIOBlock(io, "density", state->terra->tilemap.density, sizeof(state->terra->tilemap.density));
	DriftIOBlock(io, "resources", state->terra->tilemap.resources, sizeof(state->terra->tilemap.resources));
	DriftIOBlock(io, "biomass", state->terra->tilemap.biomass, sizeof(state->terra->tilemap.biomass));
	DriftIOBlock(io, "visibility", state->terra->tilemap.visibility, sizeof(state->terra->tilemap.visibility));
	if(io->read) DriftTerrainResetCache(state->terra);
	
	// Handle other tables.
	DRIFT_ARRAY_FOREACH(state->tables, table) DriftTableIO(*table, io);
	
	DriftIOBlock(io, "storage", state->inventory.skiff, sizeof(state->inventory.skiff));
	DriftIOBlock(io, "transit", state->inventory.transit, sizeof(state->inventory.transit));
	DriftIOBlock(io, "cargo", state->inventory.cargo, sizeof(state->inventory.cargo));
	DriftIOBlock(io, "scan_progress", state->scan_progress, sizeof(state->scan_progress));
}

void DriftGameStateSave(DriftGameState* state){
	DriftIOFileWrite(TMP_SAVE_FILENAME, DriftGameStateIO, state);
}

bool DriftGameStateLoad(DriftGameState* state){
	return DriftIOFileRead(TMP_SAVE_FILENAME, DriftGameStateIO, state);
}

DriftEntity DriftMakeEntity(DriftGameState* state){
	DriftAssertMainThread();
	DriftEntity e = DriftEntitySetAquire(&state->entities, 0);
	return e;
}

DriftEntity DriftMakeHotEntity(DriftGameState* state){
	DriftEntity e = DriftMakeEntity(state);
	DRIFT_ARRAY_PUSH(state->hot_entities, e);
	return e;
}

void DriftDestroyEntity(DriftGameState* state, DriftEntity entity){
	DriftAssertMainThread();
	DRIFT_ARRAY_PUSH(state->dead_entities, entity);
}

DriftGameState* DriftGameStateNew(tina_job* job){
	DriftMem* mem = DriftListMemNew(DriftSystemMem, "GameState Mem");
	DriftGameState* state = DriftAlloc(mem, sizeof(*state));
	memset(state, 0, sizeof(*state));
	
	state->mem = mem;
	state->tables = DRIFT_ARRAY_NEW(state->mem, 0, DriftTable*);
	state->components = DRIFT_ARRAY_NEW(state->mem, 0, DriftComponent*);
	DriftMapInit(&state->named_components, state->mem, "NamedComponents", 0);
	state->hot_entities = DRIFT_ARRAY_NEW(state->mem, 0, DriftEntity);
	state->dead_entities = DRIFT_ARRAY_NEW(state->mem, 256, DriftEntity);
	
	// TODO put these somewhere else?
	state->debug.sprites = DRIFT_ARRAY_NEW(state->mem, 0, DriftSprite);
	state->debug.prims = DRIFT_ARRAY_NEW(state->mem, 0, DriftPrimitive);
	
	DriftEntitySetInit(&state->entities);
	DriftSystemsInit(state);
	state->terra = DriftTerrainNew(job, false);
	
	return state;
}

void DriftGameStateFree(DriftGameState* state){
	DriftListMemFree(state->mem);
}

void DriftGameStateSetupIntro(DriftGameState* state){
	state->status.needs_tutorial = true;
	
	{ // TODO Initialize home
		DriftEntity e = DriftMakeEntity(state);
		uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
		state->transforms.matrix[transform_idx] = (DriftAffine){1, 0, 0, 1, DRIFT_SKIFF_POSITION.x, DRIFT_SKIFF_POSITION.y};
		uint scan_idx = DriftComponentAdd(&state->scan.c, e);
		state->scan.type[scan_idx] = DRIFT_SCAN_CONSTRUCTION_SKIFF;
		uint sui_idx = DriftComponentAdd(&state->scan_ui.c, e);
		state->scan_ui.type[sui_idx] = DRIFT_SCAN_UI_FABRICATOR;
		
		uint pnode_idx = DriftComponentAdd(&state->power_nodes.c, e);
		state->power_nodes.position[pnode_idx] = DRIFT_SKIFF_POSITION;
		
		// Mark the node as a root in the flow map.
		uint idx0 = DriftComponentAdd(&state->flow_maps[0].c, e);
		state->flow_maps[0].flow[idx0].next = e;
		state->flow_maps[0].flow[idx0].dist = 0;
	}{ // TODO initialize temporary power nodes
		static const DriftVec2 nodes[] = {
			{4420.767f, -4378.459f}, {2963.865f, -2405.373f}, {2957.077f, -2726.594f}, {2927.418f, -2552.352f}, {2939.031f, -2218.401f},
			{3096.513f, -2763.118f}, {2885.800f, -3020.700f}, {4582.589f, -4319.745f}, {1905.969f, -1822.248f}, {2071.023f, -1727.985f},
			{2844.675f, -3161.038f}, {2962.052f, -3294.830f}, {4150.274f, -3931.100f}, {4333.772f, -4101.141f}, {4143.719f, -4122.004f},
			{4616.612f, -4184.726f}, {4495.841f, -4078.960f}, {4261.144f, -4273.758f}, {2846.927f, -2051.307f}, {2714.250f, -1912.650f},
			{2522.985f, -1910.494f}, {2380.958f, -1831.103f}, {1878.301f, -2139.947f}, {2206.651f, -1835.729f}, {1787.889f, -1972.333f},
			{2956.574f, -2875.241f}, 
		};
		
		u8 buffer[16*1024];
		DriftMem* mem = DriftLinearMemMake(buffer, sizeof(buffer), "startup mem");
		for(uint i = 0; i < sizeof(nodes)/sizeof(*nodes); i++){
			DriftEntity e = DriftItemMake(state, DRIFT_ITEM_POWER_NODE, nodes[i], DRIFT_VEC2_ZERO, 0);
			DriftPowerNodeActivate(state, e, mem);
		}
	}
}

static DriftGameContext* DriftGameContextCreate(tina_job* job){
	DriftGameContext* ctx = DriftAlloc(DriftSystemMem, sizeof(*ctx));
	memset(ctx, 0x00, sizeof(*ctx));
	ctx->mu = DriftUIInit();
	ctx->debug.ui = DriftNuklearNew();
	
	ctx->reverb.dynamic = true;
	ctx->reverb.size = 1.5f;
	ctx->reverb.dry = 0.60f;
	ctx->reverb.wet = 0.006f;
	ctx->reverb.decay = 0.86f;
	ctx->reverb.cutoff = 0.26f;
	
	ctx->init_nanos = ctx->clock_nanos = DriftTimeNanos();
	return ctx;
}

void DriftGameStart(tina_job* job){
	TracyCZoneN(ZONE_START, "Game start", true);
	
	// TODO need to move this deeper into the event loop
	DriftGameContext* ctx = APP->app_context;
	if(ctx == NULL){
		// No context, normal startup.
		TracyCZoneN(ZONE_CREATE, "Create Context", true);
		ctx = APP->app_context = DriftGameContextCreate(job);
		TracyCZoneEnd(ZONE_CREATE);
	} else {
		// Hotload re-entry
		ctx->debug.reset_ui = true;
	}
	
	static const char* NAMES[_DRIFT_SFX_COUNT] = {
		#include "sound_defs.inc"
	};
	
	tina_group audio_group = {};
	DriftAudioLoadSamples(job, NAMES, _DRIFT_SFX_COUNT);
	
	uint queue = tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
	DriftDrawShared* draw_shared = ctx->draw_shared = DriftDrawSharedNew(job, 2);
	DriftNuklearSetupGFX(ctx->debug.ui, ctx->draw_shared);
	tina_job_switch_queue(job, queue);
	
	TracyCZoneN(ZONE_AUDIO, "load audio", true);
	tina_job_wait(job, &audio_group, 0);
	DriftAudioStartMusic();
	DriftAudioPause(false);
	TracyCZoneEnd(ZONE_AUDIO);
	
	DriftUIHotload(ctx->mu);
	TracyCZoneEnd(ZONE_START);
	
	DriftAppShowWindow();
	DriftLoopYield yield = DriftMenuLoop(job, ctx);
	
	DriftDrawSharedFree(ctx->draw_shared);
	queue = tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
	APP->gfx_driver->free_all(APP->gfx_driver);
	tina_job_switch_queue(job, queue);
	
	if(yield == DRIFT_LOOP_YIELD_HOTLOAD){
		// TODO alias callbacks?
		DriftAudioPause(true);
	}
	
	if(yield != DRIFT_LOOP_YIELD_HOTLOAD) DriftAppHaltScheduler();
}

double DriftGameContextUpdateNanos(DriftGameContext* ctx){
	u64 prev_nanos = ctx->clock_nanos;
	ctx->clock_nanos = DriftTimeNanos();
	return ctx->clock_nanos - prev_nanos;
}

static DriftVec2 light_frame_center(DriftFrame frame){
	DriftVec2 offset = {(s8)frame.anchor.x, (s8)frame.anchor.y};
	offset.x = 1 - 2*offset.x/(frame.bounds.r - frame.bounds.l + 1);
	offset.y = 1 - 2*offset.y/(frame.bounds.t - frame.bounds.b + 1);
	return offset;
}

static u16* plasma_indexes(u16* iptr, u16 i){
	*iptr++ = 0 + i; *iptr++ = 4 + i; *iptr++ = 3 + i;
	*iptr++ = 0 + i; *iptr++ = 1 + i; *iptr++ = 4 + i;
	return iptr;
}

static DriftPlasmaVert* plasma_vertexes(DriftPlasmaVert* vptr, u16 i, float value){
	*vptr++ = (DriftPlasmaVert){.pos = {i, +1}, .value = value};
	*vptr++ = (DriftPlasmaVert){.pos = {i,  0}, .value = value};
	*vptr++ = (DriftPlasmaVert){.pos = {i, -1}, .value = value};
	return vptr;
}

static void render_plasma(DriftDraw* draw){
	DriftGfxRenderer* renderer = draw->renderer;
	
	uint vcount = DRIFT_PLASMA_N;
	DriftGfxBufferSlice geo_slice = DriftGfxRendererPushGeometry(renderer, NULL, 3*vcount*sizeof(DriftPlasmaVert));
	DriftGfxBufferSlice idx_slice = DriftGfxRendererPushIndexes(renderer, NULL, 12*vcount*sizeof(u16));
	
	float* plasma = DriftGenPlasma(draw);
	DriftPlasmaVert* vptr = geo_slice.ptr;
	u16* iptr = idx_slice.ptr;
	for(uint i = 0; i < vcount; i++){
		vptr = plasma_vertexes(vptr, i, plasma[i]);
		iptr = plasma_indexes(iptr, 3*i + 0);
		iptr = plasma_indexes(iptr, 3*i + 1);
	}
	
	DriftGfxPipelineBindings* bindings = DriftGfxRendererPushBindPipelineCommand(renderer, draw->shared->plasma_pipeline);
	*bindings = draw->default_bindings;
	bindings->vertex = geo_slice.binding;
	
	DRIFT_ARRAY(DriftSegment) strands = draw->plasma_strands;
	bindings->instance = DriftGfxRendererPushGeometry(renderer, strands, DriftArraySize(strands)).binding;
	DriftGfxRendererPushDrawIndexedCommand(renderer, idx_slice.binding, 12*(vcount - 1), DriftArrayLength(strands));
}

void DriftGameStateRender(DriftDraw* draw){
	DriftGfxRenderer* renderer = draw->renderer;
	DriftDrawShared* draw_shared = draw->shared;
	
	if(draw->ctx->debug.boost_ambient){
		DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = {{0.5f, 0.5f, 0.5f, 0}},
			.matrix = DriftAffineMul(draw->vp_inverse, (DriftAffine){4, 0, 0, 4, 0, 0}),
		}));
	}
	
	uint light_count = DriftArrayLength(draw->lights);
	
	// Debug draw lights.
	if(false){
		for(uint i = 0; i < light_count; i++){
			DriftAffine light_matrix = draw->lights[i].matrix;
			float radius = draw->lights[i].radius;
			
			if(radius == 0){
				DriftDebugCircle(draw->state, DriftAffineOrigin(light_matrix), 2, DRIFT_RGBA8_YELLOW);
			} else {
				DriftVec2 origin = DriftAffineOrigin(light_matrix);
				DriftDebugCircle(draw->state, origin, radius, DRIFT_RGBA8_YELLOW);
				DriftDebugTransform(draw->state, light_matrix, 0.1f);
			}
		}
	}
	
	// Render the lightfield buffer.
	DriftGfxBufferBinding light_instances_binding = DriftGfxRendererPushGeometry(draw->renderer, draw->lights, light_count*sizeof(*draw->lights)).binding;
	for(uint pass = 0; pass < 2; pass++){
		DriftGfxRendererPushBindTargetCommand(renderer, draw_shared->lightfield_target[pass], DRIFT_VEC4_CLEAR);
		DriftGfxPipelineBindings* bindings = DriftDrawQuads(draw, draw_shared->light_pipeline[pass], light_count);
		bindings->instance = light_instances_binding;
	}

	// Calculate conservative bounds for required occluders starting with the view bounds.
	// TODO This should go into DriftDraw maybe?
	DriftAffine vp_inv = draw->vp_inverse;
	float hw = fabsf(vp_inv.a) + fabsf(vp_inv.c), hh = fabsf(vp_inv.b) + fabsf(vp_inv.d);
	DriftAABB2 screen_bounds = {vp_inv.x - hw, vp_inv.y - hh, vp_inv.x + hw, vp_inv.y + hh};
	DriftAABB2 shadow_bounds = screen_bounds;
	
	// Count shadowed lights and find bounds.
	uint shadow_count = 0;
	for(uint i = 0; i < light_count; i++){
		// DriftDebugTransform(draw->state, draw->lights[i].matrix, 1e-1f);
		
		float radius = draw->lights[i].radius;
		if(radius > 0){
			DriftAffine light_matrix = draw->lights[i].matrix;
			// DriftDebugCircle2(draw->state, DriftAffineOrigin(light_matrix), radius, radius - 1, (DriftRGBA8){0xFF, 0xFF, 0x00, 0x80});
			
			DriftVec2 center = light_frame_center(draw->lights[i].frame);
			if(DriftAffineVisibility(DriftAffineMul(draw->vp_matrix, light_matrix), center, DRIFT_VEC2_ONE)){
				// Expand the bounds to include the light + radius;
				DriftVec2 origin = DriftAffineOrigin(light_matrix);
				float radius = draw->lights[i].radius;
				DriftAABB2 light_bounds = {origin.x - radius, origin.y - radius, origin.x + radius, origin.y + radius};
				shadow_bounds = DriftAABB2Merge(shadow_bounds, light_bounds);
				shadow_count++;
			} else {
				// Light is not visible, clear it's shadow flag.
				draw->lights[i].radius = 0;
			}
		}
	}
	
	// Prep shadow light render structs.(+1 to avoid 0 length VLAs)
	DriftGfxBufferBinding shadow_uniform_bindings[shadow_count + 1];
	DriftGfxBufferBinding light_instance_bindings[shadow_count + 1];
	DriftAABB2 scissor_bounds[shadow_count + 1];
	
	// Need to convert light bounds from world to pixel coords for scissoring.
	DriftVec2 shadowfield_extent = DriftVec2Mul(draw->internal_extent, 1.0f/draw->shared->lightfield_scale);
	DriftAffine shadowfield_ortho = DriftAffineOrtho(0, shadowfield_extent.x, 0, shadowfield_extent.y);
	DriftAffine scissor_matrix = DriftAffineMul(DriftAffineInverse(shadowfield_ortho), draw->vp_matrix);
	
	for(uint i = 0, j = 0; i < light_count; i++){
		if(draw->lights[i].radius > 0){
			DriftAffine light_matrix = draw->lights[i].matrix;
			
			struct {
				DriftGPUMatrix matrix;
				float radius;
			} uniforms = {
				.matrix = DriftAffineToGPU(light_matrix),
				.radius = draw->lights[i].radius,
			};
			
			shadow_uniform_bindings[j] = DriftGfxRendererPushUniforms(renderer, &uniforms, sizeof(uniforms)).binding;
			light_instance_bindings[j] = light_instances_binding;
			
			DriftAffine m_bound = DriftAffineMul(scissor_matrix, light_matrix);
			DriftVec2 center = light_frame_center(draw->lights[i].frame);
			m_bound.x += m_bound.a*center.x + m_bound.c*center.y;
			m_bound.y += m_bound.b*center.x + m_bound.d*center.y;
			
			// TODO this is done a bunch, move to math?
			float hw = fabsf(m_bound.a) + fabsf(m_bound.c), hh = fabsf(m_bound.b) + fabsf(m_bound.d);
			scissor_bounds[j] = (DriftAABB2){m_bound.x - hw, m_bound.y - hh, m_bound.x + hw, m_bound.y + hh};
			
			j++;
		}

		// These are already buffered by the lightfield step, so it needs to skip over non-shadow lights.
		light_instances_binding.offset += sizeof(DriftLight);
	}
	
	DriftTerrainGatherShadows(draw, draw->state->terra, shadow_bounds);
	
	// Push the shadow mask data.
	uint shadow_mask_count = DriftArrayLength(draw->shadow_masks);
	DriftGfxBufferBinding shadow_mask_instances = DriftGfxRendererPushGeometry(renderer, draw->shadow_masks, shadow_mask_count*sizeof(*draw->shadow_masks)).binding;
	
	for(uint pass = 0; pass < 2; pass++){
		DriftGfxRendererPushBindTargetCommand(renderer, draw_shared->shadowfield_target[pass], DRIFT_VEC4_CLEAR);
		DriftDrawQuads(draw, draw_shared->light_blit_pipeline[pass], 1);

		for(uint i = 0; i < shadow_count; i++){
			DriftGfxRendererPushScissorCommand(renderer, scissor_bounds[i]);

			// Render shadow mask.
			DriftGfxPipelineBindings* shadow_mask_bindings = DriftDrawQuads(draw, draw_shared->shadow_mask_pipeline[pass], shadow_mask_count);
			shadow_mask_bindings->instance = shadow_mask_instances;
			shadow_mask_bindings->uniforms[1] = shadow_uniform_bindings[i];

			// Accumulate shadowfield and clear the mask.
			DriftGfxPipelineBindings* shadow_bindings = DriftDrawQuads(draw, draw_shared->shadow_pipeline[pass], 1);
			shadow_bindings->instance = light_instance_bindings[i];
		}
		
		// Disable scissor test.
		DriftGfxRendererPushScissorCommand(renderer, DRIFT_AABB2_ALL);
	}

	// Render the color buffer.
	DriftGfxTexture* color_buffer = draw->color_buffer_curr;
	
	DriftGfxPipeline* terrain_pipeline = draw_shared->terrain_pipeline;
	if(draw->ctx->debug.draw_terrain_sdf) terrain_pipeline = draw_shared->debug_terrain_pipeline;
	DriftGfxPipelineBindings terrain_bindings = draw->default_bindings;
	terrain_bindings.samplers[2] = draw_shared->repeat_sampler;
	terrain_bindings.textures[1] = draw_shared->terrain_tiles;
	
	DriftDrawBatch terrain_batch = {.arr = draw->terrain_chunks, .pipeline = terrain_pipeline, .bindings = &terrain_bindings};

	// Draw at least the terrain into the previous frame buffer if it's undefined.
	// Avoid crud on the first frame or after resizing/fullscreen swaps.
	if(draw->prev_buffer_invalid){
		DriftGfxRendererPushBindTargetCommand(renderer, draw->color_target_prev, DRIFT_VEC4_CLEAR);
		DriftDrawBatches(draw, (DriftDrawBatch[]){terrain_batch, {}});
	}
	
	DriftGfxRendererPushBindTargetCommand(renderer, draw->color_target_curr, DRIFT_VEC4_CLEAR);
	DriftDrawBatches(draw, (DriftDrawBatch[]){
		terrain_batch,
		{.arr = draw->bg_sprites, .pipeline = draw_shared->sprite_pipeline, .bindings = &draw->default_bindings},
		{.arr = draw->bg_prims, .pipeline = draw_shared->linear_primitive_pipeline, .bindings = &draw->default_bindings},
		{},
	});
	
	render_plasma(draw);
	
	DriftDrawBatches(draw, (DriftDrawBatch[]){
		{.arr = draw->bullet_sprites, .pipeline = draw_shared->sprite_pipeline, .bindings = &draw->default_bindings},
		{.arr = draw->fg_sprites, .pipeline = draw_shared->sprite_pipeline, .bindings = &draw->default_bindings},
		{.arr = draw->flash_sprites, .pipeline = draw_shared->flash_sprite_pipeline, .bindings = &draw->default_bindings},
		{},
	});
	
	// DriftGfxPipelineBindings* bindings = DriftGfxRendererPushBindPipelineCommand(renderer, draw_shared->debug_lightfield_pipeline);
	// *bindings = draw->default_bindings;
	// DriftGfxRendererPushDrawIndexedCommand(renderer, draw->quad_index_binding, 6, 1);
	
	// Resolve HDR and effects
	DriftGameState* state = draw->state;
	DriftPlayerData* player = state->players.data + DriftComponentFind(&state->players.c, state->player);
	// uint health_idx = DriftComponentFind(&state->health.c, state->player);
	// DriftHealth* health = state->health.data + health_idx;
	// float low_health = 1;DriftSmoothstep(1.0f, 0.25f, health->value/health->maximum);
	float power = DriftSaturate(player->energy/DriftPlayerEnergyCap(state));
	float heat = player->is_overheated ? DriftSmoothstep(0.0f, 0.15f, player->temp) : DriftSmoothstep(0.25f, 1.0f, player->temp);
	
	DriftVec4 inner_tint = draw->screen_tint;
	// inner_tint = DriftVec4CMul(inner_tint, DriftVec4Lerp(DRIFT_VEC4_ONE, TMP_COLOR[0], low_health));
	DriftVec4 outer_tint = inner_tint;
	// outer_tint = DriftVec4CMul(outer_tint, DriftVec4Lerp(DRIFT_VEC4_ONE, TMP_COLOR[1], low_health));
	outer_tint = DriftVec4CMul(outer_tint, DriftVec4Lerp(DRIFT_VEC4_ONE, (DriftVec4){{0.13f, 0.39f, 0.76f}}, 1 - power));
	outer_tint = DriftVec4CMul(outer_tint, DriftVec4Lerp(DRIFT_VEC4_ONE, (DriftVec4){{2.54f, 0.84f, 0.56f}}, heat));
	
	struct {
		DriftVec4 scatter[5];
		DriftVec4 transmit[5];
		DriftVec4 effect_tint[2];
		float effect_static;
		float effect_heat;
	} effects = {
		.scatter = {
			// [DRIFT_BIOME_LIGHT] = (DriftVec4){{0.14f, 0.05f, 0.00f, 0.25f}},
			[DRIFT_BIOME_LIGHT] = (DriftVec4){{0.43f, 0.25f, 0.02f, 0.12f}},
			[DRIFT_BIOME_CRYO ] = (DriftVec4){{0.24f, 0.35f, 0.41f, 0.41f}},
			[DRIFT_BIOME_RADIO] = (DriftVec4){{0.25f, 0.32f, 0.09f, 0.26f}},
			[DRIFT_BIOME_DARK ] = (DriftVec4){{0.22f, 0.01f, 0.07f, 0.25f}},
			[DRIFT_BIOME_SPACE] = (DriftVec4){{0.05f, 0.05f, 0.05f, 0.00f}},
		},
		.transmit = {
			// [DRIFT_BIOME_LIGHT] = (DriftVec4){{0.96f, 0.89f, 0.79f, 0.00f}},// Linear
			[DRIFT_BIOME_LIGHT] = (DriftVec4){{0.99f, 0.92f, 0.86f, 1.00f}},
			[DRIFT_BIOME_CRYO ] = (DriftVec4){{0.79f, 0.87f, 0.96f, 0.00f}},
			[DRIFT_BIOME_RADIO] = (DriftVec4){{0.92f, 0.98f, 0.91f, 0.00f}},
			[DRIFT_BIOME_DARK ] = (DriftVec4){{0.98f, 0.73f, 0.88f, 0.00f}},
			[DRIFT_BIOME_SPACE] = (DriftVec4){{0.80f, 0.80f, 0.80f, 0.80f}},
		},
		.effect_tint = {inner_tint, outer_tint},
		.effect_static = 1 - power,
		.effect_heat = heat*heat,
	}; // TODO GL packing warning, wants 160 bytes
	
	if(draw->ctx->debug.disable_haze){
		for(uint i = 0; i < 5; i++){
			effects.scatter[i] = DRIFT_VEC4_BLACK;
			effects.transmit[i] = DRIFT_VEC4_WHITE;
		}
	}
	
	DriftGfxRendererPushBindTargetCommand(renderer, draw_shared->resolve_target, DRIFT_VEC4_CLEAR);
	DriftGfxPipelineBindings* resolve_bindings = DriftDrawQuads(draw, draw_shared->resolve_pipeline, 1);
	resolve_bindings->textures[1] = color_buffer;
	resolve_bindings->samplers[2] = draw_shared->repeat_sampler;
	resolve_bindings->uniforms[1] = DriftGfxRendererPushUniforms(renderer, &effects, sizeof(effects)).binding;
	
	DriftGfxPipelineBindings hud_bindings = draw->default_bindings;
	hud_bindings.uniforms[0] = draw->ui_binding;
	
	// Draw overlays and HUD
	DriftDrawBatches(draw, (DriftDrawBatch[]){
		{.arr = draw->state->debug.prims, .pipeline = draw_shared->overlay_primitive_pipeline, .bindings = &draw->default_bindings},
		{.arr = draw->state->debug.sprites, .pipeline = draw_shared->overlay_sprite_pipeline, .bindings = &draw->default_bindings},
		{.arr = draw->overlay_sprites, .pipeline = draw_shared->overlay_sprite_pipeline, .bindings = &draw->default_bindings},
		{.arr = draw->overlay_prims, .pipeline = draw_shared->overlay_primitive_pipeline, .bindings = &draw->default_bindings},
		{.arr = draw->hud_sprites, .pipeline = draw_shared->overlay_sprite_pipeline, .bindings = &hud_bindings},
		{},
	});
}

DriftComponent* DriftGameStateNamedComponentMake(DriftGameState* state, DriftComponent* component, const char* name, DriftColumnSet columns, uint capacity){
	DriftComponentInit(component, (DriftTableDesc){.name = name, .mem = state->mem, .min_row_capacity = capacity, .columns = columns});
	DRIFT_ARRAY_PUSH(state->components, component);
	
	uintptr_t check = DriftMapInsert(&state->named_components, DriftFNV64Str(component->table.desc.name), (uintptr_t)component);
	DRIFT_ASSERT_HARD(check == 0, "Duplicate hash in named components.");
	
	return component;
}

static void destroy_entities(DriftGameState* state, DriftEntity* list){
	DriftAssertMainThread();
	
	// Remove all components for the entities.
	DRIFT_ARRAY_FOREACH(state->components, component){
		DRIFT_ARRAY_FOREACH(list, e) DriftComponentRemove(*component, *e);
	}
	
	// Retire the entities.
	DRIFT_ARRAY_FOREACH(list, e){
		if(DriftEntitySetCheck(&state->entities, *e)) DriftEntitySetRetire(&state->entities, *e);
	}
	
	DriftArrayHeader(list)->count = 0;
}

#if DRIFT_MODULES
static void ResetHotComponents(DriftGameState* state){
	DriftComponent** components = state->components;
	DriftArray* header = DriftArrayHeader(components);
	DriftComponent** copy = DRIFT_ARRAY_NEW(header->mem, header->capacity, DriftComponent*);
	
	DRIFT_ARRAY_FOREACH(state->components, c){
		if((*c)->reset_on_hotload){
			DRIFT_COMPONENT_FOREACH(*c, i) DRIFT_ARRAY_PUSH(state->dead_entities, DriftComponentGetEntities(*c)[i]);
			DriftComponentDestroy(*c);
		} else {
			DRIFT_ARRAY_PUSH(copy, *c);
		}
	}
	
	state->components = copy;
	DriftArrayFree(components);
	destroy_entities(state, state->dead_entities);
}
#endif

static void DriftGameStateCleanup(DriftUpdate* update){
	DriftGameState* state = update->state;
	
	destroy_entities(state, update->state->dead_entities);
	
	static uint gc_cursor = 0;
	DriftTable* table = &state->power_edges.t;
	DriftPowerNodeEdge* edges = state->power_edges.edge;
	for(uint run = 0; table->row_count > 0 && run < 20;){
		uint idx = gc_cursor % table->row_count;
		DriftEntity e0 = edges[idx].e0, e1 = edges[idx].e1;
		if(DriftComponentFind(&state->power_nodes.c, e0) && DriftComponentFind(&state->power_nodes.c, e1)){
		// if(DriftEntitySetCheck(&state->entities, e0) && DriftEntitySetCheck(&state->entities, e1)){
			run++;
			gc_cursor++;
		} else {
			run = 0;
			DriftTableCopyRow(table, idx, --table->row_count);
		}
	}
}

static void update_reverb(tina_job* job){
	DriftUpdate* update = tina_job_get_description(job)->user_data;
	DriftGameState* state = update->state;
	DriftVec2 origin = DriftAffineOrigin(DriftAffineInverse(update->prev_vp_matrix));
	
	DriftTerrain* terra = state->terra;
	static DriftRandom rand = {}; // TODO static global
	float jitter = DriftRandomUNorm(&rand);
	
	uint n = 16;
	float arr[n];
	for(uint i = 0; i < n; i++){
		float angle = 2*(float)M_PI*(i + jitter)/n;
		DriftVec2 dir = DriftVec2FMA(origin, DriftVec2ForAngle(angle), 300);
		arr[i] = DriftTerrainRaymarch(terra, origin, dir, 0, 8);
	}
	
	float area = 0, cos_inc = sinf(2*(float)M_PI/n);
	for(uint i = 0; i < n; i++) area += cos_inc*arr[i]*arr[(i + 1)%n];
	
	DriftGameContext *ctx = update->ctx;
	float size = ctx->reverb.size = DriftLerp(sqrtf(area), ctx->reverb.size, expf(-2.0f*update->dt));
	float amount = 1 - expf(-1*size);
	ctx->reverb.wet = 0.006f*amount;
	ctx->reverb.decay = amount;
	
	DriftBiomeSample bio = DriftTerrainSampleBiome(terra, origin);
	float weight = bio.weights[DRIFT_BIOME_CRYO] + bio.weights[DRIFT_BIOME_RADIO];
	ctx->reverb.cutoff = DriftLerp(0.40f, 0.20f, weight);
	
	DriftAudioSetReverb(ctx->reverb.dry, ctx->reverb.wet, ctx->reverb.decay, ctx->reverb.cutoff);
}

static const char* DRIFT_FRAME_TRACY = "DRIFT_FRAME_TRACY";

static void DriftGameContextPresent(tina_job* job){
	static const char* FRAME_PRESENT = "Present";
	TracyCFrameMarkStart(FRAME_PRESENT);
	DriftDraw* draw = tina_job_get_description(job)->user_data;
	DriftAppPresentFrame(draw->renderer);
	DriftZoneMemRelease(draw->mem);
	TracyCFrameMarkEnd(FRAME_PRESENT);
}

static double qtrunc(double f, double q){return trunc(f/q - 1)*q + q;}
static double iir_filter(double sample, uint n, double* x, double* y, const double* b, const double* a){
	double value = b[0]*sample;
	for(uint i = n - 1; i > 0; i--){
		x[i] = x[i - 1]; y[i] = y[i - 1];
		value += b[i]*x[i] - a[i]*y[i];
	}
	
	x[0] = sample; y[0] = value;
	return value;
}

DriftLoopYield DriftGameContextLoop(tina_job* job){
	DriftGameContext* ctx = APP->app_context;
	DriftGameState* state = ctx->state;
	
	// TODO better place for this?
	if(!state){
		state = ctx->state = DriftGameStateNew(job);
		DriftGameStateSetupIntro(state);
		
		state->player = DriftMakeEntity(state);
		DriftTempPlayerInit(state, state->player, DRIFT_START_POSITION);
	}
	
	state->status.save_lock = 0;
	if(state->status.needs_tutorial){
		state->tutorial = DriftScriptNew(DriftTutorialScript, NULL, ctx);
	}
	
	DriftTerrainResetCache(state->terra);
	
	// Interval filter coefficients.
	static const double filter_b[] = {6.321391700454014e-5, 0.00012642783400908025, 6.321391700454014e-5};
	static const double filter_a[] = {1.0, -1.9681971279272976, 0.9684499835953156};
	double x[3] = {}, var_x[3] = {};
	double y[3] = {}, var_y[3] = {};
	
	// Warm start the filter with the display rate;
	SDL_DisplayMode display_mode;
	SDL_GetWindowDisplayMode(APP->shell_window, &display_mode);
	for(uint i = 0; i < 3; i++) x[i] = y[i] = 1.0/display_mode.refresh_rate;
	
	if(DRIFT_DEBUG){
		ctx->debug.show_ui = true;
		// ctx->debug.ui->show_inspector = true;
		// ctx->debug.godmode = true;
		// if(ctx->state->tutorial) ctx->state->tutorial->debug_skip = true;
		// ctx->debug.pause = true;
		// ctx->debug.paint = true;
		// ctx->debug.draw_terrain_sdf = true;
		// ctx->debug.hide_terrain_decals = true;
	}
	
	DriftAffine prev_vp_matrix = DRIFT_AFFINE_IDENTITY;
	float debug_zoom = 1;
	DriftVec2 debug_pan = DRIFT_VEC2_ZERO;
	DriftAffine debug_view = DRIFT_AFFINE_IDENTITY;
	
	DriftAudioBusSetActive(DRIFT_BUS_SFX, true);
	
	tina_group reverb_job = {}, present_job = {};
	while(!APP->request_quit && !APP->shell_restart){
		DRIFT_ASSERT_WARN(tina_job_switch_queue(job, DRIFT_JOB_QUEUE_MAIN) == DRIFT_JOB_QUEUE_MAIN, "Main thread is on the wrong queue?");
		
		double unfiltered_nanos = DriftGameContextUpdateNanos(ctx);
		u64 limit_nanos = (u64)(1e9/5);
		if(unfiltered_nanos > limit_nanos){
			DRIFT_LOG("Long frame: %f ms. Skipping ahead!", unfiltered_nanos/1e6);
			unfiltered_nanos = limit_nanos;
		}
		
		double filtered_nanos = iir_filter(unfiltered_nanos, 3, x, y, filter_b, filter_a);
		double variance = iir_filter(pow(unfiltered_nanos - filtered_nanos, 2), 3, var_x, var_y, filter_b, filter_a);
		DRIFT_ASSERT(variance >= 0, "negative variance?");
		bool interval_quality = sqrt(fmax(0, variance))/filtered_nanos < 0.125;
		
		double time_scale = expf(-4*ctx->time_scale_log);
		u64 delta_nanos = (u64)(interval_quality ? qtrunc(unfiltered_nanos, filtered_nanos) : unfiltered_nanos);
		u64 update_nanos = (u64)(time_scale*(double)delta_nanos);
		if(ctx->debug.pause) update_nanos = ctx->debug.tick_nanos;
		
		TracyCZoneN(INPUT_ZONE, "Input", true);
		DriftInputEventsPoll(DriftAffineInverse(prev_vp_matrix), ctx->mu, ctx);
		if(ctx->ui_state == DRIFT_UI_STATE_NONE){
			bool exit_to_menu = false;
			if(DriftInputButtonPress(DRIFT_INPUT_PAUSE)) DriftPauseLoop(ctx, job, prev_vp_matrix, &exit_to_menu);
			if(exit_to_menu){
				DriftGameStateFree(ctx->state);
				ctx->state = NULL;
				break;
			}
			
			if(DriftInputButtonPress(DRIFT_INPUT_MAP)){
				ctx->ui_state = DRIFT_UI_STATE_MAP;
				state->status.never_seen_map = false;
			}
		}
		TracyCZoneEnd(INPUT_ZONE);
		
		switch(ctx->ui_state){
			case DRIFT_UI_STATE_MAP:
			case DRIFT_UI_STATE_SCAN:
			case DRIFT_UI_STATE_CRAFT:
			case DRIFT_UI_STATE_LOGS:
				DriftGameContextMapLoop(ctx, job, prev_vp_matrix, 0);
			default: break;
		}
		
		DriftUpdate update = {
			.ctx = ctx, .state = state, .job = job, .mem = DriftZoneMemAquire(APP->zone_heap, "UpdateMem"),
			.frame = ctx->current_frame, .tick = ctx->current_tick, .nanos = update_nanos,
			.dt = update_nanos/1e9f, .tick_dt = 1/DRIFT_TICK_HZ,
			.prev_vp_matrix = prev_vp_matrix,
		};
		
		TracyCZoneN(UPDATE_ZONE, "Update", true);
		if(update_nanos > 0){
			DriftSystemsUpdate(&update);
		}
		
		if(ctx->reverb.dynamic) tina_scheduler_enqueue(APP->scheduler, update_reverb, &update, 0, DRIFT_JOB_QUEUE_WORK, &reverb_job);
		TracyCZoneEnd(UPDATE_ZONE);
		
		u64 tick_dt_nanos = (u64)(1e9f/DRIFT_TICK_HZ);
		while(ctx->tick_nanos < ctx->update_nanos){
			update.tick = ctx->current_tick = ctx->_tick_counter;
			update.nanos = ctx->tick_nanos;
			
			if(state->tutorial && !DriftScriptTick(state->tutorial, &update)){
				DriftScriptFree(state->tutorial);
				state->tutorial = NULL;
			}
			
			if(state->script && !DriftScriptTick(state->script, &update)){
				DriftScriptFree(state->script);
				state->script = NULL;
			}
			
			TracyCZoneN(ZONE_TICK, "Tick", true);
			DriftSystemsTick(&update);
			TracyCZoneEnd(ZONE_TICK);
			
			TracyCZoneN(ZONE_PHYSICS, "Physics", true);
			DRIFT_ASSERT(DriftVec2Length(state->bodies.velocity[0]) == 0, "Velocity 0 before physics.");
			DriftPhysicsTick(&update, update.mem);
			for(uint i = 0; i < DRIFT_SUBSTEPS; i++) DriftPhysicsSubstep(&update);
			DRIFT_ASSERT(DriftVec2Length(state->bodies.velocity[0]) == 0, "Velocity 0 after physics.");
			TracyCZoneEnd(ZONE_PHYSICS);
			
			destroy_entities(state, state->dead_entities);
			ctx->tick_nanos += tick_dt_nanos;
			ctx->_tick_counter++;
		}
		
		TracyCZoneN(CLEANUP_ZONE, "Cleanup", true);
		DriftGameStateCleanup(&update);
		TracyCZoneEnd(CLEANUP_ZONE);
		
		float dt_tick_diff = (ctx->tick_nanos - ctx->update_nanos)/-1e9f;
		ctx->update_nanos += update_nanos;
		
		TracyCZoneN(INTERPOLATE_ZONE, "Interpolate", true);
		DriftPhysicsSyncTransforms(&update, dt_tick_diff);
		TracyCZoneEnd(INTERPOLATE_ZONE);
		
		TracyCZoneN(DRAW_ZONE, "Draw", true);
		TracyCZoneN(DRAW_ZONE_DRAW_SETUP, "Draw Setup", true);
		DriftVec2 prev_origin = DriftAffineOrigin(DriftAffineInverse(prev_vp_matrix));
		DriftAffine v_matrix = {1, 0, 0, 1, -prev_origin.x, -prev_origin.y};
		
		uint transform_idx = DriftComponentFind(&state->transforms.c, state->player);
		if(transform_idx){
			DriftVec2 player_pos = {state->transforms.matrix[transform_idx].x, state->transforms.matrix[transform_idx].y};
			DriftTerrainUpdateVisibility(state->terra, player_pos);
			v_matrix = (DriftAffine){1, 0, 0, 1, -player_pos.x, -player_pos.y};
		}
		
		DriftDraw* draw = DriftDrawBegin(&update, dt_tick_diff, v_matrix, prev_vp_matrix);
		if(ctx->debug.pause){
			const u8* keystate = SDL_GetKeyboardState(NULL);
			
			if(INPUT->mouse_down[DRIFT_MOUSE_RIGHT]){
				uint body_idx = DriftComponentFind(&state->bodies.c, state->player);
				state->bodies.position[body_idx] = INPUT->mouse_pos_world;
				debug_view = DRIFT_AFFINE_IDENTITY;
			}
			
			debug_zoom = DriftClamp(debug_zoom*exp2f(-0.5f*INPUT->mouse_wheel), 1/256.0f, 4);
			if(keystate[SDL_SCANCODE_1]) debug_zoom = 1/1.0;
			if(keystate[SDL_SCANCODE_2]) debug_zoom = 1/4.0;
			if(keystate[SDL_SCANCODE_3]) debug_zoom = 1/16.0;
			if(keystate[SDL_SCANCODE_4]) debug_zoom = 1/64.0;
			if(keystate[SDL_SCANCODE_5]) debug_zoom = 1/256.0;
			float zoom = powf(debug_view.a/debug_zoom, expf(-15.0f*delta_nanos/1e9f) - 1);
			
			DriftAffine p_inv = DriftAffineInverse(draw->p_matrix);
			DriftVec2 pivot = DriftAffinePoint(p_inv, INPUT->mouse_pos_clip);
			DriftVec2 pan = DriftAffineDirection(p_inv, DriftVec2Sub(INPUT->mouse_pos_clip, debug_pan));
			debug_pan = INPUT->mouse_pos_clip;
			
			bool pan_pressed = INPUT->mouse_state[DRIFT_MOUSE_MIDDLE] || keystate[SDL_SCANCODE_SPACE];
			if(!pan_pressed) pan = DRIFT_VEC2_ZERO;
			
			debug_view = DriftAffineMul((DriftAffine){1.0f, 0, 0, 1.0f, -pivot.x, -pivot.y}, debug_view);
			debug_view = DriftAffineMul((DriftAffine){zoom, 0, 0, zoom,    pan.x,    pan.y}, debug_view);
			debug_view = DriftAffineMul((DriftAffine){1.0f, 0, 0, 1.0f, +pivot.x, +pivot.y}, debug_view);
			draw->v_matrix = DriftAffineMul(debug_view, draw->v_matrix);
			draw->vp_matrix = DriftAffineMul(draw->p_matrix, draw->v_matrix);
			draw->vp_inverse = DriftAffineInverse(draw->vp_matrix);
			draw->reproj_matrix = DRIFT_AFFINE_IDENTITY;
			
			DriftDebugCircle2(state, DRIFT_START_POSITION, 30, 25, DRIFT_RGBA8_GREEN);
			DriftDebugCircle2(state, DRIFT_SKIFF_POSITION, 30, 25, DRIFT_RGBA8_ORANGE);
			DriftDrawHivesMap(draw, 1.0f);
		} else {
			debug_zoom = 1;
			debug_view = DRIFT_AFFINE_IDENTITY;
		}
		prev_vp_matrix = draw->vp_matrix;
		DriftDrawBindGlobals(draw);
		TracyCZoneEnd(DRAW_ZONE_DRAW_SETUP);
			
		DriftTerrainDrawTiles(draw, debug_zoom > 1);
		DriftSystemsDraw(draw);
		
		if(state->tutorial) DriftScriptDraw(state->tutorial, draw);
		if(state->script) DriftScriptDraw(state->script, draw);
		DriftImAudioUpdate();
		
		if(DriftEntitySetCheck(&state->entities, state->player)){
			DriftDrawHud(draw);
		} else {
			DriftVec2 ssize = draw->virtual_extent;
			{
				DriftAffine m = {ssize.x/2, 0, 0, ssize.y/2, ssize.x/4, ssize.y/4};
				DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){.color = {0x00, 0x00, 0x00, 0x80}, .matrix = m}));
			}{
				const char* message = DRIFT_TEXT_RED "Pod 9 Destroyed";
				float width = DriftDrawTextSize(message, 0).x;
				DriftAffine m = {1, 0, 0, 1, -width/2, 0};
				m = DriftAffineMul((DriftAffine){2, 0, 0, 2, draw->virtual_extent.x/2, draw->virtual_extent.y/2}, m);
				DriftDrawTextFull(draw, &draw->hud_sprites, message, (DriftTextOptions){
					.tint = DRIFT_VEC4_WHITE, .matrix = m,
				});
			}{
				const char* message = "Press {@ACCEPT} to Reconstruct";
				float width = DriftDrawTextSize(message, 0).x;
				DriftDrawText(draw, &draw->hud_sprites, (DriftVec2){(draw->virtual_extent.x - width)/2, draw->virtual_extent.y/3}, message);
			}
		}
		
		if(!state->status.disable_hud){
			DriftVec2 p = {roundf(draw->virtual_extent.x) - 10*8, 16};
			p = DriftDrawTextF(draw, &draw->hud_sprites, p, "{#40404040}% 9.2f ms\n", filtered_nanos/1e6f);
			p = DriftDrawTextF(draw, &draw->hud_sprites, p,"{#80808080}DEV {#40408080}%s\n",
				/*DRIFT_VERSION_MAJOR, DRIFT_VERSION_MINOR,*/ DRIFT_GIT_SHORT_SHA
			);
		}
		
		TracyCZoneN(DEBUG_UI_ZONE, "Debug UI", true);
		DriftDebugUI(&update, draw);
		TracyCZoneEnd(DEBUG_UI_ZONE);
		TracyCZoneEnd(DRAW_ZONE);
		
		TracyCZoneN(RENDER_ZONE, "Render", true);
		DriftGameStateRender(draw);
		DriftArrayHeader(state->debug.sprites)->count = 0;
		DriftArrayHeader(state->debug.prims)->count = 0;
		
		mu_Context* mu = ctx->mu;
		DriftUIBegin(mu, draw);
		switch(ctx->ui_state){
			default: break;
		}
		DriftUIPresent(mu, draw);
		
		// Present to the screen.
		DriftGfxRendererPushBindTargetCommand(draw->renderer, NULL, DRIFT_VEC4_CLEAR);
		DriftGfxPipelineBindings* present_bindings = DriftDrawQuads(draw, ctx->draw_shared->present_pipeline, 1);
		present_bindings->textures[1] = ctx->draw_shared->resolve_buffer;
		
		TracyCZoneN(DEBUG_UI_RENDER_ZONE, "Debug UI Draw", true);
		DriftNuklearDraw(ctx->debug.ui, draw);
		TracyCZoneEnd(DEBUG_UI_RENDER_ZONE);
		TracyCZoneEnd(RENDER_ZONE);
		
		tina_job_wait(job, &present_job, 0);
		tina_scheduler_enqueue(APP->scheduler, DriftGameContextPresent, draw, 0, DRIFT_JOB_QUEUE_GFX, &present_job);
		
		tina_job_wait(job, &reverb_job, 0);
		DriftZoneMemRelease(update.mem);
		
		TracyCFrameMark;
		ctx->current_frame = ++ctx->_frame_counter;
		
		// Yield to other tasks on the main queue.
		TracyCZoneN(YIELD_ZONE, "Yield", true);
		tina_job_yield(job);
		TracyCZoneEnd(YIELD_ZONE);
		
#if DRIFT_MODULES
		if(INPUT->request_hotload){
			DriftModuleRequestReload(job);
			
			// Waste time if the user is in a UI.
			while(ctx->ui_state && APP->module_status == DRIFT_MODULE_BUILDING) tina_job_yield(job);
		}
		INPUT->request_hotload = false;
		
		if(APP->module_status == DRIFT_MODULE_READY){
			DriftHudPushToast(ctx, 0, DRIFT_TEXT_GREEN"<HOTLOAD>");
			DriftAudioPlaySample(DRIFT_BUS_UI, DRIFT_SFX_TEXT_BLIP, (DriftAudioParams){.gain = 1});
			destroy_entities(state, state->hot_entities);
			ResetHotComponents(state);
			state->tutorial = NULL;
			state->script = NULL;
			
			tina_job_wait(job, &reverb_job, 0);
			tina_job_wait(job, &present_job, 0);
			return DRIFT_LOOP_YIELD_HOTLOAD;
		}
#endif
	}
	
	// TODO leaks script?
	// if(state->script){
	// 	DriftScriptFree(state->script);
	// 	state->script = NULL;
	// }
	
	DriftAudioBusSetActive(DRIFT_BUS_SFX, false);
	tina_job_wait(job, &reverb_job, 0);
	tina_job_wait(job, &present_job, 0);
	return (APP->shell_restart ? DRIFT_LOOP_YIELD_RELOAD : DRIFT_LOOP_YIELD_DONE);
}
