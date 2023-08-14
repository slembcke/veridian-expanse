/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <string.h>

#include "drift_game.h"
#include "drift_strings.h"

static void* script_context(tina* coro, void* value){
	DriftScript* script = coro->user_data;
	DriftGameState* state = script->update->state;
	
	state->status.save_lock++;
	script->body(script);
	state->status.save_lock--;
	
	script->run = false;
	return NULL;
}

static bool check_default(DriftScript* script){return !script->_coro->completed;}

DriftScript* DriftScriptNew(DriftScriptFunc script_func, void* ctx){
	DriftScript* script = DriftAlloc(DriftSystemMem, DRIFT_SCRIPT_BUFFER_SIZE);
	*script = (DriftScript){.body = script_func, .ctx = ctx, .run = true};
	script->_coro = tina_init(script->_stack_buffer, DRIFT_SCRIPT_BUFFER_SIZE - sizeof(*script), script_context, script);
	script->check = check_default;
	
	return script;
}

bool DriftScriptTick(DriftScript* script, DriftUpdate* update){
	script->update = update;
	if(script->check(script)){
		tina* coro = script->_coro;
		if(!coro->completed) tina_resume(coro, 0);
		return true;
	} else {
		return false;
	}
}

void DriftScriptDraw(DriftScript* script, DriftDraw* draw){
	if(script->draw) script->draw(draw, script);
}

void DriftScriptFree(DriftScript* script){
	script->run = false;
	script->check = check_default;
	DriftScriptTick(script, NULL);
	DRIFT_ASSERT(script->_coro->completed, "Script failed to exit.");
	DriftDealloc(DriftSystemMem, script, DRIFT_SCRIPT_BUFFER_SIZE);
}

#define DRIFT_SCRIPT_WHILE_YIELD(_script_, _cond_) \
	while(!(_script_)->debug_skip && (_cond_)) tina_yield((_script_)->_coro, 0);

static void DriftScriptWaitSeconds(DriftScript* script, double timeout){
	while(!script->debug_skip && timeout > 0){
		DriftScriptYield(script);
		timeout -= script->update->dt;
	}
}

static void DriftScriptMiniPause(DriftScript* script){DriftScriptWaitSeconds(script, 1);}

#define ACCEPT_TIMEOUT 15

static void DriftScriptWaitAccept(DriftScript* script, float timeout){
	DriftScriptMiniPause(script);
	
	DriftPlayerInput* input = &((DriftInput*)APP->input_context)->player;
	while(!script->debug_skip && timeout > 0){
		if(DriftInputButtonState(DRIFT_INPUT_ACCEPT)) break;
		DriftScriptYield(script);
		timeout -= script->update->dt;
	}
	
	// Wait until it's released;
	DRIFT_SCRIPT_WHILE_YIELD(script, DriftInputButtonState(DRIFT_INPUT_ACCEPT));
}

static void DriftScriptBlip(DriftScript* script){
	if(!script->debug_skip){
		DriftAudioPlaySample(DRIFT_BUS_HUD, DRIFT_SFX_TEXT_BLIP, (DriftAudioParams){.gain = 1});
	}
}

#define MAX_TASKS 4

typedef enum {
	MESSAGE_EIDA,
	MESSAGE_SHIPPY,
	MESSAGE_PLAYER,
	_MESSAGE_COUNT,
} MessageSender;

typedef struct {
	float fade_in;
	
	MessageSender message_sender;
	const char* message;
	const char* _last_message;
	float text_timer, text_rate;
	bool text_active, text_pause;
	bool show_accept;
	
	const char** list_header;
	uint list_idx;
	
	float panel_height;
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
	uint idx = DriftComponentFind(&get_state(script)->players.c, get_state(script)->player);
	DRIFT_ASSERT_WARN(idx, "No player?");
	return get_state(script)->players.data + idx;
}

static uint get_player_body_idx(DriftScript* script){
	return DriftComponentFind(&get_state(script)->bodies.c, get_state(script)->player);
}

static DriftVec2 get_player_pos(DriftScript* script){
	return get_state(script)->bodies.position[get_player_body_idx(script)];
}

static struct {
	uint frame;
	const char* name;
} MESSAGE_SENDER[_MESSAGE_COUNT] = {
	[MESSAGE_EIDA] = {.frame = DRIFT_SPRITE_AI_PORTRAIT, .name = "Eida"},
	[MESSAGE_SHIPPY] = {.frame = DRIFT_SPRITE_WRENCHY_PORTRAIT, .name = "Shippy"},
	[MESSAGE_PLAYER] = {.frame = DRIFT_SPRITE_PLAYER_PORTRAIT, .name = "Pod 9"},
};

static void show_message(DriftScript* script, MessageSender sender, const char* message){
	TutorialContext* tut = script->ctx;
	if(tut->message == message) return;
	
	tut->text_timer = 0;
	tut->text_rate = 1;
	tut->message = message;
	tut->message_sender = sender;
	tut->text_active = true;
}

static void wait_for_accept(DriftScript* script, float timeout){
	if(script->debug_skip) return;
	
	TutorialContext* tut = script->ctx;
	DRIFT_SCRIPT_WHILE_YIELD(script, tut->text_active);
	tut->show_accept = true;
	
	// Wait for press and release with a timeout.
	while(!DriftInputButtonState(DRIFT_INPUT_ACCEPT)){
		if(script->debug_skip) break;
		if(timeout < 0) break;
		DriftScriptYield(script);
		timeout -= script->update->dt;
	}
	DRIFT_SCRIPT_WHILE_YIELD(script, DriftInputButtonState(DRIFT_INPUT_ACCEPT));
	tut->show_accept = false;
}

static void wait_for_message(DriftScript* script, float timeout, MessageSender sender, const char* message){
	show_message(script, sender, message);
	wait_for_accept(script, timeout);
}

static void draw_shared(DriftDraw* draw, DriftScript* script){
	TutorialContext* tut = script->ctx;
	draw->screen_tint = DriftVec4Mul(DRIFT_VEC4_WHITE, tut->fade_in);
	
	if(tut->message != tut->_last_message){
		tut->_last_message = tut->message;
		tut->text_timer = 0;
	}
	
	if(DriftInputButtonPress(DRIFT_INPUT_ACCEPT)) tut->text_rate = 10;
	if(DriftInputButtonRelease(DRIFT_INPUT_ACCEPT)) tut->text_rate = 1;
	
	tut->text_timer += 2*tut->text_rate*draw->dt;
	if(tut->message){
		const char* name = MESSAGE_SENDER[tut->message_sender].name;
		DriftFrame portrait_frame = DRIFT_FRAMES[MESSAGE_SENDER[tut->message_sender].frame];
		DriftVec2 portrait_size = {portrait_frame.bounds.r - portrait_frame.bounds.l + 1, portrait_frame.bounds.t - portrait_frame.bounds.b + 1};
		
		DriftVec2 panel_size = {400, portrait_size.y};
		DriftVec2 panel_origin = {draw->internal_extent.x/2 - panel_size.x/2, 0.15f*draw->internal_extent.y - panel_size.y/2};
		
		DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){
			.frame = portrait_frame, .color = DRIFT_RGBA8_WHITE,
			.matrix = {1, 0, 0, 1, panel_origin.x, panel_origin.y}
		}));
		
		DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_PORTRAIT_CORNER], .color = DRIFT_RGBA8_WHITE,
			.matrix = {1, 0, 0, 1, panel_origin.x, panel_origin.y + 64}
		}));
		
		DriftAffine panel_transform = {panel_size.x - portrait_size.x, 0, 0, panel_size.y, panel_origin.x + portrait_size.x, panel_origin.y};
		DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){.color = DRIFT_PANEL_COLOR, .matrix = panel_transform}));
		
		const char* message = DriftSMPrintf(draw->mem, "{#8080FFFF}%s:"DRIFT_TEXT_WHITE" %s", name, tut->message);
		DriftDrawTextFull(draw, &draw->hud_sprites, message, (DriftTextOptions){
			.tint = DRIFT_VEC4_WHITE, .matrix = {1, 0, 0, 1, panel_origin.x + 64 + 4, panel_origin.y + 64 - 12},
			.glyph_limit = (uint)(60*tut->text_timer + 1), .max_width = (int)(panel_size.x - portrait_size.x - 2),
			.active = &tut->text_active, .pause = &tut->text_pause,
		});
		
		if(tut->text_active && !tut->text_pause){
			static DriftAudioSampler text_sampler;
			DriftImAudioSet(DRIFT_BUS_HUD, DRIFT_SFX_TEXT, &text_sampler, (DriftAudioParams){.gain = 0.25f, .loop = true});
		}
		
		if(tut->show_accept){
			const char* accept_message = "{#808080FF}Press {@ACCEPT} to continue...";
			DriftVec2 accept_pos = {panel_origin.x + panel_size.x - DriftDrawTextSize(accept_message, 0).x - 4, panel_origin.y + 4};
			DriftDrawText(draw, &draw->hud_sprites, accept_pos, accept_message);
		}
	}
	
	if(tut->indicator_label){
		const float scale = 1;
		
		DriftVec2 pos = tut->indicator_location;
		float r = 10 + 10*fabsf(DriftWaveComplex(draw->nanos, 0.5f).x);
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
		DriftAABB2 text_bounds = DriftDrawTextBounds(text, 0);
		DriftVec2 text_center = DriftAABB2Center(text_bounds), text_extents = DriftAABB2Extents(text_bounds);
		DriftVec2 text_offset = DriftVec2FMA(text_center, dir, fminf((text_extents.x + 2)/fabsf(dir.x), (text_extents.y + 3)/fabsf(dir.y)));
		DriftDrawTextFull(draw, &draw->overlay_sprites, text, (DriftTextOptions){
			.tint = DRIFT_VEC4_WHITE, .matrix = DriftAffineMul(m, (DriftAffine){1, 0, 0, 1, -text_offset.x, -text_offset.y}),
		});
	}
}

static void draw_press_to_begin(DriftDraw* draw, DriftScript* script){
	TutorialContext* tut = script->ctx;
	draw_shared(draw, script);
	
	const char* text = "Press {@ACCEPT} to wake up";
	DriftVec2 p = {draw->internal_extent.x/2 - DriftDrawTextSize(text, 0).x/2, draw->internal_extent.y/2 - 32};
	DriftDrawText(draw, &draw->hud_sprites, p, text);
}

static void draw_checklist(DriftDraw* draw, DriftScript* script){
	TutorialContext* tut = script->ctx;
	draw_shared(draw, script);
	
	DriftVec2 extents = draw->internal_extent;
	DriftVec2 panel_size = {130, tut->panel_height};
	DriftVec2 panel_origin = {extents.x - panel_size.x - 10, extents.y - 10};
	
	DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){
		.color = DRIFT_PANEL_COLOR, .matrix = {panel_size.x, 0, 0, -panel_size.y, panel_origin.x, panel_origin.y},
	}));
	
	DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_PORTRAIT_CORNER], .color = DRIFT_RGBA8_WHITE,
		.matrix = {1, 0, 0, 1, panel_origin.x, panel_origin.y},
	}));
	
	DriftTextOptions opts = {
		.tint = DRIFT_VEC4_WHITE, .spacing_adjust = 2, .max_width = 120,
		.matrix = {1, 0, 0, 1, panel_origin.x + 5, panel_origin.y - 11},
	};
	opts.matrix = DriftDrawTextFull(draw, &draw->hud_sprites, DRIFT_TEXT_ORANGE"Emergency Checklist:\n", opts);
	
	// Draw completed lists in green.
	for(uint i = 0; i < tut->list_idx; i++){
		const char* message = DriftSMPrintf(draw->mem, "{#00800080}- %s\n", tut->list_header[i]);
		opts.matrix = DriftDrawTextFull(draw, &draw->hud_sprites, message, opts);
	}
	
	{ // Draw current list.
		const char* message = DriftSMPrintf(draw->mem, "- %s\n", tut->list_header[tut->list_idx]);
		opts.matrix = DriftDrawTextFull(draw, &draw->hud_sprites, message, opts);
	}
	
	// Draw current list tasks.
	float offset = 3*6;
	DriftTextOptions task_opts = opts;
	task_opts.max_width -= offset;
	task_opts.matrix = DriftAffineMul(opts.matrix, (DriftAffine){1, 0, 0, 1, offset, 0});
	for(uint i = 0; i < MAX_TASKS; i++){
		const char* text = tut->task_text[i];
		if(!text) break;
		
		u8 pulse = (u8)(128 + 63*DriftWaveComplex(draw->nanos, 1).x);
		DriftAffine icon_matrix = DriftAffineMul(task_opts.matrix, (DriftAffine){1, 0, 0, 1, -12, 0});
		if(tut->task[i]){
			DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){
				.frame = DRIFT_FRAMES[DRIFT_SPRITE_TEXT_CHECK], .color = DRIFT_RGBA8_GREEN, .matrix = icon_matrix,
			}));
		} else {
			DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){
				.frame = DRIFT_FRAMES[DRIFT_SPRITE_TEXT_X], .color = {pulse, 0x00, 0x00, 0xFF}, .matrix = icon_matrix,
			}));
		}
		
		const char* message = DriftSMPrintf(draw->mem, DRIFT_TEXT_WHITE"%s\n", tut->task_text[i]);
		task_opts.matrix = DriftDrawTextFull(draw, &draw->hud_sprites, message, task_opts);
	}
	opts.matrix = DriftAffineMul(task_opts.matrix, (DriftAffine){1, 0, 0, 1, -offset, 0});
	
	// Draw remaining lists in gray.
	for(uint i = tut->list_idx + 1; tut->list_header[i]; i++){
		const char* message = DriftSMPrintf(draw->mem, "{#60606080}- %s\n", tut->list_header[i]);
		opts.matrix = DriftDrawTextFull(draw, &draw->hud_sprites, message, opts);
	}
	
	tut->panel_height = panel_origin.y - opts.matrix.y - 8;
}

static bool checklist_wait(DriftScript* script, uint list_idx, ...){
	script->draw = draw_checklist;
	
	TutorialContext* tut = script->ctx;
	if(tut->list_idx != list_idx){
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
	}
	
	if(script->debug_skip) return false;
	DriftScriptYield(script);
	
	for(uint i = 0; i < MAX_TASKS; i++){
		if(!tut->task_text[i]) break;
		if(!tut->task[i]) return true;
	}
	
	tut->indicator_label = NULL;
	tut->message = NULL;
	DriftScriptWaitSeconds(script, 1);
	return false;
}

static DriftVec2 reticle_pos(DriftScript* script){
	DriftGameState* state = get_state(script);
	uint transform_idx = DriftComponentFind(&state->transforms.c, get_state(script)->player);
	DriftVec2 player_pos = DriftAffineOrigin(state->transforms.matrix[transform_idx]);
	return DriftVec2Add(player_pos, get_player(script)->reticle);
}

static DriftVec2 highlight_nearest_pnode(DriftScript* script){
	DriftGameState* state = get_state(script);
	DriftVec2 target_pos = reticle_pos(script);
	
	float nearest_dist = INFINITY;
	DriftVec2 nearest_pos = DRIFT_VEC2_ZERO;
	
	DRIFT_COMPONENT_FOREACH(&state->power_nodes.c, node_idx){
		DriftEntity e = state->power_nodes.entity[node_idx];
		// Skip nodes that aren't pickups.
		if(!DriftComponentFind(&state->items.c, e)) continue;
		
		DriftVec2 pos = state->power_nodes.position[node_idx];
		float dist = DriftVec2Distance(pos, target_pos);
		// Prefer inactive nodes.
		if(state->power_nodes.active[node_idx]) dist += 1000;
		if(dist < nearest_dist){
			nearest_dist = dist;
			nearest_pos = pos;
		}
	}
	
	return nearest_pos;
}

static DriftVec2 nearest_power(DriftGameState* state, DriftVec2 search_pos){
	float nearest_dist = INFINITY;
	DriftVec2 nearest_pos = DRIFT_VEC2_ZERO;
	
	DRIFT_COMPONENT_FOREACH(&state->power_nodes.c, node_idx){
		if(!state->power_nodes.active[node_idx]) continue;
		
		DriftVec2 pos = state->power_nodes.position[node_idx];
		float dist = DriftVec2Distance(pos, search_pos);
		if(dist < nearest_dist){
			nearest_dist = dist;
			nearest_pos = pos;
		}
	}
	
	return nearest_pos;
}

static DriftVec2 nearest_thing(DriftScript* script, DriftComponent* component, uint* type_arr, uint type){
	DriftGameState* state = get_state(script);
	float nearest_dist = INFINITY;
	DriftVec2 target_pos = reticle_pos(script);
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
		float dist = DriftVec2Distance(pos, target_pos);
		if(dist < nearest_dist){
			nearest_dist = dist;
			nearest_pos = pos;
		}
	}
	
	return nearest_pos;
}

static void show_energy_message(DriftScript* script){
	show_message(script, MESSAGE_SHIPPY, "Oops! Your energy reserves are empty. " HIGHLIGHT "Follow the battery symbol on your HUD" DRIFT_TEXT_WHITE " to find your power network and recharge them.");
	
	TutorialContext* tut = script->ctx;
	tut->indicator_label = NULL;
}

void DriftTutorialScript(DriftScript* script){
	char buffer[1024];
	
	TutorialContext tut = {
		.list_header = (const char*[]){
			"Find Power",
			"Check Fabricator",
			"Recover Data",
			"Craft Headlights",
			"Expand Network",
			NULL,
		},
		.list_idx = -1,
	};
	script->ctx = &tut;
	
	// Reset status values
	get_state(script)->status.disable_look = true;
	get_state(script)->status.disable_move = true;
	get_state(script)->status.disable_hud = true;
	get_state(script)->status.disable_scan = true;
	get_state(script)->status.tool_restrict = DRIFT_TOOL_GUN;
	get_state(script)->status.scan_restrict = DRIFT_SCAN_CONSTRUCTION_SKIFF;
	get_player(script)->energy = 0;
	
	for(uint i = 0; i < _DRIFT_SCAN_COUNT; i++) get_state(script)->scan_progress[i] = 0;
	get_state(script)->status.factory_needs_reboot = true;
	script->draw = draw_shared;
	if(DRIFT_DEBUG) goto debug_start;
	
	// Tutorial starts here
	tut.fade_in = 0;
	while(tut.fade_in < 0.3f){
		tut.fade_in = DriftLerpConst(tut.fade_in, 0.3f, script->update->dt/5);
		DriftScriptYield(script);
	}
	
	script->draw = draw_press_to_begin;
	
	// Wait until ACCEPT is released;
	DRIFT_SCRIPT_WHILE_YIELD(script, DriftInputButtonState(DRIFT_INPUT_ACCEPT));
	
	// Draw periodic explosions until pressed.
	float explosion_timeout = 1;
	DriftRandom explode_rand = {};
	while(!script->debug_skip && !DriftInputButtonState(DRIFT_INPUT_ACCEPT)){
		explosion_timeout -= script->update->dt;
		if(explosion_timeout < 0){
			DriftVec2 pos = DriftVec2FMA((DriftVec2){1571.6f, -2516.6f}, DriftRandomInUnitCircle(&explode_rand), 100);
			float pan = DriftClamp(DriftAffinePoint(script->update->prev_vp_matrix, pos).x, -1, 1);
			DriftAudioPlaySample(DRIFT_BUS_SFX, DRIFT_SFX_EXPLODE, (DriftAudioParams){.gain = 1, .pan = pan});
			DriftMakeBlast(script->update, pos, (DriftVec2){1, 0}, DRIFT_BLAST_EXPLODE);
			explosion_timeout += 0.1f + DriftRandomUNorm(&explode_rand);
		}
		
		DriftScriptYield(script);
	}
	
	script->draw = draw_shared;
	
	while(tut.fade_in < 1 && !script->debug_skip){
		tut.fade_in = DriftLerpConst(tut.fade_in, 1, script->update->dt/2);
		DriftScriptYield(script);
	}
	
	debug_start:
	tut.fade_in = 1;
	wait_for_message(script, INFINITY, MESSAGE_SHIPPY,
		"Welcome back! It looks like you were unconscious. Would you like help?"
	);
	wait_for_message(script, INFINITY, MESSAGE_SHIPPY,
		"I can't help with your injuries, but if your accident has caused any memory loss I can walk you through the emergency checklist!"
	);
	
	float spin = 0, move_dist = 0;
	while(checklist_wait(script, 0,
		"{@LOOK} to Look",
		"{@MOVE} to Move",
		"Find power",
		NULL
	)){
		float dt = script->update->dt;
		uint body_idx = get_player_body_idx(script);
		spin += dt*fabsf(get_state(script)->bodies.angular_velocity[body_idx]);
		move_dist += dt*DriftVec2Length(get_state(script)->bodies.velocity[body_idx]);
		
		tut.task[0] |= spin > 6;
		tut.task[1] |= move_dist > 200;
		tut.task[2] |= get_player(script)->is_powered;
		
		// Wait for the player to move.
		if(!tut.task[0]){
			get_state(script)->status.disable_look = false;
			show_message(script, MESSAGE_SHIPPY, "First, calibrate your pod's gyros by " HIGHLIGHT "using {@LOOK} to spin around a few times" DRIFT_TEXT_WHITE ".");
		} else if(!tut.task[1]){
			get_state(script)->status.disable_move = false;
			show_message(script, MESSAGE_SHIPPY, "Great! Now test out your thruster controls " HIGHLIGHT "using {@MOVE} to move around" DRIFT_TEXT_WHITE ".");
		} else if(!tut.task[2]){
			get_state(script)->status.disable_hud = false;
			show_message(script, MESSAGE_SHIPPY, "Perfect! " HIGHLIGHT "Follow the battery symbol on your HUD" DRIFT_TEXT_WHITE " to find your power network and recharge your reserves.");
		}
	}
	
	get_state(script)->status.disable_look = false;
	get_state(script)->status.disable_move = false;
	get_state(script)->status.disable_hud = false;
	
	while(checklist_wait(script, 1,
		"Locate Base",
		"Scan Skiff",
		"Activate Fab",
		NULL
	)){
		DriftGameState* state = get_state(script);
		tut.task[0] |= DriftVec2Near(get_player_pos(script), (DriftVec2){3201.2f, -2752.4f}, 340);
		tut.task[1] |= state->scan_progress[DRIFT_SCAN_CONSTRUCTION_SKIFF] >= 1;
		tut.task[2] |= !state->status.factory_needs_reboot;
		
		DriftPlayerData* player = get_player(script);
		bool is_scanning = player->tool_idx == DRIFT_TOOL_SCAN;
		if(player->energy == 0){
			show_energy_message(script);
		} else if(!tut.task[0]){
			show_message(script, MESSAGE_SHIPPY, "Excellent! " HIGHLIGHT "The power flow will lead you home" DRIFT_TEXT_WHITE " to your construction skiff.");
		} else if(!tut.task[1]){
			state->status.tool_restrict = DRIFT_TOOL_SCAN;
			state->status.disable_scan = false;
			show_message(script, MESSAGE_SHIPPY, "Oh no! It looks like your skiff is damaged. " HIGHLIGHT "Use {@SCAN} and {@LOOK} to scan it " DRIFT_TEXT_WHITE "and check its status.");
			tut.indicator_location = DRIFT_SKIFF_POSITION;
			tut.indicator_label = (player->scanned_type == DRIFT_SCAN_CONSTRUCTION_SKIFF ? NULL : (is_scanning ? DRIFT_TEXT_GREEN"SCAN" : DRIFT_TEXT_GREEN"SCAN {@SCAN}"));
		} else if(!tut.task[2] && get_ctx(script)->ui_state == DRIFT_UI_STATE_NONE){
			state->status.tool_restrict = DRIFT_TOOL_SCAN;
			show_message(script, MESSAGE_SHIPPY, "Good news! Your skiff's onboard fabricator seems to be operational. To connect to the interface, " HIGHLIGHT "press {@SCAN} a second time while targeting the skiff" DRIFT_TEXT_WHITE ".");
			tut.indicator_location = DRIFT_SKIFF_POSITION;
			tut.indicator_label = (player->scanned_type == DRIFT_SCAN_CONSTRUCTION_SKIFF ? NULL : (is_scanning ? DRIFT_TEXT_GREEN"USE" : DRIFT_TEXT_GREEN"USE {@SCAN}"));
		} else {
			tut.message = NULL;
			tut.indicator_label = NULL;
		}
	}
	
	get_state(script)->status.tool_restrict = DRIFT_TOOL_SCAN;
	get_state(script)->status.disable_scan = false;
	get_state(script)->status.spawn_at_start = false;
	
	if(script->debug_skip){
		DriftGameState* state = get_state(script);
		state->bodies.position[DriftComponentFind(&state->bodies.c, get_state(script)->player)] = (DriftVec2){3159.9f, -2772.8f};
		state->scan_progress[DRIFT_SCAN_CONSTRUCTION_SKIFF] = 1;
		state->status.factory_needs_reboot = false;
	}
	
	
	tut.indicator_label = NULL;
	// Wait until they close the UI.
	while(get_ctx(script)->ui_state != DRIFT_UI_STATE_NONE) DriftScriptYield(script);
	wait_for_message(script, INFINITY, MESSAGE_SHIPPY, "Uh oh! Your fabricator's blueprint database is corrupt. Let's start rebuilding it by scanning some objects.");
	tut.message = NULL;
	
	while(checklist_wait(script, 2,
		"Scan Viridium",
		"Discover Lumium Source",
		"Scan Lumium",
		"Research Lumium Lights"
	)){
		DriftGameState* state = get_state(script);
		DriftPlayerData* player = get_player(script);
		bool is_scanning = player->tool_idx == DRIFT_TOOL_SCAN;
			
		tut.task[0] = state->scan_progress[DRIFT_SCAN_VIRIDIUM] >= 1;
		tut.task[1] = state->scan_progress[DRIFT_SCAN_GLOW_BUG] >= 1;
		tut.task[2] = state->scan_progress[DRIFT_SCAN_LUMIUM] >= 1;
		tut.task[3] = state->scan_progress[DRIFT_SCAN_HEADLIGHT] >= 1;
		
		// if(is_scanning) tut.message = NULL;
		DriftVec2 player_pos = get_player_pos(script);
		if(player->energy == 0){
			show_energy_message(script);
		} else if(get_ctx(script)->ui_state != DRIFT_UI_STATE_NONE){
			tut.indicator_label = NULL;
			tut.message = NULL;
		} else if(!tut.task[0]){
			state->status.tool_restrict = DRIFT_TOOL_SCAN;
			state->status.scan_restrict = DRIFT_SCAN_VIRIDIUM;
			tut.indicator_location = nearest_thing(script, &state->items.c, state->items.type, DRIFT_ITEM_VIRIDIUM);
			tut.indicator_label = (player->scanned_type == DRIFT_SCAN_VIRIDIUM ? NULL : (is_scanning ? DRIFT_TEXT_GREEN"SCAN" : DRIFT_TEXT_GREEN"SCAN {@SCAN}"));
		} else if(!tut.task[1]){
			state->status.tool_restrict = DRIFT_TOOL_SCAN;
			state->status.scan_restrict = DRIFT_SCAN_GLOW_BUG;
			tut.indicator_location = nearest_thing(script, &state->enemies.c, state->enemies.type, DRIFT_ENEMY_GLOW_BUG);
			tut.indicator_label = (player->scanned_type == DRIFT_SCAN_GLOW_BUG ? NULL : (is_scanning ? DRIFT_TEXT_GREEN"SCAN" : DRIFT_TEXT_GREEN"SCAN {@SCAN}"));
		} else if(!tut.task[2]){
			show_message(script, MESSAGE_SHIPPY, "Neat! " HIGHLIGHT "Glow bugs contain lumium."DRIFT_TEXT_WHITE" How convenient that your pod is equipped with cannons to remove it!");
			
			state->status.tool_restrict = DRIFT_TOOL_NONE;
			state->status.scan_restrict = DRIFT_SCAN_NONE;
			tut.indicator_location = nearest_thing(script, &state->items.c, state->items.type, DRIFT_ITEM_LUMIUM);
			tut.indicator_label = (player->scanned_type == DRIFT_SCAN_LUMIUM ? NULL : (is_scanning ? DRIFT_TEXT_GREEN"SCAN" : DRIFT_TEXT_GREEN"SCAN {@SCAN}"));
			
			if(DriftVec2Distance(player_pos, tut.indicator_location) > 500){
				tut.indicator_location = nearest_thing(script, &state->enemies.c, state->enemies.type, DRIFT_ENEMY_GLOW_BUG);
				tut.indicator_label = DRIFT_TEXT_GREEN"SHOOT {@FIRE}";
			}
		} else {
			show_message(script, MESSAGE_SHIPPY, "Fantastic! That should be enough scans to " HIGHLIGHT "research a blueprint at your fabricator" DRIFT_TEXT_WHITE ".");
			if(player->scanned_type != DRIFT_SCAN_CONSTRUCTION_SKIFF){
				tut.indicator_location = DRIFT_SKIFF_POSITION;
				tut.indicator_label = (is_scanning ? DRIFT_TEXT_GREEN"USE" : DRIFT_TEXT_GREEN"USE {@SCAN}");
			} else {
				tut.indicator_label = NULL;
			}
		}
	}
	tut.indicator_label = NULL;
	
	get_state(script)->status.tool_restrict = DRIFT_TOOL_NONE;
	get_state(script)->status.scan_restrict = DRIFT_SCAN_NONE;
	
	if(script->debug_skip) get_state(script)->scan_progress[DRIFT_SCAN_VIRIDIUM] = 1;
	if(script->debug_skip) get_state(script)->scan_progress[DRIFT_SCAN_GLOW_BUG] = 1;
	if(script->debug_skip) get_state(script)->scan_progress[DRIFT_SCAN_LUMIUM] = 1;
	if(script->debug_skip) get_state(script)->scan_progress[DRIFT_SCAN_HEADLIGHT] = 1;
	
	// Wait for the player to close the fabricator.
	while(get_ctx(script)->ui_state != DRIFT_UI_STATE_NONE) DriftScriptYield(script);
	
	wait_for_message(script, INFINITY, MESSAGE_SHIPPY, "Before we build the Lumium Lights, we need to " HIGHLIGHT "collect raw materials" DRIFT_TEXT_WHITE ".");
	
	while(checklist_wait(script, 3,
		"Viridium 0/5",
		"Lumium 0/5",
		"Craft Lumium Lights",
		NULL
	)){
		DriftMem* mem = DriftLinearMemInit(buffer, sizeof(buffer), "tutorial mem");
		DriftGameState* state = get_state(script);
		DriftPlayerData* player = get_player(script);
		
		tut.task[2] = DriftPlayerItemCount(state, DRIFT_ITEM_HEADLIGHT) > 0;
		{ // Update Viridium task
			uint count = DriftPlayerItemCount(state, DRIFT_ITEM_VIRIDIUM);
			tut.task_text[0] = DriftSMPrintf(mem, "Viridium %d/10", count);
			tut.task[0] |= count >= 10 || tut.task[2];
		}{ // Update Lumium task
			uint count = DriftPlayerItemCount(state, DRIFT_ITEM_LUMIUM);
			tut.task_text[1] = DriftSMPrintf(mem, "Lumium %d/5", count);
			tut.task[1] |= count >= 5 || tut.task[2];
		}
		
		bool is_scanning = player->tool_idx == DRIFT_TOOL_SCAN;
		if(player->energy == 0){
			show_energy_message(script);
		} else if(DriftPlayerCalculateCargo(state) >= DriftPlayerCargoCap(state)){
			show_message(script, MESSAGE_SHIPPY, "Your cargo hold is full. "HIGHLIGHT"Dock at the fabricator"DRIFT_TEXT_WHITE" to transfer it to storage.");
			tut.indicator_location = DRIFT_SKIFF_POSITION;
			tut.indicator_label = (is_scanning ? DRIFT_TEXT_GREEN"USE" : DRIFT_TEXT_GREEN"USE {@SCAN}");
		} else {
			DriftVec2 player_pos = get_player_pos(script);
			
			tut.message = NULL;
			if(!tut.task[0]){
				tut.indicator_label = DRIFT_TEXT_GREEN"PICK UP {@GRAB}";
				tut.indicator_location = nearest_thing(script, &state->items.c, state->items.type, DRIFT_ITEM_VIRIDIUM);
			} else if(!tut.task[1]){
				DriftVec2 nearest_item = nearest_thing(script, &state->items.c, state->items.type, DRIFT_ITEM_LUMIUM);
				
				if(DriftVec2Distance(player_pos, nearest_item) < 500){
					tut.indicator_label = DRIFT_TEXT_GREEN"PICK UP {@GRAB}";
					tut.indicator_location = nearest_item;
				} else if(player->grabbed_type != DRIFT_ITEM_LUMIUM){
					tut.indicator_label = DRIFT_TEXT_GREEN"SHOOT {@FIRE}";
					tut.indicator_location = nearest_thing(script, &state->enemies.c, state->enemies.type, DRIFT_ENEMY_GLOW_BUG);
				} else {
					tut.indicator_label = NULL;
				}
			} else if(!tut.task[2]){
				if(player->scanned_type != DRIFT_SCAN_CONSTRUCTION_SKIFF){
					tut.indicator_location = DRIFT_SKIFF_POSITION;
					tut.indicator_label = (is_scanning ? DRIFT_TEXT_GREEN"USE" : DRIFT_TEXT_GREEN"USE {@SCAN}");
				} else {
					tut.indicator_label = NULL;
				}
			}
		}
	}
	
	DRIFT_SCRIPT_WHILE_YIELD(script, get_ctx(script)->ui_state != DRIFT_UI_STATE_NONE);
	wait_for_message(script, INFINITY, MESSAGE_SHIPPY, "Amazing! Your fabricator still works perfectly. I bet you'll have the database restored in no time!");
	show_message(script, MESSAGE_SHIPPY, "Hold on. I'm detecting a large metalic ore deposit southeast of you. " HIGHLIGHT "Press {@MAP} to see it on your map.");
	
	get_state(script)->status.never_seen_map = true;
	DRIFT_SCRIPT_WHILE_YIELD(script, get_state(script)->status.never_seen_map);
	
	wait_for_message(script, INFINITY, MESSAGE_SHIPPY, "Shoot! Your power network doesn't extend far enough. You'll need to expand it by connecting more nodes.");

	while(checklist_wait(script, 4,
		"Missing Material",
		"Build 10 Power Nodes",
		"Place Nodes",
		NULL
	)){
		DriftGameState* state = get_state(script);
		DriftPlayerData* player = get_player(script);
		u16* inv = state->inventory.skiff;
		
		uint node_count = state->inventory.cargo[DRIFT_ITEM_POWER_NODE];
		tut.task[0] |= state->scan_progress[DRIFT_SCAN_SCRAP] >= 1;
		tut.task[1] |= node_count >= 10;
		tut.task[2] = tut.task[1] && node_count <= 5;
		
		if(player->energy == 0){
			show_energy_message(script);
		} else if(get_ctx(script)->ui_state != DRIFT_UI_STATE_NONE){
			tut.indicator_label = NULL;
			tut.message = NULL;
		} else if(!tut.task[0]){
			show_message(script, MESSAGE_SHIPPY, "The power node blueprint is still missing. " HIGHLIGHT "Scan objects that flash blue" DRIFT_TEXT_WHITE ", and see if you can find the missing materials.");
		} else if(!tut.task[1]){
			show_message(script, MESSAGE_SHIPPY, "That's it! Using scrap, you'll be able to " HIGHLIGHT "build power nodes" DRIFT_TEXT_WHITE ".");
		} else if(!tut.task[2]){
			show_message(script, MESSAGE_SHIPPY, "Now " HIGHLIGHT "place some nodes using {@DROP}" DRIFT_TEXT_WHITE " and extend your network to reach somewhere you couldn't reach before!");
		}
	}
	
	while(get_ctx(script)->ui_state != DRIFT_UI_STATE_NONE) DriftScriptYield(script);
	wait_for_message(script, INFINITY, MESSAGE_SHIPPY, "You're getting the hang of this! Company policy requires me to self terminate now as maintaining this level of upbeat enthusiasm is computationally expensive. Especially when " HIGHLIGHT "the pod operator is in extreme peril..." DRIFT_TEXT_WHITE " Good luck!");
	
	if(script->debug_skip){
		DriftGameState* state = get_state(script);
		state->inventory.skiff[DRIFT_ITEM_HEADLIGHT] = 1;
		state->inventory.skiff[DRIFT_ITEM_AUTOCANNON] = 1;
		state->inventory.skiff[DRIFT_ITEM_MINING_LASER] = 1;
		
		state->inventory.skiff[DRIFT_ITEM_VIRIDIUM] = 100;
		state->inventory.skiff[DRIFT_ITEM_LUMIUM] = 100;
		state->inventory.skiff[DRIFT_ITEM_SCRAP] = 100;
		state->inventory.skiff[DRIFT_ITEM_POWER_SUPPLY] = 100;
		state->inventory.skiff[DRIFT_ITEM_OPTICS] = 100;
		
		for(uint i = 0; i < _DRIFT_SCAN_COUNT; i++) state->scan_progress[i] = 1;
		
		get_state(script)->inventory.cargo[DRIFT_ITEM_POWER_NODE] = 100;
	}
	
	get_state(script)->status.needs_tutorial = false;
	script->debug_skip = false;
	script->draw = NULL;
}
