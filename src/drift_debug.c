/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <inttypes.h>
#include <string.h>

#include <SDL.h>

#include "drift_game.h"
#include "base/drift_nuklear.h"

DriftVec4 TMP_COLOR[4];
float TMP_VALUE[4];

static const float UI_LINE_HEIGHT = 12;

static DriftUpdate* UPDATE;
static DriftDraw* DRAW;
static DriftGameState* STATE;
static DriftGameContext* CTX;
static struct nk_context* NK;

static bool ANY_HOVERED;
static DriftVec2 MOUSE_POS, MOUSE_REL;
static bool MOUSE_DOWN;
static bool MOUSE_STATE;

static tina_group PAINT_PROGRESS_GROUP;

typedef struct UndoState {
		DriftRGBA8 biome[DRIFT_TERRAIN_TILEMAP_SIZE_SQ];
		DriftTerrainDensity density[DRIFT_TERRAIN_TILEMAP_SIZE_SQ];
} UndoState;

#define UNDO_MAX 10
static uint UNDO_CURSOR, UNDO_HEAD, UNDO_TAIL;
UndoState UNDO_QUEUE[UNDO_MAX];

static void wait_for_exit(tina_job* job){
	DriftGameContext* ctx = tina_job_get_description(job)->user_data;
	tina_job_wait(job, &PAINT_PROGRESS_GROUP, 0);
	ctx->debug.pause = false;
	ctx->debug.draw_terrain_sdf = false;
	ctx->debug.disable_haze = false;
}

static void SetPaintMode(bool value){
	CTX->debug.paint = value;
	CTX->debug.boost_ambient = value;
	
	if(value){
		CTX->debug.pause = true;
		CTX->debug.draw_terrain_sdf = true;
		CTX->debug.disable_haze = true;
		DriftTerrainEditIO(UPDATE->job, STATE->terra, false);
	} else {
		tina_scheduler_enqueue(APP->scheduler, DriftTerrainEditExit, STATE->terra, 0, DRIFT_JOB_QUEUE_WORK, &PAINT_PROGRESS_GROUP);
		tina_scheduler_enqueue(APP->scheduler, wait_for_exit, CTX, 0, DRIFT_JOB_QUEUE_MAIN, NULL);
	}
}

static void* get_density(DriftTerrain* terra){return terra->tilemap.density + DRIFT_TERRAIN_MIP0;}

static void PaintUI(void){
	DriftTerrain* terra = STATE->terra;
	
	static bool NEEDS_CHECKPOINT = true;
	if(NEEDS_CHECKPOINT){
		uint idx = UNDO_CURSOR % UNDO_MAX;
		memcpy(UNDO_QUEUE[idx].density, get_density(terra), sizeof(UNDO_QUEUE->density));
		memcpy(UNDO_QUEUE[idx].biome, terra->tilemap.biome, sizeof(UNDO_QUEUE->biome));
		
		UNDO_HEAD = UNDO_CURSOR;
		if(UNDO_HEAD >= UNDO_TAIL + UNDO_MAX) UNDO_TAIL = UNDO_HEAD - UNDO_MAX + 1;
		NEEDS_CHECKPOINT = false;
	}
	
	static float radius = 256;
	
	typedef enum {PAINT_MODE_DIG, PAINT_MODE_ADD, PAINT_MODE_SUB, PAINT_MODE_PERLIN_ADD, PAINT_MODE_PERLIN_SUB, PAINT_MODE_PERLIN_BLEND, PAINT_MODE_BIOME} PAINT_MODE;
	static PAINT_MODE paint_mode = PAINT_MODE_PERLIN_BLEND;
	static int biome_value;
	
	static DriftTerrainEditPerlinParams perlin_params = {
		.frq = 0.75f, .oct = 2, .exp = 0.5f, .mul = 1, .add = 0.25f,
	};
	
	if(nk_begin(NK, "Paint", nk_rect(8, 125, 200, 500), NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_SCALABLE)){
		nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
		nk_property_float(NK, "Radius", 8, &radius, 256, 1, 1);
		
		float log_radius = log2f(radius);
		nk_slider_float(NK, 3, &log_radius, 12, 1/256.0f);
		radius = powf(2, log_radius);
		
		nk_label(NK, "Mode:", NK_TEXT_ALIGN_LEFT);
		if(nk_option_label(NK, "Dig", paint_mode == PAINT_MODE_DIG)) paint_mode = PAINT_MODE_DIG;
		if(nk_option_label(NK, "Add", paint_mode == PAINT_MODE_ADD)) paint_mode = PAINT_MODE_ADD;
		if(nk_option_label(NK, "Sub", paint_mode == PAINT_MODE_SUB)) paint_mode = PAINT_MODE_SUB;
		if(nk_option_label(NK, "Perlin Add", paint_mode == PAINT_MODE_PERLIN_ADD)) paint_mode = PAINT_MODE_PERLIN_ADD;
		if(nk_option_label(NK, "Perlin Sub", paint_mode == PAINT_MODE_PERLIN_SUB)) paint_mode = PAINT_MODE_PERLIN_SUB;
		if(nk_option_label(NK, "Perlin Blend", paint_mode == PAINT_MODE_PERLIN_BLEND)) paint_mode = PAINT_MODE_PERLIN_BLEND;
		
		nk_property_float(NK, "Frq:", 0.05f, &perlin_params.frq, 4, 0.01f, 0.01f);
		nk_property_float(NK, "Oct:", 1, &perlin_params.oct, 4, 1, 1);
		nk_property_float(NK, "Exp:", 0.125, &perlin_params.exp, 1, 0.01f, 0.01f);
		nk_property_float(NK, "Mul:", -1, &perlin_params.mul, 1, 1, 1);
		nk_property_float(NK, "Add:", 0, &perlin_params.add, 1, 0.05f, 0.01f);
		
		if(nk_option_label(NK, "Biome", paint_mode == PAINT_MODE_BIOME)) paint_mode = PAINT_MODE_BIOME;
		nk_property_int(NK, "Value:", 0, &biome_value, 4, 1, 0.1f);
		
		nk_label(NK, "Presets:", NK_TEXT_ALIGN_LEFT);
		nk_layout_row_dynamic(NK, 2*UI_LINE_HEIGHT, 2);
		if(nk_button_label(NK, "Wide")){
			paint_mode = PAINT_MODE_PERLIN_BLEND, radius = 500;
			perlin_params = (DriftTerrainEditPerlinParams){.frq = 0.33f, .oct = 3, .exp = 0.5f, .mul = 1, .add = 0.15f};
		}
		if(nk_button_label(NK, "Honeycomb")){
			paint_mode = PAINT_MODE_PERLIN_BLEND, radius = 200;
			perlin_params = (DriftTerrainEditPerlinParams){.frq = 0.33f, .oct = 3, .exp = 0.5f, .mul = -1, .add = 0.05f};
		}
		if(nk_button_label(NK, "Tunnel")){
			paint_mode = PAINT_MODE_PERLIN_SUB, radius = 48;
			perlin_params = (DriftTerrainEditPerlinParams){.frq = 2.0f, .oct = 1, .exp = 0.5f};
		}
		
		nk_layout_row_dynamic(NK, 5, 1);
		nk_layout_row_dynamic(NK, 1.5f*UI_LINE_HEIGHT, 2);
		if(nk_button_label(NK, "Undo") && UNDO_CURSOR > UNDO_TAIL){
			UNDO_CURSOR--;
			uint idx = UNDO_CURSOR % UNDO_MAX;
			memcpy(get_density(terra), UNDO_QUEUE[idx].density, sizeof(UNDO_QUEUE->density));
			memcpy(terra->tilemap.biome, UNDO_QUEUE[idx].biome, sizeof(UNDO_QUEUE->biome));
			DriftTerrainResetCache(terra);
		}
		if(nk_button_label(NK, "Redo") && UNDO_CURSOR < UNDO_HEAD){
			UNDO_CURSOR++;
			uint idx = UNDO_CURSOR % UNDO_MAX;
			memcpy(get_density(terra), UNDO_QUEUE[idx].density, sizeof(UNDO_QUEUE->density));
			memcpy(terra->tilemap.biome, UNDO_QUEUE[idx].biome, sizeof(UNDO_QUEUE->biome));
			DriftTerrainResetCache(terra);
		}
		
		nk_layout_row_dynamic(NK, 5, 1);
		nk_layout_row_dynamic(NK, 1.5f*UI_LINE_HEIGHT, 2);
		if(nk_button_label(NK, "Revert")){
			DriftTerrainEditIO(UPDATE->job, terra, false);
			UNDO_CURSOR = UNDO_HEAD = UNDO_TAIL = 0;
			NEEDS_CHECKPOINT = true;
		}
		if(nk_button_label(NK, "Save")) DriftTerrainEditIO(UPDATE->job, terra, true);
		
		if(nk_button_label(NK, "Reset Cache")) DriftTerrainResetCache(terra);
		if(nk_button_label(NK, "Rectify")) DriftTerrainEditRectify(terra, APP->scheduler, &PAINT_PROGRESS_GROUP);
		
		if(nk_button_label(NK, "Cancel")) SetPaintMode(false);
		if(nk_button_label(NK, "Done")) SetPaintMode(false);
	} nk_end(NK);
	
	if(!ANY_HOVERED) DriftDebugCircle2(STATE, MOUSE_POS, radius, radius - 1, DRIFT_RGBA8_RED);
	
	const u8* keystate = SDL_GetKeyboardState(NULL);
	float radius_adj = expf(2e-2f);
	if(keystate[SDL_SCANCODE_LEFTBRACKET ]) radius /= radius_adj;
	if(keystate[SDL_SCANCODE_RIGHTBRACKET]) radius *= radius_adj;
	if(keystate[SDL_SCANCODE_F1]) paint_mode = PAINT_MODE_ADD;
	if(keystate[SDL_SCANCODE_F2]) paint_mode = PAINT_MODE_SUB;
	if(keystate[SDL_SCANCODE_F3]) paint_mode = PAINT_MODE_PERLIN_ADD;
	if(keystate[SDL_SCANCODE_F4]) paint_mode = PAINT_MODE_PERLIN_SUB;
	if(keystate[SDL_SCANCODE_F5]) paint_mode = PAINT_MODE_PERLIN_BLEND;
	if(keystate[SDL_SCANCODE_F6]) paint_mode = PAINT_MODE_BIOME;
	
	static bool IS_DIRTY = false;
	static DriftVec2 LAST_PAINT_POS = {};
	if(MOUSE_STATE){
		if(keystate[SDL_SCANCODE_LALT]){
			biome_value = DriftTerrainSampleBiome(terra, MOUSE_POS).idx;
		} else if(!DriftVec2Near(MOUSE_POS, LAST_PAINT_POS, radius/10)){
			if(paint_mode == PAINT_MODE_DIG) DriftTerrainDig(terra, MOUSE_POS, radius);
			if(paint_mode == PAINT_MODE_ADD) DriftTerrainEdit(UPDATE, MOUSE_POS, radius, DriftTerrainEditAdd, NULL);
			if(paint_mode == PAINT_MODE_SUB) DriftTerrainEdit(UPDATE, MOUSE_POS, radius, DriftTerrainEditSub, NULL);
			if(paint_mode == PAINT_MODE_PERLIN_ADD) DriftTerrainEdit(UPDATE, MOUSE_POS, radius, DriftTerrainEditPerlinAdd, &perlin_params);
			if(paint_mode == PAINT_MODE_PERLIN_SUB) DriftTerrainEdit(UPDATE, MOUSE_POS, radius, DriftTerrainEditPerlinSub, &perlin_params);
			if(paint_mode == PAINT_MODE_PERLIN_BLEND) DriftTerrainEdit(UPDATE, MOUSE_POS, radius, DriftTerrainEditPerlin, &perlin_params);
			
			static const DriftRGBA8 BIOME[] = {{255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0}, {0, 0, 0, 255}, {}};
			// if(paint_mode == PAINT_MODE_BIOME) 
			DriftBiomeEdit(terra, MOUSE_POS, 1.5f*radius, BIOME[biome_value]);
			
			LAST_PAINT_POS = MOUSE_POS;
			IS_DIRTY = true;
		}
	} else if(IS_DIRTY){
		NEEDS_CHECKPOINT = true;
		IS_DIRTY = false;
		LAST_PAINT_POS = DRIFT_VEC2_ZERO;
		UNDO_CURSOR++;
	}
}

static void get_item_name(void* userdata, int item_type, const char** name){*name = DriftItemName(item_type);}

void DriftDebugUI(DriftUpdate* _update, DriftDraw* _draw){
	UPDATE = _update;
	DRAW = _draw;
	STATE = UPDATE->state;
	CTX = UPDATE->ctx;
	NK = &CTX->debug.ui->nk;

	if(!CTX->debug.show_ui) return;
	
	// Nuklear gets messed up after hotloading and I didn't care to figure out why.
	// It hashes source line numbers, so that probably has something to do with it.
	// Skipping it for a frame resets the UI state, and that seems to fix it.
	if(CTX->debug.reset_ui){
		CTX->debug.reset_ui = false;
		return;
	}
	
	ANY_HOVERED = nk_window_is_any_hovered(NK);
	DriftInput* input = APP->input_context;
	MOUSE_POS = input->mouse_pos_world;
	MOUSE_REL = input->mouse_rel;
	MOUSE_DOWN = input->mouse_down[DRIFT_MOUSE_LEFT] && !ANY_HOVERED;
	
	static bool allow_state;
	// Allow MOUSE_STATE after clicking on the canvas.
	if(MOUSE_DOWN) allow_state = true;
	MOUSE_STATE = input->mouse_state[DRIFT_MOUSE_LEFT] && allow_state;
	// Reset allow state on mouseup.
	if(input->mouse_up[DRIFT_MOUSE_LEFT]) allow_state = false;
	
	const float menu_height = 2*UI_LINE_HEIGHT;
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
	
	if(nk_begin(NK, "MenuBar", nk_rect(0, 0, DRAW->raw_extent.x, menu_height), NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)){
		struct nk_vec2 menu_size = nk_vec2(200, 800);
		
		nk_menubar_begin(NK);{
			nk_layout_row_static(NK, 1.5f*UI_LINE_HEIGHT, 50, 16);
			if(nk_menu_begin_label(NK, "Game", NK_TEXT_LEFT, menu_size)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				
				if(nk_menu_item_label(NK, "Exit", NK_TEXT_LEFT)) APP->request_quit = true;
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
				
				static const char* BIOME_NAMES[_DRIFT_BIOME_COUNT] = {
					[DRIFT_BIOME_LIGHT] = "LIGHT",
					[DRIFT_BIOME_CRYO] = "CRYO",
					[DRIFT_BIOME_RADIO] = "RADIO",
					[DRIFT_BIOME_DARK] = "DARK",
					[DRIFT_BIOME_SPACE] = "SPACE",
				};
				for(uint biome = 0; biome < _DRIFT_BIOME_COUNT; biome++){
					DriftDecalDef* defs = DRIFT_DECAL_DEFS[biome];
					if(nk_tree_push_id(NK, NK_TREE_TAB, BIOME_NAMES[biome], NK_MINIMIZED, biome)){
						for(uint i = 0; defs[i].sprites; i++){
							DriftDecalDef* def = defs + i;
							if(nk_tree_push_id(NK, NK_TREE_TAB, def->label, NK_MINIMIZED, i)){
								nk_property_float(NK, "poisson", 0, &def->poisson, 10, 1e-2f, 1e-2f);
								nk_property_float(NK, "terrain", 0, &def->terrain, 64, 1e-2f, 1e-1f);
								nk_property_float(NK, "weight", 0, &def->weight, 500, 1e-2f, 1e-1f);
								nk_tree_pop(NK);
							}
						}
						nk_tree_pop(NK);
					}
				}
				
				nk_menu_end(NK);
			}
			
			if(nk_menu_begin_label(NK, "Misc", NK_TEXT_LEFT, menu_size)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				
				if(nk_menu_item_label(NK, "Dump nodes", NK_TEXT_LEFT)){
					uint pnode_idx, item_idx, transform_idx;
					DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
						{.component = &STATE->power_nodes.c, .variable = &pnode_idx},
						{.component = &STATE->items.c, .variable = &item_idx},
						{.component = &STATE->transforms.c, .variable = &transform_idx},
						{},
					});
					
					while(DriftJoinNext(&join)) DRIFT_LOGF("{v2:.3}", STATE->power_nodes.position[pnode_idx]);
				}
				
				extern bool DRIFT_DEBUG_SHOW_PATH;
				nk_checkbox_label(NK, "Show Paths", &DRIFT_DEBUG_SHOW_PATH);
				
				if(nk_menu_item_label(NK, "Toggle Examples", NK_TEXT_LEFT)) CTX->debug.ui->show_examples = !CTX->debug.ui->show_examples;
				
				extern uint GUN_TYPE, GUN_LEVEL;
				if(nk_option_label(NK, "Slug", GUN_TYPE == 0)) GUN_TYPE = 0;
				if(nk_option_label(NK, "Plasma", GUN_TYPE == 1)) GUN_TYPE = 1;
				if(nk_option_label(NK, "Proton", GUN_TYPE == 2)) GUN_TYPE = 2;
				if(nk_option_label(NK, "Level 1", GUN_LEVEL == 0)) GUN_LEVEL = 0;
				if(nk_option_label(NK, "Level 2", GUN_LEVEL == 1)) GUN_LEVEL = 1;
				if(nk_option_label(NK, "Level 3", GUN_LEVEL == 2)) GUN_LEVEL = 2;
				
				nk_menu_end(NK);
			}
			
			if(nk_menu_begin_label(NK, "Scans", NK_TEXT_LEFT, menu_size)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				if(nk_menu_item_label(NK, "All Scans", NK_TEXT_LEFT)){
					for(uint i = 0; i < _DRIFT_SCAN_COUNT; i++) STATE->scan_progress[i] = 1;
				}
				if(nk_menu_item_label(NK, "Clear Scans", NK_TEXT_LEFT)){
					for(uint i = 0; i < _DRIFT_SCAN_COUNT; i++) STATE->scan_progress[i] = 0;
				}
				if(nk_menu_item_label(NK, "Almost Clear Scans", NK_TEXT_LEFT)){
					for(uint i = 0; i < _DRIFT_SCAN_COUNT; i++) STATE->scan_progress[i] = 0;
					STATE->scan_progress[DRIFT_SCAN_VIRIDIUM] = 1;
					STATE->scan_progress[DRIFT_SCAN_LUMIUM] = 1;
					STATE->scan_progress[DRIFT_SCAN_SCRAP] = 1;
					STATE->scan_progress[DRIFT_SCAN_CONSTRUCTION_SKIFF] = 1;
				}
				
				if(nk_menu_item_label(NK, "Clear Research", NK_TEXT_LEFT)) STATE->fab.item = DRIFT_ITEM_NONE;
				
				for(DriftScanType i = 1; i < _DRIFT_SCAN_COUNT; i++){
					const char* name = DRIFT_SCANS[i].name;
					if(name){
						bool done = STATE->scan_progress[i] == 1;
						if(nk_checkbox_label(NK, name, &done)) STATE->scan_progress[i] = done;
					}
				}
				nk_menu_end(NK);
			}
			
			if(nk_menu_begin_label(NK, "Inv", NK_TEXT_LEFT, menu_size)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				if(nk_menu_item_label(NK, "Add Nodes", NK_TEXT_LEFT)) STATE->inventory.cargo[DRIFT_ITEM_POWER_NODE] = 100;
				if(nk_menu_item_label(NK, "Clear Cargo", NK_TEXT_LEFT)){
					for(uint i = 0; i < _DRIFT_ITEM_COUNT; i++) STATE->inventory.cargo[i] = 0;
				}
				if(nk_menu_item_label(NK, "Clear Storage", NK_TEXT_LEFT)){
					for(uint i = 0; i < _DRIFT_ITEM_COUNT; i++) STATE->inventory.skiff[i] = 0;
				}
				if(nk_menu_item_label(NK, "Ready to Transfer", NK_TEXT_LEFT)){
					for(uint i = 0; i < _DRIFT_ITEM_COUNT; i++) STATE->inventory.cargo[i] = 0;
					for(uint i = 0; i < _DRIFT_ITEM_COUNT; i++) STATE->inventory.skiff[i] = 0;
					
					STATE->inventory.cargo[DRIFT_ITEM_VIRIDIUM] = 3;
					STATE->inventory.cargo[DRIFT_ITEM_SCRAP] = 4;
					STATE->inventory.cargo[DRIFT_ITEM_LUMIUM] = 5;
					STATE->inventory.skiff[DRIFT_ITEM_POWER_NODE] = 15;
				}
				for(DriftItemType i = 1; i < _DRIFT_ITEM_COUNT; i++){
					const char* name = DriftItemName(i);
					if(!name) continue;
					
					int count = STATE->inventory.skiff[i];
					nk_property_int(NK, name, 0, &count, 1000, 1, 1);
					STATE->inventory.skiff[i] = count;
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
						nk_layout_row_dynamic(NK, 1.5f*UI_LINE_HEIGHT, 3);
						if(nk_button_label(NK, "Vec4")){
							SDL_SetClipboardText(DriftSMPrintf(UPDATE->mem, "(DriftVec4){{%.2ff, %.2ff, %.2ff, %.2ff}}", c.r, c.g, c.b, c.a));
						}
						if(nk_button_label(NK, "RGBA8")){
							DriftRGBA8 c2 = DriftRGBA8FromColor(c);
							SDL_SetClipboardText(DriftSMPrintf(UPDATE->mem, "(DriftRGBA8){0x%02X, 0x%02X, 0x%02X, 0x%02X}", c2.r, c2.g, c2.b, c2.a));
						}
						if(nk_button_label(NK, "Hex")){
							DriftRGBA8 c2 = DriftRGBA8FromColor(c);
							SDL_SetClipboardText(DriftSMPrintf(UPDATE->mem, "%02X%02X%02X%02X", c2.r, c2.g, c2.b, c2.a));
						}
						
						nk_tree_pop(NK);
					}
				}
				
				float step = 1e-4f;
				SDL_Keymod keymod = SDL_GetModState();
				if(keymod & KMOD_SHIFT) step *= 10;
				if(keymod & KMOD_ALT) step *= 100;
				
				// nk_layout_row_dynamic(nk, 1.5f*UI_LINE_HEIGHT, 2);
				nk_layout_row(NK, NK_DYNAMIC, 1.5f*UI_LINE_HEIGHT, 2, (float[]){0.75f, 0.25f});
				for(uint i = 0; i < 4; i++){
					const char* label = DriftSMPrintf(UPDATE->mem, "TMP_VALUE[%d]:", i);
					nk_property_float(NK, label, -INFINITY, TMP_VALUE + i, INFINITY, 5*step, step);
					if(nk_button_label(NK, "Copy")){
						SDL_SetClipboardText(DriftSMPrintf(UPDATE->mem, "%.4ff", TMP_VALUE[i]));
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
				
				nk_label(NK, "Effects Volume:", NK_TEXT_LEFT);
				nk_slider_float(NK, 0, &APP->prefs.effects_volume, 1, 1e-2f);
				
				DriftAudioSetParams(APP->prefs.master_volume, APP->prefs.music_volume, APP->prefs.effects_volume);
				
				nk_label(NK, "Sharpening:", NK_TEXT_LEFT);
				nk_slider_float(NK, 0, &APP->prefs.sharpening, 3, 1e-2f);
				
				nk_label(NK, "Mouse:", NK_TEXT_LEFT);
				nk_slider_float(NK, 0.5f, &APP->prefs.mouse_sensitivity, 3, 1e-2f);
				
				nk_checkbox_label(NK, "Hires", &APP->prefs.hires);
				
				nk_menu_end(NK);
			}
			
			if(did_show_prefs && !show_prefs){
				DriftIOFileWrite(TMP_PREFS_FILENAME, DriftPrefsIO, &APP->prefs);
				DRIFT_LOG("saved '%s'", TMP_PREFS_FILENAME);
			}
			did_show_prefs = show_prefs;
			
			if(nk_menu_begin_label(NK, "Reverb", NK_TEXT_LEFT, menu_size)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				
				static bool override;
				bool dirty = nk_checkbox_label(NK, "Override", &override);
				CTX->reverb.dynamic = !override;
				
				if(override){
					static float dry_db = -2, wet_db = -12, delay = 0.05f, decay = 0.35f, lowpass = 10e-5f;
					
					nk_label(NK, "Dry:", NK_TEXT_LEFT);
					dirty |= nk_slider_float(NK, -20, &dry_db, 0, 1e-2f);
					
					nk_label(NK, "Wet:", NK_TEXT_LEFT);
					dirty |= nk_slider_float(NK, -20, &wet_db, 0, 1e-2f);
					
					nk_label(NK, "Decay:", NK_TEXT_LEFT);
					dirty |= nk_slider_float(NK, 0, &decay, 1, 1e-2f);
					
					nk_label(NK, "Lowpass:", NK_TEXT_LEFT);
					dirty |= nk_slider_float(NK, 0, &lowpass, 1, 1e-2f);
					
					
					if(dirty){
						float dry = DriftDecibelsToGain(dry_db), wet = DriftDecibelsToGain(wet_db - 10);
						DriftAudioSetReverb(dry, wet, decay, lowpass);
						DRIFT_LOG("dry: %.2f, wet: %.4f, decay: %.2f, lowpass: %.2f", dry, wet, decay, lowpass);
					}
				}
				
				nk_layout_row_dynamic(NK, 2*UI_LINE_HEIGHT, 1);
				if(nk_button_label(NK, "beep")){
					DriftAudioPlaySample(DRIFT_BUS_SFX, DRIFT_SFX_PING, (DriftAudioParams){.gain = 1});
				}
				
				nk_menu_end(NK);
			}
			
			nk_checkbox_label(NK, "No HUD", &STATE->status.disable_hud);
			nk_checkbox_label(NK, "Super", &CTX->debug.godmode);
			if(STATE->tutorial && nk_button_label(NK, "Skip")) STATE->tutorial->debug_skip = true;
			if(nk_button_label(NK, "Insp")) CTX->debug.ui->show_inspector ^= true;
			if(nk_button_label(NK, "Paint")) SetPaintMode(true);
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

	
	if(CTX->debug.ui->show_inspector){
		if(nk_begin(NK, "Inspector", nk_rect(0, 32, 300, 400), NK_WINDOW_TITLE | NK_WINDOW_CLOSABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE)){
			nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
			
			nk_label(NK, DriftSMFormat(DRAW->mem, "Mouse pos: {v2: 9.2}", MOUSE_POS), NK_TEXT_LEFT);
			if(MOUSE_DOWN){
				const char* text = DriftSMPrintf(UPDATE->mem, "(DriftVec2){%.1ff, %.1ff}", MOUSE_POS.x, MOUSE_POS.y);
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
			
			if(input->mouse_down[DRIFT_MOUSE_RIGHT] && !ANY_HOVERED){
				selected_entity.id = 0;
				float selected_dist = 32;
				DRIFT_COMPONENT_FOREACH(&STATE->transforms.c, i){
					float dist = DriftVec2Distance(MOUSE_POS, DriftAffineOrigin(STATE->transforms.matrix[i]));
					if(dist < selected_dist){
						selected_dist = dist;
						selected_entity = STATE->transforms.entity[i];
					}
				}
				
				if(selected_entity.id) DriftDestroyEntity(STATE, selected_entity);
			}
			
			nk_labelf(NK, NK_TEXT_LEFT, "Selected: "DRIFT_ENTITY_FORMAT, selected_entity.id);
			
			if(nk_tree_push(NK, NK_TREE_TAB, "Input 0", NK_MINIMIZED)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				DriftPlayerInput* pinput = &input->player;
				
				for(uint i = 0; i < _DRIFT_INPUT_AXIS_COUNT; i++){
					nk_labelf(NK, NK_TEXT_LEFT, "axis[%d]: %5.2f (%5.2f, %d %d)",
						i, pinput->axes[i], pinput->_analog_axis[i], pinput->_axis_pos[i], pinput->_axis_neg[i]
					);
				}
				nk_labelf(NK, NK_TEXT_LEFT, "bstate  : 0x%08"PRIX32, (u32)pinput->bstate);
				nk_labelf(NK, NK_TEXT_LEFT, "bpress  : 0x%08"PRIX32, (u32)pinput->bpress);
				nk_labelf(NK, NK_TEXT_LEFT, "brelease: 0x%08"PRIX32, (u32)pinput->brelease);
				
				nk_tree_pop(NK);
			}
			
			if(nk_tree_push(NK, NK_TREE_TAB, "Memory", NK_MINIMIZED)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				
				DriftZoneHeapInfo info = DriftZoneHeapGetInfo(APP->zone_heap);
				nk_labelf(NK, NK_TEXT_LEFT, "Blocks: %d/%d", info.blocks_used, info.blocks_allocated);
				nk_labelf(NK, NK_TEXT_LEFT, "Zones: %d/%d", info.zones_used, info.zones_allocated);
				
				for(uint i = 0; i < 16; i++){
					const char* name = info.zone_names[i];
					if(name) nk_labelf(NK, NK_TEXT_LEFT, "[%d]: %s", i, name);
				}
				
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
				
				nk_layout_row_dynamic(NK, 1.5f*UI_LINE_HEIGHT, 1);
				if(nk_button_label(NK, "Reset Resources")){
					// Reset items
					DRIFT_COMPONENT_FOREACH(&STATE->items.c, idx){
						if(STATE->items.type[idx] != DRIFT_ITEM_POWER_NODE) DriftDestroyEntity(STATE, STATE->items.entity[idx]);
					}
					
					// Reset enemies
					DRIFT_COMPONENT_FOREACH(&STATE->enemies.c, idx) DriftDestroyEntity(STATE, STATE->enemies.entity[idx]);
					
					// Reset terrain resources
					for(uint i = 0; i < DRIFT_TERRAIN_TILEMAP_SIZE_SQ; i++){
						STATE->terra->tilemap.resources[i] = 3;
						STATE->terra->tilemap.biomass[i] = 1;
					}
				}
				
				nk_tree_pop(NK);
			}
			
			if(nk_tree_push(NK, NK_TREE_TAB, "Transforms", NK_MINIMIZED)){
				DRIFT_COMPONENT_FOREACH(&STATE->transforms.c, idx){
					DriftEntity e = STATE->transforms.entity[idx];
					DriftAffine t = STATE->transforms.matrix[idx];
					DriftVec2 p = DriftAffineOrigin(t);
					if(!DriftAffineVisibility(DRAW->vp_matrix, p, DRIFT_VEC2_ZERO)) continue;
					DriftDrawTextF(DRAW, &STATE->debug.sprites, (DriftVec2){p.x, p.y - 4}, " "DRIFT_ENTITY_FORMAT, e.id);
					
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
					if(!DriftAffineVisibility(DRAW->vp_matrix, p, DRIFT_VEC2_ZERO)) continue;
					DriftDrawTextF(DRAW, &STATE->debug.sprites, (DriftVec2){p.x, p.y - 4}, " "DRIFT_ENTITY_FORMAT, e.id);
					DriftDebugCircle(STATE, p, STATE->bodies.radius[idx], (DriftRGBA8){0x80, 0x40, 0x00, 0x80});
				}
				
				uint idx = DriftComponentFind(&STATE->bodies.c, selected_entity);
				if(idx){
					nk_layout_row_dynamic(NK, spinner_height, 2);
					nk_property_float(NK, "pos.x", -INFINITY, &STATE->bodies.position[idx].x, INFINITY, 1, 1);
					nk_property_float(NK, "pos.y", -INFINITY, &STATE->bodies.position[idx].y, INFINITY, 1, 1);
					nk_property_float(NK, "vel.x", -INFINITY, &STATE->bodies.velocity[idx].x, INFINITY, 1, 1);
					nk_property_float(NK, "vel.y", -INFINITY, &STATE->bodies.velocity[idx].y, INFINITY, 1, 1);
					
					nk_property_float(NK, "radius", 0.1f, &STATE->bodies.radius[idx], INFINITY, 1, 1);
					nk_labelf(NK, NK_TEXT_LEFT, "speed: %f", DriftVec2Length(STATE->bodies.velocity[idx]));
				} else {
					nk_label(NK, "No component for entity", NK_TEXT_LEFT);
				}
				
				nk_labelf(NK, NK_TEXT_LEFT, "bodies: %d", STATE->bodies.c.count);
				nk_tree_pop(NK);
			}
			
			if(nk_tree_push(NK, NK_TREE_TAB, "Nodes", NK_MINIMIZED)){
				nk_layout_row_dynamic(NK, UI_LINE_HEIGHT, 1);
				nk_labelf(NK, NK_TEXT_LEFT, "nodes: %d, edges: %d", STATE->power_nodes.c.count - 1, (uint)STATE->power_edges.t.row_count);
				
				nk_label(NK, "Flow Maps:", NK_TEXT_LEFT);
				static uint flow_idx = 0;
				for(uint i = 0; i < _DRIFT_FLOW_MAP_COUNT; i++){
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
					if(!DriftAffineVisibility(DRAW->vp_matrix, p0, DRIFT_VEC2_ZERO)) continue;
					
					DriftAffine m = {1, 0, 0, 1, p0.x + 10, p0.y};
					DriftDrawTextF(DRAW, &STATE->debug.sprites, (DriftVec2){p0.x + 10, p0.y}, "%sidx:%d\n{#FFFFFFFF}%d:%.1f",
						fmap->is_valid[idx] ? "{#00FF0000}" : "{#FF000000}", STATE->power_nodes.entity[idx].id,
						fmap->stamp - fmap->flow[idx].stamp, fmap->flow[idx].dist
					);
					
					uint next_idx = DriftComponentFind(&STATE->power_nodes.c, fmap->flow[flow_idx].next);
					if(next_idx){
						DriftVec2 p1 = STATE->power_nodes.position[next_idx];
						DriftDebugSegment(STATE, p0, DriftVec2Lerp(p0, p1, 0.25f), 2, DRIFT_RGBA8_WHITE);
					}
					
					if(DriftVec2Distance(MOUSE_POS, p0) < 16) selected_idx = idx;
				}
				
				for(uint i = 0; i < STATE->power_edges.t.row_count; i++){
					DriftVec2 p = DriftVec2Lerp(STATE->power_edges.edge[i].p0, STATE->power_edges.edge[i].p1, 0.5f);
					if(!DriftAffineVisibility(DRAW->vp_matrix, p, DRIFT_VEC2_ZERO)) continue;
					DriftDrawTextF(DRAW, &STATE->debug.sprites, (DriftVec2){p.x, p.y}, "%d", i);
				}
				nk_tree_pop(NK);
			}
			
			if(nk_tree_push(NK, NK_TREE_TAB, "Items", NK_MINIMIZED)){
				nk_layout_row_dynamic(NK, 1.5f*UI_LINE_HEIGHT, 1);
				
				static int item_type = 0;
				nk_combobox_callback(NK, get_item_name, NULL, &item_type, _DRIFT_ITEM_COUNT, (int)UI_LINE_HEIGHT, nk_vec2(200, 300));
				if(MOUSE_DOWN && item_type) DriftItemMake(STATE, item_type, MOUSE_POS, DRIFT_VEC2_ZERO, 0);
				
				if(nk_button_label(NK, "Reset Items")){
					DRIFT_COMPONENT_FOREACH(&STATE->items.c, idx){
						if(STATE->items.type[idx] != DRIFT_ITEM_POWER_NODE) DriftDestroyEntity(STATE, STATE->items.entity[idx]);
					}
				}
				
				nk_tree_pop(NK);
			}
			
			if(nk_tree_push(NK, NK_TREE_TAB, "Enemies", NK_MINIMIZED)){
				nk_layout_row_dynamic(NK, 1.5f*UI_LINE_HEIGHT, 1);
				
				if(nk_button_label(NK, "Reset Enemies")){
					DRIFT_COMPONENT_FOREACH(&STATE->enemies.c, idx) DriftDestroyEntity(STATE, STATE->enemies.entity[idx]);
				}
				
				nk_tree_pop(NK);
			}
			
			if(nk_tree_push(NK, NK_TREE_TAB, "Drones", NK_MINIMIZED)){
				nk_layout_row_dynamic(NK, 1.5f*UI_LINE_HEIGHT, 1);
				if(nk_button_label(NK, "Reset Drones")){
					DRIFT_NYI(); // OOPS need to inventory state here
					DRIFT_COMPONENT_FOREACH(&STATE->drones.c, idx) DriftDestroyEntity(STATE, STATE->drones.entity[idx]);
				}
				
				nk_tree_pop(NK);
			}
		} nk_end(NK);
		
		CTX->debug.ui->show_inspector = !nk_window_is_hidden(NK, "Inspector");
	}
	
	if(CTX->debug.ui->show_examples) DriftNuklearOverview(NK);
}
