#include <stdio.h>

#include "drift_game.h"

#define DRIFT_SCRIPT_WHILE_YIELD(_script_, _cond_) \
	while(!(_script_)->debug_skip && (_cond_)) tina_yield((_script_)->coro, 0);

static void DriftScriptCheckpoint(DriftScript* script){
	while(script->debug_skip) tina_yield(script->coro, 0);
}

static void DriftScriptWaitSeconds(DriftScript* script, double time){
	u64 t0 = script->ctx->update_nanos + (u64)(time*1e9);
	DRIFT_SCRIPT_WHILE_YIELD(script, script->ctx->update_nanos < t0);
}

static void DriftScriptMiniPause(DriftScript* script){
	DriftScriptWaitSeconds(script, 0.25);
}

static void DriftScriptWaitAccept(DriftScript* script){
	DriftScriptMiniPause(script);
	
	DriftPlayerInput* input = &script->ctx->input.player;
	DRIFT_SCRIPT_WHILE_YIELD(script, DriftInputButtonState(input, DRIFT_INPUT_ACCEPT));
	DRIFT_SCRIPT_WHILE_YIELD(script, !DriftInputButtonState(input, DRIFT_INPUT_ACCEPT));
}

static void DriftScriptBlip(DriftScript* script){
	if(!script->debug_skip) DriftAudioPlaySample(script->ctx->audio, DRIFT_SFX_TEXT_BLIP, 1, 0, 1, false);
}

#define TEXT_ACCEPT_PROMPT "\nPress {@ACCEPT} to continue...\n"

void* tutorial_func(tina* coro, void* value){
	DriftScript* script = coro->user_data;
	DriftGameContext* ctx = script->ctx;
	
	while(ctx->player.id == 0) tina_yield(script->coro, 0);
	uint player_idx = DriftComponentFind(&ctx->state.players.c, ctx->player);
	DRIFT_ASSERT(player_idx, "no player?");
	
	DriftPlayerData* player = ctx->state.players.data + player_idx;
	DriftPlayerInput* input = &ctx->input.player;
	
	DriftCargoSlot* slot = NULL;
	// player->headlight = true;
	
	DriftScriptBlip(script);
	ctx->message = "Welcome to "DRIFT_TEXT_BLUE"Project Drift"DRIFT_TEXT_WHITE"!" TEXT_ACCEPT_PROMPT;
	DriftScriptWaitAccept(script);
	
	DriftScriptBlip(script);
	ctx->message = "Use {@MOVE} to move around.";
	DriftScriptMiniPause(script);
	DRIFT_SCRIPT_WHILE_YIELD(script, DriftVec2Length(DriftInputJoystick(input, 0, 1)) < 0.1f);
	DriftScriptMiniPause(script);
	
	DriftScriptBlip(script);
	ctx->message = "Use {@LOOK} to look around.";
	DriftScriptMiniPause(script);
	DRIFT_SCRIPT_WHILE_YIELD(script, DriftVec2Length(DriftInputJoystick(input, 2, 3)) < 0.1f && DriftVec2Length(ctx->input.mouse_rel) == 0);
	DriftScriptMiniPause(script);
	
	DriftScriptBlip(script);
	ctx->message =
		"You don't have a tool selected so you are in "DRIFT_TEXT_BLUE"flight mode"DRIFT_TEXT_WHITE".\n"
		"Note the red chevron in front of you." TEXT_ACCEPT_PROMPT;
	DriftScriptWaitAccept(script);
	
	player->quickslots[1] = DRIFT_TOOL_GRAB;
	DriftScriptBlip(script);
	ctx->message =
		"The "DRIFT_TEXT_BLUE"grabber tool"DRIFT_TEXT_WHITE" lets you manipulate and pick up objects.\n"
		"Press {@QUICKSLOT1} to equip it.";
	DriftScriptMiniPause(script);
	DRIFT_SCRIPT_WHILE_YIELD(script, player->quickslots[player->quickslot_idx] != DRIFT_TOOL_GRAB);
	DriftScriptMiniPause(script);
	
	DriftScriptBlip(script);
	ctx->message = "Notice how the chevron changed to a reticle.\nMove it around using {@LOOK}.";
	DriftScriptMiniPause(script);
	DRIFT_SCRIPT_WHILE_YIELD(script, DriftVec2Length(DriftInputJoystick(input, 2, 3)) < 0.1f && DriftVec2Length(ctx->input.mouse_rel) == 0);
	DriftScriptMiniPause(script);
	
	DriftScriptBlip(script);
	ctx->message =
		"Try "DRIFT_TEXT_BLUE"grabbing some ore"DRIFT_TEXT_WHITE" using {@LOOK} to target it,\n"
		"then hold {@ACTION1} to grab it.";
	DriftScriptMiniPause(script);
	DRIFT_SCRIPT_WHILE_YIELD(script, !player->grabbed_entity.id || player->grabbed_type != DRIFT_ITEM_TYPE_ORE);
	DriftScriptMiniPause(script);
	
	DriftScriptBlip(script);
	ctx->message =
		"Hold {@ACTION2} while grabbing an object to open your cargo hatch.\n"
		"Use {@LOOK} to pull the object into your ship, then release {@ACTION1} to stash it.";
	DriftScriptMiniPause(script);
	while(!script->debug_skip){
		DriftCargoSlot* slot = DriftPlayerGetCargoSlot(player, DRIFT_ITEM_TYPE_ORE);
		if(slot && slot->count >= 1) break; else tina_yield(coro, 0);
	}
	DriftScriptMiniPause(script);
	
	DriftScriptBlip(script);
	ctx->message = script->message;
	while(!script->debug_skip){
		DRIFT_ASSERT_HARD(slot = DriftPlayerGetCargoSlot(player, DRIFT_ITEM_TYPE_ORE), "No slot available.");
		uint count = slot ? slot->count : 0;
		snprintf(script->message, sizeof(script->message), "Now "DRIFT_TEXT_BLUE"collect %d more ore"DRIFT_TEXT_WHITE" so we can build a thingamabob.", 10 - count);
		if(count >= 10) break; else tina_yield(coro, 0);
	}
	
	// DRIFT_ASSERT_HARD(slot = DriftPlayerGetCargoSlot(player, DRIFT_ITEM_TYPE_ORE), "No slot available.");
	// slot->count = 10;
	
	DRIFT_ASSERT_HARD(slot = DriftPlayerGetCargoSlot(player, DRIFT_ITEM_TYPE_POWER_NODE), "No slot available.");
	slot->count = 10;
	slot->request = slot->type;
	
	DriftScriptBlip(script);
	ctx->message =
		"10 "DRIFT_TEXT_BLUE"power nodes"DRIFT_TEXT_WHITE" have been added to your inventory.\n"
		"Press {@CARGO_PREV} or {@CARGO_NEXT} to cycle inventory slots.";
	DriftScriptMiniPause(script);
	DRIFT_SCRIPT_WHILE_YIELD(script, player->cargo_slots[player->cargo_idx].type != DRIFT_ITEM_TYPE_POWER_NODE);
	DriftScriptMiniPause(script);
	
	DriftScriptBlip(script);
	ctx->message =
		"Hold {@ACTION2} to open your cargo hatch, then use\n"
		"{@LOOK} and {@ACTION1} to "DRIFT_TEXT_BLUE"deploy a power node"DRIFT_TEXT_WHITE".";
	while(!script->debug_skip){
		DriftCargoSlot* slot = DriftPlayerGetCargoSlot(player, DRIFT_ITEM_TYPE_POWER_NODE);
		if(slot && slot->count < 10) break; else tina_yield(coro, 0);
	}
	
	DriftScriptBlip(script);
	ctx->message = "That's all the farther the tutorial goes for now.\nHave fun!" TEXT_ACCEPT_PROMPT;
	DriftScriptWaitAccept(script);
	
	// player->quickslots[2] = DRIFT_TOOL_DIG;
	// player->quickslots[3] = DRIFT_TOOL_GUN;
	DriftScriptBlip(script);
	ctx->message = NULL;
	script->debug_skip = false;
	return 0;
}

void* u_ded_func(tina* coro, void* value){
	DriftScript* script = coro->user_data;
	DriftGameContext* ctx = script->ctx;
	DriftPlayerData* player = ctx->state.players.data + 1;
	
	DriftCargoSlot* slot = NULL;
	// player->headlight = true;
	
	DriftScriptBlip(script);
	ctx->message = "Whoops! No power!"TEXT_ACCEPT_PROMPT;
	DriftScriptWaitAccept(script);
	
	ctx->message = NULL;
	return 0;
}
