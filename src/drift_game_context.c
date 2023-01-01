#include <string.h>
#include <inttypes.h>
#include <stdio.h>

#include "tina/tina.h"
#include <SDL.h>
#include "tracy/TracyC.h"

#include "drift_game.h"
#include "base/drift_nuklear.h"

void DriftGameStateIO(DriftIO* io){
	DriftGameState* state = io->user_ptr;
	
	// Handle terrain.
	DriftIOBlock(io, "terrain", &state->terra->tilemap.density, sizeof(state->terra->tilemap.density));
	if(io->read) DriftTerrainResetCache(state->terra);
	
	// Handle ECS data.
	DriftIOBlock(io, "entities", &state->entities, sizeof(state->entities));
	DRIFT_ARRAY_FOREACH(state->components, component) DriftComponentIO(*component, io);
	
	// Handle other tables.
	DRIFT_ARRAY_FOREACH(state->tables, table) DriftTableIO(*table, io);
}

DriftEntity DriftMakeEntity(DriftGameState* state){
	mtx_lock(&state->entities_mtx);
	DriftEntity e = DriftEntitySetAquire(&state->entities, 0);
	mtx_unlock(&state->entities_mtx);
	return e;
}

void DriftDestroyEntity(DriftUpdate* update, DriftEntity entity){
	mtx_lock(&update->state->entities_mtx);
	DRIFT_ARRAY_PUSH(update->_dead_entities, entity);
	mtx_unlock(&update->state->entities_mtx);
}

void DriftGameStateInit(DriftGameState* state, bool reset_game){
	state->tables = DRIFT_ARRAY_NEW(DriftSystemMem, 64, DriftTable*);
	state->components = DRIFT_ARRAY_NEW(DriftSystemMem, 64, DriftComponent*);
	
	DriftEntitySetInit(&state->entities);
	DriftSystemsInit(state);
	
	if(reset_game){
		{ // TODO Initialize home
			DriftEntity e = DriftMakeEntity(state);
			uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
			state->transforms.matrix[transform_idx] = (DriftAffine){1, 0, 0, 1, DRIFT_HOME_POSITION.x, DRIFT_HOME_POSITION.y};
			uint scan_idx = DriftComponentAdd(&state->scan.c, e);
			state->scan.type[scan_idx] = DRIFT_SCAN_CRASHED_SHIP;
			
			uint pnode_idx = DriftComponentAdd(&state->power_nodes.c, e);
			state->power_nodes.position[pnode_idx] = DRIFT_HOME_POSITION;
			
			// Mark the node as a root in the flow map.
			uint idx0 = DriftComponentAdd(&state->flow_maps[0].c, e);
			// state->flow_maps[0].node[idx0].next = e;
			// state->flow_maps[0].node[idx0].topo_dist = 0;
			state->flow_maps[0].flow[idx0].dist = 0;
		}{ // TODO initialize factory
			DriftEntity e = state->status.factory_node = DriftMakeEntity(state);
			uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
			state->transforms.matrix[transform_idx] = (DriftAffine){1, 0, 0, 1, DRIFT_FACTORY_POSITION.x, DRIFT_FACTORY_POSITION.y};
			uint scan_idx = DriftComponentAdd(&state->scan.c, e);
			state->scan.type[scan_idx] = DRIFT_SCAN_FACTORY;
			
			uint pnode_idx = DriftComponentAdd(&state->power_nodes.c, e);
			state->power_nodes.position[pnode_idx] = DRIFT_FACTORY_POSITION;
			
			static const DriftVec2 nodes[] = {
				{ 776.504822f, -6417.437012f}, {1080.529419f, -6147.798828f},
			// 	{ 961.307556f, -6149.188965f}, { 812.382935f, -6235.325684f},
			// 	{ 791.330078f, -6135.786133f}, { 698.192078f, -6036.168457f},
			// 	{ 666.177185f, -5924.809082f}, { 785.853455f, -5818.169434f},
			// 	{ 937.032349f, -5761.960938f}, {1072.341187f, -5778.540039f},
			// 	{1178.818970f, -5873.663086f}, {1351.409302f, -5855.001953f},
			// 	{1378.697998f, -5730.277344f}, {1407.379395f, -5556.059082f},
			// 	{1449.643311f, -5433.543457f}, {1569.712280f, -5300.566406f},
			// 	{1673.919678f, -5220.365234f}, {1733.368896f, -5050.591309f},
			// 	{1823.671509f, -4945.645020f}, {1452.292725f, -6126.862305f},
			// 	{1595.359863f, -6170.755859f}, {1636.837402f, -6312.337891f},
			// 	{1517.781738f, -6455.876953f}, {1406.651855f, -6533.980469f},
			// 	{1294.234131f, -6648.239258f}, {1470.014893f, -6817.790527f},
			// 	{1643.240479f, -6828.110840f}, {1811.197998f, -6842.036621f},
			// 	{1800.954224f, -7014.615723f}, {1695.205078f, -7137.684570f},
			// 	{1597.286133f, -7096.293945f}, {1581.863037f, -6536.433105f},
			// 	{1715.755981f, -6471.106445f}, {1818.337524f, -6409.083008f},
			// 	{ 930.199158f, -6314.508789f}, {1324.249146f, -6774.219727f},
			// 	{1602.564575f, -6017.763672f},
			};
			
			u8 buffer[16*1024];
			DriftMem* mem = DriftLinearMemInit(buffer, sizeof(buffer), "startup mem");
			for(uint i = 0; i < sizeof(nodes)/sizeof(*nodes); i++){
				DriftEntity e = DriftItemMake(state, DRIFT_ITEM_POWER_NODE, nodes[i], DRIFT_VEC2_ZERO);
				DriftPowerNodeActivate(state, e, mem);
			}
		}
	} else {
		DriftIOFileRead(TMP_SAVE_FILENAME, DriftGameStateIO, state);
	}
}

static DriftGameContext* DriftGameContextCreate(DriftApp* app, tina_job* job){
	DriftGameContext* ctx = DriftAlloc(DriftSystemMem, sizeof(*ctx));
	memset(ctx, 0x00, sizeof(*ctx));
	ctx->app = app;
	
	{ // Init ctx.state.
		mtx_init(&ctx->state.entities_mtx, mtx_plain);
		ctx->state.terra = DriftTerrainNew(job, false);
		DriftGameStateInit(&ctx->state, true);
		
		ctx->player = DriftMakeEntity(&ctx->state);
		DriftTempPlayerInit(&ctx->state, ctx->player, (DriftVec2){1870, -4970});
	}
	
	ctx->mu = DriftUIInit();
	
	uint queue = tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
	ctx->draw_shared = DriftDrawSharedNew(app, job);
	ctx->debug.ui = DriftNuklearNew();
	DriftNuklearSetupGFX(ctx->debug.ui, ctx->draw_shared);
	tina_job_switch_queue(job, queue);
	
	ctx->init_nanos = ctx->clock_nanos = DriftTimeNanos();
	
	return ctx;
}

tina_job_func DriftGameContextStart;
void DriftGameContextStart(tina_job* job){
	TracyCZoneN(ZONE_START, "Context start", true);
	DriftApp* app = tina_job_get_description(job)->user_data;
	DriftGameContext* ctx = app->app_context;
	tina_func* script_func = DriftTutorialScript;
	
	if(ctx == NULL){
		// No context, normal startup.
		TracyCZoneN(ZONE_CREATE, "Create Context", true);
		ctx = app->app_context = DriftGameContextCreate(app, job);
		TracyCZoneEnd(ZONE_CREATE);
	} else {
		if(ctx->debug.reset_on_load){
			DriftGameStateInit(&ctx->state, true);
		}
		
		script_func = ctx->script.coro ? DriftTutorialScript : NULL;
		
		DriftTerrainResetCache(ctx->state.terra);
		if(ctx->debug.regen_terrain_on_load){
			DriftTerrainFree(ctx->state.terra);
			ctx->state.terra = DriftTerrainNew(job, true);
		}
		
		uint queue = tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
		DriftDrawSharedFree(ctx->draw_shared);
		ctx->draw_shared = DriftDrawSharedNew(app, job);
		DriftNuklearSetupGFX(ctx->debug.ui, ctx->draw_shared);
		tina_job_switch_queue(job, queue);
	}
	
	DriftUIHotload(ctx->mu);
	
	TracyCZoneN(ZONE_SAMPLES, "Load Samples", true);
	DriftAudioLoadSamples(app->audio, job);
	TracyCZoneEnd(ZONE_SAMPLES);
	TracyCZoneN(ZONE_MUSIC, "load Music", true);
	DriftAudioStartMusic(app->audio);
	TracyCZoneEnd(ZONE_MUSIC);
	DriftAudioPause(app->audio, false);
	
	if(script_func) ctx->script.coro = tina_init(ctx->script.buffer, sizeof(ctx->script.buffer), script_func, &ctx->script);
	TracyCZoneEnd(ZONE_START);
	
	DriftAppShowWindow(app);
	DriftGameContextLoop(job);
}

double DriftGameContextUpdateNanos(DriftGameContext* ctx){
	u64 prev_nanos = ctx->clock_nanos;
	ctx->clock_nanos = DriftTimeNanos();
	return ctx->clock_nanos - prev_nanos;
}

static void debug_bb(DriftGameState* state, DriftAABB2 bb, DriftRGBA8 color){
	DRIFT_ARRAY_PUSH(state->debug.prims, ((DriftPrimitive){.p0 = {bb.l, bb.b}, .p1 = {bb.r, bb.b}, .radii = {1}, .color = color}));
	DRIFT_ARRAY_PUSH(state->debug.prims, ((DriftPrimitive){.p0 = {bb.l, bb.t}, .p1 = {bb.r, bb.t}, .radii = {1}, .color = color}));
	DRIFT_ARRAY_PUSH(state->debug.prims, ((DriftPrimitive){.p0 = {bb.l, bb.b}, .p1 = {bb.l, bb.t}, .radii = {1}, .color = color}));
	DRIFT_ARRAY_PUSH(state->debug.prims, ((DriftPrimitive){.p0 = {bb.r, bb.b}, .p1 = {bb.r, bb.t}, .radii = {1}, .color = color}));
}

static DriftVec2 light_frame_center(DriftFrame frame){
	DriftVec2 offset = {(s8)frame.anchor.x, (s8)frame.anchor.y};
	offset.x = 1 - 2*offset.x/(frame.bounds.r - frame.bounds.l + 1);
	offset.y = 1 - 2*offset.y/(frame.bounds.t - frame.bounds.b + 1);
	return offset;
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
	
	// Render the lightfield buffer.
	uint light_count = DriftArrayLength(draw->lights);
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
		float radius = draw->lights[i].radius;
		if(radius > 0){
			DriftAffine light_matrix = draw->lights[i].matrix;
			// DriftDebugCircle(draw->state, DriftAffineOrigin(light_matrix), radius, (DriftRGBA8){0xFF, 0xFF, 0x00, 0x80});
			
			DriftVec2 center = light_frame_center(draw->lights[i].frame);
			if(DriftAffineVisibility(DriftAffineMul(draw->vp_matrix, light_matrix), center, DRIFT_VEC2_ONE)){
				// Expand the bounds to include the light + radius;
				DriftVec2 origin = DriftAffineOrigin(light_matrix);
				float radius = draw->lights[i].radius;
				DriftAABB2 light_bounds = {origin.x - radius, origin.y - radius, origin.x + radius, origin.y + radius};
				shadow_bounds = DriftAABB2Merge(shadow_bounds, light_bounds);
				shadow_count++;
				
				// debug_bb(draw->state, light_bounds, DRIFT_RGBA8_YELLOW);
				
				// DriftAffine m_bound = draw->lights[i].matrix;
				// m_bound.x += m_bound.a*center.x + m_bound.c*center.y;
				// m_bound.y += m_bound.b*center.x + m_bound.d*center.y;
				
				// float hw = fabsf(m_bound.a) + fabsf(m_bound.c), hh = fabsf(m_bound.b) + fabsf(m_bound.d);
				// DriftAABB2 bb = (DriftAABB2){m_bound.x - hw, m_bound.y - hh, m_bound.x + hw, m_bound.y + hh};
				// debug_bb(draw->state, bb, DRIFT_RGBA8_YELLOW);
			} else {
				// Light is not visible, clear it's shadow flag.
				draw->lights[i].radius = 0;
			}
		}
	}
	
	// Prep shadow light render structs.
	// + 1 to avoid 0 length VLAs.
	DriftGfxBufferBinding shadow_uniform_bindings[shadow_count + 1];
	DriftGfxBufferBinding light_instance_bindings[shadow_count + 1];
	DriftAABB2 light_bounds[shadow_count + 1];
	
	DriftVec2 shadowfield_extent = DriftVec2Mul(draw->internal_extent, 1.0/DRIFT_SHADOWFIELD_SCALE);
	DriftAffine shadowfield_ortho = DriftAffineOrtho(0, shadowfield_extent.x, 0, shadowfield_extent.y);
	DriftAffine shadowfield_matrix = DriftAffineMul(DriftAffineInverse(shadowfield_ortho), draw->vp_matrix);
	
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
			
			DriftAffine m_bound = DriftAffineMul(shadowfield_matrix, light_matrix);
			DriftVec2 center = light_frame_center(draw->lights[i].frame);
			m_bound.x += m_bound.a*center.x + m_bound.c*center.y;
			m_bound.y += m_bound.b*center.x + m_bound.d*center.y;
			
			// TODO this is done a bunch, move to math?
			float hw = fabsf(m_bound.a) + fabsf(m_bound.c), hh = fabsf(m_bound.b) + fabsf(m_bound.d);
			light_bounds[j] = (DriftAABB2){m_bound.x - hw, m_bound.y - hh, m_bound.x + hw, m_bound.y + hh};
			
			j++;
		}

		// These are already buffered by the lightfield step, so it needs to skip over non-shadow lights.
		light_instances_binding.offset += sizeof(DriftLight);
	}
	
	DriftTerrainDrawShadows(draw, draw->state->terra, shadow_bounds);
	
	// Push the shadow mask data.
	uint shadow_mask_count = DriftArrayLength(draw->shadow_masks);
	DriftGfxBufferBinding shadow_mask_instances = DriftGfxRendererPushGeometry(renderer, draw->shadow_masks, shadow_mask_count*sizeof(*draw->shadow_masks)).binding;
	
	for(uint pass = 0; pass < 2; pass++){
		DriftGfxRendererPushBindTargetCommand(renderer, draw_shared->shadowfield_target[pass], DRIFT_VEC4_CLEAR);
		DriftDrawQuads(draw, draw_shared->light_blit_pipeline[pass], 1);

		for(uint i = 0; i < shadow_count; i++){
			DriftGfxRendererPushScissorCommand(renderer, light_bounds[i]);

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
		{.arr = draw->fg_sprites, .pipeline = draw_shared->sprite_pipeline, .bindings = &draw->default_bindings},
		{},
	});
	
	// Resolve HDR
	DriftPlayerData* player = draw->state->players.data + DriftComponentFind(&draw->state->players.c, draw->ctx->player);
	struct {
		DriftVec4 scatter[4];
		DriftVec4 transmit[4];
		DriftVec4 effect_tint;
		float effect_static;
		float effect_heat;
	} effects = {
		.scatter = {
			// [DRIFT_BIOME_LIGHT] = (DriftVec4){{0.14f, 0.05f, 0.00f, 0.25f}},
			[DRIFT_BIOME_LIGHT] = (DriftVec4){{0.43f, 0.25f, 0.02f, 0.12f}},
			[DRIFT_BIOME_CRYO ] = (DriftVec4){{0.24f, 0.35f, 0.41f, 0.41f}},
			[DRIFT_BIOME_RADIO] = (DriftVec4){{0.25f, 0.32f, 0.09f, 0.26f}},
			[DRIFT_BIOME_DARK ] = (DriftVec4){{0.22f, 0.01f, 0.07f, 0.25f}},
			// [DRIFT_BIOME_SPACE] = (DriftVec4){{0.00f, 0.00f, 0.00f, 0.00f}},
		},
		.transmit = {
			// [DRIFT_BIOME_LIGHT] = (DriftVec4){{0.96f, 0.89f, 0.79f, 0.00f}},// Linear
			[DRIFT_BIOME_LIGHT] = (DriftVec4){{0.99f, 0.92f, 0.86f, 1.00f}},
			[DRIFT_BIOME_CRYO ] = (DriftVec4){{0.79f, 0.87f, 0.96f, 0.00f}},
			[DRIFT_BIOME_RADIO] = (DriftVec4){{0.92f, 0.98f, 0.91f, 0.00f}},
			[DRIFT_BIOME_DARK ] = (DriftVec4){{0.98f, 0.73f, 0.88f, 0.00f}},
			// [DRIFT_BIOME_SPACE] = (DriftVec4){{1.00f, 1.00f, 1.00f, 1.00f}},
		},
		.effect_tint = draw->screen_tint,
		.effect_static = DriftSaturate(1 - player->energy/player->energy_cap),
		.effect_heat = DriftSaturate(player->temp),
	}; // TODO GL packing warning, wants 160 bytes
	
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
		{.arr = draw->flash_sprites, .pipeline = draw_shared->flash_sprite_pipeline, .bindings = &draw->default_bindings},
		{.arr = draw->overlay_sprites, .pipeline = draw_shared->overlay_sprite_pipeline, .bindings = &draw->default_bindings},
		{.arr = draw->overlay_prims, .pipeline = draw_shared->overlay_primitive_pipeline, .bindings = &draw->default_bindings},
		{.arr = draw->hud_sprites, .pipeline = draw_shared->overlay_sprite_pipeline, .bindings = &hud_bindings},
		{},
	});
}

static void destroy_entities(DriftGameState* state, DriftEntity* list){
	mtx_lock(&state->entities_mtx);
	// Remove all components for the entities.
	DRIFT_ARRAY_FOREACH(state->components, component){
		DRIFT_ARRAY_FOREACH(list, e) DriftComponentRemove(*component, *e);
	}
	
	// Retire the entities.
	DRIFT_ARRAY_FOREACH(list, e){
		if(DriftEntitySetCheck(&state->entities, *e)) DriftEntitySetRetire(&state->entities, *e);
	}
	
	DriftArrayHeader(list)->count = 0;
	mtx_unlock(&state->entities_mtx);
}

#if DRIFT_MODULES
static void ResetHotComponents(DriftUpdate* update){
	DriftComponent** components = update->state->components;
	DriftArray* header = DriftArrayHeader(components);
	DriftComponent** copy = DRIFT_ARRAY_NEW(header->mem, header->capacity, DriftComponent*);
	
	DRIFT_ARRAY_FOREACH(update->state->components, c){
		if((*c)->reset_on_hotload){
			DRIFT_COMPONENT_FOREACH(*c, i) DRIFT_ARRAY_PUSH(update->_dead_entities, DriftComponentGetEntities(*c)[i]);
			DriftComponentDestroy(*c);
		} else {
			DRIFT_ARRAY_PUSH(copy, *c);
		}
	}
	
	update->state->components = copy;
	DriftArrayFree(components);
	destroy_entities(update->state, update->_dead_entities);
}
#endif

static void DriftGameStateCleanup(DriftUpdate* update){
	// DriftUpdate* update = tina_job_get_description(job)->user_data;
	DriftGameState* state = update->state;
	
	destroy_entities(state, update->_dead_entities);
	
	DriftZoneMemRelease(update->mem);
	DriftArrayHeader(state->debug.sprites)->count = 0;
	DriftArrayHeader(state->debug.prims)->count = 0;
	
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

static float toast_alpha(u64 current_nanos, DriftToast* toast){
	return 1 - DriftSaturate((current_nanos - toast->timestamp)*1e-9f - 4);
}

void DriftContextPushToast(DriftGameContext* ctx, const char* format, ...){
	DriftToast toast = {.timestamp = ctx->update_nanos, .count = 1};
	
	va_list arg;
	va_start(arg, format);
	char* end = toast.message + vsnprintf(toast.message, sizeof(toast.message), format, arg);
	va_end(arg);
	
	// Find a matching toast and update it's count
	uint i = 0;
	while(i < DRIFT_MAX_TOASTS){
		bool visible = toast_alpha(ctx->current_tick, ctx->toasts + i) > 0;
		if(visible && strncmp(toast.message, ctx->toasts[i].message, sizeof(toast.message)) == 0){
			toast.count += ctx->toasts[i].count;
			break;
		}
		
		i++;
	}
	
	// Reuse last if no match.
	if(i == DRIFT_MAX_TOASTS) i--;
	
	push_up:
	for(; i; i--) ctx->toasts[i] = ctx->toasts[i - 1];
	ctx->toasts[0] = toast;
}

static void draw_hud(DriftDraw* draw){
	TracyCZoneN(ZONE_HUD, "HUD", true);
	DriftGameContext* ctx = draw->ctx;
	DriftGameState* state = draw->state;
	DriftPlayerData* player = ctx->state.players.data + DriftComponentFind(&ctx->state.players.c, ctx->player);
	DriftVec2 screen_size = {roundf(draw->virtual_extent.x), roundf(draw->virtual_extent.y)};
	
	{ // Draw controls
		DriftAffine t = {1, 0, 0, 1, 8, screen_size.y - 18};
		t = DriftDrawText(draw, &draw->hud_sprites, t, DRIFT_VEC4_WHITE, "{@FIRE} Shoot\n"); t.y -= 3;
		t = DriftDrawText(draw, &draw->hud_sprites, t, DRIFT_VEC4_WHITE, "{@GRAB} Grab Object\n"); t.y -= 3;
		t = DriftDrawText(draw, &draw->hud_sprites, t, DRIFT_VEC4_WHITE, "{@DROP} Place Node\n"); t.y -= 3;
		t = DriftDrawText(draw, &draw->hud_sprites, t, DRIFT_VEC4_WHITE, "{@SCAN} Scan Object\n"); t.y -= 3;
		if(state->inventory[DRIFT_ITEM_MINING_LASER]){
			t = DriftDrawText(draw, &draw->hud_sprites, t, DRIFT_VEC4_WHITE, "{@LASER} Laser\n"); t.y -= 3;
		}
		t = DriftDrawText(draw, &draw->hud_sprites, t, DRIFT_VEC4_WHITE, "{@OPEN_MAP} Show Map\n"); t.y -= 3;
		// t = DriftDrawText(draw, &draw->hud_sprites, t, DRIFT_VEC4_WHITE, "{@LIGHT} Toggle Light\n"); t.y -= 3;
		// if(ctx->input.player.ui_active ) t = DriftDrawText(draw, &draw->hud_sprites, t, DRIFT_VEC4_WHITE, "{@CANCEL} Close Crafting\n");
	}
	
	// Draw inventory
	DriftAffine t = {1, 0, 0, 1, 8, screen_size.y - 110};
	t = DriftDrawText(draw, &draw->hud_sprites, t, DRIFT_VEC4_WHITE, "Cargo Hold:\n");
	
	for(uint i = 0; i < DRIFT_PLAYER_CARGO_SLOT_COUNT; i++){
		DriftCargoSlot* slot = player->cargo_slots + i;
		const char* name = DRIFT_ITEMS[slot->type].name;
		if(slot->count || slot->request){
			t = DriftDrawTextF(draw, &draw->hud_sprites, t, DRIFT_VEC4_WHITE, "%2d "DRIFT_TEXT_GRAY"%s\n", slot->count, name);
		} else {
			t = DriftDrawText(draw, &draw->hud_sprites, t, DRIFT_VEC4_WHITE, "--"DRIFT_TEXT_GRAY" (empty)\n");
		}
	}
	
	uint health_idx = DriftComponentFind(&state->health.c, ctx->player);
	DriftHealth* health = state->health.data + health_idx;
	
	if(player->energy){ // Draw Heat
		DriftAffine t = {1, 0, 0, 1, 256, screen_size.y - 18};
		float value = DriftClamp(player->temp, -1, 1);
		u8 r = 255 - (u8)fmaxf(0, -255*value);
		u8 g = 255 - (u8)fmaxf(0,  255*value);
		u8 b = 255 - (u8)fmaxf(0,  255*value);
		DriftDrawTextF(draw, &draw->hud_sprites, t, DRIFT_VEC4_WHITE, "Heat: {#%02X%02X%02XFF}%- d%", r, g, b, (uint)(100*value));
	}
	
	DriftVec2 info_cursor = {screen_size.x/2, screen_size.y/3};
	float info_flash = (DriftWaveSaw(draw->nanos, 1) < 0.9f ? 1 : 0.25);
	
	if(player->energy == 0){
		const char* text = "THRUSTER POWER ONLY";
		DriftAffine m = {1, 0, 0, 1, info_cursor.x - DriftDrawTextSize(text, 0, draw->input_icons).x/2, info_cursor.y};
		DriftDrawText(draw, &draw->hud_sprites, m, DriftVec4Mul(DRIFT_VEC4_RED, info_flash), text);
		info_cursor.y -= 10;
	} else if(!player->is_powered){
		const char* text = DriftSMPrintf(draw->mem, "CAPACITORS %2.0f%%", 100*player->energy/player->energy_cap);
		DriftAffine m = {1, 0, 0, 1, info_cursor.x - DriftDrawTextSize(text, 0, draw->input_icons).x/2, info_cursor.y};
		DriftDrawText(draw, &draw->hud_sprites, m, DriftVec4Mul(DRIFT_VEC4_ORANGE, info_flash), text);
		info_cursor.y -= 10;
	}
	
	float shield = health->value/health->maximum;
	if(shield <= 0.5f){
		const char* text = shield > 0 ? DRIFT_TEXT_ORANGE"SHIELDS LOW" : DRIFT_TEXT_RED"SHIELDS DOWN";
		DriftAffine m = {1, 0, 0, 1, info_cursor.x - DriftDrawTextSize(text, 0, draw->input_icons).x/2, info_cursor.y};
		DriftDrawText(draw, &draw->hud_sprites, m, DriftVec4Mul(DRIFT_VEC4_WHITE, info_flash), text);
		info_cursor.y -= 10;
	}
	
	if(player->is_overheated){
		const char* text = "OVERHEAT";
		DriftAffine m = {1, 0, 0, 1, info_cursor.x - DriftDrawTextSize(text, 0, draw->input_icons).x/2, info_cursor.y};
		DriftDrawText(draw, &draw->hud_sprites, m, DriftVec4Mul(DRIFT_VEC4_RED, info_flash), text);
		info_cursor.y -= 10;
	} else if(fabsf(player->temp) > 0.25){
		const char* text = "TEMPERATURE";
		DriftAffine m = {1, 0, 0, 1, info_cursor.x - DriftDrawTextSize(text, 0, draw->input_icons).x/2, info_cursor.y};
		DriftDrawText(draw, &draw->hud_sprites, m, DriftVec4Mul(DRIFT_VEC4_ORANGE, info_flash), text);
		info_cursor.y -= 10;
	}
	
	// Draw toasts
	DriftVec2 toast_cursor = {15, draw->internal_extent.y/3};
	for(uint i = 0; i < DRIFT_MAX_TOASTS; i++){
		DriftToast* toast = ctx->toasts + i;
		float alpha = toast_alpha(draw->nanos, toast);
		if(alpha > 0){
			DriftVec4 fade = {{alpha, alpha, alpha, alpha}};
			const char* message = toast->message;
			
			if(toast->count > 1) message = DriftSMPrintf(draw->mem, "%s "DRIFT_TEXT_GRAY"x%d", toast->message, toast->count);
			DriftDrawTextF(draw, &draw->hud_sprites, (DriftAffine){1, 0, 0, 1, toast_cursor.x, toast_cursor.y}, fade, "%s", message);
			toast_cursor.y -= 10;
		}
	}
	
	DriftVec2 player_pos = DriftAffineOrigin(state->transforms.matrix[DriftComponentFind(&state->transforms.c, ctx->player)]);
	
	if(!player->is_powered){
		float nearest_dist = INFINITY;
		DriftVec2 nearest_pos = DRIFT_VEC2_ZERO;
		
		DRIFT_COMPONENT_FOREACH(&state->power_nodes.c, node_idx){
			if(!state->power_nodes.active[node_idx]) continue;
			
			DriftVec2 pos = state->power_nodes.position[node_idx];
			float dist = DriftVec2Distance(pos, player_pos);
			if(dist < nearest_dist){
				nearest_dist = dist;
				nearest_pos = pos;
			}
		}
		
		float anim = DriftSaturate((draw->tick - player->power_tick0)/0.15e9f);
		DriftVec4 color = {{0, anim, anim, anim}};
		DriftRGBA8 color8 = DriftRGBA8FromColor(color);
		float wobble = 4*fabsf(DriftWaveComplex(draw->nanos, 1).x), r = 8 + wobble;
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = nearest_pos, .p1 = nearest_pos, .radii = {r, r - 1}, .color = color8}));
		
		DriftVec2 dir = DriftVec2Normalize(DriftVec2Sub(nearest_pos, player_pos));
		DriftVec2 chevron_pos = DriftVec2FMA(player_pos, dir, 96 - wobble);
		float scale = DriftLogerp(4, 1, anim);
		DriftAffine m = {scale, 0, 0, scale, chevron_pos.x, chevron_pos.y};
		
		DriftAffine m_chev = DriftAffineMul(m, (DriftAffine){dir.x, dir.y, -dir.y, dir.x, 0, 0});

		DriftVec2 p[] = {
			DriftAffinePoint(m_chev, (DriftVec2){0, -4}),
			DriftAffinePoint(m_chev, (DriftVec2){2,  0}),
			DriftAffinePoint(m_chev, (DriftVec2){0,  4}),
		};
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[0], .p1 = p[1], .radii = {1.5f*scale}, .color = color8}));
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[1], .p1 = p[2], .radii = {1.5f*scale}, .color = color8}));
		
		const char* text = "POWER";
		DriftAABB2 text_bounds = DriftDrawTextBounds(text, 0, draw->input_icons);
		DriftVec2 text_center = DriftAABB2Center(text_bounds), text_extents = DriftAABB2Extents(text_bounds);
		DriftVec2 text_offset = DriftVec2FMA(text_center, dir, fminf((text_extents.x + 2)/fabsf(dir.x), (text_extents.y + 3)/fabsf(dir.y)));
		DriftDrawText(draw, &draw->overlay_sprites, DriftAffineMul(m, (DriftAffine){1, 0, 0, 1, -text_offset.x, -text_offset.y}), color , text);
	}
	
	TracyCZoneEnd(ZONE_HUD);
}

static const char* DRIFT_FRAME_TRACY = "DRIFT_FRAME_TRACY";

static void DriftGameContextPresent(tina_job* job){
	static const char* FRAME_PRESENT = "Present";
	TracyCFrameMarkStart(FRAME_PRESENT);
	DriftDraw* draw = tina_job_get_description(job)->user_data;
	DriftAppPresentFrame(draw->shared->app, draw->renderer);
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

void DriftGameContextLoop(tina_job* job){
	DriftApp* app = tina_job_get_description(job)->user_data;
	DriftGameContext* ctx = app->app_context;
	DriftGameState* state = &ctx->state;
	
	// Interval filter coefficients.
	static const double filter_b[] = {6.321391700454014e-5, 0.00012642783400908025, 6.321391700454014e-5};
	static const double filter_a[] = {1.0, -1.9681971279272976, 0.9684499835953156};
	double x[3] = {}, var_x[3] = {};
	double y[3] = {}, var_y[3] = {};
	
	// Warm start the filter with the display rate;
	SDL_DisplayMode display_mode;
	SDL_GetWindowDisplayMode(app->shell_window, &display_mode);
	for(uint i = 0; i < 3; i++) x[i] = y[i] = 1.0/display_mode.refresh_rate;
	
	ctx->debug.show_ui = true;
	ctx->debug.ui->show_info = true;
	ctx->script.debug_skip = true;
	ctx->debug.godmode = true;
	// ctx->debug.pause = true;
	// ctx->debug.paint = true;
	// ctx->debug.draw_terrain_sdf = true;
	// ctx->debug.hide_terrain_decals = true;
	
	if(ctx->debug.godmode){
		state->inventory[DRIFT_ITEM_VIRIDIUM] = 100;
		state->inventory[DRIFT_ITEM_LUMIUM] = 100;
		state->inventory[DRIFT_ITEM_SCRAP] = 100;
		state->inventory[DRIFT_ITEM_POWER_SUPPLY] = 100;
		state->inventory[DRIFT_ITEM_OPTICS] = 100;
		state->inventory[DRIFT_ITEM_POWER_NODE] = 100;
		
		state->inventory[DRIFT_ITEM_HEADLIGHT] = 1;
		state->inventory[DRIFT_ITEM_CANNON] = 1;
		state->inventory[DRIFT_ITEM_AUTOCANNON] = 1;
		state->inventory[DRIFT_ITEM_MINING_LASER] = 1;

		for(uint i = 0; i < _DRIFT_SCAN_COUNT; i++) state->scan_progress[i] = 1;
	}
	
	DriftAffine prev_vp_matrix = DRIFT_AFFINE_IDENTITY;
	state->debug.sprites = DRIFT_ARRAY_NEW(DriftSystemMem, 0, DriftSprite);
	state->debug.prims = DRIFT_ARRAY_NEW(DriftSystemMem, 0, DriftPrimitive);
	
	float debug_zoom = 1;
	DriftVec2 debug_pan = DRIFT_VEC2_ZERO;
	DriftAffine debug_view = DRIFT_AFFINE_IDENTITY;
	
	tina_group present_jobs = {}, cleanup_jobs = {};
	while(!ctx->input.quit && !app->shell_restart){
		DRIFT_ASSERT_WARN(tina_job_switch_queue(job, DRIFT_JOB_QUEUE_MAIN) == DRIFT_JOB_QUEUE_MAIN, "Main thread is on the wrong queue?");
		
		double unfiltered_nanos = DriftGameContextUpdateNanos(ctx);
		double filtered_nanos = iir_filter(unfiltered_nanos, 3, x, y, filter_b, filter_a);
		double variance = iir_filter(pow(unfiltered_nanos - filtered_nanos, 2), 3, var_x, var_y, filter_b, filter_a);
		DRIFT_ASSERT(variance >= 0, "negative variance?");
		bool interval_quality = sqrt(fmax(0, variance))/filtered_nanos < 0.125;
		
		double time_scale = expf(-4*ctx->time_scale_log);
		u64 delta_nanos = (u64)(interval_quality ? qtrunc(unfiltered_nanos, filtered_nanos) : unfiltered_nanos);
		u64 update_nanos = (u64)(time_scale*(double)delta_nanos);
		if(ctx->debug.pause) update_nanos = ctx->debug.tick_nanos;
		
		// Check if the time is way behind and jump ahead.
		u64 limit_nanos = (u64)(1e9/5);
		if(update_nanos > limit_nanos){
			DRIFT_LOG("Long frame: %f ms. Skipping ahead!", update_nanos/1e6);
			update_nanos = limit_nanos;
		}
		
		TracyCZoneN(INPUT_ZONE, "Input", true);
		DriftInputEventsPoll(ctx, DriftAffineInverse(prev_vp_matrix));
		if(DriftInputButtonPress(&ctx->input.player, DRIFT_INPUT_PAUSE)) DriftPauseLoop(ctx, job, prev_vp_matrix);	
		if(DriftInputButtonPress(&ctx->input.player, DRIFT_INPUT_OPEN_MAP)) DriftGameContextMapLoop(ctx, job, prev_vp_matrix, DRIFT_UI_STATE_NONE, 0);
		TracyCZoneEnd(INPUT_ZONE);
		
		DriftUpdate update = {
			.ctx = ctx, .job = job, .state = state, .audio = app->audio,
			.scheduler = app->scheduler, .mem = DriftZoneMemAquire(app->zone_heap, "UpdateMem"),
			.frame = ctx->current_frame, .tick = ctx->current_tick, .nanos = update_nanos,
			.dt = update_nanos/1e9f, .tick_dt = 1/DRIFT_TICK_HZ,
			.prev_vp_matrix = prev_vp_matrix,
		};
		update._dead_entities = DRIFT_ARRAY_NEW(update.mem, 256, DriftEntity);
		
		DriftScript* script = &ctx->script;
		script->update = &update;
		
		TracyCZoneN(UPDATE_ZONE, "Update", true);
		if(update_nanos > 0){
			DriftSystemsUpdate(&update);
			
			if(script->coro && !script->coro->completed){
				tina_resume(script->coro, 0);
			} else {
				script->coro = NULL;
			}
		}
		TracyCZoneEnd(UPDATE_ZONE);
		
		u64 tick_dt_nanos = (u64)(1e9f/DRIFT_TICK_HZ);
		while(ctx->tick_nanos < ctx->update_nanos){
			update.tick = ctx->current_tick = ctx->_tick_counter;
			update.nanos = ctx->tick_nanos;
			
			TracyCZoneN(ZONE_TICK, "Tick", true);
			DriftSystemsTick(&update);
			TracyCZoneEnd(ZONE_TICK);
			
			TracyCZoneN(ZONE_PHYSICS, "Physics", true);
			DriftPhysicsTick(&update);
			for(uint i = 0; i < DRIFT_SUBSTEPS; i++) DriftPhysicsSubstep(&update);
			TracyCZoneEnd(ZONE_PHYSICS);
			
			ctx->tick_nanos += tick_dt_nanos;
			ctx->_tick_counter++;
		}
		
		float dt_since_tick = (ctx->tick_nanos - ctx->update_nanos)/-1e9f;
		ctx->update_nanos += update_nanos;
		
		TracyCZoneN(INTERPOLATE_ZONE, "Interpolate", true);
		DriftPhysicsSyncTransforms(&update, dt_since_tick);
		TracyCZoneEnd(INTERPOLATE_ZONE);
		
		TracyCZoneN(DRAW_ZONE, "Draw", true);
		TracyCZoneN(DRAW_ZONE_DRAW_SETUP, "Draw Setup", true);
		DriftVec2 prev_origin = DriftAffineOrigin(DriftAffineInverse(prev_vp_matrix));
		DriftAffine v_matrix = {1, 0, 0, 1, -prev_origin.x, -prev_origin.y};
		
		uint transform_idx = DriftComponentFind(&state->transforms.c, ctx->player);
		if(transform_idx){
			DriftVec2 player_pos = {state->transforms.matrix[transform_idx].x, state->transforms.matrix[transform_idx].y};
			DriftTerrainUpdateVisibility(state->terra, player_pos);
			v_matrix = (DriftAffine){1, 0, 0, 1, -player_pos.x, -player_pos.y};
		}
		
		DriftDraw* draw = DriftDrawBegin(ctx, job, update.dt, dt_since_tick, v_matrix, prev_vp_matrix);
		if(ctx->debug.pause){
			debug_zoom = DriftClamp(debug_zoom*exp2f(-0.5f*ctx->input.mouse_wheel), 1/256.0f, 4);
			float zoom = powf(debug_view.a/debug_zoom, expf(-15.0f*delta_nanos/1e9f) - 1);
			
			DriftAffine p_inv = DriftAffineInverse(draw->p_matrix);
			DriftVec2 pivot = DriftAffinePoint(p_inv, ctx->input.mouse_pos_clip);
			DriftVec2 pan = DriftAffineDirection(p_inv, DriftVec2Sub(ctx->input.mouse_pos_clip, debug_pan));
			debug_pan = ctx->input.mouse_pos_clip;
			if(!ctx->input.mouse_state[DRIFT_MOUSE_MIDDLE]) pan = DRIFT_VEC2_ZERO;
			debug_view = DriftAffineMul((DriftAffine){1.0f, 0, 0, 1.0f, -pivot.x, -pivot.y}, debug_view);
			debug_view = DriftAffineMul((DriftAffine){zoom, 0, 0, zoom,    pan.x,    pan.y}, debug_view);
			debug_view = DriftAffineMul((DriftAffine){1.0f, 0, 0, 1.0f, +pivot.x, +pivot.y}, debug_view);
			draw->v_matrix = DriftAffineMul(debug_view, draw->v_matrix);
			draw->vp_matrix = DriftAffineMul(draw->p_matrix, draw->v_matrix);
			draw->vp_inverse = DriftAffineInverse(draw->vp_matrix);
		} else {
			debug_zoom = 1;
			debug_view = DRIFT_AFFINE_IDENTITY;
		}
		prev_vp_matrix = draw->vp_matrix;
		DriftDrawBindGlobals(draw);
		TracyCZoneEnd(DRAW_ZONE_DRAW_SETUP);
			
		DriftTerrainDrawTiles(draw, debug_zoom > 1);
		DriftSystemsDraw(draw);
		
		if(script->draw) script->draw(draw, script);
		
		if(!ctx->debug.hide_hud){
			if(DriftEntitySetCheck(&state->entities, ctx->player)){
				if(state->status.show_hud) draw_hud(draw);
			} else {
				DriftVec2 ssize = draw->virtual_extent;
				{
					DriftAffine m = {ssize.x/2, 0, 0, ssize.y/2, ssize.x/4, ssize.y/4};
					DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){.color = {0x00, 0x00, 0x00, 0x80}, .matrix = m}));
				}{
					const char* message = DRIFT_TEXT_RED "Mining Pod Destroyed";
					float width = DriftDrawTextSize(message, 0, draw->input_icons).x;
					DriftAffine m = {1, 0, 0, 1, -width/2, 0};
					m = DriftAffineMul((DriftAffine){2, 0, 0, 2, draw->virtual_extent.x/2, draw->virtual_extent.y/2}, m);
					DriftDrawText(draw, &draw->hud_sprites, m, DRIFT_VEC4_WHITE, message);
				}{
					const char* message = "Press {@ACCEPT} to Reconstruct";
					float width = DriftDrawTextSize(message, 0, draw->input_icons).x;
					DriftAffine m = {1, 0, 0, 1, (draw->virtual_extent.x - width)/2, draw->virtual_extent.y/3};
					DriftDrawText(draw, &draw->hud_sprites, m, DRIFT_VEC4_WHITE, message);
				}
			}
			
			DriftAffine t = {1, 0, 0, 1, roundf(draw->virtual_extent.x) - 10*8, 8};
			DriftDrawTextF(draw, &draw->hud_sprites, t, DRIFT_VEC4_WHITE,"{#80808080}DEV {#40408080}%s",
				/*DRIFT_VERSION_MAJOR, DRIFT_VERSION_MINOR,*/ DRIFT_GIT_SHORT_SHA
			);
		}
		
		TracyCZoneN(DEBUG_UI_ZONE, "Debug UI", true);
		DriftDebugUI(&update, draw);
		TracyCZoneEnd(DEBUG_UI_ZONE);
		TracyCZoneEnd(DRAW_ZONE);
		
		TracyCZoneN(RENDER_ZONE, "Render", true);
		DriftGameStateRender(draw);
		
		DriftUIBegin(draw);
		switch(ctx->ui_state){
			case DRIFT_UI_STATE_SCAN: DriftScanUI(draw, &ctx->ui_state, ctx->last_scan); break;
			case DRIFT_UI_STATE_CRAFT: DriftCraftUI(draw, &ctx->ui_state); break;
			default: break;
		}
		DriftUIPresent(draw);
		
		// Present to the screen.
		DriftGfxRendererPushBindTargetCommand(draw->renderer, NULL, DRIFT_VEC4_CLEAR);
		DriftGfxPipelineBindings* present_bindings = DriftDrawQuads(draw, ctx->draw_shared->present_pipeline, 1);
		present_bindings->textures[1] = ctx->draw_shared->resolve_buffer;
		
		TracyCZoneN(DEBUG_UI_RENDER_ZONE, "Debug UI Draw", true);
		DriftNuklearDraw(ctx->debug.ui, draw);
		TracyCZoneEnd(DEBUG_UI_RENDER_ZONE);
		TracyCZoneEnd(RENDER_ZONE);
		
		// Cleanup and render in parallel.
		// tina_scheduler_enqueue(app->scheduler, "JobGameStateCleanup", DriftGameStateCleanup, &update, 0, DRIFT_JOB_QUEUE_WORK, &cleanup_jobs);
		tina_job_wait(job, &present_jobs, 0);
		tina_scheduler_enqueue(app->scheduler, DriftGameContextPresent, draw, 0, DRIFT_JOB_QUEUE_GFX, &present_jobs);
		// tina_job_wait(job, &cleanup_jobs, 0);
		
	#if DRIFT_MODULES
		if(ctx->input.request_hotload) DriftModuleRequestReload(app, job);
		ctx->input.request_hotload = false;
		
		if(app->module_status == DRIFT_MODULE_READY){
			DriftContextPushToast(ctx, DRIFT_TEXT_GREEN"<HOTLOAD>");
			DriftAudioPause(app->audio, true);

			ResetHotComponents(&update);
			DriftZoneMemRelease(update.mem);
			
			// TODO alias callbacks?
			
			tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
			app->gfx_driver->free_all(app->gfx_driver);
			return;
		}
	#endif
	
		TracyCZoneN(CLEANUP_ZONE, "Cleanup", true);
		DriftGameStateCleanup(&update);
		TracyCZoneEnd(CLEANUP_ZONE);
		
		TracyCFrameMark;
		ctx->current_frame = ++ctx->_frame_counter;
		
		// Yield to other tasks on the main queue.
		TracyCZoneN(YIELD_ZONE, "Yield", true);
		tina_job_yield(job);
		TracyCZoneEnd(YIELD_ZONE);
	}
	
	tina_job_wait(job, &present_jobs, 0);
	
	DriftAppHaltScheduler(app);
}
