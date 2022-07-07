#include <string.h>

#include "SDL.h"

#include "drift_game.h"
#include "base/drift_nuklear.h"

static const char _MULTI_ICON[] = "_MULTI_ICON";
const char* DRIFT_INPUT_ICON_MULTI = _MULTI_ICON;

static const DriftInputIcon ICONS_MOUSE_KEYBOARD[] = {
	{"MOVE"      , DRIFT_SPRITE_KEY_W, 2},
	{_MULTI_ICON , DRIFT_SPRITE_KEY_A, 2},
	{_MULTI_ICON , DRIFT_SPRITE_KEY_S, 2},
	{_MULTI_ICON , DRIFT_SPRITE_KEY_D, 1},
	{"LOOK"      , DRIFT_SPRITE_MOUSE_POS, 1},
	{"ACCEPT"    , DRIFT_SPRITE_KEY_ENTER, 3},
	{"CANCEL"    , DRIFT_SPRITE_KEY_ESC, 1},
	{"HEADLIGHT" , DRIFT_SPRITE_KEY_H, 1},
	{"ACTION1"   , DRIFT_SPRITE_MOUSE_LEFT, 1},
	{"ACTION2"   , DRIFT_SPRITE_MOUSE_RIGHT, 1},
	{"CARGO_PREV", DRIFT_SPRITE_KEY_Q, 1},
	{"CARGO_NEXT", DRIFT_SPRITE_KEY_E, 1},
	{"QUICKSLOT1", DRIFT_SPRITE_KEY_1, 1},
	{"QUICKSLOT2", DRIFT_SPRITE_KEY_2, 1},
	{"QUICKSLOT3", DRIFT_SPRITE_KEY_3, 1},
	{"QUICKSLOT4", DRIFT_SPRITE_KEY_4, 1},
	{"OPEN_UI"   , DRIFT_SPRITE_KEY_TAB, 2},
	{},
};

static const DriftInputIcon ICONS_XBOX[] = {
	{"MOVE"      , DRIFT_SPRITE_PAD_LSTICK, 1},
	{"LOOK"      , DRIFT_SPRITE_PAD_RSTICK, 1},
	{"ACCEPT"    , DRIFT_SPRITE_XB_A, 1},
	{"CANCEL"    , DRIFT_SPRITE_XB_B, 1},
	{"HEADLIGHT" , DRIFT_SPRITE_XB_Y, 1},
	{"ACTION1"   , DRIFT_SPRITE_XB_RT, 1},
	{"ACTION2"   , DRIFT_SPRITE_XB_LT, 1},
	{"CARGO_PREV", DRIFT_SPRITE_XB_LB, 1},
	{"CARGO_NEXT", DRIFT_SPRITE_XB_RB, 1},
	{"QUICKSLOT1", DRIFT_SPRITE_PAD_RIGHT, 1},
	{"QUICKSLOT2", DRIFT_SPRITE_PAD_UP, 1},
	{"QUICKSLOT3", DRIFT_SPRITE_PAD_LEFT, 1},
	{"QUICKSLOT4", DRIFT_SPRITE_PAD_DOWN, 1},
	{"OPEN_UI"   , DRIFT_SPRITE_PAD_START, 1},
	{},
};

static const DriftInputIcon ICONS_PLAYSTATION[] = {
	{"MOVE"      , DRIFT_SPRITE_PAD_LSTICK, 1},
	{"LOOK"      , DRIFT_SPRITE_PAD_RSTICK, 1},
	{"ACCEPT"    , DRIFT_SPRITE_PS_CROSS, 1},
	{"CANCEL"    , DRIFT_SPRITE_PS_CIRCLE, 1},
	{"HEADLIGHT" , DRIFT_SPRITE_PS_TRIANGLE, 1},
	{"ACTION1"   , DRIFT_SPRITE_PS_R2, 1},
	{"ACTION2"   , DRIFT_SPRITE_PS_L2, 1},
	{"CARGO_PREV", DRIFT_SPRITE_PS_L1, 1},
	{"CARGO_NEXT", DRIFT_SPRITE_PS_R1, 1},
	{"QUICKSLOT1", DRIFT_SPRITE_PAD_RIGHT, 1},
	{"QUICKSLOT2", DRIFT_SPRITE_PAD_UP, 1},
	{"QUICKSLOT3", DRIFT_SPRITE_PAD_LEFT, 1},
	{"QUICKSLOT4", DRIFT_SPRITE_PAD_DOWN, 1},
	{"OPEN_UI"   , DRIFT_SPRITE_PAD_START, 1},
	{},
};

const DriftInputIcon* DRIFT_INPUT_ICON_SETS[_DRIFT_INPUT_SET_TYPE_COUNT] = {
	[DRIFT_INPUT_SET_TYPE_MOUSE_KEYBOARD] = ICONS_MOUSE_KEYBOARD,
	[DRIFT_INPUT_SET_TYPE_XBOX] = ICONS_XBOX,
	[DRIFT_INPUT_SET_TYPE_PLAYSTATION] = ICONS_PLAYSTATION,
};

const DriftInputIcon* DriftInputIconFind(const DriftInputIcon* icons, const char* label){
	for(uint icon_idx = 0; icons[icon_idx].label; icon_idx++){
		const DriftInputIcon* icon = icons + icon_idx;
		const char* l = label;
		const char* c = icon->label;
		
		while(true){
			if(*c == 0 && *l == '}') return icon;
			if(*c != *l) break;
			c++, l++;
		}
	}
	
	DRIFT_ABORT("Control label not found '%s'.", label);
}

static const uint MOUSE_MAP[8] = {
	[SDL_BUTTON_LEFT] = DRIFT_MOUSE_LEFT,
	[SDL_BUTTON_MIDDLE] = DRIFT_MOUSE_MIDDLE,
	[SDL_BUTTON_RIGHT] = DRIFT_MOUSE_RIGHT,
};

static const u8 GAMEPAD_AXIS_MAP[SDL_CONTROLLER_AXIS_MAX] = {
	[SDL_CONTROLLER_AXIS_LEFTX] = DRIFT_INPUT_AXIS_MOVE_X,
	[SDL_CONTROLLER_AXIS_LEFTY] = DRIFT_INPUT_AXIS_MOVE_Y,
	[SDL_CONTROLLER_AXIS_RIGHTX] = DRIFT_INPUT_AXIS_LOOK_X,
	[SDL_CONTROLLER_AXIS_RIGHTY] = DRIFT_INPUT_AXIS_LOOK_Y,
	[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = DRIFT_INPUT_AXIS_ACTION1,
	[SDL_CONTROLLER_AXIS_TRIGGERLEFT ] = DRIFT_INPUT_AXIS_ACTION2,
};

static const u64 GAMEPAD_BUTTON_MAP[SDL_CONTROLLER_BUTTON_MAX] = {
	[SDL_CONTROLLER_BUTTON_A] = DRIFT_INPUT_ACCEPT,
	[SDL_CONTROLLER_BUTTON_B] = DRIFT_INPUT_CANCEL,
	[SDL_CONTROLLER_BUTTON_Y] = DRIFT_INPUT_TOGGLE_HEADLIGHT,
	[SDL_CONTROLLER_BUTTON_START] = DRIFT_INPUT_OPEN_UI,
	[SDL_CONTROLLER_BUTTON_LEFTSHOULDER ] = DRIFT_INPUT_CARGO_PREV,
	[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = DRIFT_INPUT_CARGO_NEXT,
	[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = DRIFT_INPUT_QUICK_SLOT1,
	[SDL_CONTROLLER_BUTTON_DPAD_UP   ] = DRIFT_INPUT_QUICK_SLOT2,
	[SDL_CONTROLLER_BUTTON_DPAD_LEFT ] = DRIFT_INPUT_QUICK_SLOT3,
	[SDL_CONTROLLER_BUTTON_DPAD_DOWN ] = DRIFT_INPUT_QUICK_SLOT4,
};

typedef struct {
	u64 button_mask;
	u8 axis_idx;
	s8 axis_value;
} DriftMouseKeyboardMap;

static const DriftMouseKeyboardMap MOUSE_BUTTON_MAP[_DRIFT_MOUSE_COUNT] = {
	[DRIFT_MOUSE_LEFT ] = {.axis_idx = DRIFT_INPUT_AXIS_ACTION1, .axis_value = 1},
	[DRIFT_MOUSE_RIGHT] = {.axis_idx = DRIFT_INPUT_AXIS_ACTION2, .axis_value = 1},
};

static const DriftMouseKeyboardMap KEYBOARD_MAP[SDL_NUM_SCANCODES] = {
	[SDL_SCANCODE_A    ] = {.axis_idx = DRIFT_INPUT_AXIS_MOVE_X , .axis_value = -1},
	[SDL_SCANCODE_D    ] = {.axis_idx = DRIFT_INPUT_AXIS_MOVE_X , .axis_value =  1},
	[SDL_SCANCODE_W    ] = {.axis_idx = DRIFT_INPUT_AXIS_MOVE_Y , .axis_value = -1},
	[SDL_SCANCODE_S    ] = {.axis_idx = DRIFT_INPUT_AXIS_MOVE_Y , .axis_value =  1},
	[SDL_SCANCODE_LEFT ] = {.axis_idx = DRIFT_INPUT_AXIS_LOOK_X , .axis_value = -1},
	[SDL_SCANCODE_RIGHT] = {.axis_idx = DRIFT_INPUT_AXIS_LOOK_X , .axis_value =  1},
	[SDL_SCANCODE_UP   ] = {.axis_idx = DRIFT_INPUT_AXIS_LOOK_Y , .axis_value = -1},
	[SDL_SCANCODE_DOWN ] = {.axis_idx = DRIFT_INPUT_AXIS_LOOK_Y , .axis_value =  1},
	[SDL_SCANCODE_SPACE] = {.axis_idx = DRIFT_INPUT_AXIS_ACTION1, .axis_value =  1},
	[SDL_SCANCODE_LCTRL] = {.axis_idx = DRIFT_INPUT_AXIS_ACTION2, .axis_value =  1},
	
	[SDL_SCANCODE_RETURN] = {.button_mask = DRIFT_INPUT_ACCEPT   },
	[SDL_SCANCODE_ESCAPE] = {.button_mask = DRIFT_INPUT_CANCEL   },
	[SDL_SCANCODE_H     ] = {.button_mask = DRIFT_INPUT_TOGGLE_HEADLIGHT},
	[SDL_SCANCODE_TAB   ] = {.button_mask = DRIFT_INPUT_OPEN_UI         },
	[SDL_SCANCODE_Q     ] = {.button_mask = DRIFT_INPUT_CARGO_PREV      },
	[SDL_SCANCODE_E     ] = {.button_mask = DRIFT_INPUT_CARGO_NEXT      },
	[SDL_SCANCODE_1     ] = {.button_mask = DRIFT_INPUT_QUICK_SLOT1     },
	[SDL_SCANCODE_2     ] = {.button_mask = DRIFT_INPUT_QUICK_SLOT2     },
	[SDL_SCANCODE_3     ] = {.button_mask = DRIFT_INPUT_QUICK_SLOT3     },
	[SDL_SCANCODE_4     ] = {.button_mask = DRIFT_INPUT_QUICK_SLOT4     },
};

static void input_button_up(DriftPlayerInput* player, u64 mask){
	player->brelease |= mask & player->bstate;
	player->bstate &= ~mask;
}

static void input_button_down(DriftPlayerInput* player, u64 mask){
	player->bpress |= mask & ~player->bstate;
	player->bstate |= mask;
}

static void input_axis_button(DriftPlayerInput* player, float value, uint axis, u64 mask){
	if(value < 0.75f && player->axes[axis] > 0.75f) input_button_down(player, mask);
	if(value > 0.25f && player->axes[axis] < 0.25f) input_button_up(player, mask);
}

static void set_mouse_capture(DriftGameContext* ctx, bool capture){
	if(ctx->input.mouse_captured != capture){
		SDL_SetRelativeMouseMode(capture);
		ctx->input.mouse_captured = capture;
		
		// Remove/restore mouse input when capture state changes.
		for(uint i = 0; i < _DRIFT_MOUSE_COUNT; i++){
			if(ctx->input.mouse_state[i]){
				DriftMouseKeyboardMap map = MOUSE_BUTTON_MAP[i];
				ctx->input.player._digital_axis[map.axis_idx] += (capture ? 1 : -1)*map.axis_value;
			}
		}
	}
}

void DriftInputEventsPoll(DriftApp* app, DriftGameContext* ctx, tina_job* job, DriftNuklear* dnk, DriftAffine vp_inverse){
	DriftInput* input = &ctx->input;
	
	// Reset mouse info.
	input->mouse_rel_clip = DRIFT_VEC2_ZERO;
	input->mouse_wheel = 0;
	for(uint i = 0; i < _DRIFT_MOUSE_COUNT; i++){
		input->mouse_up[i] = input->mouse_down[i] = false;
	}
	
	// TODO need to determine player from the event's source.
	DriftPlayerInput* player = &input->player;
	
	float action1[DRIFT_PLAYER_COUNT];
	float action2[DRIFT_PLAYER_COUNT];
	for(uint i = 0; i < DRIFT_PLAYER_COUNT; i++){
		// Reset button events for all players.
		player->bpress = player->brelease = 0;
		action1[i] = player->axes[DRIFT_INPUT_AXIS_ACTION1];
		action2[i] = player->axes[DRIFT_INPUT_AXIS_ACTION2];
	}
	
	struct nk_context* nk = dnk ? &dnk->nk : NULL;
	if(nk) nk_input_begin(nk);
	set_mouse_capture(ctx, !nk->active && input->icon_type == DRIFT_INPUT_SET_TYPE_MOUSE_KEYBOARD);
	
	for(SDL_Event event; SDL_PollEvent(&event);){
		switch(event.type){
			case SDL_QUIT: ctx->input.quit = true; break;

			case SDL_KEYDOWN:{
				if(!event.key.repeat){
					SDL_Scancode scancode = event.key.keysym.scancode;
					DriftMouseKeyboardMap map = KEYBOARD_MAP[scancode];
					player->_digital_axis[map.axis_idx] += map.axis_value;
					input_button_down(player, map.button_mask);
					
					SDL_Keymod keymod = SDL_GetModState();
					if(scancode == SDL_SCANCODE_F11 || ((keymod & KMOD_ALT) && scancode == SDL_SCANCODE_RETURN)) DriftAppToggleFullscreen(app);
					if((keymod & KMOD_ALT) && scancode == SDL_SCANCODE_F4) ctx->input.quit = true;
					if(scancode == SDL_SCANCODE_GRAVE) ctx->debug.show_ui = !ctx->debug.show_ui;

#if DRIFT_MODULES
					if(scancode == SDL_SCANCODE_F5) input->request_hotload = true;
#endif
					
					input->icon_type = DRIFT_INPUT_SET_TYPE_MOUSE_KEYBOARD;
				}
			} break;

			case SDL_KEYUP:{
				if(!event.key.repeat){
					SDL_Scancode scancode = event.key.keysym.scancode;
					
					DriftMouseKeyboardMap map = KEYBOARD_MAP[scancode];
					player->_digital_axis[map.axis_idx] -= map.axis_value;
					input_button_up(player, map.button_mask);
				}
			} break;
			
			case SDL_MOUSEMOTION:{
				float dx = 2.0f/app->window_w, dy = 2.0f/app->window_h;
				input->mouse_pos_clip = (DriftVec2){dx*event.motion.x - 1, 1 - dy*event.motion.y};
				input->mouse_rel_clip = (DriftVec2){dx*event.motion.xrel, -dy*event.motion.yrel};
			} break;
			
			case SDL_MOUSEBUTTONDOWN: {
				if(ctx->input.mouse_captured){
					DriftMouseKeyboardMap map = MOUSE_BUTTON_MAP[MOUSE_MAP[event.button.button]];
					player->_digital_axis[map.axis_idx] += map.axis_value;
					input_button_down(player, map.button_mask);
				}
				
				input->mouse_down[MOUSE_MAP[event.button.button]] = true;
				input->mouse_state[MOUSE_MAP[event.button.button]] = true;
				input->icon_type = DRIFT_INPUT_SET_TYPE_MOUSE_KEYBOARD;
			} break;
			
			case SDL_MOUSEBUTTONUP: {
				if(ctx->input.mouse_captured){
					DriftMouseKeyboardMap map = MOUSE_BUTTON_MAP[MOUSE_MAP[event.button.button]];
					player->_digital_axis[map.axis_idx] -= map.axis_value;
					input_button_up(player, map.button_mask);
				}
				
				input->mouse_up[MOUSE_MAP[event.button.button]] = true;
				input->mouse_state[MOUSE_MAP[event.button.button]] = false;
			} break;
			
			case SDL_MOUSEWHEEL: {
				input->mouse_wheel = event.wheel.y;
			} break;

			case SDL_CONTROLLERDEVICEADDED:{
				SDL_GameController* pad = SDL_GameControllerOpen(event.cdevice.which);
				DRIFT_LOG("Game controller connected: %s", SDL_GameControllerName(pad));
				// TODO associate with player somehow?
				set_mouse_capture(ctx, false);
				input->icon_type = DRIFT_INPUT_SET_TYPE_XBOX;
			} break;

			case SDL_CONTROLLERAXISMOTION:{
				u8 axis_idx = GAMEPAD_AXIS_MAP[event.caxis.axis];
				player->_analog_axis[axis_idx] = (float)event.caxis.value/(float)SDL_MAX_SINT16;
			} break;

			case SDL_CONTROLLERBUTTONDOWN:{
				input_button_down(player, GAMEPAD_BUTTON_MAP[event.cbutton.button]);
#if DRIFT_MODULES
				if(event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) input->request_hotload = true;
#endif
				set_mouse_capture(ctx, false);
				input->icon_type = DRIFT_INPUT_SET_TYPE_XBOX;
				
				if(event.cbutton.button == SDL_CONTROLLER_BUTTON_TOUCHPAD) ctx->script.debug_skip = true;
			} break;

			case SDL_CONTROLLERBUTTONUP:{
				input_button_up(player, GAMEPAD_BUTTON_MAP[event.cbutton.button]);
				if(event.cbutton.button == SDL_CONTROLLER_BUTTON_TOUCHPAD) ctx->script.debug_skip = false;
			} break;
		}
		
		if(nk && !input->mouse_captured) DriftNuklearHandleEvent(dnk, &event);
	}
	if(nk) nk_input_end(nk);
	
	input->mouse_pos = DriftAffinePoint(vp_inverse, input->mouse_pos_clip);
	input->mouse_rel = DriftAffineDirection(vp_inverse, input->mouse_rel_clip);
	
	
	// Combine analog and digital inputs for all players.
	for(uint p = 0; p < DRIFT_PLAYER_COUNT; p++){
		for(uint i = 0; i < _DRIFT_INPUT_AXIS_COUNT; i++){
			player->axes[i] = player->_digital_axis[i] + player->_analog_axis[i];
		}
	}
	
	for(uint i = 0; i < DRIFT_PLAYER_COUNT; i++){
		input_axis_button(player, action1[i], DRIFT_INPUT_AXIS_ACTION1, DRIFT_INPUT_ACTION1);
		input_axis_button(player, action2[i], DRIFT_INPUT_AXIS_ACTION2, DRIFT_INPUT_ACTION2);
	}
}
