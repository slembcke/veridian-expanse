#include "inttypes.h"

#include "SDL.h"

#include "drift_game.h"
#include "base/drift_nuklear.h"

static const float UI_LINE_HEIGHT = 12;

void DriftGameContextDebugUI(DriftUpdate* update, DriftDraw* draw){
	DriftGameState* state = update->state;
	DriftGameContext* ctx = update->ctx;
	DriftApp* app = ctx->app;
	struct nk_context* nk = &ctx->debug.ui->nk;

	float menu_height = UI_LINE_HEIGHT + 8;
	float spinner_height = 20;
	
#if DRIFT_MODULES
	if(app->module_status != DRIFT_APP_MODULE_IDLE){
		nk_begin(nk, "hotload", nk_rect(32, 32, 150, 40), NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR); {
			nk_layout_row_dynamic(nk, 0, 1);
			const char* statuses[] = {"Idle", "Building", "Error", "Ready"};
			nk_labelf(nk, NK_TEXT_LEFT, "Hotload: %s", statuses[app->module_status]);
		} nk_end(nk);
	}
#endif
	
	if(!ctx->debug.show_ui) return;

	if(nk_begin(nk, "MenuBar", nk_rect(0, 0, draw->pixel_extent.x, menu_height), NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)){
		struct nk_vec2 menu_size = nk_vec2(200, 400);
		
		nk_menubar_begin(nk);{
			nk_layout_row_static(nk, UI_LINE_HEIGHT, 50, 10);
			if(nk_menu_begin_label(nk, "Game", NK_TEXT_LEFT, menu_size)){
				extern void DriftGameStateIO(DriftIO* io);
				extern void DriftGameStateReset(DriftGameState* state, bool reset_game);
				
				nk_layout_row_dynamic(nk, UI_LINE_HEIGHT, 1);
				if(nk_menu_item_label(nk, "Save", NK_TEXT_LEFT)) DriftIOFileWrite(TMP_SAVE_FILENAME, DriftGameStateIO, state);
				if(nk_menu_item_label(nk, "Load", NK_TEXT_LEFT)) DriftGameStateReset(state, false);
				
				if(nk_menu_item_label(nk, "Reset Game", NK_TEXT_LEFT)) DriftGameStateReset(state, true);
				ctx->debug.reset_on_load = nk_check_label(nk, "Reset on Hotload", ctx->debug.reset_on_load);
				// if(nk_menu_item_label(nk, "Capture mouse", NK_TEXT_LEFT)){
				// 	SDL_SetRelativeMouseMode(true);
				// 	ctx->input.mouse_keyboard_captured = true;
				// }

#if DRIFT_MODULES
				if(nk_menu_item_label(nk, "Hotload", NK_TEXT_LEFT)) ctx->input.request_hotload = true;
#endif
				if(nk_menu_item_label(nk, "Breakpoint", NK_TEXT_LEFT)) DriftBreakpoint();
				if(nk_menu_item_label(nk, "Exit", NK_TEXT_LEFT)) ctx->input.quit = true;
				nk_menu_end(nk);
			}
			
			if(nk_menu_begin_label(nk, "Time", NK_TEXT_LEFT, menu_size)){
				nk_layout_row_dynamic(nk, UI_LINE_HEIGHT, 1);
				if(nk_menu_item_label(nk, "Halt", NK_TEXT_LEFT)) ctx->debug.pause = true;
				// ctx->debug.debug_pause = nk_check_label(nk, "Pause", ctx->debug.debug_pause);
				nk_label(nk, "Time scale:", NK_TEXT_LEFT);
				ctx->time_scale_log = nk_slide_float(nk, 0, ctx->time_scale_log, 1, 0.01f);
				nk_menu_end(nk);
			}
			
			if(nk_menu_begin_label(nk, "GFX", NK_TEXT_LEFT, menu_size)){
				nk_layout_row_dynamic(nk, UI_LINE_HEIGHT, 1);
				nk_label(nk, "Renderer:", NK_TEXT_LEFT);
				if(nk_radio_label(nk, "OpenGL 3.3", (nk_bool[]){app->shell_func == DriftShellSDLGL})) app->shell_restart = DriftShellSDLGL;
#if DRIFT_VULKAN
				if(nk_radio_label(nk, "Vulkan 1.0", (nk_bool[]){app->shell_func == DriftShellSDLVk})) app->shell_restart = DriftShellSDLVk;
#endif
				
				nk_menu_end(nk);
			}
			
			if(nk_menu_begin_label(nk, "Map", NK_TEXT_LEFT, menu_size)){
				nk_layout_row_dynamic(nk, UI_LINE_HEIGHT, 1);
				if(nk_menu_item_label(nk, "Paint", NK_TEXT_LEFT)) ctx->debug.pause = ctx->debug.paint = true;
				
				nk_layout_row_dynamic(nk, UI_LINE_HEIGHT, 1);
				if(nk_menu_item_label(nk, "Regenerate Now", NK_TEXT_LEFT)){
					DriftTerrainFree(state->terra);
					state->terra = DriftTerrainNew(draw->job, true);
				}
				
				ctx->debug.regen_terrain_on_load = nk_check_label(nk, "Regenerate on Hotload", ctx->debug.regen_terrain_on_load);
				ctx->debug.draw_terrain_sdf = nk_check_label(nk, "Draw SDF", ctx->debug.draw_terrain_sdf);
				nk_menu_end(nk);
			}
			
			if(nk_menu_begin_label(nk, "Misc", NK_TEXT_LEFT, menu_size)){
				nk_layout_row_dynamic(nk, UI_LINE_HEIGHT, 1);
				if(nk_menu_item_label(nk, "Show Info", NK_TEXT_LEFT)) ctx->debug.ui->show_info = true;
				if(nk_menu_item_label(nk, "Toggle Examples", NK_TEXT_LEFT)) ctx->debug.ui->show_examples = !ctx->debug.ui->show_examples;
				if(nk_menu_item_label(nk, "Spawn Drone", NK_TEXT_LEFT)) DriftDroneMake(state, DriftAffineOrigin(draw->vp_inverse));
				if(nk_menu_item_label(nk, "Reset Items", NK_TEXT_LEFT)){
					DRIFT_COMPONENT_FOREACH(&state->pickups.c, idx) DriftDestroyEntity(update, state->pickups.entity[idx]);
				}
				
				// DriftEntity MakeDog(DriftGameContext* ctx, DriftVec2 pos);
				// if(nk_menu_item_label(nk, "Spawn Dogs", NK_TEXT_LEFT)) DOGS.spawn_counter = 10;
				nk_menu_end(nk);
			}
			
			static bool did_show_prefs = false;
			bool show_prefs = nk_menu_begin_label(nk, "Prefs", NK_TEXT_LEFT, menu_size);
			if(show_prefs){
				nk_layout_row_dynamic(nk, UI_LINE_HEIGHT, 1);
				nk_label(nk, "Master Volume:", NK_TEXT_LEFT);
				nk_slider_float(nk, 0, &ctx->prefs.master_volume, 1, 1e-2f);
				
				nk_label(nk, "Music Volume:", NK_TEXT_LEFT);
				nk_slider_float(nk, 0, &ctx->prefs.music_volume, 1, 1e-2f);
				
				DriftAudioSetParams(ctx->audio, ctx->prefs.master_volume, ctx->prefs.music_volume);
				
				nk_menu_end(nk);
			}
			
			if(did_show_prefs && !show_prefs){
				extern void DriftPrefsIO(DriftIO* io);
				DriftIOFileWrite(TMP_PREFS_FILENAME, DriftPrefsIO, &ctx->prefs);
				DRIFT_LOG("saved '%s'", TMP_PREFS_FILENAME);
			}
			did_show_prefs = show_prefs;
		} nk_menubar_end(nk);
	} nk_end(nk);
	
	bool any_hovered = nk_window_is_any_hovered(nk);
	DriftVec2 mouse_pos = ctx->input.mouse_pos;
	bool mouse_down = ctx->input.mouse_down[DRIFT_MOUSE_LEFT] && !any_hovered;
	bool mouse_state = ctx->input.mouse_state[DRIFT_MOUSE_LEFT] && !any_hovered;
	
	if(ctx->debug.pause){
		if(nk_begin(nk, "Time", nk_rect(8, menu_height + 8, 150, 8*UI_LINE_HEIGHT), NK_WINDOW_NO_SCROLLBAR)){
			nk_layout_row_template_begin(nk, 1.5f*UI_LINE_HEIGHT);
			nk_layout_row_template_push_static(nk, 20);
			nk_layout_row_template_push_dynamic(nk);
			nk_layout_row_template_push_dynamic(nk);
			nk_layout_row_template_end(nk);
			
			if(nk_button_symbol(nk, NK_SYMBOL_TRIANGLE_RIGHT)) ctx->debug.pause = false;
			ctx->debug.tick_nanos = 0;
			if(nk_button_label(nk, "Tick")) ctx->debug.tick_nanos = (u64)(1e9f/DRIFT_TICK_HZ);
			if(nk_button_label(nk, "Sub")) ctx->debug.tick_nanos = (u64)(1e9f/DRIFT_SUBSTEP_HZ);
			
			nk_layout_row_dynamic(nk, UI_LINE_HEIGHT, 1);
			nk_labelf(nk, NK_TEXT_LEFT, "Ticks: %d", ctx->current_tick);
			nk_labelf(nk, NK_TEXT_LEFT, "Frames: %d", ctx->current_frame);
			nk_labelf(nk, NK_TEXT_LEFT, "Update ms: %"PRIu64, ctx->update_nanos/(uint)1e6);
			nk_labelf(nk, NK_TEXT_LEFT, "  Tick ms: %"PRIu64, ctx->tick_nanos/(uint)1e6);
		} nk_end(nk);
	}
	
	if(ctx->debug.paint){
		DriftTerrain* terra = ctx->state.terra;
		
		static float radius = 32;

		typedef enum {PAINT_MODE_DIG, PAINT_MODE_ADD, PAINT_MODE_SUB, PAINT_MODE_PERLIN, PAINT_MODE_BIOME} PAINT_MODE;
		static PAINT_MODE paint_mode = PAINT_MODE_ADD;
		static int biome_value;
		
		static DriftTerrainEditPerlinParams perlin_params = {
			.frq = 1, .oct = 2, .exp = 0.5, .mul = 16, .add = 3.2f,
		};
		
		if(nk_begin(nk, "Paint", nk_rect(8, 150, 150, 400), NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE)){
			nk_layout_row_dynamic(nk, UI_LINE_HEIGHT, 1);
			nk_property_float(nk, "Radius", 8, &radius, 256, 1, 1);
			
			float log_radius = log2f(radius);
			nk_slider_float(nk, 3, &log_radius, 12, 1/256.0f);
			radius = powf(2, log_radius);
			
			nk_label(nk, "Mode:", NK_TEXT_ALIGN_LEFT);
			if(nk_option_label(nk, "Dig", paint_mode == PAINT_MODE_DIG)) paint_mode = PAINT_MODE_DIG;
			if(nk_option_label(nk, "Add", paint_mode == PAINT_MODE_ADD)) paint_mode = PAINT_MODE_ADD;
			if(nk_option_label(nk, "Sub", paint_mode == PAINT_MODE_SUB)) paint_mode = PAINT_MODE_SUB;
			if(nk_option_label(nk, "Perlin", paint_mode == PAINT_MODE_PERLIN)) paint_mode = PAINT_MODE_PERLIN;
			
			nk_property_float(nk, "Frq:", 0.25f, &perlin_params.frq, 4, 0.01f, 0.01f);
			nk_property_float(nk, "Oct:", 1, &perlin_params.oct, 4, 1, 1);
			nk_property_float(nk, "Exp:", 0.125, &perlin_params.exp, 1, 0.01f, 0.01f);
			nk_property_float(nk, "Mul:", -32, &perlin_params.mul, 32, 1, 0.1f);
			nk_property_float(nk, "Add:", -32, &perlin_params.add, 32, 1, 0.1f);
			
			if(nk_option_label(nk, "Biome", paint_mode == PAINT_MODE_BIOME)) paint_mode = PAINT_MODE_BIOME;
			nk_property_int(nk, "Value:", 0, &biome_value, 3, 1, 0.1f);
			
			// Spacer
			nk_layout_row_dynamic(nk, UI_LINE_HEIGHT, 1);
			
			nk_layout_row_dynamic(nk, 2*UI_LINE_HEIGHT, 2);
			if(nk_button_label(nk, "Load")) DriftTerrainEditIO(update->job, terra, false);
			if(nk_button_label(nk, "Save")) DriftTerrainEditIO(update->job, terra, true);
			
			nk_layout_row_dynamic(nk, 2*UI_LINE_HEIGHT, 1);
			if(nk_button_label(nk, "Reset Cache")) DriftTerrainResetCache(terra);
			if(nk_button_label(nk, "Rectify")) DriftTerrainEditRectify(terra, update->job);
			if(nk_button_label(nk, "Done")) ctx->debug.pause = ctx->debug.paint = false;
		} nk_end(nk);
		
		if(!any_hovered) DriftDebugCircle2(state, mouse_pos, radius, radius - 1, DRIFT_RGBA8_RED);
		
		if(mouse_state){
			if(paint_mode == PAINT_MODE_DIG) DriftTerrainDig(terra, mouse_pos, radius);
			if(paint_mode == PAINT_MODE_ADD) DriftTerrainEdit(terra, mouse_pos, radius, DriftTerrainEditAdd, NULL);
			if(paint_mode == PAINT_MODE_SUB) DriftTerrainEdit(terra, mouse_pos, radius, DriftTerrainEditSub, NULL);
			if(paint_mode == PAINT_MODE_PERLIN) DriftTerrainEdit(terra, mouse_pos, radius, DriftTerrainEditPerlin, &perlin_params);
			
			static const DriftRGBA8 BIOME[] = {{255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0}, {0, 0, 0, 255}};
			if(paint_mode == PAINT_MODE_BIOME) DriftBiomeEdit(terra, mouse_pos, radius, BIOME[biome_value]);
		}
	}
	
	if(ctx->debug.ui->show_info){
		if(nk_begin(nk, "Info", nk_rect(0, 32, 300, 300), NK_WINDOW_TITLE | NK_WINDOW_CLOSABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE)){
			nk_layout_row_dynamic(nk, UI_LINE_HEIGHT, 1);
			
			nk_label(nk, DriftSMFormat(draw->mem, "Mouse pos: {v2: 9.2}", mouse_pos), NK_LEFT);
			
			static DriftEntity selected_entity = {};
			if(mouse_down){
				selected_entity.id = 0;
				float selected_dist = 64;
				DRIFT_COMPONENT_FOREACH(&state->transforms.c, i){
					float dist = DriftVec2Distance(mouse_pos, DriftAffineOrigin(state->transforms.matrix[i]));
					if(dist < selected_dist){
						selected_dist = dist;
						selected_entity = state->transforms.entity[i];
					}
				}
			}
			
			nk_labelf(nk, NK_LEFT, "Selected: "DRIFT_ENTITY_FORMAT, selected_entity.id);
			
			if(nk_tree_push(nk, NK_TREE_TAB, "Input 0", NK_MINIMIZED)){
				nk_layout_row_dynamic(nk, UI_LINE_HEIGHT, 1);
				DriftPlayerInput* input = &ctx->input.player;
				
				for(uint i = 0; i < _DRIFT_INPUT_AXIS_COUNT; i++){
					nk_labelf(nk, NK_TEXT_LEFT, "axis[%d]: %5.2f (%5.2f, %5.2f)", i, input->axes[i], input->_analog_axis[i], input->_digital_axis[i]);
				}
				nk_labelf(nk, NK_TEXT_LEFT, "bstate  : 0x%08"PRIX32, (u32)input->bstate);
				nk_labelf(nk, NK_TEXT_LEFT, "bpress  : 0x%08"PRIX32, (u32)input->bpress);
				nk_labelf(nk, NK_TEXT_LEFT, "brelease: 0x%08"PRIX32, (u32)input->brelease);
				
				nk_tree_pop(nk);
			}
			
			if(nk_tree_push(nk, NK_TREE_TAB, "Terrain", NK_MINIMIZED)){
				nk_layout_row_dynamic(nk, UI_LINE_HEIGHT, 1);
				uint tile_idx = DriftTerrainTileAt(state->terra, mouse_pos);
				DriftTerrainTileCoord coord = state->terra->tilemap.coord[tile_idx];
				nk_labelf(nk, NK_TEXT_LEFT, "tile: %5d, coord: (%3d, %3d)", tile_idx, coord.x, coord.y);
				uint tile_state = state->terra->tilemap.state[tile_idx];
				uint texture_idx = state->terra->tilemap.texture_idx[tile_idx];
				nk_labelf(nk, NK_TEXT_LEFT, "state: %d, texture: %d", tile_state, texture_idx);
				
				DriftTerrainSampleInfo coarse = DriftTerrainSampleCoarse(state->terra, mouse_pos);
				DriftDebugSegment(state, mouse_pos, DriftVec2FMA(mouse_pos, coarse.grad, -coarse.dist), 1, DRIFT_RGBA8_RED);
				nk_label(nk, DriftSMFormat(draw->mem, "coarse: (d: {f: 6.2}, g: {v2: 5.2})", coarse.dist, coarse.grad), NK_LEFT);
				
				DriftTerrainSampleInfo fine = DriftTerrainSampleFine(state->terra, mouse_pos);
				DriftDebugSegment(state, mouse_pos, DriftVec2FMA(mouse_pos, fine.grad, -fine.dist), 1, DRIFT_RGBA8_GREEN);
				nk_label(nk, DriftSMFormat(draw->mem, "  fine: (d: {f: 6.2}, g: {v2: 5.2})", fine.dist, fine.grad), NK_LEFT);
				
				uint biome = DriftTerrainSampleBiome(state->terra, mouse_pos);
				nk_label(nk, DriftSMPrintf(draw->mem, "  biome: %d", biome), NK_LEFT);
				
				nk_tree_pop(nk);
			}
			
			if(nk_tree_push(nk, NK_TREE_TAB, "Transforms", NK_MINIMIZED)){
				DRIFT_COMPONENT_FOREACH(&state->transforms.c, idx){
					DriftEntity e = state->transforms.entity[idx];
					DriftAffine t = state->transforms.matrix[idx];
					DriftVec2 p = DriftAffineOrigin(t);
					DriftDrawTextF(draw, &draw->overlay_sprites, (DriftAffine){1, 0, 0, 1, p.x, p.y - 4}, " "DRIFT_ENTITY_FORMAT, e.id);
					
					DriftDebugSegment(state, p, (DriftVec2){p.x + 12*t.a, p.y + 12*t.b}, 1, DRIFT_RGBA8_RED);
					DriftDebugSegment(state, p, (DriftVec2){p.x + 12*t.c, p.y + 12*t.d}, 1, DRIFT_RGBA8_GREEN);
				}
				
				uint idx = DriftComponentFind(&state->transforms.c, selected_entity);
				if(idx){
					nk_layout_row_dynamic(nk, spinner_height, 3);
					nk_property_float(nk, "a", -INFINITY, &state->transforms.matrix[idx].a, INFINITY, 1e-2f, 1e-2f);
					nk_property_float(nk, "c", -INFINITY, &state->transforms.matrix[idx].c, INFINITY, 1e-2f, 1e-2f);
					nk_property_float(nk, "x", -INFINITY, &state->transforms.matrix[idx].x, INFINITY, 1.0f, 1.0f);
					
					nk_property_float(nk, "b", -INFINITY, &state->transforms.matrix[idx].b, INFINITY, 1e-2f, 1e-2f);
					nk_property_float(nk, "d", -INFINITY, &state->transforms.matrix[idx].d, INFINITY, 1e-2f, 1e-2f);
					nk_property_float(nk, "y", -INFINITY, &state->transforms.matrix[idx].y, INFINITY, 1.0f, 1.0f);
				} else {
					nk_label(nk, "No component for entity", NK_TEXT_LEFT);
				}
				
				nk_tree_pop(nk);
			}
			
			if(nk_tree_push(nk, NK_TREE_TAB, "Bodies", NK_MINIMIZED)){
				DRIFT_COMPONENT_FOREACH(&state->bodies.c, idx){
					DriftEntity e = state->bodies.entity[idx];
					DriftVec2 p = state->bodies.position[idx];
					DriftDrawTextF(draw, &draw->overlay_sprites, (DriftAffine){1, 0, 0, 1, p.x, p.y - 4}, " "DRIFT_ENTITY_FORMAT, e.id);
					DriftDebugCircle(state, p, state->bodies.radius[idx], (DriftRGBA8){0x80, 0x40, 0x00, 0x80});
				}
				
				uint idx = DriftComponentFind(&state->bodies.c, selected_entity);
				if(idx){
					nk_layout_row_dynamic(nk, spinner_height, 2);
					nk_property_float(nk, "pos.x", -INFINITY, &state->bodies.position[idx].x, INFINITY, 1, 1);
					nk_property_float(nk, "pos.y", -INFINITY, &state->bodies.position[idx].y, INFINITY, 1, 1);
					
					nk_property_float(nk, "radius", -INFINITY, &state->bodies.radius[idx], INFINITY, 1, 1);
				} else {
					nk_label(nk, "No component for entity", NK_TEXT_LEFT);
				}
				
				nk_tree_pop(nk);
			}
			
			if(nk_tree_push(nk, NK_TREE_TAB, "Power", NK_MINIMIZED)){
				nk_layout_row_dynamic(nk, UI_LINE_HEIGHT, 1);
				nk_labelf(nk, NK_TEXT_LEFT, "nodes: %d, edges: %d", state->power_nodes.c.count - 1, (uint)state->power_edges.t.row_count);
				
				nk_label(nk, "Flow Maps:", NK_TEXT_LEFT);
				static uint flow_idx = 0;
				for(uint i = 0; i < DRIFT_FLOW_MAP_COUNT; i++){
					if(nk_radio_label(nk, state->flow_maps[i].name.str, (nk_bool[]){flow_idx == i})) flow_idx = i;
				}
				
				uint selected_idx = 0;
				
				DriftPowerNode* nodes = state->power_nodes.node;
				DriftComponentFlowMap* fmap = state->flow_maps + flow_idx;
				DriftFlowMapNode* flow = fmap->node;
				DRIFT_COMPONENT_FOREACH(&state->power_nodes.c, idx){
					uint flow_idx = DriftComponentFind(&fmap->c, state->power_nodes.entity[idx]);
					DriftVec2 p0 = {nodes[idx].x, nodes[idx].y};
					DriftAffine m = {1, 0, 0, 1, p0.x, p0.y - 14};
					DriftDrawTextF(draw, &draw->overlay_sprites, m, " %.1f", flow[flow_idx].root_dist);
					
					uint next_idx = DriftComponentFind(&state->power_nodes.c, flow[flow_idx].next);
					if(next_idx){
						DriftVec2 p1 = {nodes[next_idx].x, nodes[next_idx].y};
						DriftDebugSegment(state, p0, DriftVec2Lerp(p0, p1, 0.25f), 2, DRIFT_RGBA8_WHITE);
					}
					
					if(DriftVec2Distance(mouse_pos, p0) < 16) selected_idx = idx;
				}
				
				// nk_labelf(nk, NK_TEXT_LEFT, "%s[0].root_dist: %.1f", fmap->name.str, flow[selected_idx].root_dist);
				// nk_labelf(nk, NK_TEXT_LEFT, "%s[%d].next_dist: %.1f, next: %d", fmap->name.str, selected_idx, flow[selected_idx].next_dist, flow[selected_idx].next.id);
				nk_tree_pop(nk);
			}
		} nk_end(nk);
		
		ctx->debug.ui->show_info = !nk_window_is_hidden(nk, "Info");
	}
	
	if(ctx->debug.ui->show_examples) DriftNuklearOverview(nk);
}
