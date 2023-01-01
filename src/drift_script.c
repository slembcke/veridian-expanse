#include <stdio.h>
#include <string.h>

#include "drift_game.h"

// Place node here: (DriftVec2){1219.864136f, -6101.666016f}

static void DriftScriptYield(DriftScript* script){tina_yield(script->coro, 0);}

#define DRIFT_SCRIPT_WHILE_YIELD(_script_, _cond_) \
	while(!(_script_)->debug_skip && (_cond_)) tina_yield((_script_)->coro, 0);

static void DriftScriptWaitSeconds(DriftScript* script, double timeout){
	while(!script->debug_skip && timeout > 0){
		DriftScriptYield(script);
		timeout -= script->update->dt;
	}
}

static void DriftScriptMiniPause(DriftScript* script){DriftScriptWaitSeconds(script, 1);}

static void DriftScriptWaitAccept(DriftScript* script, float timeout){
	DriftScriptMiniPause(script);
	
	DriftPlayerInput* input = &script->update->ctx->input.player;
	while(!script->debug_skip && timeout > 0){
		if(DriftInputButtonState(input, DRIFT_INPUT_ACCEPT)) break;
		DriftScriptYield(script);
		timeout -= script->update->dt;
	}
	
	// Wait until it's released;
	DRIFT_SCRIPT_WHILE_YIELD(script, DriftInputButtonState(input, DRIFT_INPUT_ACCEPT));
}

static void DriftScriptBlip(DriftScript* script){
	if(!script->debug_skip) DriftAudioPlaySample(script->update->audio, DRIFT_SFX_TEXT_BLIP, 1, 0, 1, false);
}

#define TEXT_ACCEPT_PROMPT "{#808080FF}Press {@ACCEPT} to continue...\n"
#define HIGHLIGHT "{#8080FFFF}"

#define MAX_TASKS 4

typedef struct {
	const char* message;
	float fade_in;
	
	const char** list_header;
	uint list_idx;
	
	bool task[MAX_TASKS];
	const char* task_text[MAX_TASKS];
	
	const char* indicator_label;
	DriftVec2 indicator_location;
	
	bool has_moved, has_looked;
} TutorialContext;

static DriftGameContext* get_ctx(DriftScript* script){return script->update->ctx;}
static DriftGameState* get_state(DriftScript* script){return script->update->state;}

static DriftPlayerData* get_player(DriftScript* script){
	DriftGameState* state = get_state(script);
	uint idx = DriftComponentFind(&get_state(script)->players.c, get_ctx(script)->player);
	DRIFT_ASSERT_WARN(idx, "No player?");
	return get_state(script)->players.data + idx;
}

static void draw_shared(DriftDraw* draw, DriftScript* script){
	TutorialContext* tut = script->script_ctx;
	draw->screen_tint = DriftVec4Mul(DRIFT_VEC4_WHITE, tut->fade_in);
	
	if(tut->message){
		DriftAABB2 bounds = DriftDrawTextBounds(tut->message, 0, draw->input_icons);
		DriftVec2 center = DriftAABB2Center(bounds), extents = DriftAABB2Extents(bounds);
		DriftAffine message_transform = {1, 0, 0, 1, draw->internal_extent.x/2 - center.x, 0.2f*draw->internal_extent.y - center.y};
		
		float padding = 2;
		DriftAffine panel_transform = {2*(extents.x + padding), 0, 0, 2*(extents.y + padding), bounds.l - padding, bounds.b - padding};
		DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){
			.color = {0x00, 0x00, 0x00, 0xC0},
			.matrix = DriftAffineMul(message_transform, panel_transform),
		}));
		
		DriftDrawText(draw, &draw->hud_sprites, message_transform, DRIFT_VEC4_WHITE, tut->message);
	}
	
	if(tut->indicator_label){
		const float scale = 1;
		
		DriftVec2 pos = tut->indicator_location;
		float r = 10 + 10*fabsf(DriftWaveComplex(draw->nanos, 1).x);
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = pos, .p1 = pos, .radii = {r, r - scale}, .color = DRIFT_RGBA8_GREEN}));
		
		DriftVec2 screen_pos = DriftAffineOrigin(draw->vp_inverse);
		DriftVec2 delta = DriftVec2Sub(pos, screen_pos);
		DriftVec2 chevron_pos, dir = DriftVec2Normalize(delta);
		if(DriftVec2Length(delta) > 128){
			chevron_pos = DriftVec2FMA(screen_pos, dir, 128 - r);
		} else {
			// Flip it to the other side when it's close.
			dir = DriftVec2Neg(dir);
			chevron_pos = DriftVec2FMA(pos, dir, -r - 12);
		}
		DriftAffine m = {scale, 0, 0, scale, chevron_pos.x, chevron_pos.y};
		
		DriftAffine m_chev = DriftAffineMul(m, (DriftAffine){dir.x, dir.y, -dir.y, dir.x, 0, 0});
		DriftVec2 p[] = {
			DriftAffinePoint(m_chev, (DriftVec2){0, -4}),
			DriftAffinePoint(m_chev, (DriftVec2){2,  0}),
			DriftAffinePoint(m_chev, (DriftVec2){0,  4}),
		};
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[0], .p1 = p[1], .radii = {scale}, .color = DRIFT_RGBA8_GREEN}));
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[1], .p1 = p[2], .radii = {scale}, .color = DRIFT_RGBA8_GREEN}));
		
		const char* text = tut->indicator_label;
		DriftAABB2 text_bounds = DriftDrawTextBounds(text, 0, draw->input_icons);
		DriftVec2 text_center = DriftAABB2Center(text_bounds), text_extents = DriftAABB2Extents(text_bounds);
		DriftVec2 text_offset = DriftVec2FMA(text_center, dir, fminf((text_extents.x + 2)/fabsf(dir.x), (text_extents.y + 3)/fabsf(dir.y)));
		DriftDrawText(draw, &draw->overlay_sprites, DriftAffineMul(m, (DriftAffine){1, 0, 0, 1, -text_offset.x, -text_offset.y}), DRIFT_VEC4_GREEN , text);
	}
}

static void draw_press_to_begin(DriftDraw* draw, DriftScript* script){
	TutorialContext* tut = script->script_ctx;
	draw_shared(draw, script);
	
	const char* text = "Press {@ACCEPT} to wake up";
	DriftAffine m = {1, 0, 0, 1, draw->internal_extent.x/2 - DriftDrawTextSize(text, 0, draw->input_icons).x/2, draw->internal_extent.y/2 - 32};
	DriftDrawText(draw, &draw->hud_sprites, m, DRIFT_VEC4_WHITE, text);
}

static void draw_checklist(DriftDraw* draw, DriftScript* script){
	TutorialContext* tut = script->script_ctx;
	draw_shared(draw, script);
	
	static float pane_height = 0;
	DriftVec2 extents = draw->internal_extent;
	
	DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){
		.frame = {}, .color = {0x00, 0x00, 0x00, 0xC0},
		.matrix = {-125, 0, 0, pane_height - extents.y + 15, extents.x - 10, extents.y - 10},
	}));
	
	DriftAffine t = {1, 0, 0, 1, extents.x - 130, extents.y - 20};
	t = DriftDrawText(draw, &draw->hud_sprites, t, DRIFT_VEC4_ORANGE, "Emergency Checklist:\n");
	
	// Draw completed lists in green.
	for(uint i = 0; i < tut->list_idx; i++){
		t = DriftDrawTextF(draw, &draw->hud_sprites, t, (DriftVec4){{0.0f, 0.3f, 0.0f, 0.3f}}, "- %s\n", tut->list_header[i]);
		t.y -= 2;
	}
	
	// Draw current list.
	t = DriftDrawTextF(draw, &draw->hud_sprites, t, DRIFT_VEC4_WHITE, "- %s\n", tut->list_header[tut->list_idx]);
	t.y -= 2;
	
	// Draw current list tasks.
	for(uint i = 0; i < MAX_TASKS; i++){
		const char* text = tut->task_text[i];
		if(!text) break;
		
		const char* check = tut->task[i] ? DRIFT_TEXT_GREEN"o" : DRIFT_TEXT_RED"x";
		t = DriftDrawTextF(draw, &draw->hud_sprites, t, (DriftVec4){{0.7f, 0.7f, 0.7f, 1.0}}, " %s"DRIFT_TEXT_WHITE" %s\n", check, tut->task_text[i]);
		t.y -= 2;
	}
	
	// Draw remaining lists in red.
	for(uint i = tut->list_idx + 1; tut->list_header[i]; i++){
		t = DriftDrawTextF(draw, &draw->hud_sprites, t, (DriftVec4){{0.3f, 0.0f, 0.0f, 0.3f}}, "- %s\n", tut->list_header[i]);
		t.y -= 2;
	}
	
	pane_height = t.y;
}

static void checklist_start(DriftScript* script, uint list_idx, ...){
	script->draw = draw_checklist;
	
	TutorialContext* tut = script->script_ctx;
	tut->list_idx = list_idx;
	memset(tut->task, 0, sizeof(tut->task));
	memset(tut->task_text, 0, sizeof(tut->task_text));
	
	va_list args;
	va_start(args, list_idx);
	for(uint i = 0; i < MAX_TASKS; i++){
		const char* arg = va_arg(args, const char*);
		if(arg) tut->task_text[i] = arg; else break;
	}
	va_end(args);
	
	DriftScriptBlip(script);
	DriftScriptMiniPause(script);
}

static bool checklist_wait(DriftScript* script){
	if(script->debug_skip) return false;
	DriftScriptYield(script);
	
	TutorialContext* tut = script->script_ctx;
	for(uint i = 0; i < MAX_TASKS; i++){
		if(!tut->task_text[i]) break;
		if(!tut->task[i]) return true;
	}
	
	tut->indicator_label = NULL;
	DriftScriptWaitSeconds(script, 1);
	return false;
}

static DriftVec2 highlight_nearest_pnode(DriftScript* script){
	DriftGameState* state = get_state(script);
	DriftVec2 player_pos = state->bodies.position[DriftComponentFind(&state->bodies.c, get_ctx(script)->player)];
	
	float nearest_dist = INFINITY;
	DriftVec2 nearest_pos = DRIFT_VEC2_ZERO;
	
	DRIFT_COMPONENT_FOREACH(&state->power_nodes.c, node_idx){
		if(!DriftComponentFind(&state->items.c, state->power_nodes.entity[node_idx])) continue;
		
		DriftVec2 pos = state->power_nodes.position[node_idx];
		float dist = DriftVec2Distance(pos, player_pos);
		if(dist < nearest_dist){
			nearest_dist = dist;
			nearest_pos = pos;
		}
	}
	
	return nearest_pos;
}

static DriftVec2 nearest_thing(DriftGameState* state, DriftVec2 player_pos, DriftComponent* component, uint* type_arr, uint type){
	float nearest_dist = INFINITY;
	DriftVec2 nearest_pos = DRIFT_VEC2_ZERO;
	
	uint comp_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = component, .variable = &comp_idx},
		{.component = &state->bodies.c, .variable = &body_idx},
		{}
	});
	
	while(DriftJoinNext(&join)){
		if(type_arr[comp_idx] != type) continue;
		
		DriftVec2 pos = state->bodies.position[body_idx];
		float dist = DriftVec2Distance(pos, player_pos);
		if(dist < nearest_dist){
			nearest_dist = dist;
			nearest_pos = pos;
		}
	}
	
	return nearest_pos;
}

static uint get_item_count(DriftScript* script, DriftItemType type){
	DriftCargoSlot* slot = DriftPlayerGetCargoSlot(get_player(script), type);
	return get_state(script)->inventory[type] + (slot ? slot->count : 0);
}

void* DriftTutorialScript(tina* coro, void* value){
	DriftScript* script = coro->user_data;
	char buffer[1024];
	
	TutorialContext tut = {
		.list_header = (const char*[]){
			"Find Power",
			"Gather Nodes",
			"Activate Factory",
			"Recover Data",
			"Gather Resources",
			"Craft Tools",
			NULL,
		},
	};
	script->script_ctx = &tut;
	
	// script->debug_skip = true;
	
	// while(get_ctx(script)->player.id == 0) DriftScriptYield(script);
	DriftPlayerInput* input = &get_ctx(script)->input.player;
	DriftCargoSlot* slot = NULL;
	
	// Reset status values
	tut.fade_in = 0.25f;
	get_state(script)->status.enable_controls = false;
	get_state(script)->status.show_hud = false;
	get_state(script)->status.scan_restrict = DRIFT_SCAN_FACTORY;
	get_player(script)->energy = 0;
	// for(uint i = 0; i < _DRIFT_SCAN_COUNT; i++) get_state(script)->scan_progress[i] = 0;
	
	// Tutorial starts here
	script->draw = draw_press_to_begin;
	DriftScriptBlip(script);
	DriftScriptWaitAccept(script, INFINITY);
	script->draw = draw_shared;
	
	uint t0 = script->update->tick;
	while(tut.fade_in < 1 && !script->debug_skip){
		DriftScriptYield(script);
		tut.fade_in = DriftLerpConst(tut.fade_in, 1, script->update->dt/2);
	}
	tut.fade_in = 1;
	
	tut.message =
		"Pod 9, seismic activity has been detected in your\n"
		"vicinity. Are you injured? Pod 9 please respond.\n"
		TEXT_ACCEPT_PROMPT;
	DriftScriptBlip(script);
	DriftScriptWaitAccept(script, INFINITY);
	
	get_state(script)->status.enable_controls = true;
	
	tut.message = 
		"This is fortuitous. Any injury would have delayed\n"
		"the repair schedule significantly.\n"
		TEXT_ACCEPT_PROMPT;
	DriftScriptBlip(script);
	DriftScriptWaitAccept(script, 8);
	
	tut.message =
		"Pod 9, head south to the power network\n"
		"to recharge your energy reserves.";
	DriftScriptBlip(script);
	
	checklist_start(script, 0,
		"{@MOVE} to Move",
		"{@LOOK} to Look",
		"Find power",
		NULL
	);
	
	while(checklist_wait(script)){
		tut.task[0] |= DriftVec2Length(DriftInputJoystick(input, 0, 1)) > 0.1f;
		tut.task[1] |= DriftVec2Length(DriftInputJoystick(input, 2, 3)) > 0.1f;
		tut.task[1] |= DriftVec2Length(get_ctx(script)->input.mouse_rel) > 0;
		tut.task[2] |= get_player(script)->is_powered;
		
		// Show the HUD after the player has moved.
		get_state(script)->status.show_hud = tut.task[0] && tut.task[1];
	}
	
	if(script->debug_skip){
		DriftGameState* state = get_state(script);
		state->status.show_hud = true;
		state->bodies.position[DriftComponentFind(&state->bodies.c, get_ctx(script)->player)] = (DriftVec2){1339.319336f, -5952.195801f};
	}
	
	tut.message =
		"Your work site has incurred minor damage. You'll need to\n"
		"reconnect the "HIGHLIGHT"fabricator"DRIFT_TEXT_WHITE" to power before you can proceed.\n"
		TEXT_ACCEPT_PROMPT;
	DriftScriptBlip(script);
	DriftScriptWaitAccept(script, 8);
	
	checklist_start(script, 1,
		"Equip Gripper",
		"Grab Power Node",
		"Stow Power Node",
		"Collect 0/5", // TODO should be inactive power nodes
		NULL
	);
	do {
		DriftMem* mem = DriftLinearMemInit(buffer, sizeof(buffer), "tutorial mem");
		DriftPlayerData* player = get_player(script);
		uint count = get_item_count(script, DRIFT_ITEM_POWER_NODE);
		
		tut.task[0] = player->tool_idx == DRIFT_TOOL_GRAB;
		tut.task[1] = count > 0 || player->grabbed_type == DRIFT_ITEM_POWER_NODE;
		tut.task[2] = count > 0;
		tut.task[3] = count >= 5;
		tut.task_text[3] = DriftSMPrintf(mem, "Collect %d/5", count);
		
		if(!tut.task[0]){
			tut.message = "Press {@GRAB} to equip\nyour "HIGHLIGHT"gripper";
			tut.indicator_label = NULL;
		} else if(player->grabbed_type == DRIFT_ITEM_POWER_NODE){
			tut.message = "Use {@LOOK} to pull it into\nyour pod and "HIGHLIGHT"stash it";
			tut.indicator_label = NULL;
		} else {
			tut.message = NULL;
			tut.indicator_label = "GRAB {@GRAB}";
			tut.indicator_location = highlight_nearest_pnode(script);
		}
	} while(checklist_wait(script));
	tut.indicator_label = NULL;

	tut.message =
		"Those power nodes should be sufficient to repair\n"
		"the network. Now we must assess your "HIGHLIGHT"fabricator.\n";
	DriftScriptBlip(script);
	
	checklist_start(script, 2,
		"Connect Factory\n   to Power",
		"Scan the Factory",
		"Activate Factory\n   Interface",
		NULL
	);
	while(checklist_wait(script)){
		DriftGameState* state = get_state(script);
		uint factory_node_idx = DriftComponentFind(&state->power_nodes.c, state->status.factory_node);
		tut.task[0] = state->power_nodes.active[factory_node_idx];
		tut.task[1] = state->scan_progress[DRIFT_SCAN_FACTORY] == 1;
		tut.task[2] = state->status.factory_rebooted;
		
		DriftPlayerData* player = get_player(script);
		if(!tut.task[0]){
			tut.indicator_location = (DriftVec2){1191.642578f, -6071.555664f};
			tut.indicator_label = "PLACE NODE\n(Hold {@DROP})";
		} else if(!tut.task[1]){
			tut.message = NULL;
			tut.indicator_location = DRIFT_FACTORY_POSITION;
			tut.indicator_label = (player->tool_idx == DRIFT_TOOL_SCAN ? "SCAN" : "SCAN {@SCAN}");
		} else if(player->tool_idx != DRIFT_TOOL_SCAN || player->scanned_entity.id != state->status.factory_node.id){
			tut.indicator_location = DRIFT_FACTORY_POSITION;
			tut.indicator_label = "USE {@SCAN}";
		} else {
			tut.indicator_label = NULL;
		}
	}
	tut.indicator_label = NULL;
	if(script->debug_skip) get_state(script)->scan_progress[DRIFT_SCAN_FACTORY] = 1;
	
	// Wait until they close the UI.
	while(get_ctx(script)->ui_state != DRIFT_UI_STATE_NONE) DriftScriptYield(script);
	
	tut.message =
		"How unfortunate. It appears your fabricator's database\n"
		"has been corrupted. It's imperative to recover that data.\n"
		"Equip your "HIGHLIGHT"scanner"DRIFT_TEXT_WHITE" by pressing {@SCAN}.";
	DriftScriptBlip(script);
	
	checklist_start(script, 3,
		"Scan Viridium",
		"Discover Lumium\n   Source",
		"Scan Lumium",
		"Research Lumium\n   Lights"
	);
	while(checklist_wait(script)){
		DriftGameState* state = get_state(script);
		DriftPlayerData* player = get_player(script);
			
		tut.task[0] = state->scan_progress[DRIFT_SCAN_VIRIDIUM] == 1;
		tut.task[1] = state->scan_progress[DRIFT_SCAN_GLOW_BUG] == 1;
		tut.task[2] = state->scan_progress[DRIFT_SCAN_LUMIUM] == 1;
		tut.task[3] = state->scan_progress[DRIFT_SCAN_HEADLIGHT] == 1;
		
		if(player->tool_idx == DRIFT_TOOL_SCAN) tut.message = NULL;
		DriftVec2 player_pos = state->bodies.position[DriftComponentFind(&state->bodies.c, get_ctx(script)->player)];
		if(!tut.task[0]){
			state->status.scan_restrict = DRIFT_SCAN_VIRIDIUM;
			tut.indicator_location = nearest_thing(state, player_pos, &state->items.c, state->items.type, DRIFT_ITEM_VIRIDIUM);
			tut.indicator_label = (player->tool_idx == DRIFT_TOOL_SCAN ? "SCAN" : "SCAN {@SCAN}");
		} else if(!tut.task[1]){
			state->status.scan_restrict = DRIFT_SCAN_GLOW_BUG;
			tut.indicator_location = nearest_thing(state, player_pos, &state->enemies.c, state->enemies.type, DRIFT_ENEMY_GLOW_BUG);
			tut.indicator_label = (player->tool_idx == DRIFT_TOOL_SCAN ? "SCAN" : "SCAN {@SCAN}");
		} else if(!tut.task[2]){
			tut.message =
				"It appears "HIGHLIGHT"lumium"DRIFT_TEXT_WHITE" can be obtained by destroying "HIGHLIGHT"glow bugs\n"DRIFT_TEXT_WHITE
				"keep scanning other creatures to discover more resources.";
			
			state->status.scan_restrict = DRIFT_SCAN_LUMIUM;
			tut.indicator_location = nearest_thing(state, player_pos, &state->items.c, state->items.type, DRIFT_ITEM_LUMIUM);
			tut.indicator_label = (player->tool_idx == DRIFT_TOOL_SCAN ? "SCAN" : "SCAN {@SCAN}");
			
			if(tut.indicator_location.x == 0){
				tut.indicator_location = nearest_thing(state, player_pos, &state->enemies.c, state->enemies.type, DRIFT_ENEMY_GLOW_BUG);
				tut.indicator_label = "SHOOT {@FIRE}";
			}
		} else if(player->scanned_entity.id != state->status.factory_node.id){
			state->status.scan_restrict = DRIFT_SCAN_FACTORY;
			tut.indicator_location = DRIFT_FACTORY_POSITION;
			tut.indicator_label = "USE {@SCAN}";
		} else {
			tut.indicator_label = NULL;
		}
	}
	tut.indicator_label = NULL;
	get_state(script)->status.scan_restrict = DRIFT_SCAN_NONE;
	
	tut.message =
		"In order to recover the rest of the blueprints, keep\n"
		"an eye out for more things to scan. Next we must\n"
		HIGHLIGHT"gather resources"DRIFT_TEXT_WHITE" to test the fabricators function.\n"
		TEXT_ACCEPT_PROMPT;
	DriftScriptBlip(script);
	DriftScriptWaitAccept(script, 10);
	tut.message = NULL;
	
	checklist_start(script, 4,
		"Viridium 0/5",
		"Lumium 0/5",
		NULL
	);
	do {
		DriftMem* mem = DriftLinearMemInit(buffer, sizeof(buffer), "tutorial mem");
		DriftPlayerData* player = get_player(script);
		
		{ // Update Viridium task
			uint count = get_item_count(script, DRIFT_ITEM_VIRIDIUM);
			tut.task_text[0] = DriftSMPrintf(mem, "Viridium %d/10", count);
			tut.task[0] = count >= 10;
		}{ // Update Lumium task
			uint count = get_item_count(script, DRIFT_ITEM_LUMIUM);
			tut.task_text[1] = DriftSMPrintf(mem, "Lumium %d/5", count);
			tut.task[1] = count >= 5;
		}
		
		if(player->energy > 0){
			DriftGameState* state = get_state(script);
			DriftVec2 player_pos = state->bodies.position[DriftComponentFind(&state->bodies.c, get_ctx(script)->player)];
			
			if(!tut.task[0]){
				tut.indicator_label = "PICK UP {@GRAB}";
				tut.indicator_location = nearest_thing(state, player_pos, &state->items.c, state->items.type, DRIFT_ITEM_VIRIDIUM);
			} else if(!tut.task[1]){
				DriftVec2 nearest_item = nearest_thing(state, player_pos, &state->items.c, state->items.type, DRIFT_ITEM_LUMIUM);
				
				if(DriftVec2Distance(player_pos, nearest_item) < 500){
					tut.indicator_label = "PICK UP {@GRAB}";
					tut.indicator_location = nearest_item;
				} else if(player->grabbed_type != DRIFT_ITEM_LUMIUM){
					tut.indicator_label = "SHOOT {@FIRE}";
					tut.indicator_location = nearest_thing(state, player_pos, &state->enemies.c, state->enemies.type, DRIFT_ENEMY_GLOW_BUG);
				} else {
					tut.indicator_label = NULL;
				}
			} else if(!tut.task[2]){
				DriftVec2 nearest_item = nearest_thing(state, player_pos, &state->items.c, state->items.type, DRIFT_ITEM_SCRAP);
				
				if(DriftVec2Distance(player_pos, nearest_item) < 500){
					tut.indicator_label = "PICK UP {@GRAB}";
					tut.indicator_location = nearest_item;
				} else if(player->grabbed_type != DRIFT_ITEM_SCRAP){
					tut.indicator_label = "SHOOT {@FIRE}";
					tut.indicator_location = nearest_thing(state, player_pos, &state->enemies.c, state->enemies.type, DRIFT_ENEMY_WORKER_BUG);
				} else {
					tut.indicator_label = NULL;
				}
			}
		} else {
			tut.indicator_label = NULL;
		}
	} while(checklist_wait(script));

	checklist_start(script, 5,
		"Craft Lumium\n   Lights",
		"Craft Autocannon",
		"Craft Mining\n   Laser",
		// TODO make them construct something
		NULL
	);
	while(checklist_wait(script)){
		DriftGameState* state = get_state(script);
		tut.task[0] = state->inventory[DRIFT_ITEM_HEADLIGHT];
		tut.task[1] = state->inventory[DRIFT_ITEM_AUTOCANNON];
		tut.task[2] = state->inventory[DRIFT_ITEM_MINING_LASER];
	}
	
	DriftScriptBlip(script);
	tut.message = "END OF CURRENT TUTORIAL";
	DriftScriptWaitSeconds(script, 30);
	
	script->debug_skip = false;
	script->draw = NULL;
	return 0;
}
