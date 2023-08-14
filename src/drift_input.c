/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <string.h>

#include "SDL.h"

#include "drift_game.h"
#include "base/drift_nuklear.h"
#include "microui/microui.h"

static const char _MULTI_ICON[] = "_MULTI_ICON";
const char* DRIFT_INPUT_ICON_MULTI = _MULTI_ICON;

static const DriftInputIcon ICONS_MOUSE_KEYBOARD[] = {
	{"MOVE",      DRIFT_SPRITE_KEY_W, 2},
	{_MULTI_ICON, DRIFT_SPRITE_KEY_A, 2},
	{_MULTI_ICON, DRIFT_SPRITE_KEY_S, 2},
	{_MULTI_ICON, DRIFT_SPRITE_KEY_D, 2},
	{"LOOK",      DRIFT_SPRITE_MOUSE_POS, 2},
	{"ACCEPT",    DRIFT_SPRITE_KEY_ENTER, 4},
	{"CANCEL",    DRIFT_SPRITE_KEY_ESC, 2},
	{"PAUSE",     DRIFT_SPRITE_KEY_ESC, 2},
	{"MAP",       DRIFT_SPRITE_KEY_TAB, 3},
	{"PREV",      DRIFT_SPRITE_KEY_Q, 2},
	{"NEXT",      DRIFT_SPRITE_KEY_E, 2},
	{"LIGHT",     DRIFT_SPRITE_KEY_H, 2},
	{"FIRE",      DRIFT_SPRITE_MOUSE_LEFT, 2},
	{"ALT",       DRIFT_SPRITE_MOUSE_RIGHT, 2},
	{"GRAB",      DRIFT_SPRITE_KEY_F, 2},
	{"DROP",      DRIFT_SPRITE_KEY_SHIFT, 5},
	{"SCAN",      DRIFT_SPRITE_KEY_R, 2},
	{"LASER",     DRIFT_SPRITE_KEY_E, 2},
	{"STASH",     DRIFT_SPRITE_MOUSE_MIDDLE, 2},
	{},
};

static const DriftInputIcon ICONS_XBOX[] = {
	{"MOVE",   DRIFT_SPRITE_PAD_LSTICK, 2},
	{"LOOK",   DRIFT_SPRITE_PAD_RSTICK, 2},
	{"ACCEPT", DRIFT_SPRITE_XB_A, 2},
	{"CANCEL", DRIFT_SPRITE_XB_B, 2},
	{"PAUSE",  DRIFT_SPRITE_XB_START, 2},
	{"MAP",    DRIFT_SPRITE_XB_BACK, 2},
	{"PREV",   DRIFT_SPRITE_XB_LB, 2},
	{"NEXT",   DRIFT_SPRITE_XB_RB, 2},
	{"LIGHT",  DRIFT_SPRITE_PAD_UP, 2},
	{"FIRE",   DRIFT_SPRITE_XB_RT, 2},
	{"ALT",    DRIFT_SPRITE_XB_LT, 2},
	{"GRAB",   DRIFT_SPRITE_XB_RB, 2},
	{"DROP",   DRIFT_SPRITE_XB_LB, 2},
	{"SCAN",   DRIFT_SPRITE_XB_X, 2},
	{"LASER",  DRIFT_SPRITE_PAD_DOWN, 2},
	{"STASH",  DRIFT_SPRITE_PAD_RCLICK, 2},
	{},
};

static const DriftInputIcon ICONS_PLAYSTATION[] = {
	{"MOVE",   DRIFT_SPRITE_PAD_LSTICK, 2},
	{"LOOK",   DRIFT_SPRITE_PAD_RSTICK, 2},
	{"ACCEPT", DRIFT_SPRITE_PS_CROSS, 2},
	{"CANCEL", DRIFT_SPRITE_PS_CIRCLE, 2},
	{"PAUSE",  DRIFT_SPRITE_PS_START, 2},
	{"MAP",    DRIFT_SPRITE_PS_START, 2},
	{"PREV",   DRIFT_SPRITE_PS_L1, 2},
	{"NEXT",   DRIFT_SPRITE_PS_R1, 2},
	{"LIGHT",  DRIFT_SPRITE_PAD_UP, 2},
	{"FIRE",   DRIFT_SPRITE_PS_R2, 2},
	{"ALT",    DRIFT_SPRITE_PS_L2, 2},
	{"GRAB",   DRIFT_SPRITE_PS_R1, 2},
	{"DROP",   DRIFT_SPRITE_PS_L1, 2},
	{"SCAN",   DRIFT_SPRITE_PS_SQUARE, 2},
	{"LASER",  DRIFT_SPRITE_PAD_DOWN, 2},
	{"STASH",  DRIFT_SPRITE_PAD_RCLICK, 2},
	{},
};

const DriftInputIcon* DRIFT_INPUT_ICON_SETS[_DRIFT_INPUT_SET_COUNT] = {
	[DRIFT_INPUT_SET_MOUSE_KEYBOARD] = ICONS_MOUSE_KEYBOARD,
	[DRIFT_INPUT_SET_XBOX] = ICONS_XBOX,
	[DRIFT_INPUT_SET_PLAYSTATION] = ICONS_PLAYSTATION,
};

const DriftInputIcon* DriftInputIconFind(const DriftInputIcon* icons, const char* label){
	for(uint icon_idx = 0; icons[icon_idx].label; icon_idx++){
		const DriftInputIcon* icon = icons + icon_idx;
		const char* l = label;
		const char* c = icon->label;
		
		while(true){
			if(*c == 0) return icon;
			if(*c != *l) break;
			c++, l++;
		}
	}
	
	static const DriftInputIcon NONE = {"", DRIFT_SPRITE_INPUT_NONE, 1};
	return &NONE;
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
	[SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = DRIFT_INPUT_AXIS_FIRE,
	[SDL_CONTROLLER_AXIS_TRIGGERLEFT ] = DRIFT_INPUT_AXIS_ALT,
};

static const u64 GAMEPAD_BUTTON_MAP[SDL_CONTROLLER_BUTTON_MAX] = {
	[SDL_CONTROLLER_BUTTON_A] = DRIFT_INPUT_ACCEPT,
	[SDL_CONTROLLER_BUTTON_B] = DRIFT_INPUT_CANCEL,
	[SDL_CONTROLLER_BUTTON_DPAD_UP] = DRIFT_INPUT_LIGHT,
	[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = DRIFT_INPUT_LASER,
	[SDL_CONTROLLER_BUTTON_START] = DRIFT_INPUT_PAUSE,
	[SDL_CONTROLLER_BUTTON_BACK] = DRIFT_INPUT_MAP,
	[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = DRIFT_INPUT_DROP | DRIFT_INPUT_PREV,
	[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = DRIFT_INPUT_GRAB | DRIFT_INPUT_NEXT,
	[SDL_CONTROLLER_BUTTON_X] = DRIFT_INPUT_SCAN,
	[SDL_CONTROLLER_BUTTON_RIGHTSTICK] = DRIFT_INPUT_QUICK_GRAB,
};

typedef struct {
	u64 button_mask;
	u8 axis_idx;
	bool pos_value, neg_value;
} DriftMouseKeyboardMap;

static const DriftMouseKeyboardMap MOUSE_BUTTON_MAP[_DRIFT_MOUSE_COUNT] = {
	[DRIFT_MOUSE_LEFT   ] = {.axis_idx = DRIFT_INPUT_AXIS_FIRE, .pos_value = 1},
	[DRIFT_MOUSE_MIDDLE ] = {.button_mask = DRIFT_INPUT_QUICK_GRAB},
	[DRIFT_MOUSE_RIGHT  ] = {.axis_idx = DRIFT_INPUT_AXIS_ALT , .pos_value = 1},
};

static const DriftMouseKeyboardMap KEYBOARD_MAP[SDL_NUM_SCANCODES] = {
	[SDL_SCANCODE_A    ] = {.axis_idx = DRIFT_INPUT_AXIS_MOVE_X, .neg_value = 1},
	[SDL_SCANCODE_D    ] = {.axis_idx = DRIFT_INPUT_AXIS_MOVE_X, .pos_value = 1},
	[SDL_SCANCODE_W    ] = {.axis_idx = DRIFT_INPUT_AXIS_MOVE_Y, .neg_value = 1},
	[SDL_SCANCODE_S    ] = {.axis_idx = DRIFT_INPUT_AXIS_MOVE_Y, .pos_value = 1},
	
	[SDL_SCANCODE_RETURN] = {.button_mask = DRIFT_INPUT_ACCEPT},
	[SDL_SCANCODE_ESCAPE] = {.button_mask = DRIFT_INPUT_CANCEL | DRIFT_INPUT_PAUSE},
	[SDL_SCANCODE_H     ] = {.button_mask = DRIFT_INPUT_LIGHT},
	[SDL_SCANCODE_TAB   ] = {.button_mask = DRIFT_INPUT_MAP},
	[SDL_SCANCODE_LSHIFT] = {.button_mask = DRIFT_INPUT_DROP},
	[SDL_SCANCODE_F     ] = {.button_mask = DRIFT_INPUT_GRAB},
	[SDL_SCANCODE_Q     ] = {.button_mask = DRIFT_INPUT_PREV},
	[SDL_SCANCODE_E     ] = {.button_mask = DRIFT_INPUT_LASER | DRIFT_INPUT_NEXT},
	[SDL_SCANCODE_R     ] = {.button_mask = DRIFT_INPUT_SCAN},
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

static void set_mouse_capture(DriftInput* input, bool capture){
	if(input->mouse_captured != capture){
		SDL_SetRelativeMouseMode(capture);
		input->mouse_captured = capture;
		
		// Remove/restore mouse input when capture state changes.
		for(uint i = 0; i < _DRIFT_MOUSE_COUNT; i++){
			DriftMouseKeyboardMap map = MOUSE_BUTTON_MAP[i];
			input->player._axis_pos[map.axis_idx] = capture && input->mouse_state[i] && map.pos_value;
			input->player._axis_neg[map.axis_idx] = capture && input->mouse_state[i] && map.neg_value;
		}
	}
}

void DriftInputEventsPoll(DriftAffine vp_inverse, mu_Context* mu, DriftGameContext* ctx){
	DriftInput* input = APP->input_context;
	if(input == NULL) input = APP->input_context = DRIFT_COPY(DriftSystemMem, ((DriftInput){}));
	
	// Reset mouse info.
	input->mouse_rel = input->mouse_rel_world = DRIFT_VEC2_ZERO;
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
		action1[i] = player->axes[DRIFT_INPUT_AXIS_FIRE];
		action2[i] = player->axes[DRIFT_INPUT_AXIS_ALT];
	}
	
	struct nk_context* nk = NULL;
	if(ctx){
		nk = &ctx->debug.ui->nk;
		if(nk) nk_input_begin(nk);
	}
	
	DriftVec2 clip_coef = {2.0f/APP->window_w, 2.0f/APP->window_h};
	bool using_mouse = input->icon_type == DRIFT_INPUT_SET_MOUSE_KEYBOARD;
	bool ui_active = mu && mu->next_hover_ve;
	bool debug_ui_active = nk ? nk->active : false;
	set_mouse_capture(input, using_mouse && !(ui_active || debug_ui_active));
	
	for(SDL_Event event; SDL_PollEvent(&event);){
		switch(event.type){
			case SDL_QUIT: APP->request_quit = true; break;

			case SDL_KEYDOWN:{
				if(!event.key.repeat){
					SDL_Scancode scancode = event.key.keysym.scancode;
					SDL_Keymod keymod = SDL_GetModState();
					
					if(scancode == SDL_SCANCODE_F11 || ((keymod & KMOD_ALT) && scancode == SDL_SCANCODE_RETURN)){
						DriftAppToggleFullscreen();
						break;
					}
					if((keymod & KMOD_ALT) && scancode == SDL_SCANCODE_F4){
						APP->request_quit = true;
						break;
					}
					if(ctx && scancode == SDL_SCANCODE_GRAVE){
						ctx->debug.show_ui = !ctx->debug.show_ui;
						break;
					}

#if DRIFT_MODULES
					if(scancode == SDL_SCANCODE_F5){
						input->request_hotload = true;
						break;
					}
#endif
					
					DriftMouseKeyboardMap map = KEYBOARD_MAP[scancode];
					player->_axis_pos[map.axis_idx] |= map.pos_value;
					player->_axis_neg[map.axis_idx] |= map.neg_value;
					input_button_down(player, map.button_mask);
					
					input->icon_type = DRIFT_INPUT_SET_MOUSE_KEYBOARD;
				}
			} break;

			case SDL_KEYUP:{
				if(!event.key.repeat){
					SDL_Scancode scancode = event.key.keysym.scancode;
					
					DriftMouseKeyboardMap map = KEYBOARD_MAP[scancode];
					player->_axis_pos[map.axis_idx] &= !map.pos_value;
					player->_axis_neg[map.axis_idx] &= !map.neg_value;
					input_button_up(player, map.button_mask);
				}
			} break;
			
			case SDL_MOUSEMOTION:{
				input->mouse_pos_clip = (DriftVec2){clip_coef.x*event.motion.x - 1, 1 - clip_coef.y*event.motion.y};
				input->mouse_rel.x += event.motion.xrel;
				input->mouse_rel.y -= event.motion.yrel;
				// input->icon_type = DRIFT_INPUT_SET_MOUSE_KEYBOARD;
			} break;
			
			case SDL_MOUSEBUTTONDOWN:{
				if(input->mouse_captured){
					DriftMouseKeyboardMap map = MOUSE_BUTTON_MAP[MOUSE_MAP[event.button.button]];
					player->_axis_pos[map.axis_idx] |= map.pos_value;
					player->_axis_neg[map.axis_idx] |= map.neg_value;
					input_button_down(player, map.button_mask);
				}
				
				input->mouse_down[MOUSE_MAP[event.button.button]] = true;
				input->mouse_state[MOUSE_MAP[event.button.button]] = true;
				input->icon_type = DRIFT_INPUT_SET_MOUSE_KEYBOARD;
			} break;
			
			case SDL_MOUSEBUTTONUP: {
				if(input->mouse_captured){
					DriftMouseKeyboardMap map = MOUSE_BUTTON_MAP[MOUSE_MAP[event.button.button]];
					player->_axis_pos[map.axis_idx] &= !map.pos_value;
					player->_axis_neg[map.axis_idx] &= !map.neg_value;
					input_button_up(player, map.button_mask);
				}
				
				input->mouse_up[MOUSE_MAP[event.button.button]] = true;
				input->mouse_state[MOUSE_MAP[event.button.button]] = false;
			} break;
			
			case SDL_MOUSEWHEEL: {
				input->mouse_wheel = event.wheel.y;
				input->icon_type = DRIFT_INPUT_SET_MOUSE_KEYBOARD;
			} break;

			case SDL_CONTROLLERDEVICEADDED:{
				SDL_GameController* pad = SDL_GameControllerOpen(event.cdevice.which);
				input->gamepad = pad;
				
				set_mouse_capture(input, false);
				switch(SDL_GameControllerGetType(pad)){
					default:
					case SDL_CONTROLLER_TYPE_XBOX360:
					case SDL_CONTROLLER_TYPE_XBOXONE: {
						input->gamepad_icons = DRIFT_INPUT_SET_XBOX;
					} break;
					case SDL_CONTROLLER_TYPE_PS3:
					case SDL_CONTROLLER_TYPE_PS4:
					case SDL_CONTROLLER_TYPE_PS5: {
						input->gamepad_icons = DRIFT_INPUT_SET_PLAYSTATION;
					} break;
				}
				
				input->icon_type = input->gamepad_icons;
				DRIFT_LOG("Game controller connected: %s", SDL_GameControllerName(pad));
			} break;

			case SDL_CONTROLLERAXISMOTION:{
				u8 axis_idx = GAMEPAD_AXIS_MAP[event.caxis.axis];
				player->_analog_axis[axis_idx] = DriftClamp((float)event.caxis.value/(float)SDL_MAX_SINT16, -1, 1);
				
				if(event.caxis.value > SDL_MAX_SINT16/2) input->icon_type = input->gamepad_icons;
			} break;

			case SDL_CONTROLLERBUTTONDOWN:{
				input_button_down(player, GAMEPAD_BUTTON_MAP[event.cbutton.button]);
				
				set_mouse_capture(input, false);
				input->icon_type = input->gamepad_icons;
				
#if DRIFT_MODULES
				if(event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) input->request_hotload = true;
#endif
			} break;

			case SDL_CONTROLLERBUTTONUP:{
				input_button_up(player, GAMEPAD_BUTTON_MAP[event.cbutton.button]);
			} break;
		}
		
		if(!input->mouse_captured){
			if(mu) DriftUIHandleEvent(mu, &event, APP->scaling_factor);
			if(nk) DriftNuklearHandleEvent(ctx->debug.ui, &event);
		}
	}
	if(nk) nk_input_end(nk);
	
	input->mouse_pos_world = DriftAffinePoint(vp_inverse, input->mouse_pos_clip);
	input->mouse_rel_world = DriftAffineDirection(vp_inverse, (DriftVec2){clip_coef.x*input->mouse_rel.x, clip_coef.y*input->mouse_rel.y});
	
	// Combine analog and digital inputs;
	for(uint i = 0; i < _DRIFT_INPUT_AXIS_COUNT; i++){
		player->axes[i] = player->_analog_axis[i] + player->_axis_pos[i] - player->_axis_neg[i];
	}
	
	input_axis_button(player, action1[0], DRIFT_INPUT_AXIS_FIRE, DRIFT_INPUT_FIRE);
	input_axis_button(player, action2[0], DRIFT_INPUT_AXIS_ALT, DRIFT_INPUT_ALT);
	// DRIFT_LOG("value: %f", player->axes[4]);
	
	// TODO blocking UI is currently not allowed in the main game loop
	// if(ui_active){
	// 	// Blank out input when UI is active.
	// 	for(uint i = 0; i < _DRIFT_INPUT_AXIS_COUNT; i++) player->axes[i] = 0;
	// 	u64 mask = DRIFT_INPUT_PAUSE | DRIFT_INPUT_MAP | DRIFT_INPUT_PREV | DRIFT_INPUT_NEXT;
	// 	player->bpress &= mask;
	// 	player->brelease &= mask;
	// 	player->bstate &= mask;
	// }
}

void DriftRumble(void){
	if(INPUT->gamepad) SDL_GameControllerRumble(INPUT->gamepad, 0, 0xFFFF, 80);
}

void DriftRumbleLow(void){
	if(INPUT->gamepad) SDL_GameControllerRumble(INPUT->gamepad, 0xFFFF, 0, 120);
}
