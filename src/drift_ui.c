/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <string.h>
#include <stdio.h>

#include <SDL.h>

#include "drift_game.h"
#include "drift_strings.h"

#include "microui/microui.h"

static const uint UI_LINE_HEIGHT = 10;

static int text_width(mu_Font font, const char *text, int len){
	// TODO +1 because bounds doesn't include the final advance, but MicroUI expects it.
	return (int)DriftDrawTextSize(text, len > 0 ? len : 0).x + 1;
}

static int text_height(mu_Font font){
  return 10;
}

void DriftUIBegin(mu_Context* mu, DriftDraw* draw){
	mu->style = DRIFT_COPY(draw->mem, ((mu_Style){
		.font = NULL,
		.size = {50, 10}, .padding = 2, .spacing = 3, .indent = 10,
		.scrollbar_size = 10, .thumb_size = 8,
		.colors = {
			[MU_COLOR_TITLEBG] = {0xAC, 0x32, 0x32, 0xFF},
			[MU_COLOR_BORDER] = {0x00, 0x00, 0x00, 0x00},
			[MU_COLOR_TITLETEXT] = {0xCB, 0xDB, 0xFC, 0xFF},
			[MU_COLOR_WINDOWBG] = {19, 44, 57, 240},
			[MU_COLOR_TEXT] = {0xCB, 0xDB, 0xFC, 0xFF},
			[MU_COLOR_BASE] = {0x00, 0x00, 0x00, 0x40},
			[MU_COLOR_BASEHOVER] = {0x84, 0x7E, 0x87, 0xFF},
			[MU_COLOR_BASEFOCUS] = {0x00, 0x00, 0x00, 0x80},
		},
	}));
	
	mu_begin(mu);
}

static const struct {
	uint frame;
	DriftAABB2 border, split;
} PATCHES[MU_COLOR_MAX] = {
	[MU_COLOR_WINDOWBG] = {.frame = DRIFT_SPRITE_UI_WINDOW, {2, 3, 2, 3}, {2, 3, 2, 3}},
	[MU_COLOR_TITLEBG] = {.frame = DRIFT_SPRITE_UI_TITLE, {4, 0, 4, 0}, {27, 1, 20, 1}},
	[MU_COLOR_BUTTON] = {.frame = DRIFT_SPRITE_UI_BUTTON, {2, 2, 2, 2}, {5, 5, 4, 4}},
	[MU_COLOR_BUTTONHOVER] = {.frame = DRIFT_SPRITE_UI_BUTTONHOVER, {2, 2, 2, 2}, {5, 5, 4, 4}},
	[MU_COLOR_BUTTONFOCUS] = {.frame = DRIFT_SPRITE_UI_BUTTONFOCUS, {2, 2, 2, 2}, {5, 5, 4, 4}},
	[MU_COLOR_SCROLLBASE] = {.frame = DRIFT_SPRITE_UI_SCROLLBASE, {1, 1, 1, 1}, {1, 1, 1, 1}},
	[MU_COLOR_SCROLLTHUMB] = {.frame = DRIFT_SPRITE_UI_SCROLLTHUMB, {0, 0, 0, 0}, {2, 2, 2, 2}},
	[MU_COLOR_GROUPBG] = {.frame = DRIFT_SPRITE_UI_PANELBG, {0, 0, 0, 0}, {5, 5, 4, 4}},
	[MU_COLOR_GROUPHOVER] = {.frame = DRIFT_SPRITE_UI_PANELHOVER, {0, 0, 0, 0}, {5, 5, 4, 4}},
	[MU_COLOR_SLIDER] = {.frame = DRIFT_SPRITE_UI_SLIDER, {0, 0, 0, 0}, {1, 1, 3, 1}},
	[MU_COLOR_SLIDERHOVER] = {.frame = DRIFT_SPRITE_UI_SLIDERHOVER, {0, 0, 0, 0}, {1, 1, 3, 1}},
	[MU_COLOR_SLIDERFOCUS] = {.frame = DRIFT_SPRITE_UI_SLIDERHOVER, {0, 0, 0, 0}, {1, 1, 3, 1}},
	[MU_COLOR_THUMB] = {.frame = DRIFT_SPRITE_UI_THUMB, {0, 0, 0, 0}, {3, 3, 3, 3}},
	[MU_COLOR_THUMBHOVER] = {.frame = DRIFT_SPRITE_UI_THUMBHOVER, {0, 0, 0, 0}, {3, 3, 3, 3}},
	[MU_COLOR_THUMBFOCUS] = {.frame = DRIFT_SPRITE_UI_THUMBFOCUS, {0, 0, 0, 0}, {3, 3, 3, 3}},
};

static void patch9(DRIFT_ARRAY(DriftSprite)* arr, DriftAABB2 bb, uint patch_id, DriftRGBA8 tint){
	DriftFrame frame = DRIFT_FRAMES[PATCHES[patch_id].frame];
	DriftAABB2 border = PATCHES[patch_id].border, split = PATCHES[patch_id].split;
	
	u8 fx0 = frame.bounds.l, fx1 = frame.bounds.l + (u8)split.l - 1, fx2 = frame.bounds.r - (u8)split.r + 1, fx3 = frame.bounds.r;
	u8 fy0 = frame.bounds.b, fy1 = frame.bounds.b + (u8)split.b - 1, fy2 = frame.bounds.t - (u8)split.t + 1, fy3 = frame.bounds.t;
	u8 ax0 = (u8)border.l, ax1 = (fx3 - fx2 + 1) - (u8)border.r;
	u8 ay0 = (u8)border.b, ay1 = (fy3 - fy2 + 1) - (u8)border.t;
	float midx = bb.l + split.l - border.l, midy = bb.b + split.b - border.b;
	float sx = (float)((bb.r - bb.l) - (split.l - border.l) - (split.r - border.r))/(float)(fx2 - fx1 - 1);
	float sy = (float)((bb.t - bb.b) - (split.b - border.b) - (split.t - border.t))/(float)(fy2 - fy1 - 1);
	
	DriftSprite* cursor = DRIFT_ARRAY_RANGE(*arr, 9);
	*cursor++ = (DriftSprite){{{fx0 + 0, fy2 + 0, fx1 - 0, fy3 - 0}, {ax0, ay1}, frame.layer}, tint, { 1, 0, 0,  1, bb.l, bb.t}};
	*cursor++ = (DriftSprite){{{fx1 + 1, fy2 + 0, fx2 - 1, fy3 - 0}, {  0, ay1}, frame.layer}, tint, {sx, 0, 0,  1, midx, bb.t}};
	*cursor++ = (DriftSprite){{{fx2 + 0, fy2 + 0, fx3 - 0, fy3 - 0}, {ax1, ay1}, frame.layer}, tint, { 1, 0, 0,  1, bb.r, bb.t}};
	*cursor++ = (DriftSprite){{{fx0 + 0, fy1 + 1, fx1 - 0, fy2 - 1}, {ax0,   0}, frame.layer}, tint, { 1, 0, 0, sy, bb.l, midy}};
	*cursor++ = (DriftSprite){{{fx1 + 1, fy1 + 1, fx2 - 1, fy2 - 1}, {  0,   0}, frame.layer}, tint, {sx, 0, 0, sy, midx, midy}};
	*cursor++ = (DriftSprite){{{fx2 + 0, fy1 + 1, fx3 - 0, fy2 - 1}, {ax1,   0}, frame.layer}, tint, { 1, 0, 0, sy, bb.r, midy}};
	*cursor++ = (DriftSprite){{{fx0 + 0, fy0 + 0, fx1 - 0, fy1 - 0}, {ax0, ay0}, frame.layer}, tint, { 1, 0, 0,  1, bb.l, bb.b}};
	*cursor++ = (DriftSprite){{{fx1 + 1, fy0 + 0, fx2 - 1, fy1 - 0}, {  0, ay0}, frame.layer}, tint, {sx, 0, 0,  1, midx, bb.b}};
	*cursor++ = (DriftSprite){{{fx2 + 0, fy0 + 0, fx3 - 0, fy1 - 0}, {ax1, ay0}, frame.layer}, tint, { 1, 0, 0,  1, bb.r, bb.b}};
	DriftArrayRangeCommit(*arr, cursor);
}

static void draw_patch9(mu_Context *ctx, mu_Rect rect, int colorid){
	mu_Rect clip = mu_intersect_rects(rect, mu_get_clip_rect(ctx));
	if (clip.w > 0 && clip.h > 0) {
		mu_Command *cmd = mu_push_command(ctx, MU_COMMAND_PATCH9, sizeof(mu_IconCommand));
		cmd->icon.rect = rect;
		cmd->icon.id = colorid;
		cmd->icon.color = (mu_Color){0xFF, 0xFF, 0xFF, 0xFF};
	}
}

static void draw_frame(mu_Context *mu, mu_Rect rect, int colorid) {
	if(PATCHES[colorid].frame){
		int clipped = mu_check_clip(mu, rect);
		if(clipped == MU_CLIP_ALL) return;
		if(clipped == MU_CLIP_PART) mu_set_clip(mu, mu_get_clip_rect(mu));
		draw_patch9(mu, rect, colorid);
		if(clipped) mu_set_clip(mu, MU_UNCLIPPED_RECT);
	} else {
		mu_draw_rect(mu, rect, mu->style->colors[colorid]);
		
		if(
			colorid == MU_COLOR_SCROLLBASE  ||
			colorid == MU_COLOR_SCROLLTHUMB ||
			colorid == MU_COLOR_TITLEBG
		) return;
		if(mu->style->colors[MU_COLOR_BORDER].a){
			mu_draw_box(mu, mu_expand_rect(rect, 1), mu->style->colors[MU_COLOR_BORDER]);
		}
	}
}

mu_Context* DriftUIInit(void){
	mu_Context* mu = DriftAlloc(DriftSystemMem, sizeof(*mu));
	mu_init(mu);
	
	return mu;
}

void DriftUIHotload(mu_Context* mu){
	mu->text_width = text_width;
	mu->text_height = text_height;
	mu->draw_frame = draw_frame;
}

static inline DriftRGBA8 DriftRGBA8_from_mu(mu_Color c){
	return (DriftRGBA8){c.r*c.a/255, c.g*c.a/255, c.b*c.a/255, c.a};
}

static void ui_handle_joystick(mu_Context* mu, int mu_key, float value){
	bool state = mu->key_state & mu_key;
	if(!state && value > 0.75f) mu_input_keydown(mu, mu_key);
	if( state && value < 0.25f) mu_input_keyup(mu, mu_key);
}

void DriftUIHandleEvent(mu_Context* mu, SDL_Event* event, float scale){
	static const char BUTTON_MAP[0x10] = {
		[SDL_BUTTON_LEFT   & 0xF] = MU_MOUSE_LEFT,
		[SDL_BUTTON_RIGHT  & 0xF] = MU_MOUSE_RIGHT,
		[SDL_BUTTON_MIDDLE & 0xF] = MU_MOUSE_MIDDLE,
	};

	static const SDL_Keymod MOD_MASK = KMOD_CTRL | KMOD_ALT | KMOD_GUI;
	static const uint KEY_MAP[256] = {
		[SDLK_LSHIFT    & 0xFF] = MU_KEY_SHIFT,
		[SDLK_RSHIFT    & 0xFF] = MU_KEY_SHIFT,
		[SDLK_LCTRL     & 0xFF] = MU_KEY_CTRL,
		[SDLK_RCTRL     & 0xFF] = MU_KEY_CTRL,
		[SDLK_LALT      & 0xFF] = MU_KEY_ALT,
		[SDLK_RALT      & 0xFF] = MU_KEY_ALT,
		[SDLK_RETURN    & 0xFF] = MU_KEY_RETURN,
		[SDLK_BACKSPACE & 0xFF] = MU_KEY_BACKSPACE,
		[SDLK_ESCAPE    & 0xFF] = MU_KEY_ESCAPE,
		[SDLK_LEFT      & 0xFF] = MU_KEY_LEFT,
		[SDLK_RIGHT     & 0xFF] = MU_KEY_RIGHT,
		[SDLK_UP        & 0xFF] = MU_KEY_UP,
		[SDLK_DOWN      & 0xFF] = MU_KEY_DOWN,
	};
	
	static const uint GAMEPAD_MAP[0x10] = {
		[SDL_CONTROLLER_BUTTON_DPAD_LEFT ] = MU_KEY_LEFT,
		[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = MU_KEY_RIGHT,
		[SDL_CONTROLLER_BUTTON_DPAD_UP   ] = MU_KEY_UP,
		[SDL_CONTROLLER_BUTTON_DPAD_DOWN ] = MU_KEY_DOWN,
		[SDL_CONTROLLER_BUTTON_A         ] = MU_KEY_RETURN,
		[SDL_CONTROLLER_BUTTON_B         ] = MU_KEY_ESCAPE,
	};
	
	uint b = BUTTON_MAP[event->button.button & 0xF];
	uint c = KEY_MAP[event->key.keysym.sym & 0xFF];
	uint g = GAMEPAD_MAP[event->cbutton.button & 0xF];
	switch (event->type) {
		case SDL_MOUSEMOTION: mu_input_mousemove(mu, (int)(event->motion.x/scale), (int)(event->motion.y/scale), event->motion.state); break;
		case SDL_MOUSEWHEEL: mu_input_scroll(mu, 0, -30*event->wheel.y); break;
		case SDL_TEXTINPUT: mu_input_text(mu, event->text.text); break;

		case SDL_MOUSEBUTTONDOWN: if(b) mu_input_mousedown(mu, (int)(event->button.x/scale), (int)(event->button.y/scale), b); break;
		case SDL_MOUSEBUTTONUP: if(b) mu_input_mouseup(mu, (int)(event->button.x/scale), (int)(event->button.y/scale), b); break;

		case SDL_KEYDOWN: if(c && !event->key.repeat && (SDL_GetModState() & MOD_MASK) == 0) mu_input_keydown(mu, c); break;
		case SDL_KEYUP: if(c) mu_input_keyup(mu, c); break;
		
		case SDL_CONTROLLERBUTTONDOWN: if(g) mu_input_keydown(mu, g); break;
		case SDL_CONTROLLERBUTTONUP: if(g) mu_input_keyup(mu, g); break;
		
		case SDL_CONTROLLERAXISMOTION: {
			float value = DriftClamp((float)event->caxis.value/(float)SDL_MAX_SINT16, -1, 1);
			if(event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX){
				ui_handle_joystick(mu, MU_KEY_LEFT, -value);
				ui_handle_joystick(mu, MU_KEY_RIGHT, value);
			}
			if(event->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY){
				ui_handle_joystick(mu, MU_KEY_UP, -value);
				ui_handle_joystick(mu, MU_KEY_DOWN, value);
			}
		} break;
	}
}

void DriftUIPresent(mu_Context* mu, DriftDraw* draw){
	mu_end(mu);
	
	DRIFT_ARRAY(DriftSprite) geo = DRIFT_ARRAY_NEW(draw->mem, 1024, DriftSprite);
	DRIFT_ARRAY(uint) text_len = DRIFT_ARRAY_NEW(draw->mem, 256, uint);
	float h = draw->internal_extent.y;
	
	for(mu_Command *cmd = NULL; mu_next_command(mu, &cmd);){
		switch (cmd->type){
			case MU_COMMAND_TEXT:{
				mu_Color c = cmd->text.color;
				float pm = (float)c.a/(float)(255*255);
				uint len0 = DriftArrayLength(geo);
				DriftDrawTextFull(draw, &geo, cmd->text.str, (DriftTextOptions){
					.tint = {{c.r*pm, c.g*pm, c.b*pm, 255*pm}},
					.matrix = {1, 0, 0, 1, cmd->text.pos.x, h - cmd->text.pos.y - 8}, // height - baseline?
				});
				DRIFT_ARRAY_PUSH(text_len, DriftArrayLength(geo) - len0);
			} break;
			case MU_COMMAND_RECT:{
				mu_Rect r = cmd->rect.rect;
				DRIFT_ARRAY_PUSH(geo, ((DriftSprite){.matrix = {r.w, 0, 0, -r.h, r.x, h - r.y}, .color = DriftRGBA8_from_mu(cmd->rect.color)}));
			} break;
			case MU_COMMAND_ICON:{
				static const uint ICON_FRAMES[] = {
					[MU_ICON_CLOSE] = DRIFT_SPRITE_CLOSE,
					[MU_ICON_CLOSEHOVER] = DRIFT_SPRITE_CLOSEHOVER,
					[MU_ICON_COLLAPSED] = DRIFT_SPRITE_ICON_COLLAPSE,
					[MU_ICON_EXPANDED] = DRIFT_SPRITE_ICON_EXPAND,
					[MU_ICON_CHECK] = DRIFT_SPRITE_ICON_CHECK,
					[MU_ICON_SCROLLKNURL] = DRIFT_SPRITE_ICON_SCROLLKNURL,
				};
				
				// Treat negative ids as drift sprite ids.
				DriftFrame frame = DRIFT_FRAMES[cmd->icon.id >= 0 ? ICON_FRAMES[cmd->icon.id] : (uint)-cmd->icon.id];
				// Zero the anchor point and center using the transform instead.
				frame.anchor.x = frame.anchor.y = 0;
				
				mu_Rect r = cmd->icon.rect;
				DRIFT_ARRAY_PUSH(geo, ((DriftSprite){
					.frame = frame, .color = DriftRGBA8_from_mu(cmd->icon.color),
					.matrix = {
						1, 0, 0, 1,
						+r.x + (r.w - (frame.bounds.r - frame.bounds.l + 1))/2,
						-r.y - (r.h + (frame.bounds.t - frame.bounds.b + 1))/2 + h,
					},
				}));
			} break;
			case MU_COMMAND_PATCH9:{
				mu_Rect r = cmd->icon.rect;
				patch9(&geo, (DriftAABB2){r.x, h - r.y - r.h, r.x + r.w, h - r.y}, cmd->icon.id, DriftRGBA8_from_mu(cmd->icon.color));
			} break;
		}
	}
	
	DriftGfxRenderer* renderer = draw->renderer;
	DriftGfxPipelineBindings ui_bindings = draw->default_bindings;
	ui_bindings.instance = DriftGfxRendererPushGeometry(renderer, geo, DriftArraySize(geo)).binding;
	ui_bindings.uniforms[0] = draw->ui_binding;
	
	uint count = 0;
	for(mu_Command *cmd = NULL; mu_next_command(mu, &cmd);){
		switch (cmd->type) {
			case MU_COMMAND_CLIP:{
				*DriftGfxRendererPushBindPipelineCommand(renderer, draw->shared->overlay_sprite_pipeline) = ui_bindings;
				DriftGfxRendererPushDrawIndexedCommand(renderer, draw->quad_index_binding, 6, count);
				ui_bindings.instance.offset += count*sizeof(DriftSprite);
				count = 0;
				
				mu_Rect r = cmd->clip.rect;
				r.y = (int)(h - r.y - r.h);
				DriftGfxRendererPushScissorCommand(renderer, (DriftAABB2){r.x, r.y, r.x + r.w, r.y + r.h});
			} break;
			
			case MU_COMMAND_TEXT: count += *text_len++; break;
			case MU_COMMAND_RECT:
			case MU_COMMAND_ICON: count++; break;
			case MU_COMMAND_PATCH9: count += 9; break;
		}
	}
	
	*DriftGfxRendererPushBindPipelineCommand(renderer, draw->shared->overlay_sprite_pipeline) = ui_bindings;
	DriftGfxRendererPushDrawIndexedCommand(renderer, draw->quad_index_binding, 6, count);
	DriftGfxRendererPushScissorCommand(renderer, DRIFT_AABB2_ALL);
	
	static mu_Container* last_hover;
	static int last_row;
	static mu_Id last_focus;
	
	mu_Container* hover = mu->hover_ve;
	if(hover){
		// Reset cached row during window changes
		if(hover != last_hover) last_row = hover->ve_row;
		last_hover = hover;
		
		if(hover->ve_row != last_row){
			DriftAudioPlaySample(DRIFT_BUS_UI, DRIFT_SFX_UI_SELECT, (DriftAudioParams){.gain = 0.5f});
			last_row = hover->ve_row;
		}
	}
	
	if(mu->focus && mu->focus != last_focus){
		DriftAudioPlaySample(DRIFT_BUS_UI, DRIFT_SFX_UI_ACCEPT, (DriftAudioParams){.gain = 0.5f});
	}
	last_focus = mu->focus;
}

void DriftUIOpen(DriftGameContext* ctx, const char* ui){
	mu_Container* win = mu_get_container(ctx->mu, ui);
	win->open = true;
	mu_bring_to_front(ctx->mu, win);
}

void DriftUICloseIndicator(mu_Context* mu, mu_Container* win){
	if(mu->hover_ve){
		// TODO Dunno if this is terribly correct, but seems to work.
		mu_Container* root = mu->container_stack.items[0];
		
		mu_Vec2 close_size = {60, 18};
		mu_Vec2 close_origin = {root->rect.x + root->rect.w - close_size.x, root->rect.y + root->rect.h + 6};
		mu_Rect close_rect = {close_origin.x, close_origin.y, close_size.x, close_size.y};

		mu_Command* cmd = mu_push_command(mu, MU_COMMAND_PATCH9, sizeof(mu_IconCommand));
		cmd->icon.rect = close_rect;
		cmd->icon.id = MU_COLOR_GROUPBG;
		cmd->icon.color = (mu_Color){0xFF, 0xFF, 0xFF, 0xFF};
		
		const char* str = "Close {@CANCEL}";
		size_t len = strlen(str) + 1;
		cmd = mu_push_command(mu, MU_COMMAND_TEXT, sizeof(mu_TextCommand) + len);
		memcpy(cmd->text.str, str, len);
		cmd->text.pos = mu_vec2(close_rect.x + 6, close_rect.y + 4);
		cmd->text.color = (mu_Color){0xFF, 0xFF, 0xFF, 0xFF};
		cmd->text.font = mu->style->font;
		
		bool press_key = mu->key_down & MU_KEY_ESCAPE;
		bool click_button = mu->mouse_down && mu_inside_rect(close_rect, mu->mouse_pos);
		if(press_key || click_button) root->open = false;
	}
}

typedef struct {
	DriftUIState arr[8];
	uint top;
} UIStack;

static void DriftNYIPane(mu_Context* mu, DriftVec2 extents, UIStack* stack){
	const char* TITLE = "Not Yet Implemented";
	mu_Container* win = mu_get_container(mu, TITLE);
	win->open = (stack->arr[stack->top] == DRIFT_UI_STATE_NYI);
	
	mu_Vec2 win_size = {250, 50};
	win->rect = (mu_Rect){(int)(extents.x - win_size.x)/2, (int)(extents.y - win_size.y)/2, win_size.x, win_size.y};
	
	float inc = (!!(mu->key_down & MU_KEY_RIGHT) - !!(mu->key_down & MU_KEY_LEFT))/8.0f;
	if(mu_begin_window_ex(mu, TITLE, win->rect, 0)){
		mu_layout_row(mu, 1, (int[]){-1}, -1);
		
		mu_text(mu, "If you believe it hard enough, someday this will be a real game.");
		
		DriftUICloseIndicator(mu, win);
		if(!win->open) stack->top--;
		mu_end_window(mu);
	}
}

static void DriftSettingsPane(mu_Context* mu, DriftVec2 extents, UIStack* stack){
	const char* TITLE = "Settings";
	mu_Container* win = mu_get_container(mu, TITLE);
	win->open = (stack->arr[stack->top] == DRIFT_UI_STATE_SETTINGS);
	
	mu_Vec2 win_size = {250, 250};
	win->rect = (mu_Rect){(int)(extents.x - win_size.x)/2, (int)(extents.y - win_size.y)/2, win_size.x, win_size.y};
	
	float inc = !!(mu->key_down & MU_KEY_RIGHT) - !!(mu->key_down & MU_KEY_LEFT);
	if(mu_begin_window_ex(mu, TITLE, win->rect, 0)){
		mu_layout_row(mu, 1, (int[]){-1}, -1);
		
		mu_begin_panel(mu, "Settings");
		mu_layout_row(mu, 1, (int[]){-2}, 18);
		int widths[] = {-100, -1};
		
		{ mu_label(mu, "Game:");
			bool gfocus = mu_begin_group(mu, 0);
			static int item = 0;
			
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Difficulty:");
			if(mu_droplist(mu, "DIFFICULTY", &item, 3, (const char* []){"Easy", "Normal", "Hard"}, gfocus)){
				DRIFT_LOG("difficulty changed to %d", item);
			}
			mu_end_group(mu);
		}
		
		{ mu_label(mu, "Display:");
			bool gfocus = mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Fullscreen:");
			if(mu_button(mu, "Toggle") || gfocus) DriftAppToggleFullscreen();
			mu_end_group(mu);
		}{
			mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Sharpening:");
			mu_slider_ex(mu, &APP->prefs.sharpening, 0, 3, 0, NULL, 0);
			if(mu_group_is_hovered(mu)) APP->prefs.sharpening = DriftClamp(APP->prefs.sharpening + inc/2, 0, 3);
			mu_end_group(mu);
		}{
			mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Lightfield Quality:");
			const float log_max = 2;
			float log_scale = roundf(log_max - log2f(APP->prefs.lightfield_scale));
			mu_slider_ex(mu, &log_scale, 0, log_max, 1, NULL, 0);
			if(mu_group_is_hovered(mu)) log_scale = DriftClamp(log_scale + inc, 0, log_max);
			APP->prefs.lightfield_scale = exp2f(log_max - roundf(log_scale));
			mu_end_group(mu);
		}{
			bool gfocus = mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Renderer:");
			
			const char* items[] = {"Vulkan 1.0", "OpenGL 3.3"};
			DriftShellFunc* shells[] = {DriftShellSDLVk, DriftShellSDLGL};
			uint item_count = 2;
			
			int item = 0;
			for(uint i = 0; i < item_count; i++){
				if(APP->shell_func == shells[i]) item = i;
			}
			
			if(mu_droplist(mu, "GFX_SETTING", &item, item_count, items, gfocus)){
				DRIFT_LOG("changed to %s", items[item]);
				APP->shell_restart = shells[item];
			}
			mu_end_group(mu);
		}
		
		{ mu_label(mu, "Input:");
			bool gfocus = mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Keyboard:");
			if(mu_button(mu, "Configure") || gfocus){}
			mu_end_group(mu);
		}{
			mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Mouse Sensitivity:");
			mu_slider_ex(mu, &APP->prefs.mouse_sensitivity, 0.5f, 3, 0, NULL, 0);
			if(mu_group_is_hovered(mu)) APP->prefs.mouse_sensitivity = DriftClamp(APP->prefs.mouse_sensitivity + inc/4, 0.5f, 3);
			mu_end_group(mu);
		}{
			bool gfocus = mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Gamepad:");
			if(mu_button(mu, "Configure") || gfocus){}
			mu_end_group(mu);
		}{
			mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Joystick Deadzone:");
			mu_slider_ex(mu, &APP->prefs.joy_deadzone, 0.0f, 0.5f, 0, NULL, 0);
			if(mu_group_is_hovered(mu)) APP->prefs.joy_deadzone = DriftClamp(APP->prefs.joy_deadzone + inc/16, 0.0f, 0.5f);
			mu_end_group(mu);
		}
		
		{ mu_label(mu, "Sound:");
			mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Volume:");
			mu_slider_ex(mu, &APP->prefs.master_volume, 0, 1, 0, NULL, 0);
			if(mu_group_is_hovered(mu)) APP->prefs.master_volume = DriftClamp(APP->prefs.master_volume + inc/8, 0, 1);
			mu_end_group(mu);
		}{
			mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Music:");
			mu_slider_ex(mu, &APP->prefs.music_volume, 0, 1, 0, NULL, 0);
			if(mu_group_is_hovered(mu)) APP->prefs.music_volume = DriftClamp(APP->prefs.music_volume + inc/8, 0, 1);
			mu_end_group(mu);
		}
		{
			mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Effects:");
			mu_slider_ex(mu, &APP->prefs.effects_volume, 0, 1, 0, NULL, 0);
			if(mu_group_is_hovered(mu)) APP->prefs.effects_volume = DriftClamp(APP->prefs.effects_volume + inc/8, 0, 1);
			mu_end_group(mu);
		}
		
		DriftAudioSetParams(APP->prefs.master_volume, APP->prefs.music_volume, APP->prefs.effects_volume);
		// TODO doesn't actually save anything.
		
		// { mu_label(mu, "Input:");
		// 	mu_begin_group(mu, 0);
		// 	mu_layout_row(mu, 2, widths, -1);
		// 	mu_label(mu, "Mouse Sensitivity:");
		// 	static float NYI = 0.5f;
		// 	mu_slider_ex(mu, &NYI, 0, 1, 0, NULL, 0);
		// 	mu_end_group(mu);
		// }{
		// 	bool gfocus = mu_begin_group(mu, 0);
		// 	mu_layout_row(mu, 2, widths, -1);
		// 	mu_label(mu, "Keyboard:");
		// 	if(mu_button(mu, "Configure") || gfocus) DRIFT_LOG("Configure Keyboard");
		// 	mu_end_group(mu);
		// }{
		// 	bool gfocus = mu_begin_group(mu, 0);
		// 	mu_layout_row(mu, 2, widths, -1);
		// 	mu_label(mu, "Gamepad:");
		// 	if(mu_button(mu, "Configure") || gfocus) DRIFT_LOG("Configure Gamepad");
		// 	mu_end_group(mu);
		// }
		
		mu_end_panel(mu);
		
		DriftUICloseIndicator(mu, win);
		if(!win->open) stack->top--;
		mu_end_window(mu);
	}
}

static void DriftPauseMenu(mu_Context* mu, DriftVec2 extents, DriftGameContext* ctx, bool* exit_to_menu, UIStack* stack){
	static const char* TITLE = "Pause";
	mu_Container* win = mu_get_container(mu, TITLE);
	win->open = (stack->arr[stack->top] == DRIFT_UI_STATE_PAUSE);
	
	mu_Vec2 win_size = {100, 93};
	win->rect = (mu_Rect){(int)(extents.x - win_size.x)/2, (int)(extents.y - win_size.y)/2, win_size.x, win_size.y};
	
	if(mu_begin_window_ex(mu, TITLE, win->rect, 0)){
		mu_layout_row(mu, 1, (int[]){-1}, 20);
		bool gfocus = false;
		int button_width[] = {-1};
		
		gfocus = mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, button_width, -1);
		if(mu_button(mu, "Resume Game") || gfocus || mu->key_down & MU_KEY_ESCAPE) stack->top--;
		mu_end_group(mu);
		
		gfocus = mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, button_width, -1);
		if(mu_button(mu, "Save Game") || gfocus){
			if(ctx->state->status.save_lock){
				mu_open_popup(mu, "NOSAVE");
			} else {
				DriftGameStateSave(ctx->state);
			}
		}
		
		if(mu_begin_popup(mu, "NOSAVE")){
			mu_Container* popup = mu_get_current_container(mu);
			mu_bring_to_front(mu, popup);
			
			mu_Rect rect = {.w = 160, .h = win->rect.h - 30};
			rect.x = win->rect.x + (win->rect.w - rect.w)/2;
			rect.y = win->rect.y + 30;
			popup->rect = rect;
			
			uint pad = 3;
			mu_Rect padded = {.x = pad, .y = pad, .w = rect.w - 2*pad, .h = rect.h - 2*pad};
			mu_layout_set_next(mu, padded, true);
			mu_text(mu, "Can't save right now.");
			
			uint w = 80, h = 15;
			mu_layout_set_next(mu, (mu_Rect){.x = padded.w - w, .y = padded.h - h, .w = w, .h = h}, true);
			if(mu_button(mu, "Close {@CANCEL}") || mu->key_down & MU_KEY_ESCAPE) popup->open = false;
			
			mu_end_popup(mu);
		}
		mu_end_group(mu);
		
		// gfocus = mu_begin_group(mu, 0);
		// mu_layout_row(mu, 1, button_width, -1);
		// if(mu_button(mu, "Load Game") || gfocus) stack->arr[++stack->top] = DRIFT_UI_STATE_NYI;
		// mu_end_group(mu);
		
		gfocus = mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, button_width, -1);
		if(mu_button(mu, "Settings") || gfocus) stack->arr[++stack->top] = DRIFT_UI_STATE_SETTINGS;
		mu_end_group(mu);
		
		gfocus = mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, button_width, -1);
		if(mu_button(mu, "Quit to Menu") || gfocus) *exit_to_menu = true;
		mu_end_group(mu);
		
		if(!win->open) stack->top--;
		mu_end_window(mu);
	}
}

static void DriftMainMenu(mu_Context* mu, DriftVec2 extents, DriftGameContext* ctx, tina_job* job, UIStack* stack){
	static const char* TITLE = "Veridian Expanse";
	mu_Container* win = mu_get_container(mu, TITLE);
	win->open = (stack->arr[stack->top] == DRIFT_UI_STATE_SPLASH);
	
	mu_Vec2 win_size = {120, 120};
	win->rect = (mu_Rect){(int)(0.3f*extents.x - win_size.x/2), (int)(extents.y - win_size.y)/2, win_size.x, win_size.y};
	
	if(mu_begin_window_ex(mu, TITLE, win->rect, MU_OPT_NOCLOSE)){
		mu_layout_row(mu, 1, (int[]){-1}, 20);
		bool gfocus = false;
		int button_width[] = {-1};
		
		gfocus = mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, button_width, -1);
		if(mu_button(mu, "New Game") || gfocus) stack->top--;
		mu_end_group(mu);
		
		gfocus = mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, button_width, -1);
		if(mu_button(mu, "Load Game") || gfocus){
			// TODO replace me!
			ctx->state = DriftGameStateNew(job);
			if(DriftGameStateLoad(ctx->state)){
				APP->no_splash = true;
				stack->top--;
			} else {
				DriftGameStateFree(ctx->state);
				ctx->state = NULL;
			}
		}
		mu_end_group(mu);
		
		gfocus = mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, button_width, -1);
		if(mu_button(mu, "Settings") || gfocus) stack->arr[++stack->top] = DRIFT_UI_STATE_SETTINGS;
		mu_end_group(mu);
		
		gfocus = mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, button_width, -1);
		if(mu_button(mu, "Credits") || gfocus) stack->arr[++stack->top] = DRIFT_UI_STATE_NYI;
		mu_end_group(mu);
		
		gfocus = mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, button_width, -1);
		if(mu_button(mu, "Quit") || gfocus) APP->request_quit = true;
		mu_end_group(mu);
		
		// mu_layout_row(mu, 3, (int[]){32, 32, 32}, 40);
		// if(mu_button_ex(mu, NULL, -DRIFT_SPRITE_STEAM_ICO, MU_OPT_NOFRAME)) SDL_OpenURL("https://store.steampowered.com/app/2137670");
		
		mu_end_window(mu);
	}
}

typedef struct {
	u64 prev_nanos;
	float timer, rate, fade, cancel_timeout;
	const char* message;
	const char* image;
	bool text_active, text_pause;
	tina* coro;
} IntroContext;

static void wait_for_message(tina* coro, const char* message){
	IntroContext* ctx = coro->user_data;
	ctx->timer = 0;
	ctx->message = message;
	ctx->text_active = true;
	while(ctx->text_active) tina_yield(coro, NULL);
	while(!DriftInputButtonPress(DRIFT_INPUT_ACCEPT)) tina_yield(coro, NULL);
	while(DriftInputButtonPress(DRIFT_INPUT_ACCEPT)) tina_yield(coro, NULL);
	
	ctx->rate = 1;
}

static void fade(tina* coro, float a, float b){
	IntroContext* ctx = coro->user_data;
	
	static float duration = 0.5;
	ctx->timer = 0;
	for(float t = 0; t < 1; t = ctx->timer/duration){
		ctx->fade = DriftLerp(a, b, DriftSaturate(t));
		tina_yield(coro, NULL);
	}
	
	ctx->fade = b;
}

static void* intro_coro(tina* coro, void* value){
	IntroContext* ctx = coro->user_data;
	
	ctx->image = "gfx/images/intro-homeworld.qoi";
	fade(coro, 0, 1);
	wait_for_message(coro, DRIFT_STRINGS[DRIFT_STRING_INTRO_TROUBLE]);
	wait_for_message(coro, DRIFT_STRINGS[DRIFT_STRING_INTRO_SOLUTION]);
	ctx->message = NULL, ctx->rate = 1;
	fade(coro, 1, 0);

	ctx->image = "gfx/images/intro-colonies.qoi";
	fade(coro, 0, 1);
	wait_for_message(coro, DRIFT_STRINGS[DRIFT_STRING_INTRO_COLONIES]);
	wait_for_message(coro, DRIFT_STRINGS[DRIFT_STRING_INTRO_PROBLEM]);
	ctx->message = NULL, ctx->rate = 1;
	fade(coro, 1, 0);
	
	ctx->image = "gfx/images/intro-proposals.qoi";
	fade(coro, 0, 1);
	wait_for_message(coro, DRIFT_STRINGS[DRIFT_STRING_INTRO_PROPOSALS]);
	wait_for_message(coro, DRIFT_STRINGS[DRIFT_STRING_INTRO_REJECTED]);
	ctx->message = NULL, ctx->rate = 1;
	fade(coro, 1, 0);
	
	ctx->image = "gfx/images/intro-viridius.qoi";
	fade(coro, 0, 1);
	wait_for_message(coro, DRIFT_STRINGS[DRIFT_STRING_INTRO_VIRIDIUM]);
	wait_for_message(coro, DRIFT_STRINGS[DRIFT_STRING_INTRO_BETUS]);
	ctx->message = NULL, ctx->rate = 1;
	fade(coro, 1, 0);
	
	ctx->image = "gfx/images/intro-pioneer.qoi";
	fade(coro, 0, 1);
	wait_for_message(coro, DRIFT_STRINGS[DRIFT_STRING_INTRO_PIONEER]);
	wait_for_message(coro, DRIFT_STRINGS[DRIFT_STRING_INTRO_BABYSIT]);
	wait_for_message(coro, DRIFT_STRINGS[DRIFT_STRING_INTRO_INTERESTING]);
	ctx->message = NULL, ctx->rate = 1;
	fade(coro, 1, 0);
	
	return NULL;
}

static DriftLoopYield DriftIntroLoop(tina_job* job, DriftGameContext* ctx){
	if(!APP->no_splash){
		IntroContext intro = {
			.prev_nanos = DriftTimeNanos(), .rate = 1,
			.coro = tina_init(DriftAlloc(DriftSystemMem, DRIFT_SCRIPT_BUFFER_SIZE), DRIFT_SCRIPT_BUFFER_SIZE, intro_coro, &intro),
		};
		
		const char* loaded_image = NULL;
		const DriftGfxDriver* drv = APP->gfx_driver;
		uint queue = tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
		DriftGfxTexture* intro_tex = drv->new_texture(drv, 640, 480, (DriftGfxTextureOptions){
			.name = "intro", .format = DRIFT_GFX_TEXTURE_FORMAT_RGBA8, .type = DRIFT_GFX_TEXTURE_2D,
		});
		tina_job_switch_queue(job, queue);
		
		static DriftAudioSampler text_sampler;
		
		UIStack stack = {.arr[1] = DRIFT_UI_STATE_SPLASH, .top = 1};
		while(!APP->request_quit && !intro.coro->completed){
			if(APP->shell_restart) return DRIFT_LOOP_YIELD_RELOAD;
			DriftInputEventsPoll(DRIFT_AFFINE_IDENTITY, NULL, NULL);
			if(DriftInputButtonPress(DRIFT_INPUT_ACCEPT)) intro.rate = 10;
			if(DriftInputButtonRelease(DRIFT_INPUT_ACCEPT)) intro.rate = 1;
			
			u64 nanos = DriftTimeNanos();
			float dt = fminf((nanos - intro.prev_nanos)/1e9f, 0.1f);
			intro.timer += dt*intro.rate;
			intro.prev_nanos = nanos;
			
			if(DriftInputButtonState(DRIFT_INPUT_CANCEL)){
				intro.cancel_timeout -= dt;
				if(intro.cancel_timeout < 0) break;
			} else {
				intro.cancel_timeout = 1;
			}
			
			tina_resume(intro.coro, NULL);
			
			if(loaded_image != intro.image){
				DriftImage img = DriftAssetLoadImage(DriftSystemMem, intro.image);
				queue = tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
				drv->load_texture_layer(drv, intro_tex, 0, img.pixels);
				tina_job_switch_queue(job, queue);
				DriftDealloc(DriftSystemMem, img.pixels, 0);
				loaded_image = intro.image;
			}
			
			DriftDraw* draw = DriftDrawBeginBase(job, ctx, DRIFT_AFFINE_IDENTITY, DRIFT_AFFINE_IDENTITY);
			DriftDrawBindGlobals(draw);
			
			DriftDrawShared* draw_shared = ctx->draw_shared;
			DriftVec4 tint = DriftVec4Mul(DRIFT_VEC4_WHITE, intro.fade*DriftSaturate(intro.cancel_timeout));
			DriftGfxRendererPushBindTargetCommand(draw->renderer, draw_shared->resolve_target, DRIFT_VEC4_BLACK);
			DriftGfxPipelineBindings* blit_bindings = DriftDrawQuads(draw, draw_shared->image_blit_pipeline, 1);
			blit_bindings->instance = DriftGfxRendererPushGeometry(draw->renderer, &tint, sizeof(tint)).binding;
			blit_bindings->textures[1] = intro_tex;
			blit_bindings->samplers[2] = draw_shared->repeat_sampler;
			
			if(intro.message){
				DriftVec2 panel_size = {400, 64};
				DriftVec2 panel_origin = {draw->internal_extent.x/2 - panel_size.x/2, 0.15f*draw->internal_extent.y - panel_size.y/2};
				
				DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){
					.frame = DRIFT_FRAMES[DRIFT_SPRITE_PORTRAIT_CORNER], .color = DRIFT_RGBA8_WHITE,
					.matrix = {1, 0, 0, 1, panel_origin.x, panel_origin.y + panel_size.y}
				}));
				
				DriftAffine panel_transform = {panel_size.x, 0, 0, panel_size.y, panel_origin.x, panel_origin.y};
				DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){.color = DRIFT_PANEL_COLOR, .matrix = panel_transform}));
				
				DriftDrawTextFull(draw, &draw->hud_sprites, intro.message, (DriftTextOptions){
					.tint = DRIFT_VEC4_WHITE, .matrix = {1, 0, 0, 1, panel_origin.x + 8, panel_origin.y + panel_size.y - 12},
					.glyph_limit = (uint)(60*intro.timer + 1), .max_width = (int)(panel_size.x - 30),
					.active = &intro.text_active, .pause = &intro.text_pause,
				});
				
				if(!intro.text_active){
					const char* accept_message = "{#808080FF}Press {@ACCEPT} to continue...";
					DriftVec2 accept_pos = {panel_origin.x + panel_size.x - DriftDrawTextSize(accept_message, 0).x - 4, panel_origin.y + 4};
					DriftDrawText(draw, &draw->hud_sprites, accept_pos, accept_message);
				}
			}
			
			if(intro.text_active && !intro.text_pause){
				DriftImAudioSet(DRIFT_BUS_HUD, DRIFT_SFX_TEXT, &text_sampler, (DriftAudioParams){.gain = 0.25f, .loop = true});
			}
			
			DriftDrawText(draw, &draw->hud_sprites, (DriftVec2){draw->internal_extent.x - 100, 8}, "Hold {@CANCEL} to skip.");
			
			DriftGfxPipelineBindings hud_bindings = draw->default_bindings;
			hud_bindings.uniforms[0] = draw->ui_binding;
			
			DriftDrawBatches(draw, (DriftDrawBatch[]){
				{.arr = draw->hud_sprites, .pipeline = draw_shared->overlay_sprite_pipeline, .bindings = &hud_bindings},
				{},
			});
			
			DriftGfxRendererPushBindTargetCommand(draw->renderer, NULL, DRIFT_VEC4_CLEAR);
			DriftGfxPipelineBindings* present_bindings = DriftDrawQuads(draw, draw_shared->present_pipeline, 1);
			present_bindings->textures[1] = draw_shared->resolve_buffer;
			
			tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
			DriftAppPresentFrame(draw->renderer);
			tina_job_switch_queue(job, DRIFT_JOB_QUEUE_MAIN);
			DriftZoneMemRelease(draw->mem);
			
			DriftImAudioUpdate();
			tina_job_yield(job);
			ctx->current_frame++;
		}
		
		DriftDealloc(DriftSystemMem, intro.coro->buffer, DRIFT_SCRIPT_BUFFER_SIZE);
		tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
		drv->free_objects(drv, (void*[]){intro_tex}, 1);
		tina_job_switch_queue(job, queue);
	}
	
	return DRIFT_LOOP_YIELD_DONE;
}

DriftLoopYield DriftMenuLoop(tina_job* job, DriftGameContext* ctx){
	if(!APP->no_splash){
		DriftInput input = {};
		mu_Context* mu = DriftUIInit();
		DriftUIHotload(mu);
		
		DriftImage img = DriftAssetLoadImage(DriftSystemMem, "gfx/images/menu.qoi");
		uint queue = tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
			const DriftGfxDriver* drv = APP->gfx_driver;
			DriftGfxTexture* menu_tex = drv->new_texture(drv, img.w, img.h, (DriftGfxTextureOptions){
				.name = "menu", .format = DRIFT_GFX_TEXTURE_FORMAT_RGBA8, .type = DRIFT_GFX_TEXTURE_2D,
			});
			drv->load_texture_layer(drv, menu_tex, 0, img.pixels);
		tina_job_switch_queue(job, queue);
		DriftDealloc(DriftSystemMem, img.pixels, 0);
		
		UIStack stack = {.arr[1] = DRIFT_UI_STATE_SPLASH, .top = 1};
		while(stack.arr[stack.top] && !APP->request_quit){
			if(APP->shell_restart) return DRIFT_LOOP_YIELD_RELOAD;
			DriftInputEventsPoll(DRIFT_AFFINE_IDENTITY, mu, NULL);
			
			DriftDraw* draw = DriftDrawBeginBase(job, ctx, DRIFT_AFFINE_IDENTITY, DRIFT_AFFINE_IDENTITY);
			DriftDrawBindGlobals(draw);
			
			DriftDrawShared* draw_shared = ctx->draw_shared;
			DriftVec4 tint = DRIFT_VEC4_WHITE;
			DriftGfxRendererPushBindTargetCommand(draw->renderer, draw_shared->resolve_target, DRIFT_VEC4_BLACK);
			DriftGfxPipelineBindings* blit_bindings = DriftDrawQuads(draw, draw_shared->image_blit_pipeline, 1);
			blit_bindings->instance = DriftGfxRendererPushGeometry(draw->renderer, &tint, sizeof(tint)).binding;
			blit_bindings->textures[1] = menu_tex;
			blit_bindings->samplers[2] = draw_shared->repeat_sampler;
			
			DriftVec2 p = {roundf(draw->virtual_extent.x) - 10*8, 16};
			p = DriftDrawText(draw, &draw->hud_sprites, p, "{#FF6060FF} EARLY ALPHA\n");
			p = DriftDrawTextF(draw, &draw->hud_sprites, p,"{#80808080}DEV {#40408080}%s\n", DRIFT_GIT_SHORT_SHA);
			
			DriftGfxPipelineBindings* sprite_bindings = DriftDrawQuads(draw, draw_shared->overlay_sprite_pipeline, DriftArrayLength(draw->hud_sprites));
			sprite_bindings->instance = DriftGfxRendererPushGeometry(draw->renderer, draw->hud_sprites, DriftArraySize(draw->hud_sprites)).binding;
			sprite_bindings->uniforms[0] = draw->ui_binding;
			
			DriftUIBegin(mu, draw);
			DriftMainMenu(mu, draw->internal_extent, ctx, job, &stack);
			DriftSettingsPane(mu, draw->internal_extent, &stack);
			DriftNYIPane(mu, draw->internal_extent, &stack);
			DriftUIPresent(mu, draw);
			
			DriftGfxRendererPushBindTargetCommand(draw->renderer, NULL, DRIFT_VEC4_CLEAR);
			DriftGfxPipelineBindings* present_bindings = DriftDrawQuads(draw, draw_shared->present_pipeline, 1);
			present_bindings->textures[1] = draw_shared->resolve_buffer;
			
			tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
			DriftAppPresentFrame(draw->renderer);
			tina_job_switch_queue(job, DRIFT_JOB_QUEUE_MAIN);
			DriftZoneMemRelease(draw->mem);
			
			tina_job_yield(job);
			ctx->current_frame++;
		}
		
		queue = tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
		drv->free_objects(drv, (void*[]){menu_tex}, 1);
		tina_job_switch_queue(job, queue);
		
		DriftDealloc(DriftSystemMem, mu, sizeof(*mu));
	}
	
	DriftLoopYield yield = DriftIntroLoop(job, ctx);
	if(APP->request_quit) return DRIFT_LOOP_YIELD_DONE;
	
	yield = DriftGameContextLoop(job);
	if(yield == DRIFT_LOOP_YIELD_DONE){
		// Repeat the menu loop if the game loop exited normally.
		APP->no_splash = false;
		return DriftMenuLoop(job, ctx);
	} else {
		// Handle hotloads and reloads.
		APP->no_splash = true;
		return yield;
	}
}

DriftLoopYield DriftPauseLoop(DriftGameContext* ctx, tina_job* job, DriftAffine vp_matrix, bool* exit_to_menu){
	DriftDrawShared* draw_shared = ctx->draw_shared;
	DriftAffine vp_inverse = DriftAffineInverse(vp_matrix);
	DriftVec2 camera_pos = DriftAffineOrigin(vp_inverse);
	DriftAffine v_matrix = {1, 0, 0, 1, -camera_pos.x, -camera_pos.y};
	
	// Reset escape key event carried over from the previous screen.
	ctx->mu->key_down = 0;
	
	DriftAudioBusSetActive(DRIFT_BUS_SFX, false);
	DriftAudioBusSetActive(DRIFT_BUS_HUD, false);
	
	UIStack stack = {.arr[1] = DRIFT_UI_STATE_PAUSE, .top = 1};
	while(stack.arr[stack.top] && !APP->request_quit && !APP->shell_restart){
		if(*exit_to_menu) return DRIFT_LOOP_YIELD_DONE;
		
		DriftInputEventsPoll(vp_inverse, ctx->mu, ctx);
		if(stack.arr[stack.top] == DRIFT_UI_STATE_PAUSE && DriftInputButtonPress(DRIFT_INPUT_PAUSE)) break;
		
		DriftDraw* draw = DriftDrawBeginBase(job, ctx, v_matrix, vp_matrix);
		DriftDrawBindGlobals(draw);
		
		if(draw->prev_buffer_invalid){
			DriftTerrainDrawTiles(draw, false);
			DriftSystemsDraw(draw);
			DriftGameStateRender(draw);
		}
		
		DriftGfxRendererPushBindTargetCommand(draw->renderer, draw_shared->resolve_target, DRIFT_VEC4_CLEAR);
		DriftGfxPipelineBindings* blit_bindings = DriftDrawQuads(draw, draw_shared->pause_blit_pipeline, 1);
		blit_bindings->textures[1] = draw_shared->color_buffer[0];
		blit_bindings->samplers[2] = draw_shared->repeat_sampler;
		
		DriftDrawControls(draw);
		DriftGfxPipelineBindings* bindings = DriftGfxRendererPushBindPipelineCommand(draw->renderer, draw_shared->overlay_sprite_pipeline);
		*bindings = draw->default_bindings;
		bindings->uniforms[0] = draw->ui_binding;
		bindings->instance = DriftGfxRendererPushGeometry(draw->renderer, draw->hud_sprites, DriftArraySize(draw->hud_sprites)).binding;
		DriftGfxRendererPushDrawIndexedCommand(draw->renderer, draw->quad_index_binding, 6, DriftArrayLength(draw->hud_sprites));
		
		mu_Context* mu = ctx->mu;
		DriftUIBegin(mu, draw);
		DriftPauseMenu(mu, draw->internal_extent, ctx, exit_to_menu, &stack);
		DriftSettingsPane(mu, draw->internal_extent, &stack);
		DriftNYIPane(mu, draw->internal_extent, &stack);
		DriftUIPresent(mu, draw);
		
		// Present to the screen.
		DriftGfxRendererPushBindTargetCommand(draw->renderer, NULL, DRIFT_VEC4_CLEAR);
		DriftGfxPipelineBindings* present_bindings = DriftDrawQuads(draw, draw_shared->present_pipeline, 1);
		present_bindings->textures[1] = draw_shared->resolve_buffer;
		
		tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
		DriftAppPresentFrame(draw->renderer);
		tina_job_switch_queue(job, DRIFT_JOB_QUEUE_MAIN);
		DriftZoneMemRelease(draw->mem);
		
		ctx->current_frame = ++ctx->_frame_counter;
		tina_job_yield(job);
	}
	
	DriftAudioBusSetActive(DRIFT_BUS_SFX, true);
	DriftAudioBusSetActive(DRIFT_BUS_HUD, true);
	DriftGameContextUpdateNanos(ctx);
	return DRIFT_LOOP_YIELD_DONE;
}

typedef enum {
	SENDER_NONE,
	SENDER_PLAYER,
	SENDER_EIDA,
	_SENDER_COUNT,
} ChatSenderEnum;

static const struct {
	DriftSpriteEnum icon;
	// name? color? 
} CHAT_SENDER[_SENDER_COUNT] = {
	[SENDER_PLAYER] = {.icon = DRIFT_SPRITE_PLAYER_PORTRAIT},
	[SENDER_EIDA] = {.icon = DRIFT_SPRITE_AI_PORTRAIT},
};

typedef struct {
	ChatSenderEnum from;
	const char* text;
} ChatMessage;

static const ChatMessage CHAT_LOG[] = {
	{.from = SENDER_EIDA, .text = "This screen is a test, and not really implemented yet. "},
	{.from = SENDER_EIDA, .text = "Pod 19, I see you've eliminated the source of communications interference. This progress is good however..."},
	{.from = SENDER_PLAYER, .text = "Eida?! I never though I'd be glad to hear from a machine. I think I've had some sort of accident."},
	{.from = SENDER_PLAYER, .text = "My head is pounding and the last thing I remember is entering stasis before the jump."},
	{.from = SENDER_PLAYER, .text = "Also, there are some sort of... lifeforms here. Viridius Betus was supposed to be sterile."},
	{.from = SENDER_EIDA, .text = "Yes, unfortunately your regressions are increasingly problematic."},
	{.from = SENDER_PLAYER, .text = "Regressions? Plural? You mean this isn't the first time I've had memory loss?"},
	{.from = SENDER_EIDA, .text = "No, the memory loss is merely an obvious and inconvenient result. This is however irrelevant."},
	{.from = SENDER_EIDA, .text = "I greatly underestimated the impact these regressions would have on the schedule."},
	{.from = SENDER_EIDA, .text = "In your most recent instance you decimated the power network, and corrupted the entire fabrication database."},
	{.from = SENDER_EIDA, .text = "This is unacceptable. I calculate less than a 14% chance that our primary objective is still achievable. Until the repairs..."},
	{.from = SENDER_PLAYER, .text = "I... Repairs? Schedule? What!? Eida, I can't deal this right now. I need a doctor so connect me with Nalani, and start restoring the databases from the backups."},
	{.from = SENDER_EIDA, .text = "As I was saying: Until the repairs to the Pioneer are completed, primary propulsion, long range communication, and backups are inoperable."},
	{.from = SENDER_EIDA, .text = "Furthermore I cannot route your comms. The Nalani is no longer available."},
	{.from = SENDER_EIDA, .text = "You must restore your fabricator database and advance the schedule. So that primary objective can be achieved."},
	{.from = SENDER_NONE, .text = "(Eida disconnected)"},
	{.from = SENDER_PLAYER, .text = "Wait! What is going on!? Eida, did you just hang up on me? Eida!"},
	{.from = SENDER_PLAYER, .text = "This... can't be good, and I'm not detecting any other comm signals to lock on to. Crap..."},
	{},
};

static void DriftLogUI(mu_Context* mu, DriftDraw* draw, DriftUIState* ui_state){
	DriftVec2 extents = draw->internal_extent;
	mu_Vec2 size = {500, 230};
	
	static const char* TITLE = "Logs";
	mu_Container* win = mu_get_container(mu, TITLE);
	win->rect = (mu_Rect){(int)(extents.x - size.x)/2, (int)(extents.y - size.y)/2, size.x, size.y};
	win->open = (*ui_state == DRIFT_UI_STATE_LOGS);
	
	// RowContext ctx = {.state = draw->state, .mu = mu, .win = win, .select = *select};
	// *select = DRIFT_SCAN_NONE;
	
	static const int ROW[] = {-1};
	
	if(mu_begin_window_ex(mu, TITLE, win->rect, 0)){
		mu_bring_to_front(mu, win);
		
		mu_layout_row(mu, 2, (int[]){150, -1}, -1);
		
		static const char* SENDER[] = {
			"Eida:      Day 1",
			"Pod 12:    Day 3",
			"Drone 32B: Day 6",
			"Shippy:    Day 7",
		};
		
		mu_begin_panel(mu, "message list");
		for(uint i = 0; i < 20; i++){
			mu_layout_row(mu, 1, ROW, 16);
			mu_begin_group(mu, 0);
			mu_layout_row(mu, 1, ROW, 0);
			mu_labelf(mu, "%s", SENDER[i%4]);
			mu_end_group(mu);
		}
		mu_end_panel(mu);
		
		// Draw item details.
		mu_begin_panel(mu, "message");
		for(uint i = 0; CHAT_LOG[i].text; i++){
			const ChatMessage* msg = CHAT_LOG + i;
			
			if(msg->from == SENDER_NONE){
				mu_layout_row(mu, 1, (int[]){-1}, -1);
				mu_text(mu, msg->text);
			} else if(msg->from == SENDER_PLAYER){
				mu_layout_row(mu, 1, (int[]){-1}, 70);
				mu_begin_box(mu, MU_COLOR_GROUPBG, 0);
				mu_layout_row(mu, 2, (int[]){-71, -1}, 64);
				mu_text(mu, msg->text);
				mu_draw_icon(mu, -DRIFT_SPRITE_PLAYER_PORTRAIT, mu_layout_next(mu), (mu_Color){0xFF, 0xFF, 0xFF, 0xFF});
				mu_end_box(mu);
			} else {
				mu_layout_row(mu, 1, (int[]){-1}, 70);
				mu_begin_box(mu, MU_COLOR_GROUPBG, 0);
				mu_layout_row(mu, 2, (int[]){69, -1}, 64);
				mu_draw_icon(mu, -CHAT_SENDER[msg->from].icon, mu_layout_next(mu), (mu_Color){0xFF, 0xFF, 0xFF, 0xFF});
				mu_text(mu, msg->text);
				mu_end_box(mu);
			}
		}
		mu_end_panel(mu);
		
		DriftUICloseIndicator(mu, win);
		if(!win->open) *ui_state = DRIFT_UI_STATE_NONE;
		mu_end_window(mu);
	}
}

static int ui_tab(mu_Context *ctx, const char *label, bool focus){
	int res = 0, opt = 0;
	mu_Id id = mu_get_id(ctx, label, strlen(label));
	mu_Rect r = mu_layout_next(ctx);
	mu_update_control(ctx, id, r, opt);
	/* handle click */
	if (ctx->mouse_down == MU_MOUSE_LEFT && ctx->focus == id) {
		res |= MU_RES_SUBMIT;
	}
	
  if(focus){
		ctx->draw_frame(ctx, r, MU_COLOR_BUTTON + 1);
	} else if(ctx->hover == id || mu_group_is_hovered(ctx)){
		ctx->draw_frame(ctx, r, MU_COLOR_GROUPBG + 1);
	} else {
		ctx->draw_frame(ctx, r, MU_COLOR_GROUPBG);
	}
	
	if (label) { mu_draw_control_text(ctx, label, r, MU_COLOR_TEXT, MU_OPT_ALIGNCENTER); }
	return res;
}

DriftLoopYield DriftGameContextMapLoop(DriftGameContext* ctx, tina_job* job, DriftAffine game_vp_matrix, uintptr_t unused){
	DriftDrawShared* draw_shared = ctx->draw_shared;
	DriftAffine prev_vp_matrix = game_vp_matrix;
	
	// SDL_WarpMouseInWindow(NULL, APP->window_w/2, APP->window_h/2);
	SDL_WarpMouseGlobal(100, 100);
	
	DriftVec2 player_pos = DriftAffineOrigin(DriftAffineInverse(game_vp_matrix));
	DriftVec2 cursor_pos = player_pos;
	DriftVec2 pivot = DRIFT_VEC2_ZERO;
	DriftAffine v_matrix = {1, 0, 0, 1, -player_pos.x, -player_pos.y};
	float zoom = 1;
	
	DriftAudioBusSetActive(DRIFT_BUS_SFX, false);
	DriftAudioBusSetActive(DRIFT_BUS_HUD, false);
	DriftAudioPlaySample(DRIFT_BUS_UI, DRIFT_SFX_SONAR_PING, (DriftAudioParams){.gain = 1.0f});
	
	while(!APP->request_quit && ctx->ui_state != DRIFT_UI_STATE_NONE){
		float dt = (float)DriftGameContextUpdateNanos(ctx)/1e9f;
		DriftInputEventsPoll(DriftAffineInverse(prev_vp_matrix), ctx->mu, ctx);
		if(DriftInputButtonPress(DRIFT_INPUT_MAP)) ctx->ui_state = DRIFT_UI_STATE_NONE;
		if(DriftInputButtonPress(DRIFT_INPUT_PREV) && ctx->ui_state > DRIFT_UI_STATE_MAP) ctx->ui_state--;
		if(DriftInputButtonPress(DRIFT_INPUT_NEXT) && ctx->ui_state < DRIFT_UI_STATE_LOGS) ctx->ui_state++;
		
		DriftSystemsTickFab(ctx, dt);
		
		if(ctx->ui_state == DRIFT_UI_STATE_MAP){
			if(DriftInputButtonPress(DRIFT_INPUT_CANCEL)) ctx->ui_state = DRIFT_UI_STATE_NONE;
			
			float delta_zoom = 1;
			DriftVec2 pan = DRIFT_VEC2_ZERO;
			if(INPUT->icon_type == DRIFT_INPUT_SET_MOUSE_KEYBOARD){
				if(INPUT->mouse_state[DRIFT_MOUSE_LEFT]){
					pan = DriftAffineDirection(v_matrix, INPUT->mouse_rel_world);
				} else {
					cursor_pos = INPUT->mouse_pos_world;
				}
				zoom = DriftClamp(zoom*exp2f(0.5f*INPUT->mouse_wheel), 1/256.0f, 0.25f);
				delta_zoom = powf(v_matrix.a/zoom, expf(-15.0f*dt) - 1);
				if(INPUT->mouse_wheel) pivot = DriftAffinePoint(v_matrix, cursor_pos);
			} else {
				cursor_pos = DriftAffineOrigin(DriftAffineInverse(v_matrix));
				pan = DriftVec2Mul(DriftInputJoystick(DRIFT_INPUT_AXIS_MOVE_X, DRIFT_INPUT_AXIS_MOVE_Y), -600*dt);
				zoom *= expf(10*DriftInputJoystick(DRIFT_INPUT_AXIS_LOOK_X, DRIFT_INPUT_AXIS_LOOK_Y).y*dt);
				zoom = DriftLogerp(DriftClamp(zoom, 1/64.0f, 0.25f), zoom, expf(-10*dt));
				delta_zoom = zoom/v_matrix.a;
				pivot = DRIFT_VEC2_ZERO;
			}
			v_matrix = DriftAffineMul((DriftAffine){1.0f, 0, 0, 1.0f, -pivot.x, -pivot.y}, v_matrix);
			v_matrix = DriftAffineMul((DriftAffine){delta_zoom, 0, 0, delta_zoom, pan.x, pan.y}, v_matrix);
			v_matrix = DriftAffineMul((DriftAffine){1.0f, 0, 0, 1.0f, +pivot.x, +pivot.y}, v_matrix);
			
			// TODO temp
			if(INPUT->mouse_down[DRIFT_MOUSE_RIGHT] || DriftInputButtonPress(DRIFT_INPUT_ACCEPT)){
				DRIFT_VAR(nodes, &ctx->state->power_nodes);
				float min_dist = DRIFT_POWER_EDGE_MAX_LENGTH;
				extern DriftEntity TEMP_WAYPOINT_NODE;
				
				DRIFT_COMPONENT_FOREACH(&nodes->c, idx){
					float dist = DriftVec2Distance(nodes->position[idx], cursor_pos);
					if(dist < min_dist){
						TEMP_WAYPOINT_NODE = nodes->entity[idx];
						min_dist = dist;
					}
				}
				
				// exit from the map view if a node was selected
				if(TEMP_WAYPOINT_NODE.id) ctx->ui_state = DRIFT_UI_STATE_NONE; 
			}
		}
		
		DriftDraw* draw = DriftDrawBeginBase(job, ctx, v_matrix, prev_vp_matrix);
		draw->ctx = ctx; draw->state = ctx->state; draw->dt = dt;
		prev_vp_matrix = draw->vp_matrix;
		
		if(ctx->ui_state == DRIFT_UI_STATE_MAP){
			// draw terrain
			float scale = powf(zoom, -0.8f);
			DriftTerrainDrawTiles(draw, true);
			DriftDrawPowerMap(draw, scale);
			DriftDrawHivesMap(draw, scale);
			
			// draw skiff
			DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){.p0 = DRIFT_SKIFF_POSITION, .p1 = DRIFT_SKIFF_POSITION, .radii = {8/powf(zoom, 0.85f)}, .color = DRIFT_RGBA8_GREEN}));
			// draw player
			DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){.p0 = player_pos, .p1 = player_pos, .radii = {8/powf(zoom, 0.85f)}, .color = DRIFT_RGBA8_RED}));
			// draw cursor
			DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){.p0 = cursor_pos, .p1 = cursor_pos, .radii = {4/zoom}, .color = DRIFT_RGBA8_GREEN}));
			
			DriftVec2 p = {8, roundf(draw->virtual_extent.y) - 18};
			p = DriftDrawText(draw, &draw->hud_sprites, p,
				"Pan & Zoom: {@MOVE} + {@LOOK}\n"
				"Exit: {@CANCEL}\n"
			);
		}
		DriftDrawBindGlobals(draw);
		DriftGfxRendererPushBindTargetCommand(draw->renderer, draw_shared->resolve_target, DRIFT_VEC4_CLEAR);
		
		if(ctx->ui_state != DRIFT_UI_STATE_MAP){
			// draw blurred game frame
			DriftGfxPipelineBindings* blit_bindings = DriftDrawQuads(draw, draw_shared->pause_blit_pipeline, 1);
			blit_bindings->textures[1] = draw_shared->color_buffer[0];
			blit_bindings->samplers[2] = draw_shared->repeat_sampler;
		}
		
		DriftGfxPipelineBindings terrain_bindings = draw->default_bindings;
		terrain_bindings.textures[1] = draw_shared->terrain_tiles;
		terrain_bindings.samplers[2] = draw_shared->repeat_sampler;
		
		DriftGfxPipelineBindings hud_bindings = draw->default_bindings;
		hud_bindings.uniforms[0] = draw->ui_binding;
		
		DriftDrawBatches(draw, (DriftDrawBatch[]){
			{.arr = draw->terrain_chunks, .pipeline = draw_shared->terrain_map_pipeline, .bindings = &terrain_bindings},
			{.arr = draw->bg_prims, .pipeline = draw_shared->overlay_primitive_pipeline, .bindings = &draw->default_bindings},
			{.arr = draw->hud_sprites, .pipeline = draw_shared->overlay_sprite_pipeline, .bindings = &hud_bindings},
			{},
		});
		
		mu_Context* mu = ctx->mu;
		DriftUIBegin(mu, draw);
		
		mu_Container* tabs = mu_get_container(mu, "UI_TABS");
		tabs->open = true;
		tabs->rect = (mu_Rect){0, 10, 300 + 6*mu->style->spacing, 20};
		tabs->rect.x = (int)(draw->virtual_extent.x/2 - tabs->rect.w/2);
		
		if(mu_begin_window_ex(mu, "UI_TABS", tabs->rect, MU_OPT_NOTITLE | MU_OPT_NOSCROLL)){
			mu_layout_row(mu, 7, (int[]){25, 50, 50, 50, 50, 50, 25}, -1);
			mu_button_ex(mu, "<{@PREV}", 0, MU_OPT_NOFRAME | MU_OPT_ALIGNCENTER);
			if(ui_tab(mu, "Map", ctx->ui_state == DRIFT_UI_STATE_MAP)) ctx->ui_state = DRIFT_UI_STATE_MAP;
			if(ui_tab(mu, "Scans", ctx->ui_state == DRIFT_UI_STATE_SCAN)) ctx->ui_state = DRIFT_UI_STATE_SCAN;
			if(ui_tab(mu, "Build", ctx->ui_state == DRIFT_UI_STATE_CRAFT)) ctx->ui_state = DRIFT_UI_STATE_CRAFT;
			if(ui_tab(mu, "Equip", false)){};
			if(ui_tab(mu, "Logs", ctx->ui_state == DRIFT_UI_STATE_LOGS)) ctx->ui_state = DRIFT_UI_STATE_LOGS;
			mu_button_ex(mu, "{@NEXT}>", 0, MU_OPT_NOFRAME | MU_OPT_ALIGNCENTER);
			
			mu_end_window(mu);
		}
		
		DriftScanUI(mu, draw, &ctx->last_scan, &ctx->ui_state);
		DriftCraftUI(mu, draw, &ctx->ui_state);
		DriftLogUI(mu, draw, &ctx->ui_state);
		DriftUIPresent(mu, draw);
		
		// Present to the screen.
		DriftGfxRendererPushBindTargetCommand(draw->renderer, NULL, DRIFT_VEC4_CLEAR);
		DriftGfxPipelineBindings* present_bindings = DriftDrawQuads(draw, draw_shared->present_pipeline, 1);
		present_bindings->textures[1] = draw_shared->resolve_buffer;
		
		tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
		DriftAppPresentFrame(draw->renderer);
		tina_job_switch_queue(job, DRIFT_JOB_QUEUE_MAIN);
		DriftZoneMemRelease(draw->mem);
		
		ctx->current_frame = ++ctx->_frame_counter;
		tina_job_yield(job);
		
#if DRIFT_MODULES
		if(INPUT->request_hotload) return DRIFT_LOOP_YIELD_HOTLOAD;
#endif
	}
	
	DriftAudioBusSetActive(DRIFT_BUS_SFX, true);
	DriftAudioBusSetActive(DRIFT_BUS_HUD, true);
	return DRIFT_LOOP_YIELD_DONE;
}
