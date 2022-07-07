#include <string.h>
#include <inttypes.h>
#include <stdio.h>

#include "tina/tina.h"
#include "SDL.h"
#include "tracy/TracyC.h"

#include "drift_game.h"
#include "base/drift_nuklear.h"

void DriftPrefsIO(DriftIO* io){
	DriftPreferences* prefs = io->user_ptr;
	DriftIOBlock(io, "prefs", prefs, sizeof(*prefs));
}

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

// TODO terrible name
void DriftGameStateReset(DriftGameState* state, bool reset_game){
	state->tables = DRIFT_ARRAY_NEW(DriftSystemMem, 64, DriftTable*);
	state->components = DRIFT_ARRAY_NEW(DriftSystemMem, 64, DriftComponent*);
	
	DriftEntitySetInit(&state->entities);
	DriftSystemsInit(state);
	
	if(reset_game){
		{ // TODO
			DriftEntity e = DriftMakeEntity(state);
			uint pnode_idx = DriftComponentAdd(&state->power_nodes.c, e);
			state->power_nodes.node[pnode_idx].x = 128;
			
			uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
			state->transforms.matrix[transform_idx] = (DriftAffine){1, 0, 0, 1, 128, 0};
			
			uint sprite_idx = DriftComponentAdd(&state->sprites.c, e);
			state->sprites.data[sprite_idx].frame = DRIFT_SPRITE_POWER_NODE;
			state->sprites.data[sprite_idx].color = DRIFT_RGBA8_WHITE;
			
			uint idx0 = DriftComponentAdd(&state->flow_maps[0].c, e);
			state->flow_maps[0].node[idx0].next = e;
			state->flow_maps[0].node[idx0].root_dist = 0;
		}
	} else {
		DriftIOFileRead(TMP_SAVE_FILENAME, DriftGameStateIO, state);
	}
}

DriftGameContext* DriftGameContextCreate(DriftApp* app, tina_job* job){
	DriftGameContext* ctx = DriftAlloc(DriftSystemMem, sizeof(*ctx));
	memset(ctx, 0x00, sizeof(*ctx));
	ctx->app = app;
	
	ctx->prefs = (DriftPreferences){.master_volume = 1, .music_volume = 0.5f};
	DriftIOFileRead(TMP_PREFS_FILENAME, DriftPrefsIO, &ctx->prefs);
	
	ctx->audio = DriftAudioContextCreate();
	DriftAudioSetParams(ctx->audio, ctx->prefs.master_volume, ctx->prefs.music_volume);
	
	{ // Init ctx.state.
		mtx_init(&ctx->state.entities_mtx, mtx_plain);
		ctx->state.terra = DriftTerrainNew(job, false);
		DriftGameStateReset(&ctx->state, true);
	}
	
	ctx->script.ctx = ctx;
	
	uint queue = tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
	ctx->draw_shared = DriftDrawSharedNew(app, job);
	ctx->debug.ui = DriftNuklearNew();
	DriftNuklearSetupGFX(ctx->debug.ui, ctx->draw_shared);
	tina_job_switch_queue(job, queue);
	
	ctx->init_nanos = ctx->clock_nanos = DriftTimeNanos();
	
	return ctx;
}

void DriftGameContextStart(tina_job* job){
	DriftApp* app = tina_job_get_description(job)->user_data;
	DriftGameContext* ctx = app->app_context;
	DriftAssetsReset();	extern tina_func tutorial_func;
	tina_func* script_func = tutorial_func;
	
	if(ctx == NULL){
		// No context, normal startup.
		ctx = app->app_context = DriftGameContextCreate(app, job);
	} else {
		if(ctx->debug.reset_on_load){
			DriftGameStateReset(&ctx->state, true);
		}
		
		script_func = ctx->script.coro ? tutorial_func : NULL;
		ctx->message = NULL;
		
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
	
	if(!ctx->audio_ready){
		DriftAudioOpenDevice(ctx->audio);
		ctx->audio_ready = true;
	}

	DriftAudioLoadSamples(ctx->audio, job);
	DriftAudioStartMusic(ctx->audio);
	DriftAudioPause(ctx->audio, false);
	
	ctx->script.debug_skip = true;
	if(script_func) ctx->script.coro = tina_init(ctx->script.buffer, sizeof(ctx->script.buffer), script_func, &ctx->script);
	
	DriftAppShowWindow(app);
	DriftGameContextLoop(job);
}

static void DriftGameStateRender(DriftGameState* state, DriftDraw* draw){
	DriftGfxRenderer* renderer = draw->renderer;
	DriftDrawShared* draw_shared = draw->shared;	
	
	typedef struct {
		DriftGPUMatrix v_matrix, p_matrix, terrain_matrix;
		DriftGPUMatrix vp_matrix, vp_inverse, reproj_matrix;
		DriftVec2 pixel_extent, screen_extent, buffer_extent;
		float atlas_size, biome_layer;
	} GlobalUniforms;
	
	GlobalUniforms global_uniforms = {
		.v_matrix = DriftAffineToGPU(draw->v_matrix),
		.p_matrix = DriftAffineToGPU(draw->p_matrix),
		.terrain_matrix = DriftAffineToGPU(state->terra->map_to_world),
		.vp_matrix = DriftAffineToGPU(draw->vp_matrix),
		.vp_inverse = DriftAffineToGPU(draw->vp_inverse),
		.reproj_matrix = DriftAffineToGPU(draw->reproj_matrix),
		.pixel_extent = draw->pixel_extent,
		.screen_extent = draw->screen_extent,
		.buffer_extent = draw->buffer_extent,
		.atlas_size = DRIFT_ATLAS_SIZE, .biome_layer = DRIFT_ATLAS_BIOME,
	};
	
	DriftAffine ui_projection = DriftAffineOrtho(0, draw->screen_extent.x, 0, draw->screen_extent.y);
	GlobalUniforms ui_uniforms = global_uniforms;
	ui_uniforms.v_matrix = DriftAffineToGPU(DRIFT_AFFINE_IDENTITY);
	ui_uniforms.p_matrix = DriftAffineToGPU(ui_projection);
	ui_uniforms.vp_matrix = ui_uniforms.p_matrix;
	ui_uniforms.vp_inverse = DriftAffineToGPU(DriftAffineInverse(ui_projection));

	// Push global data.
	static const DriftVec2 QUAD_UVS[] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
	static const u16 QUAD_INDICES[] = {0, 1, 2, 3, 2, 1};
	draw->quad_vertex_binding = DriftGfxRendererPushGeometry(renderer, QUAD_UVS, sizeof(QUAD_UVS)).binding;
	draw->quad_index_binding = DriftGfxRendererPushIndexes(renderer, QUAD_INDICES, sizeof(QUAD_INDICES)).binding;
	draw->globals_binding = DriftGfxRendererPushUniforms(renderer, &global_uniforms, sizeof(global_uniforms)).binding;
	DriftGfxBufferBinding ui_binding = DriftGfxRendererPushUniforms(renderer, &ui_uniforms, sizeof(ui_uniforms)).binding;
	
	// Render the lightfield buffer.
	uint light_count = DriftArrayLength(draw->lights);
	DriftGfxBufferBinding light_instances_binding = DriftGfxRendererPushGeometry(draw->renderer, draw->lights, light_count*sizeof(*draw->lights)).binding;

	for(uint pass = 0; pass < 2; pass++){
		DriftGfxRendererPushBindTargetCommand(renderer, draw_shared->lightfield_target[pass], DRIFT_VEC4_CLEAR);
		DriftGfxPipelineBindings* bindings = DriftDrawQuads(draw, draw_shared->light_pipeline[pass], light_count);
		bindings->instance = light_instances_binding;
	}

	// Render the shadow buffer.
	uint shadow_count = 0;
	for(uint i = 0; i < light_count; i++){
		// TODO perform culling here? (set shadow caster prop I suppose?)
		if(draw->lights[i].shadow_caster) shadow_count++;
	}

	// Push the shadow mask data.
	uint shadow_mask_count = DriftArrayLength(draw->shadow_masks);
	// DRIFT_LOG("shadows to render: %d, masks: %d", shadow_count, shadow_mask_count);
	DriftGfxBufferBinding shadow_mask_instances = DriftGfxRendererPushGeometry(renderer, draw->shadow_masks, shadow_mask_count*sizeof(*draw->shadow_masks)).binding;
	// + 1 to avoid 0 length VLAs.
	DriftGfxBufferBinding shadow_uniform_bindings[shadow_count + 1];
	DriftGfxBufferBinding light_instance_bindings[shadow_count + 1];
	DriftAABB2 light_bounds[shadow_count + 1];
	
	DriftVec2 shadowfield_extent = DriftVec2Mul(draw->buffer_extent, 1.0/DRIFT_SHADOWFIELD_SCALE);
	DriftAffine shadowfield_ortho = DriftAffineOrtho(0, shadowfield_extent.x, 0, shadowfield_extent.y);
	DriftAffine shadowfield_matrix = DriftAffineMult(DriftAffineInverse(shadowfield_ortho), draw->vp_matrix);
	
	for(uint i = 0, j = 0; i < light_count; i++){
		if(draw->lights[i].shadow_caster){
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
			
			DriftAffine m_bound = DriftAffineMult(shadowfield_matrix, light_matrix);
			DriftSpriteFrame frame = draw->lights[i].frame;
			DriftVec2 offset = {(s8)frame.anchor.x, (s8)frame.anchor.y};
			offset.x = 2*offset.x/(frame.bounds.r - frame.bounds.l) - 1;
			offset.y = 2*offset.y/(frame.bounds.t - frame.bounds.b) - 1;
			m_bound.x -= m_bound.a*offset.x + m_bound.c*offset.y;
			m_bound.y -= m_bound.b*offset.x + m_bound.d*offset.y;
			
			float hw = fabsf(m_bound.a) + fabsf(m_bound.c), hh = fabsf(m_bound.b) + fabsf(m_bound.d);
			light_bounds[j] = (DriftAABB2){m_bound.x - hw, m_bound.y - hh, m_bound.x + hw, m_bound.y + hh};
			
			j++;
		}

		// These are already buffered by the lightfield step, so it needs to skip over non-shadow lights.
		light_instances_binding.offset += sizeof(DriftLight);
	}

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
	DriftGfxRendererPushBindTargetCommand(renderer, draw_shared->color_target[draw_shared->color_buffer_index & 1], DRIFT_VEC4_CLEAR);
	// DriftGfxPipelineBindings* light_debug_bindings = DriftDrawQuads(draw, draw->debug_lightfield_pipeline, 1);

	typedef struct {
		DRIFT_ARRAY(void) arr;
		DriftGfxPipeline* pipeline;
		DriftGfxPipelineBindings* bindings;
	} DriftDrawBatch;
	
	DriftGfxTexture* color_buffer = draw_shared->color_buffer[(draw_shared->color_buffer_index + 0) % 2];
	draw->color_buffer_prev = draw_shared->color_buffer[(draw_shared->color_buffer_index + 1) % 2];
	draw_shared->color_buffer_index++;
	
	DriftGfxPipelineBindings shared_bindings = {
		.vertex = draw->quad_vertex_binding,
		.uniforms[0] = draw->globals_binding,
		.samplers[0] = draw_shared->nearest_sampler,
		.samplers[1] = draw_shared->linear_sampler,
		.textures[0] = draw_shared->atlas_texture,
		.textures[5] = draw->color_buffer_prev,
		.textures[6] = draw_shared->lightfield_buffer,
		.textures[7] = draw_shared->shadowfield_buffer,
	};
	
	DriftGfxPipeline* terrain_pipeline = draw_shared->terrain_pipeline;
	if(draw->ctx->debug.draw_terrain_sdf) terrain_pipeline = draw_shared->debug_terrain_pipeline;
	DriftGfxPipelineBindings terrain_bindings = shared_bindings;
	terrain_bindings.samplers[2] = draw_shared->terrain_sampler;
	terrain_bindings.textures[1] = draw_shared->terrain_tiles;
	
	DriftGfxPipelineBindings hud_bindings = shared_bindings;
	hud_bindings.uniforms[0] = ui_binding;
	
	DriftDrawBatch batches[] = {
		{.arr = draw->terrain_chunks, .pipeline = terrain_pipeline, .bindings = &terrain_bindings},
		{.arr = draw->bg_sprites, .pipeline = draw_shared->sprite_pipeline, .bindings = &shared_bindings},
		{.arr = draw->bg_prims, .pipeline = draw_shared->primitive_pipeline, .bindings = &shared_bindings},
		{.arr = draw->fg_sprites, .pipeline = draw_shared->sprite_pipeline, .bindings = &shared_bindings},
		{.arr = draw->overlay_sprites, .pipeline = draw_shared->overlay_sprite_pipeline, .bindings = &shared_bindings},
		{.arr = draw->hud_sprites, .pipeline = draw_shared->overlay_sprite_pipeline, .bindings = &hud_bindings},
		{.arr = draw->overlay_prims, .pipeline = draw_shared->primitive_pipeline, .bindings = &shared_bindings},
		{.arr = state->debug_prims, .pipeline = draw_shared->primitive_pipeline, .bindings = &shared_bindings},
		{},
	};
	
	for(uint i = 0; batches[i].arr; i++){
		DriftDrawBatch* batch = batches + i;
		DriftArray* header = DriftArrayHeader(batch->arr);
		
		DriftGfxPipelineBindings* bindings = DriftGfxRendererPushBindPipelineCommand(renderer, batch->pipeline);
		*bindings = *batch->bindings;
		bindings->instance = DriftGfxRendererPushGeometry(renderer, batch->arr, header->count*header->elt_size).binding;
		DriftGfxRendererPushDrawIndexedCommand(renderer, draw->quad_index_binding, 6, header->count);
	}
	
	// Blit to the screen.
	DriftGfxRendererPushBindTargetCommand(renderer, NULL, DRIFT_VEC4_CLEAR);
	DriftGfxPipelineBindings* blit_bindings = DriftDrawQuads(draw, draw_shared->present_pipeline, 1);
	blit_bindings->textures[1] = color_buffer;
}

static const float UI_LINE_HEIGHT = 12;

static void craft_ui(struct nk_context* nk, DriftGameContext* ctx){
	nk_layout_row_dynamic(nk, 2*UI_LINE_HEIGHT, 1);
	if(nk_button_label(nk, "cheaty cheater")){
		ctx->inventory[DRIFT_ITEM_TYPE_ORE] += 100;
		ctx->inventory[DRIFT_ITEM_TYPE_LUMIUM] += 100;
		ctx->inventory[DRIFT_ITEM_TYPE_SCRAP] += 100;
	}
	
	for(uint i = 0; i < _DRIFT_ITEM_TYPE_COUNT; i++){
		const DriftCraftableItem* craftable = DRIFT_CRAFTABLES + i;
		const char* name = DRIFT_ITEMS[i].name;
		const DriftIngredient* ingredients = DRIFT_CRAFTABLES[i].ingredients;
		// static const DriftIngredient ingredients[] = {
		// 	{.type = DRIFT_ITEM_TYPE_ORE, .count = 0},
		// 	{},
		// };
		
		uint p_count = 0;
		while(p_count < DRIFT_CRAFTABLE_MAX_INGREDIENTS){
			if(ingredients[p_count].type == DRIFT_ITEM_TYPE_NONE) break;
			p_count++;
		}
		
		if(p_count > 0){
			float panel_height = (1 + p_count)*UI_LINE_HEIGHT;
			nk_layout_row_dynamic(nk, panel_height + 10, 1);
			if(nk_group_begin(nk, name, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)){
				nk_layout_space_begin(nk, NK_STATIC, panel_height, 10);
				
				float line_cursor = 0;
				nk_layout_space_push(nk, nk_rect(0, 0, 200, UI_LINE_HEIGHT)); line_cursor += UI_LINE_HEIGHT;
				nk_labelf(nk, NK_TEXT_LEFT, "%-16s    (have %u)", name, ctx->inventory[i]);
				bool can_craft = true;
				
				for(uint p = 0; p < p_count; p++){
					uint p_type = ingredients[p].type;
					const char* p_name = DRIFT_ITEMS[p_type].name;
					uint have = ctx->inventory[p_type], need = ingredients[p].count;
					bool enough = have >= need;
					can_craft = can_craft && enough;
					
					static const struct nk_color green = {0x00, 0xFF, 0x00, 0xFF};
					static const struct nk_color red = {0xFF, 0x00, 0x00, 0xFF};
					struct nk_color color = enough ? green : red;
					
					nk_layout_space_push(nk, nk_rect(0, line_cursor, 200, UI_LINE_HEIGHT)); line_cursor += UI_LINE_HEIGHT;
					nk_labelf_colored(nk, NK_TEXT_LEFT, color, "  %-16s %4d / %2d", p_name, have, need);
				}
				
				float panel_width = nk_window_get_panel(nk)->clip.w;
				struct nk_rect button_rect = {panel_width, 0, 50, 2*UI_LINE_HEIGHT};
				button_rect.x -= button_rect.w;
				button_rect.y += (panel_height - button_rect.h)/2;
				nk_layout_space_push(nk, button_rect);
				if(can_craft && nk_button_label(nk, "Craft")){
					ctx->inventory[i] += 1;
					for(uint p = 0; p < p_count; p++){
						ctx->inventory[ingredients[p].type] -= ingredients[p].count;
					}
					
					// TODO
					DriftPlayerData* player = ctx->state.players.data + DriftComponentFind(&ctx->state.players.c, ctx->player);
					if(i == DRIFT_ITEM_TYPE_RUSTY_CANNON) player->quickslots[3] = DRIFT_TOOL_GUN;
					if(i == DRIFT_ITEM_TYPE_MINING_LASER) player->quickslots[2] = DRIFT_TOOL_DIG;
				}
				
				nk_layout_space_end(nk);
				nk_group_end(nk);
			}
		}
	}
}

static void cargo_ui(struct nk_context* nk, DriftPlayerData* player){
	struct nk_style_button button = nk->style.button;
	struct nk_style_button green = button;
	green.normal = nk_style_item_color(nk_rgb(45, 150, 69));
	green.hover = nk_style_item_color(nk_rgb(45, 170, 69));
	green.active = nk_style_item_color(nk_rgb(45, 190, 69));
	
	static int popup_slot = -1;
	if(popup_slot >= 0){
		struct nk_rect r = nk_window_get_content_region(nk);
		if(nk_popup_begin(nk, NK_POPUP_STATIC, "Choose Slot", NK_WINDOW_BORDER, nk_rect(0, -UI_LINE_HEIGHT, r.w, r.h + UI_LINE_HEIGHT))){
			for(uint i = 0; i < _DRIFT_ITEM_TYPE_COUNT; i++){
				if(i > 0 && !DRIFT_ITEMS[i].cargo) continue;
				
				nk_layout_row_dynamic(nk, 2*UI_LINE_HEIGHT, 3);
				
				nk->style.button = i ? green : button;
				if(nk_button_label(nk, i ? DRIFT_ITEMS[i].name : "To Storage")){
					player->cargo_slots[popup_slot].request = i;
					nk_popup_close(nk);
					popup_slot = -1;
				}
			}
			nk->style.button = button;
			
			nk_popup_end(nk);
		}
	}
	
	for(uint i = 0; i < DRIFT_PLAYER_CARGO_SLOT_COUNT; i++){
		DriftItemType type = player->cargo_slots[i].request;
		nk_layout_row_dynamic(nk, 2*UI_LINE_HEIGHT, 2);
		nk_labelf(nk, NK_TEXT_LEFT, "Slot %d type:", i);
		
		nk->style.button = type ? green : button;
		if(nk_button_label(nk, type ? DRIFT_ITEMS[type].name : "To Storage")) popup_slot = i;
	}
	
	nk->style.button = button;
}

static void equip_ui(struct nk_context* nk, DriftPlayerData* player){
	static const char* names[] = {
		"Headlight",
		"Grabber",
		"Laser Shovel",
		"Laser Pick",
		"Blaster",
	};
	uint count = 5;
	
	struct nk_style_button button = nk->style.button;
	struct nk_style_button green = button;
	green.normal = nk_style_item_color(nk_rgb(45, 150, 69));
	green.hover = nk_style_item_color(nk_rgb(45, 170, 69));
	green.active = nk_style_item_color(nk_rgb(45, 190, 69));
	
	nk_layout_row_dynamic(nk, 2*UI_LINE_HEIGHT, 3);
	for(uint i = 0; i < count; i++){
		const char* name = names[i];
		nk_label(nk, name, NK_TEXT_LEFT);
		nk_label(nk, "foobar", NK_TEXT_LEFT);
		
		static bool equipped = true;
		nk->style.button = equipped ? green : button;
		if(nk_button_label(nk, equipped ? "Unequip" : "Equip")){
			equipped = !equipped;
		}
		
		nk->style.button = button;
	}
}

static void DriftGameContextTempUI(DriftUpdate* update, DriftDraw* draw){
	DriftGameState* state = update->state;
	struct nk_context* nk = &update->ctx->debug.ui->nk;
	
	DriftPlayerInput* input = &update->ctx->input.player;
	input->ui_active ^= DriftInputButtonPress(input, DRIFT_INPUT_OPEN_UI);
	if(!input->ui_active) return;
	
	struct nk_rect craft_rect = {.x = 256, .w = 300, .h = 512};
	craft_rect.y = (draw->pixel_extent.y - craft_rect.h)/2;
	if(nk_begin(nk, "TempUI", craft_rect, NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_NO_SCROLLBAR)){
		struct nk_vec2 wsize = nk_window_get_size(nk);
		if(wsize.x < craft_rect.w) wsize.x = craft_rect.w;
		if(wsize.y < craft_rect.h) wsize.y = craft_rect.h;
		nk_window_set_size(nk, nk->current->name_string, wsize);
		
		struct nk_style_button button = nk->style.button;
		struct nk_style_button tab_active = button;
		struct nk_style_button tab_inactive = button;
		tab_inactive.normal = nk_style_item_color(nk_rgb(40, 40, 40));
		tab_inactive.hover = nk_style_item_color(nk_rgb(80, 80, 80));
		
		static uint tab = 0, TAB_COUNT = 2;
		static const char* TABS[] = {"Craft", "Cargo", "Equip"};
		nk_layout_row_dynamic(nk, 2*UI_LINE_HEIGHT, TAB_COUNT);
		for(uint i = 0; i < TAB_COUNT; i++){
			nk->style.button = tab == i ? tab_active : tab_inactive;
			if(nk_button_label(nk, TABS[i])) tab = i;
		}
		nk->style.button = button;
		
		float panel_height = nk_window_get_content_region(nk).h - 2*UI_LINE_HEIGHT;
		nk_layout_row_dynamic(nk, panel_height, 1);
		if(nk_group_begin(nk, TABS[tab], 0)){
			switch(tab){
				case 0: craft_ui(nk, update->ctx); break;
				case 1: cargo_ui(nk, state->players.data + 1); break;
				case 2: equip_ui(nk, state->players.data + 1); break;
				case 3: break;
			}
			nk_group_end(nk);
		}
	} nk_end(nk);
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

static void DriftGameStateCleanup(tina_job* job){
	DriftUpdate* update = tina_job_get_description(job)->user_data;
	DriftGameState* state = update->state;
	
	DriftZoneMemRelease(update->zone);
	DriftArrayHeader(state->debug_prims)->count = 0;
	
	destroy_entities(state, update->_dead_entities);
	
	static uint gc_cursor = 0;
	DriftTable* table = &state->power_edges.t;
	DriftPowerNodeEdge* edges = state->power_edges.edge;
	for(uint run = 0; table->row_count > 0 && run < 20;){
		uint idx = gc_cursor % table->row_count;
		DriftEntity e0 = edges[idx].e0, e1 = edges[idx].e1;
		// TODO these should be cleared on removal.
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

static void DriftHUD(DriftDraw* draw){
	TracyCZoneN(ZONE_HUD, "HUD", true);
	DriftGameContext* ctx = draw->ctx;
	DriftGameState* state = draw->state;
	DriftPlayerData* player = state->players.data + 1; // TODO player
	
	{ // Draw quickslots
		DriftAffine t = {1, 0, 0, 1, 8, draw->screen_extent.y - 18};
		t = DriftDrawText(draw, &draw->hud_sprites, t, "Tools:\n");
		
		const char* icons[] = {" ", "{@QUICKSLOT1}", "{@QUICKSLOT2}", "{@QUICKSLOT3}", "{@QUICKSLOT4}"};
		for(uint i = 1; i < DRIFT_QUICKSLOT_COUNT; i++){
			uint tool_idx = player->quickslots[i];
			const char* tool_name = tool_idx ? DRIFT_TOOLS[tool_idx].name : "-";
			const char* highlight = (player->quickslot_idx == i ? DRIFT_TEXT_GREEN : DRIFT_TEXT_WHITE);
			t = DriftDrawTextF(draw, &draw->hud_sprites, t, DRIFT_TEXT_GRAY"%s %s%s\n", icons[i], highlight, tool_name);
		}
	}
	
	{ // Draw inventory
		DriftAffine t = {1, 0, 0, 1, 8, draw->screen_extent.y - 100};
		t = DriftDrawText(draw, &draw->hud_sprites, t, "Inventory: {@CARGO_PREV} | {@CARGO_NEXT}\n");
		
		for(uint i = 0; i < DRIFT_PLAYER_CARGO_SLOT_COUNT; i++){
			const char* name = DRIFT_ITEMS[player->cargo_slots[i].type].name;
			uint count = player->cargo_slots[i].count;
			if(name && count){
				const char* highlight = (player->cargo_idx == i ? DRIFT_TEXT_GREEN : DRIFT_TEXT_GRAY);
				t = DriftDrawTextF(draw, &draw->hud_sprites, t, "%2d %s%s\n", count, highlight, name);
			} else {
				t = DriftDrawText(draw, &draw->hud_sprites, t, "--"DRIFT_TEXT_GRAY" (empty)\n");
			}
		}
		
		t = DriftDrawText(draw, &draw->hud_sprites, t, "\nPress {@OPEN_UI} to craft.\n");
		t = DriftDrawText(draw, &draw->hud_sprites, t, "Toggle light with {@HEADLIGHT}.");
	}
	
	{ // Draw Health
		uint health_idx = DriftComponentFind(&state->health.c, ctx->player);
		DriftHealth* health = state->health.data + health_idx;
		DriftAffine t = {1, 0, 0, 1, 128, draw->screen_extent.y - 18};
		float percent = health->value/health->maximum;
		const char* color = percent > 0.3f ? DRIFT_TEXT_GREEN : DRIFT_TEXT_RED;
		DriftDrawTextF(draw, &draw->hud_sprites, t, "Health: %s%d%%", color, (uint)(100*percent));
	}
	
	{ // Draw Power
		uint health_idx = DriftComponentFind(&state->health.c, ctx->player);
		DriftHealth* health = state->health.data + health_idx;
		DriftAffine t = {1, 0, 0, 1, 256, draw->screen_extent.y - 18};
		float percent = player->power_reserve/player->power_capacity;
		const char* color = DRIFT_TEXT_GREEN;
		if(percent < 1) color = "{#FF8000FF}";
		if(percent < 0.3f) color = DRIFT_TEXT_RED;
		DriftDrawTextF(draw, &draw->hud_sprites, t, "Power: %s%d%%", color, (uint)(100*percent));
	}
	
	// TODO Temporary message area.
	DriftAffine t1 = {1, 0, 0, 1, 8, 2*10 + 8};
	if(ctx->message) DriftDrawText(draw, &draw->hud_sprites, t1, ctx->message);
	TracyCZoneEnd(ZONE_HUD);
}

static const char* DRIFT_FRAME_TRACY = "DRIFT_FRAME_TRACY";

void DriftGameContextPresent(tina_job* job){
	static const char* FRAME_PRESENT = "Present";
	TracyCFrameMarkStart(FRAME_PRESENT);
	DriftDraw* draw = tina_job_get_description(job)->user_data;
	DriftAppPresentFrame(draw->shared->app, draw->renderer);
	DriftZoneMemRelease(draw->zone);
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
	
	// ctx->debug.show_ui = true;
	// ctx->debug.pause = true;
	// ctx->debug.paint = true;
	// ctx->debug.draw_terrain_sdf = true;
	
	DriftAffine prev_vp_matrix = DRIFT_AFFINE_IDENTITY;
	state->debug_prims = DRIFT_ARRAY_NEW(DriftSystemMem, 0, DriftPrimitive);
	
	float debug_zoom = 1;
	DriftVec2 debug_pan = DRIFT_VEC2_ZERO;
	DriftAffine debug_view = DRIFT_AFFINE_IDENTITY;
	
	tina_group present_jobs = {}, cleanup_jobs = {};
	while(!ctx->input.quit && !app->shell_restart){
		DRIFT_ASSERT_WARN(tina_job_switch_queue(job, DRIFT_JOB_QUEUE_MAIN) == DRIFT_JOB_QUEUE_MAIN, "Main thread is on the wrong queue?");
		
		// Update the time.
		u64 prev_nanos = ctx->clock_nanos;
		ctx->clock_nanos = DriftTimeNanos();
		
		double unfiltered_nanos = ctx->clock_nanos - prev_nanos;
		double filtered_nanos = iir_filter(unfiltered_nanos, 3, x, y, filter_b, filter_a);
		double variance = iir_filter(pow(unfiltered_nanos - filtered_nanos, 2), 3, var_x, var_y, filter_b, filter_a);
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
		DriftInputEventsPoll(app, ctx, job, ctx->debug.ui, DriftAffineInverse(prev_vp_matrix));
		TracyCZoneEnd(INPUT_ZONE);
		
		DriftZoneMem* zone = DriftZoneMemAquire(app->zone_heap, "UpdateMem");
		DriftUpdate update = {
			.ctx = ctx, .job = job, .state = state, .audio = ctx->audio,
			.scheduler = app->scheduler, .zone = zone, .mem = DriftZoneMemWrap(zone),
			.dt = update_nanos/1e9f, .tick_dt = 1/DRIFT_TICK_HZ, .prev_vp_matrix = prev_vp_matrix,
		};
		
		update._dead_entities = DRIFT_ARRAY_NEW(update.mem, 256, DriftEntity);
		
		TracyCZoneN(UPDATE_ZONE, "Update", true);
		if(update_nanos > 0){
			DriftSystemsUpdate(&update);
			
			if(ctx->script.coro && !ctx->script.coro->completed){
				tina_resume(ctx->script.coro, 0);
			} else {
				ctx->script.coro = NULL;
			}
		}
		TracyCZoneEnd(UPDATE_ZONE);
		
		u64 tick_dt_nanos = (u64)(1e9f/DRIFT_TICK_HZ);
		while(ctx->tick_nanos < ctx->update_nanos){
			TracyCZoneN(ZONE_TICK, "Tick", true);
			DriftSystemsTick(&update);
			TracyCZoneEnd(ZONE_TICK);
			
			TracyCZoneN(ZONE_PHYSICS, "Physics", true);
			DriftPhysicsTick(&update);
			for(uint i = 0; i < DRIFT_SUBSTEPS; i++) DriftPhysicsSubstep(&update);
			TracyCZoneEnd(ZONE_PHYSICS);
			
			ctx->tick_nanos += tick_dt_nanos;
			ctx->current_tick++;
		}
		
		float dt_since_tick = (ctx->tick_nanos - ctx->update_nanos)/-1e9f;
		ctx->update_nanos += update_nanos;
		
		TracyCZoneN(INTERPOLATE_ZONE, "Interpolate", true);
		DriftPhysicsSyncTransforms(&update, dt_since_tick);
		TracyCZoneEnd(INTERPOLATE_ZONE);
		
		TracyCZoneN(DRAW_ZONE, "Draw", true);
		
		TracyCZoneN(DRAW_ZONE_CAMERA, "Camera Setup", true);
		uint transform_idx = DriftComponentFind(&state->transforms.c, ctx->player);
		DriftVec2 camera_pos = {state->transforms.matrix[transform_idx].x, state->transforms.matrix[transform_idx].y};
		
		DriftDraw* draw = DriftDrawCreate(ctx, job);
		draw->v_matrix = (DriftAffine){1, 0, 0, 1, -camera_pos.x, -camera_pos.y};
		draw->p_matrix = DriftAffineOrtho(-0.5f*draw->buffer_extent.x, 0.5f*draw->buffer_extent.x, -0.5f*draw->buffer_extent.y, 0.5f*draw->buffer_extent.y);
		draw->dt = update.dt;
		draw->dt_since_tick = dt_since_tick;
		
		if(ctx->debug.pause){
			debug_zoom = DriftClamp(debug_zoom*exp2f(-0.5f*ctx->input.mouse_wheel), 1/256.0f, 4);
			float zoom = powf(debug_view.a/debug_zoom, expf(-15.0f*delta_nanos/1e9f) - 1);
			
			DriftAffine p_inv = DriftAffineInverse(draw->p_matrix);
			DriftVec2 pivot = DriftAffinePoint(p_inv, ctx->input.mouse_pos_clip);
			DriftVec2 pan = DriftAffineDirection(p_inv, DriftVec2Sub(ctx->input.mouse_pos_clip, debug_pan));
			debug_pan = ctx->input.mouse_pos_clip;
			if(!ctx->input.mouse_state[DRIFT_MOUSE_MIDDLE]) pan = DRIFT_VEC2_ZERO;
			debug_view = DriftAffineMult((DriftAffine){1.0f, 0, 0, 1.0f, -pivot.x, -pivot.y}, debug_view);
			debug_view = DriftAffineMult((DriftAffine){zoom, 0, 0, zoom,    pan.x,    pan.y}, debug_view);
			debug_view = DriftAffineMult((DriftAffine){1.0f, 0, 0, 1.0f, +pivot.x, +pivot.y}, debug_view);
			draw->v_matrix = DriftAffineMult(debug_view, draw->v_matrix);
		} else {
			debug_zoom = 1;
			debug_view = DRIFT_AFFINE_IDENTITY;
		}
		
		draw->vp_matrix = DriftAffineMult(draw->p_matrix, draw->v_matrix);
		draw->vp_inverse = DriftAffineInverse(draw->vp_matrix);
		draw->reproj_matrix = DriftAffineMult(prev_vp_matrix, draw->vp_inverse);
		prev_vp_matrix = draw->vp_matrix;
		tina_group draw_jobs = {};
		TracyCZoneEnd(DRAW_ZONE_CAMERA);
			
		DriftTerrainDraw(draw);
		DriftSystemsDraw(draw);
		DriftHUD(draw);

		DriftAffine t = {1, 0, 0, 1, draw->screen_extent.x - 64, 32};
		char short_sha[] = DRIFT_GIT_SHA;
		short_sha[8] = 0;
		DriftDrawTextF(draw, &draw->hud_sprites, t,"%s%6.2f Hz\n {#80404080}v%d.%d {#80808080}DEV\n {#40408080}%8s",
			interval_quality ? "{#00FF00FF}" : "{#FF0000FF}", 1e9/delta_nanos, DRIFT_VERSION_MAJOR, DRIFT_VERSION_MINOR, short_sha
		);
		
		tina_job_wait(job, &draw_jobs, 0);
		TracyCZoneEnd(DRAW_ZONE);
		
		TracyCZoneN(UI_ZONE, "UI", true);
		void DriftGameContextDebugUI(DriftUpdate* update, DriftDraw* draw);
		DriftGameContextDebugUI(&update, draw);
		DriftGameContextTempUI(&update, draw);
		TracyCZoneEnd(UI_ZONE);
		
		TracyCZoneN(RENDER_ZONE, "Render", true);
		DriftGameStateRender(draw->state, draw);
		DriftNuklearDraw(ctx->debug.ui, draw);
		TracyCZoneEnd(RENDER_ZONE);
		
		// Cleanup and render in parallel.
		tina_scheduler_enqueue(app->scheduler, "JobGameStateCleanup", DriftGameStateCleanup, &update, 0, DRIFT_JOB_QUEUE_WORK, &cleanup_jobs);
		tina_job_wait(job, &present_jobs, 0);
		tina_scheduler_enqueue(app->scheduler, "JobGameContextPresent", DriftGameContextPresent, draw, 0, DRIFT_JOB_QUEUE_GFX, &present_jobs);
		tina_job_wait(job, &cleanup_jobs, 0);
		
	#if DRIFT_MODULES
		if(ctx->input.request_hotload && DriftAppModuleRequestReload(app, job)){
			DriftAudioPause(ctx->audio, true);

			// Reset hot components.
			ResetHotComponents(&update);
			
			// TODO alias callbacks?
			
			ctx->input.request_hotload = false;
			tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
			app->gfx_driver->free_all(app->gfx_driver);
			return;
		}
	#endif
		
		TracyCFrameMark;
		// TracyCFrameMarkNamed("Update");
		ctx->current_frame++;
		
		// Yield to other tasks on the main queue.
		TracyCZoneN(YIELD_ZONE, "YIELD", true);
		tina_job_yield(job);
		TracyCZoneEnd(YIELD_ZONE);
	}
	
	tina_job_wait(job, &present_jobs, 0);
	
	DriftAppHaltScheduler(app);
	DriftAudioCloseDevice(ctx->audio);
	ctx->audio_ready = false;
}
