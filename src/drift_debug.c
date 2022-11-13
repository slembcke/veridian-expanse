#include "inttypes.h"

#include "SDL.h"

#include "drift_game.h"
#include "base/drift_nuklear.h"

DriftVec4 TMP_COLOR[4];
float TMP_VALUE[4];

static const float UI_LINE_HEIGHT = 12;

static DriftUpdate* UPDATE;
static DriftDraw* DRAW;
static DriftGameState* STATE;
static DriftGameContext* CTX;
static DriftApp* APP;
static struct nk_context* NK;

static bool ANY_HOVERED;
static DriftVec2 MOUSE_POS;
static bool MOUSE_DOWN;
static bool MOUSE_STATE;

static tina_group PAINT_PROGRESS_GROUP;

static void wait_for_exit(tina_job* job){
	DriftGameContext* ctx = tina_job_get_description(job)->user_data;
	tina_job_wait(job, &PAINT_PROGRESS_GROUP, 0);
	ctx->debug.pause = false;
	
}

static void SetPaintMode(bool value){
	CTX->debug.paint = value;
	CTX->debug.boost_ambient = value;
	
	if(value){
		CTX->debug.pause = true;
		DriftTerrainEditEnter();
	} else {
		tina_scheduler_enqueue(UPDATE->scheduler, DriftTerrainEditExit, STATE->terra, 0, DRIFT_JOB_QUEUE_WORK, &PAINT_PROGRESS_GROUP);
		tina_scheduler_enqueue(UPDATE->scheduler, wait_for_exit, CTX, 0, DRIFT_JOB_QUEUE_MAIN, NULL);
	}
}

static void PaintUI(void){
	DriftTerrain* terra = CTX->state.terra;
	static float radius = 32;
	
	typedef enum {PAINT_MODE_DIG, PAINT_MODE_ADD, PAINT_MODE_SUB, PAINT_MODE_PERLIN, PAINT_MODE_BIOME} PAINT_MODE;
	static PAINT_MODE paint_mode = PAINT_MODE_ADD;
	static int biome_value;
	
	static DriftTerrainEditPerlinParams perlin_params = {
		.frq = 1, .oct = 2, .exp = 0.5, .mul = 16, .add = 3.2f,
	};
	
	if(nk_begin(NK, "Paint", nk_rect(8, 125, 200, 350), NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_SCALABLE)){
		nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
		nk_property_float(NK, "Radius", 8, &radius, 256, 1, 1);
		
		float log_radius = log2f(radius);
		nk_slider_float(NK, 3, &log_radius, 12, 1/256.0f);
		radius = powf(2, log_radius);
		
		nk_label(NK, "Mode:", NK_TEXT_ALIGN_LEFT);
		if(nk_option_label(NK, "Dig", paint_mode == PAINT_MODE_DIG)) paint_mode = PAINT_MODE_DIG;
		if(nk_option_label(NK, "Add", paint_mode == PAINT_MODE_ADD)) paint_mode = PAINT_MODE_ADD;
		if(nk_option_label(NK, "Sub", paint_mode == PAINT_MODE_SUB)) paint_mode = PAINT_MODE_SUB;
		if(nk_option_label(NK, "Perlin", paint_mode == PAINT_MODE_PERLIN)) paint_mode = PAINT_MODE_PERLIN;
		
		nk_property_float(NK, "Frq:", 0.25f, &perlin_params.frq, 4, 0.01f, 0.01f);
		nk_property_float(NK, "Oct:", 1, &perlin_params.oct, 4, 1, 1);
		nk_property_float(NK, "Exp:", 0.125, &perlin_params.exp, 1, 0.01f, 0.01f);
		nk_property_float(NK, "Mul:", -32, &perlin_params.mul, 32, 1, 0.1f);
		nk_property_float(NK, "Add:", -32, &perlin_params.add, 32, 1, 0.1f);
		
		if(nk_option_label(NK, "Biome", paint_mode == PAINT_MODE_BIOME)) paint_mode = PAINT_MODE_BIOME;
		nk_property_int(NK, "Value:", 0, &biome_value, 4, 1, 0.1f);
		
		// Spacer
		nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
		
		nk_layout_row_dynamic(NK, 2*UI_LINE_HEIGHT, 2);
		if(nk_button_label(NK, "Load")) DriftTerrainEditIO(UPDATE->job, terra, false);
		if(nk_button_label(NK, "Save")) DriftTerrainEditIO(UPDATE->job, terra, true);
		
		if(nk_button_label(NK, "Reset Cache")) DriftTerrainResetCache(terra);
		if(nk_button_label(NK, "Rectify")) DriftTerrainEditRectify(terra, UPDATE->scheduler, &PAINT_PROGRESS_GROUP);
		
		nk_layout_row_dynamic(NK, 2*UI_LINE_HEIGHT, 1);
		if(nk_button_label(NK, "Done")) SetPaintMode(false);
	} nk_end(NK);
	
	if(!ANY_HOVERED) DriftDebugCircle2(STATE, MOUSE_POS, radius, radius - 1, DRIFT_RGBA8_RED);
	
	if(MOUSE_STATE){
		if(paint_mode == PAINT_MODE_DIG) DriftTerrainDig(terra, MOUSE_POS, radius);
		if(paint_mode == PAINT_MODE_ADD) DriftTerrainEdit(terra, MOUSE_POS, radius, DriftTerrainEditAdd, NULL);
		if(paint_mode == PAINT_MODE_SUB) DriftTerrainEdit(terra, MOUSE_POS, radius, DriftTerrainEditSub, NULL);
		if(paint_mode == PAINT_MODE_PERLIN) DriftTerrainEdit(terra, MOUSE_POS, radius, DriftTerrainEditPerlin, &perlin_params);
		
		static const DriftRGBA8 BIOME[] = {{255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0}, {0, 0, 0, 255}, {}};
		if(paint_mode == PAINT_MODE_BIOME) DriftBiomeEdit(terra, MOUSE_POS, radius, BIOME[biome_value]);
	}
}

void DriftDebugUI(DriftUpdate* _update, DriftDraw* _draw){
	UPDATE = _update;
	DRAW = _draw;
	STATE = UPDATE->state;
	CTX = UPDATE->ctx;
	APP = CTX->app;
	NK = &CTX->debug.ui->nk;

	ANY_HOVERED = nk_window_is_any_hovered(NK);
	MOUSE_POS = CTX->input.mouse_pos_world;
	MOUSE_DOWN = CTX->input.mouse_down[DRIFT_MOUSE_LEFT] && !ANY_HOVERED;
	
	static bool allow_state;
	// Allow MOUSE_STATE after clicking on the canvas.
	if(MOUSE_DOWN) allow_state = true;
	MOUSE_STATE = CTX->input.mouse_state[DRIFT_MOUSE_LEFT] && allow_state;
	// Reset allow state on mouseup.
	if(CTX->input.mouse_up[DRIFT_MOUSE_LEFT]) allow_state = false;
	
	const float menu_height = UI_LINE_HEIGHT + 8;
	const float spinner_height = 20;
	
#if DRIFT_MODULES
	if(APP->module_status != DRIFT_MODULE_RUNNING){
		nk_begin(NK, "hotload", nk_rect(32, 32, 150, 40), NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR); {
			nk_layout_row_dynamic(NK, 0, 1);
			const char* statuses[] = {"Idle", "Building", "Error", "Ready"};
			nk_labelf(NK, NK_TEXT_LEFT, "Hotload: %s", statuses[APP->module_status]);
		} nk_end(NK);
	}
#endif
	
	if(!CTX->debug.show_ui) return;

	if(nk_begin(NK, "MenuBar", nk_rect(0, 0, DRAW->raw_extent.x, menu_height), NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)){
		struct nk_vec2 menu_size = nk_vec2(200, 800);
		
		nk_menubar_begin(NK);{
			nk_layout_row_static(NK, UI_LINE_HEIGHT, 50, 10);
			if(nk_menu_begin_label(NK, "Game", NK_TEXT_LEFT, menu_size)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				if(nk_menu_item_label(NK, "Save", NK_TEXT_LEFT)) DriftIOFileWrite(TMP_SAVE_FILENAME, DriftGameStateIO, STATE);
				if(nk_menu_item_label(NK, "Load", NK_TEXT_LEFT)) DriftGameStateInit(STATE, false);
				
				if(nk_menu_item_label(NK, "Reset Game", NK_TEXT_LEFT)) DriftGameStateInit(STATE, true);
				nk_checkbox_label(NK, "Reset on Hotload", &CTX->debug.reset_on_load);
				// if(nk_menu_item_label(nk, "Capture mouse", NK_TEXT_LEFT)){
				// 	SDL_SetRelativeMouseMode(true);
				// 	ctx->input.mouse_keyboard_captured = true;
				// }

#if DRIFT_MODULES
				if(nk_menu_item_label(NK, "Hotload", NK_TEXT_LEFT)) CTX->input.request_hotload = true;
#endif
				if(nk_menu_item_label(NK, "Breakpoint", NK_TEXT_LEFT)) DriftBreakpoint();
				if(nk_menu_item_label(NK, "Exit", NK_TEXT_LEFT)) CTX->input.quit = true;
				nk_menu_end(NK);
			}
			
			if(nk_menu_begin_label(NK, "Time", NK_TEXT_LEFT, menu_size)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				if(nk_menu_item_label(NK, "Halt", NK_TEXT_LEFT)) CTX->debug.pause = true;
				nk_label(NK, "Time scale:", NK_TEXT_LEFT);
				CTX->time_scale_log = nk_slide_float(NK, 0, CTX->time_scale_log, 1, 0.01f);
				nk_menu_end(NK);
			}
			
			if(nk_menu_begin_label(NK, "GFX", NK_TEXT_LEFT, menu_size)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				nk_label(NK, "Renderer:", NK_TEXT_LEFT);
				if(nk_radio_label(NK, "OpenGL 3.3", (nk_bool[]){APP->shell_func == DriftShellSDLGL})) APP->shell_restart = DriftShellSDLGL;
#if DRIFT_VULKAN
				if(nk_radio_label(NK, "Vulkan 1.0", (nk_bool[]){APP->shell_func == DriftShellSDLVk})) APP->shell_restart = DriftShellSDLVk;
#endif
				
				nk_menu_end(NK);
			}
			
			if(nk_menu_begin_label(NK, "Map", NK_TEXT_LEFT, menu_size)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				if(nk_menu_item_label(NK, "Paint", NK_TEXT_LEFT)) SetPaintMode(true);
				
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				if(nk_menu_item_label(NK, "Regenerate Now", NK_TEXT_LEFT)){
					DriftTerrainFree(STATE->terra);
					STATE->terra = DriftTerrainNew(DRAW->job, true);
				}
				
				nk_checkbox_label(NK, "Regenerate on Hotload", &CTX->debug.regen_terrain_on_load);
				nk_checkbox_label(NK, "Draw SDF", &CTX->debug.draw_terrain_sdf);
				nk_checkbox_label(NK, "Hide Decals", &CTX->debug.hide_terrain_decals);
				nk_checkbox_label(NK, "Disable Haze", &CTX->debug.disable_haze);
				nk_checkbox_label(NK, "Boost Ambient", &CTX->debug.boost_ambient);
				if(nk_menu_item_label(NK, "Full Visibility", NK_TEXT_LEFT)){
					memset(STATE->terra->tilemap.visibility, 0xFF, sizeof(STATE->terra->tilemap.visibility));
				}
				
				nk_menu_end(NK);
			}
			
			if(nk_menu_begin_label(NK, "Misc", NK_TEXT_LEFT, menu_size)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				nk_checkbox_label(NK, "Godmode", &CTX->debug.godmode);
				nk_checkbox_label(NK, "Hide HUD", &CTX->debug.hide_hud);
				if(nk_menu_item_label(NK, "Tutorial Skip", NK_TEXT_LEFT)) CTX->script.debug_skip = true;
				if(nk_menu_item_label(NK, "Show Info", NK_TEXT_LEFT)) CTX->debug.ui->show_info = true;
				if(nk_menu_item_label(NK, "Spawn Drone", NK_TEXT_LEFT)) DriftDroneMake(STATE, DriftAffineOrigin(DRAW->vp_inverse));
				if(nk_menu_item_label(NK, "Reset Items", NK_TEXT_LEFT)){
					DRIFT_COMPONENT_FOREACH(&STATE->items.c, idx) DriftDestroyEntity(UPDATE, STATE->items.entity[idx]);
				}
				if(nk_menu_item_label(NK, "Reset Enemies", NK_TEXT_LEFT)){
					DRIFT_COMPONENT_FOREACH(&STATE->enemies.c, idx) DriftDestroyEntity(UPDATE, STATE->enemies.entity[idx]);
				}
				
				if(nk_menu_item_label(NK, "Dump nodes", NK_TEXT_LEFT)){
					uint pnode_idx, transform_idx;
					DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
						{.component = &STATE->power_nodes.c, .variable = &pnode_idx},
						{.component = &STATE->transforms.c, .variable = &transform_idx},
						{},
					});
					
					while(DriftJoinNext(&join)){
						DriftVec2 pos = STATE->power_nodes.position[pnode_idx];
						DRIFT_LOG("%f, %f", pos.x, pos.y);
					}
				}
				
				if(nk_menu_item_label(NK, "Toggle Examples", NK_TEXT_LEFT)) CTX->debug.ui->show_examples = !CTX->debug.ui->show_examples;
				nk_menu_end(NK);
			}
			
			if(nk_menu_begin_label(NK, "Progress", NK_TEXT_LEFT, menu_size)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				nk_label(NK, "Scans:", NK_TEXT_LEFT);
				
				if(nk_menu_item_label(NK, "All Scans", NK_TEXT_LEFT)){
					for(uint i = 0; i < _DRIFT_SCAN_COUNT; i++) STATE->scan_progress[i] = 1;
				}
				if(nk_menu_item_label(NK, "Clear Scans", NK_TEXT_LEFT)){
					for(uint i = 0; i < _DRIFT_SCAN_COUNT; i++) STATE->scan_progress[i] = 0;
				}
				
				for(DriftScanType i = 1; i < _DRIFT_SCAN_COUNT; i++){
					const char* name = DRIFT_SCANS[i].name;
					if(name){
						bool done = STATE->scan_progress[i] == 1;
						if(nk_checkbox_label(NK, name, &done)) STATE->scan_progress[i] = done;
					}
				}
				
				nk_label(NK, "Items:", NK_TEXT_LEFT);
				if(nk_menu_item_label(NK, "Clear Inventory", NK_TEXT_LEFT)){
					for(uint i = 0; i < _DRIFT_ITEM_COUNT; i++) STATE->inventory[i] = 0;
				}
				for(DriftItemType i = 1; i < _DRIFT_ITEM_COUNT; i++){
					const char* name = DRIFT_ITEMS[i].name;
					if(!name) continue;
					
					int count = STATE->inventory[i];
					nk_property_int(NK, name, 0, &count, 1000, 1, 1);
					STATE->inventory[i] = count;
				}
				
				nk_menu_end(NK);
			}
			
			if(nk_menu_begin_label(NK, "TMP", NK_TEXT_LEFT, nk_vec2(300, 900))){
				for(uint i = 0; i < 4; i++){
					const char* label = DriftSMPrintf(UPDATE->mem, "TMP_COLOR[%d]:", i);
					if(nk_tree_push_id(NK, NK_TREE_TAB, label, NK_MINIMIZED, i)){
						static float multiplier[4] = {1, 1, 1, 1};
						static DriftVec4 color[4];
						float* m = multiplier + i;
						nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
						nk_property_float(NK, "multiplier:", 1, m, INFINITY, 0.05f, 0.01f);
						
						nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 4);
						nk_property_float(NK, "r:", 0, &color[i].r, 1, 0.05f, 0.01f);
						nk_property_float(NK, "g:", 0, &color[i].g, 1, 0.05f, 0.01f);
						nk_property_float(NK, "b:", 0, &color[i].b, 1, 0.05f, 0.01f);
						nk_property_float(NK, "a:", 0, &color[i].a, 1, 0.05f, 0.01f);
						
						nk_layout_row_dynamic(NK, 250, 1);
						nk_color_pick(NK, (struct nk_colorf*)color + i, NK_RGBA);
						
						DriftVec4 c = TMP_COLOR[i] = DriftVec4Mul(color[i], multiplier[i]);
						nk_layout_row_dynamic(NK, 1.5f*UI_LINE_HEIGHT, 2);
						if(nk_button_label(NK, "Copy as Vec4")){
							SDL_SetClipboardText(DriftSMPrintf(UPDATE->mem, "(DriftVec4){{%.2ff, %.2ff, %.2ff, %.2ff}}", c.r, c.g, c.b, c.a));
						}
						if(nk_button_label(NK, "Copy as Hex")){
							// SDL_SetClipboardText(DriftSMPrintf(update->mem, "(DriftVec4){{%.2ff, %.2ff, %.2ff, %.2ff}}", c->r, c->g, c->b, c->a));
						}
						
						nk_tree_pop(NK);
					}
				}
				
				float step = 0.01f;
				SDL_Keymod keymod = SDL_GetModState();
				if(keymod & KMOD_SHIFT) step *= 10;
				if(keymod & KMOD_ALT) step *= 100;
				
				// nk_layout_row_dynamic(nk, 1.5f*UI_LINE_HEIGHT, 2);
				nk_layout_row(NK, NK_DYNAMIC, 1.5f*UI_LINE_HEIGHT, 2, (float[]){0.75f, 0.25f});
				for(uint i = 0; i < 4; i++){
					const char* label = DriftSMPrintf(UPDATE->mem, "TMP_VALUE[%d]:", i);
					nk_property_float(NK, label, -INFINITY, TMP_VALUE + i, INFINITY, 5*step, step);
					if(nk_button_label(NK, "Copy")){
						SDL_SetClipboardText(DriftSMPrintf(UPDATE->mem, "%.3ff", TMP_VALUE[i]));
					}
				}
				
				nk_menu_end(NK);
			}
			
			static bool did_show_prefs = false;
			bool show_prefs = nk_menu_begin_label(NK, "Prefs", NK_TEXT_LEFT, menu_size);
			if(show_prefs){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				nk_label(NK, "Master Volume:", NK_TEXT_LEFT);
				nk_slider_float(NK, 0, &APP->prefs.master_volume, 1, 1e-2f);
				
				nk_label(NK, "Music Volume:", NK_TEXT_LEFT);
				nk_slider_float(NK, 0, &APP->prefs.music_volume, 1, 1e-2f);
				
				DriftAudioSetParams(APP->audio, APP->prefs.master_volume, APP->prefs.music_volume);
				
				nk_menu_end(NK);
			}
			
			if(did_show_prefs && !show_prefs){
				DriftIOFileWrite(TMP_PREFS_FILENAME, DriftPrefsIO, &APP->prefs);
				DRIFT_LOG("saved '%s'", TMP_PREFS_FILENAME);
			}
			did_show_prefs = show_prefs;
		} nk_menubar_end(NK);
	} nk_end(NK);
	
	if(CTX->debug.pause){
		if(nk_begin(NK, "Time", nk_rect(8, menu_height + 8, 150, 8*UI_LINE_HEIGHT), NK_WINDOW_NO_SCROLLBAR)){
			nk_layout_row_template_begin(NK, 1.5f*UI_LINE_HEIGHT);
			nk_layout_row_template_push_static(NK, 20);
			nk_layout_row_template_push_dynamic(NK);
			nk_layout_row_template_push_dynamic(NK);
			nk_layout_row_template_end(NK);
			
			if(nk_button_symbol(NK, NK_SYMBOL_TRIANGLE_RIGHT)) CTX->debug.pause = false;
			CTX->debug.tick_nanos = 0;
			if(nk_button_label(NK, "Tick")) CTX->debug.tick_nanos = (u64)(1e9f/DRIFT_TICK_HZ);
			if(nk_button_label(NK, "Sub")) CTX->debug.tick_nanos = (u64)(1e9f/DRIFT_SUBSTEP_HZ);
			
			nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
			nk_labelf(NK, NK_TEXT_LEFT, "Ticks: %d", CTX->current_tick);
			nk_labelf(NK, NK_TEXT_LEFT, "Frames: %d", CTX->current_frame);
			// nk_labelf(NK, NK_TEXT_LEFT, "Update ms: %"PRIu64, CTX->update_nanos/(uint)1e6);
			// nk_labelf(NK, NK_TEXT_LEFT, "  Tick ms: %"PRIu64, CTX->tick_nanos/(uint)1e6);
		} nk_end(NK);
	}
	
	uint paint_waiting = tina_job_wait(UPDATE->job, &PAINT_PROGRESS_GROUP, 1);
	if(paint_waiting){
		DriftVec2 center = DriftVec2Mul(DRAW->raw_extent, 0.5f), size = {300, 64};
		struct nk_rect rect = nk_rect(center.x - size.x/2, center.y - size.y/2, size.x, size.y);
		nk_begin(NK, "Terrain Progress", rect, NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR); {
			nk_layout_row_dynamic(NK, 0, 1);
			nk_size progress = (nk_size)(STATE->terra->rectify_progress*UINT16_MAX);
			nk_progress(NK, &progress, UINT16_MAX, false);
		} nk_end(NK);
		
	} else if(CTX->debug.paint){
		PaintUI();
	}

	
	if(CTX->debug.ui->show_info){
		if(nk_begin(NK, "Info", nk_rect(0, 32, 300, 300), NK_WINDOW_TITLE | NK_WINDOW_CLOSABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE)){
			nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
			
			nk_label(NK, DriftSMFormat(DRAW->mem, "Mouse pos: {v2: 9.2}", MOUSE_POS), NK_TEXT_LEFT);
			if(MOUSE_DOWN){
				const char* text = DriftSMPrintf(UPDATE->mem, "(DriftVec2){%ff, %ff}", MOUSE_POS.x, MOUSE_POS.y);
				DRIFT_LOG("Copied '%s' into clipboard", text);
				SDL_SetClipboardText(text);
			}

			static DriftEntity selected_entity = {};
			if(MOUSE_DOWN){
				selected_entity.id = 0;
				float selected_dist = 64;
				DRIFT_COMPONENT_FOREACH(&STATE->transforms.c, i){
					float dist = DriftVec2Distance(MOUSE_POS, DriftAffineOrigin(STATE->transforms.matrix[i]));
					if(dist < selected_dist){
						selected_dist = dist;
						selected_entity = STATE->transforms.entity[i];
					}
				}
			}
			
			nk_labelf(NK, NK_TEXT_LEFT, "Selected: "DRIFT_ENTITY_FORMAT, selected_entity.id);
			
			if(nk_tree_push(NK, NK_TREE_TAB, "Input 0", NK_MINIMIZED)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				DriftPlayerInput* input = &CTX->input.player;
				
				for(uint i = 0; i < _DRIFT_INPUT_AXIS_COUNT; i++){
					nk_labelf(NK, NK_TEXT_LEFT, "axis[%d]: %5.2f (%5.2f, %5.2f)", i, input->axes[i], input->_analog_axis[i], input->_digital_axis[i]);
				}
				nk_labelf(NK, NK_TEXT_LEFT, "bstate  : 0x%08"PRIX32, (u32)input->bstate);
				nk_labelf(NK, NK_TEXT_LEFT, "bpress  : 0x%08"PRIX32, (u32)input->bpress);
				nk_labelf(NK, NK_TEXT_LEFT, "brelease: 0x%08"PRIX32, (u32)input->brelease);
				
				nk_tree_pop(NK);
			}
			
			if(nk_tree_push(NK, NK_TREE_TAB, "Terrain", NK_MINIMIZED)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				uint tile_idx = DriftTerrainTileAt(STATE->terra, MOUSE_POS);
				DriftTerrainTileCoord coord = STATE->terra->tilemap.coord[tile_idx];
				nk_labelf(NK, NK_TEXT_LEFT, "tile: %5d, coord: (%3d, %3d)", tile_idx, coord.x, coord.y);
				uint tile_state = STATE->terra->tilemap.state[tile_idx];
				uint texture_idx = STATE->terra->tilemap.texture_idx[tile_idx];
				nk_labelf(NK, NK_TEXT_LEFT, "state: %d, texture: %d", tile_state, texture_idx);
				
				DriftTerrainSampleInfo coarse = DriftTerrainSampleCoarse(STATE->terra, MOUSE_POS);
				DriftDebugSegment(STATE, MOUSE_POS, DriftVec2FMA(MOUSE_POS, coarse.grad, -coarse.dist), 1, DRIFT_RGBA8_RED);
				nk_label(NK, DriftSMFormat(DRAW->mem, "coarse: (d: {f: 6.2}, g: {v2: 5.2})", coarse.dist, coarse.grad), NK_TEXT_LEFT);
				
				DriftTerrainSampleInfo fine = DriftTerrainSampleFine(STATE->terra, MOUSE_POS);
				DriftDebugSegment(STATE, MOUSE_POS, DriftVec2FMA(MOUSE_POS, fine.grad, -fine.dist), 1, DRIFT_RGBA8_GREEN);
				nk_label(NK, DriftSMFormat(DRAW->mem, "  fine: (d: {f: 6.2}, g: {v2: 5.2})", fine.dist, fine.grad), NK_TEXT_LEFT);
				
				uint biome = DriftTerrainSampleBiome(STATE->terra, MOUSE_POS).idx;
				nk_label(NK, DriftSMPrintf(DRAW->mem, "  biome: %d", biome), NK_TEXT_LEFT);
				
				nk_tree_pop(NK);
			}
			
			if(nk_tree_push(NK, NK_TREE_TAB, "Transforms", NK_MINIMIZED)){
				DRIFT_COMPONENT_FOREACH(&STATE->transforms.c, idx){
					DriftEntity e = STATE->transforms.entity[idx];
					DriftAffine t = STATE->transforms.matrix[idx];
					DriftVec2 p = DriftAffineOrigin(t);
					DriftDrawTextF(DRAW, &STATE->debug.sprites, (DriftAffine){1, 0, 0, 1, p.x, p.y - 4}, DRIFT_VEC4_WHITE, " "DRIFT_ENTITY_FORMAT, e.id);
					
					DriftDebugSegment(STATE, p, (DriftVec2){p.x + 12*t.a, p.y + 12*t.b}, 1, DRIFT_RGBA8_RED);
					DriftDebugSegment(STATE, p, (DriftVec2){p.x + 12*t.c, p.y + 12*t.d}, 1, DRIFT_RGBA8_GREEN);
				}
				
				uint idx = DriftComponentFind(&STATE->transforms.c, selected_entity);
				if(idx){
					nk_layout_row_dynamic(NK, spinner_height, 3);
					nk_property_float(NK, "a", -INFINITY, &STATE->transforms.matrix[idx].a, INFINITY, 1e-2f, 1e-2f);
					nk_property_float(NK, "c", -INFINITY, &STATE->transforms.matrix[idx].c, INFINITY, 1e-2f, 1e-2f);
					nk_property_float(NK, "x", -INFINITY, &STATE->transforms.matrix[idx].x, INFINITY, 1.0f, 1.0f);
					
					nk_property_float(NK, "b", -INFINITY, &STATE->transforms.matrix[idx].b, INFINITY, 1e-2f, 1e-2f);
					nk_property_float(NK, "d", -INFINITY, &STATE->transforms.matrix[idx].d, INFINITY, 1e-2f, 1e-2f);
					nk_property_float(NK, "y", -INFINITY, &STATE->transforms.matrix[idx].y, INFINITY, 1.0f, 1.0f);
				} else {
					nk_label(NK, "No component for entity", NK_TEXT_LEFT);
				}
				
				nk_tree_pop(NK);
			}
			
			if(nk_tree_push(NK, NK_TREE_TAB, "Bodies", NK_MINIMIZED)){
				DRIFT_COMPONENT_FOREACH(&STATE->bodies.c, idx){
					DriftEntity e = STATE->bodies.entity[idx];
					DriftVec2 p = STATE->bodies.position[idx];
					DriftDrawTextF(DRAW, &STATE->debug.sprites, (DriftAffine){1, 0, 0, 1, p.x, p.y - 4}, DRIFT_VEC4_WHITE, " "DRIFT_ENTITY_FORMAT, e.id);
					DriftDebugCircle(STATE, p, STATE->bodies.radius[idx], (DriftRGBA8){0x80, 0x40, 0x00, 0x80});
				}
				
				uint idx = DriftComponentFind(&STATE->bodies.c, selected_entity);
				if(idx){
					nk_layout_row_dynamic(NK, spinner_height, 2);
					nk_property_float(NK, "pos.x", -INFINITY, &STATE->bodies.position[idx].x, INFINITY, 1, 1);
					nk_property_float(NK, "pos.y", -INFINITY, &STATE->bodies.position[idx].y, INFINITY, 1, 1);
					
					nk_property_float(NK, "radius", -INFINITY, &STATE->bodies.radius[idx], INFINITY, 1, 1);
				} else {
					nk_label(NK, "No component for entity", NK_TEXT_LEFT);
				}
				
				nk_tree_pop(NK);
			}
			
			if(nk_tree_push(NK, NK_TREE_TAB, "Power", NK_MINIMIZED)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				nk_labelf(NK, NK_TEXT_LEFT, "nodes: %d, edges: %d", STATE->power_nodes.c.count - 1, (uint)STATE->power_edges.t.row_count);
				
				nk_label(NK, "Flow Maps:", NK_TEXT_LEFT);
				static uint flow_idx = 0;
				for(uint i = 0; i < DRIFT_FLOW_MAP_COUNT; i++){
					if(nk_radio_label(NK, STATE->flow_maps[i].c.table.desc.name, (nk_bool[]){flow_idx == i})) flow_idx = i;
				}
				
				DriftComponentFlowMap* fmap = STATE->flow_maps + flow_idx;
				nk_layout_row_static(NK, 1.5f*UI_LINE_HEIGHT, 80, 1);
				if(nk_button_label(NK, "Reset")){
					DRIFT_COMPONENT_FOREACH(&fmap->c, idx) fmap->flow[idx] = (DriftFlowNode){.dist = INFINITY};
				}
				
				uint selected_idx = 0;
				DRIFT_COMPONENT_FOREACH(&STATE->power_nodes.c, idx){
					uint flow_idx = DriftComponentFind(&fmap->c, STATE->power_nodes.entity[idx]);
					DriftVec2 p0 = STATE->power_nodes.position[idx];
					DriftAffine m = {1, 0, 0, 1, p0.x + 10, p0.y};
					DriftDrawTextF(DRAW, &STATE->debug.sprites, m, DRIFT_VEC4_WHITE, "%d\n%f", UPDATE->tick - fmap->flow[idx].mark, fmap->flow[idx].dist);
					
					uint next_idx = DriftComponentFind(&STATE->power_nodes.c, fmap->flow[flow_idx].next);
					if(next_idx){
						DriftVec2 p1 = STATE->power_nodes.position[next_idx];
						DriftDebugSegment(STATE, p0, DriftVec2Lerp(p0, p1, 0.25f), 2, DRIFT_RGBA8_WHITE);
					}
					
					if(DriftVec2Distance(MOUSE_POS, p0) < 16) selected_idx = idx;
				}
				
				for(uint i = 0; i < STATE->power_edges.t.row_count; i++){
					DriftVec2 p = DriftVec2Lerp(STATE->power_edges.edge[i].p0, STATE->power_edges.edge[i].p1, 0.5f);
					DriftDrawTextF(DRAW, &STATE->debug.sprites, (DriftAffine){1, 0, 0, 1, p.x, p.y}, DRIFT_VEC4_WHITE, "%d", i);
				}
				nk_tree_pop(NK);
			}
		} nk_end(NK);
		
		CTX->debug.ui->show_info = !nk_window_is_hidden(NK, "Info");
	}
	
	if(CTX->debug.ui->show_examples) DriftNuklearOverview(NK);
}
