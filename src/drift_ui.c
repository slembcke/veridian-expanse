#include <stdio.h>

#include <SDL.h>

#include "drift_game.h"
#include "microui/microui.h"

static const uint UI_LINE_HEIGHT = 10;

static int text_width(mu_Font font, const char *text, int len){
	// TODO +1 because bounds doesn't include the final advance, but MicroUI expects it.
	return (int)DriftDrawTextSize(text, len > 0 ? len : 0, font).x + 1;
}

static int text_height(mu_Font font){
  return 10;
}

static void draw_patch9(mu_Context *ctx, mu_Rect rect, int colorid){
	mu_Rect clip = mu_intersect_rects(rect, mu_get_clip_rect(ctx));
	if (clip.w > 0 && clip.h > 0) {
		mu_Command *cmd = mu_push_command(ctx, MU_COMMAND_PATCH9, sizeof(mu_Patch9Command));
		cmd->patch9.rect = rect;
		cmd->patch9.id = colorid;
		cmd->patch9.color = (mu_Color){0xFF, 0xFF, 0xFF, 0xFF};
	}
}

static const mu_Style STYLE = {
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
};

static const struct {
	uint frame;
	DriftAABB2 border, split;
} PATCHES[MU_COLOR_MAX] = {
	[MU_COLOR_WINDOWBG] = {.frame = DRIFT_SPRITE_UI_WINDOW, {2, 3, 2, 3}, {2, 3, 2, 3}},
	[MU_COLOR_TITLEBG] = {.frame = DRIFT_SPRITE_UI_TITLE, {4, 0, 4, 0}, {27, 1, 20, 1}},
	[MU_COLOR_BUTTON] = {.frame = DRIFT_SPRITE_UI_BUTTON, {2, 2, 2, 2}, {5, 5, 4, 4}},
	[MU_COLOR_BUTTONHOVER] = {.frame = DRIFT_SPRITE_UI_BUTTONHOVER, {2, 2, 2, 2}, {5, 5, 4, 4}},
	[MU_COLOR_BUTTONFOCUS] = {.frame = DRIFT_SPRITE_UI_BUTTONFOCUS, {2, 2, 2, 2}, {5, 5, 4, 4}},
	[MU_COLOR_PROGRESSBASE] = {.frame = DRIFT_SPRITE_UI_PROGRESSBASE, {1, 1, 1, 1}, {5, 5, 4, 4}},
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
			mu_draw_box(mu, expand_rect(rect, 1), mu->style->colors[MU_COLOR_BORDER]);
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

void DriftUIHandleEvent(mu_Context* mu, SDL_Event* event, float scale){
	static const char BUTTON_MAP[0x10] = {
		[SDL_BUTTON_LEFT   & 0xF] = MU_MOUSE_LEFT,
		[SDL_BUTTON_RIGHT  & 0xF] = MU_MOUSE_RIGHT,
		[SDL_BUTTON_MIDDLE & 0xF] = MU_MOUSE_MIDDLE,
	};

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
		case SDL_QUIT: exit(EXIT_SUCCESS); break;
		case SDL_MOUSEMOTION: mu_input_mousemove(mu, (int)(event->motion.x/scale), (int)(event->motion.y/scale)); break;
		case SDL_MOUSEWHEEL: mu_input_scroll(mu, 0, -30*event->wheel.y); break;
		case SDL_TEXTINPUT: mu_input_text(mu, event->text.text); break;

		case SDL_MOUSEBUTTONDOWN: if(b) mu_input_mousedown(mu, (int)(event->button.x/scale), (int)(event->button.y/scale), b); break;
		case SDL_MOUSEBUTTONUP: if(b) mu_input_mouseup(mu, (int)(event->button.x/scale), (int)(event->button.y/scale), b); break;

		case SDL_KEYDOWN: if(!event->key.repeat && c) mu_input_keydown(mu, c); break;
		case SDL_KEYUP: if(c) mu_input_keyup(mu, c); break;
		
		case SDL_CONTROLLERBUTTONDOWN: if(g) mu_input_keydown(mu, g); break;
		case SDL_CONTROLLERBUTTONUP: if(g) mu_input_keyup(mu, g); break;
	}
}

void DriftUIBegin(DriftDraw* draw){
	mu_Context* mu = draw->ctx->mu;
	
	mu->style = DriftAlloc(draw->mem, sizeof(STYLE));
	*mu->style = STYLE;
	mu->style->font = (void*)draw->input_icons;
	
	mu_begin(mu);
}

void DriftUIPresent(DriftDraw* draw){
	mu_Context* mu = draw->ctx->mu;
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
				DriftAffine m = {1, 0, 0, 1, cmd->text.pos.x, h - cmd->text.pos.y - 8}; // height - baseline?
				DriftDrawText(draw, &geo, m, (DriftVec4){{c.r*pm, c.g*pm, c.b*pm, 255*pm}}, cmd->text.str);
				DRIFT_ARRAY_PUSH(text_len, DriftArrayLength(geo) - len0);
			} break;
			case MU_COMMAND_RECT:{
				mu_Rect r = cmd->rect.rect;
				DRIFT_ARRAY_PUSH(geo, ((DriftSprite){.matrix = {r.w, 0, 0, -r.h, r.x, h - r.y}, .color = DriftRGBA8_from_mu(cmd->rect.color)}));
			} break;
			case MU_COMMAND_ICON:{
				static const uint ICON_FRAMES[] = {
					[MU_ICON_CLOSE] = DRIFT_SPRITE_ICON_CLOSE,
					[MU_ICON_COLLAPSED] = DRIFT_SPRITE_ICON_COLLAPSE,
					[MU_ICON_EXPANDED] = DRIFT_SPRITE_ICON_EXPAND,
					[MU_ICON_CHECK] = DRIFT_SPRITE_ICON_CHECK,
					[MU_ICON_SCROLLKNURL] = DRIFT_SPRITE_ICON_SCROLLKNURL,
				};
				
				// Treat negative ids as drift sprite ids.
				int id = cmd->icon.id;
				DriftSpriteEnum frame = (id >= 0 ? ICON_FRAMES[id] : (uint)-id);
				
				mu_Rect r = cmd->icon.rect;
				DRIFT_ARRAY_PUSH(geo, ((DriftSprite){
					.frame = DRIFT_FRAMES[frame],
					.color = DriftRGBA8_from_mu(cmd->icon.color),
					.matrix = {1, 0, 0, 1, r.x + r.w/2, h - r.y - r.h/2},
				}));
			} break;
			case MU_COMMAND_PATCH9:{
				mu_Rect r = cmd->patch9.rect;
				patch9(&geo, (DriftAABB2){r.x, h - r.y - r.h, r.x + r.w, h - r.y}, cmd->patch9.id, DriftRGBA8_from_mu(cmd->patch9.color));
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
}

void DriftUIOpen(DriftGameContext* ctx, const char* ui){
	mu_Container* win = mu_get_container(ctx->mu, ui);
	win->open = true;
	mu_bring_to_front(ctx->mu, win);
}

static void DriftSettingsPane(DriftDraw* draw, DriftPauseState* pause_state){
	mu_Context* mu = draw->ctx->mu;
	
	DriftGameContext* ctx = draw->ctx;
	DriftApp* app = draw->ctx->app;
	
	const char* TITLE = "Settings";
	mu_Container* win = mu_get_container(mu, TITLE);
	win->open = (*pause_state == DRIFT_PAUSE_STATE_SETTINGS);
	
	DriftVec2 extents = draw->internal_extent;
	mu_Vec2 win_size = {250, 250};
	win->rect = (mu_Rect){(int)(extents.x - win_size.x)/2, (int)(extents.y - win_size.y)/2, win_size.x, win_size.y};
	
	float inc = (!!(mu->key_down & MU_KEY_RIGHT) - !!(mu->key_down & MU_KEY_LEFT))/8.0f;
	if(mu_begin_window_ex(mu, TITLE, win->rect, 0)){
		mu_layout_row(mu, 2, (int[]){-1}, 18);
		mu_begin_box(mu, MU_COLOR_GROUPBG, 0);
		mu_layout_row(mu, 2, (int[]){-60, -1}, -1);
		mu_label(mu, "Settings:");
		if(mu_button(mu, "Back {@CANCEL}") || mu->key_down & MU_KEY_ESCAPE) *pause_state = DRIFT_PAUSE_STATE_MENU;
		mu_end_box(mu);
		
		mu_layout_row(mu, 1, (int[]){-1}, -1);
		
		mu_begin_panel(mu, "Settings");
		mu_layout_row(mu, 1, (int[]){-2}, 18);
		int widths[] = {-100, -1};
		
		{ mu_label(mu, "General:");
			bool gfocus = mu_begin_group(mu, 0);
			static int item = 0;
			
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Difficulty:");
			if(mu_droplist(mu, "DIFFICULTY", &item, 3, (const char* []){"Easy", "Normal", "Hard"}, gfocus)){
				DRIFT_LOG("difficulty changed to %d", item);
			}
			mu_end_group(mu);
		}
		
		{ mu_label(mu, "Sound:");
			mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Volume:");
			mu_slider_ex(mu, &app->prefs.master_volume, 0, 1, 0, NULL, 0);
			if(mu_group_is_hovered(mu)){
				app->prefs.master_volume = DriftClamp(app->prefs.master_volume + inc, 0, 1);
			}
			mu_end_group(mu);
		}{
			mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Music:");
			mu_slider_ex(mu, &app->prefs.music_volume, 0, 1, 0, NULL, 0);
			if(mu_group_is_hovered(mu)){
				app->prefs.music_volume = DriftClamp(app->prefs.music_volume + inc, 0, 1);
			}
			mu_end_group(mu);
		}{
			mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Effects:");
			static float NYI = 0.5f;
			mu_slider_ex(mu, &NYI, 0, 1, 0, NULL, 0);
			mu_end_group(mu);
		}
		
		DriftAudioSetParams(app->audio, app->prefs.master_volume, app->prefs.music_volume);
		// TODO doesn't actually save anything.
		
		{ mu_label(mu, "Input:");
			mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Mouse Sensitivity:");
			static float NYI = 0.5f;
			mu_slider_ex(mu, &NYI, 0, 1, 0, NULL, 0);
			mu_end_group(mu);
		}{
			bool gfocus = mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Keyboard:");
			if(mu_button(mu, "Configure") || gfocus) DRIFT_LOG("Configure Keyboard");
			mu_end_group(mu);
		}{
			bool gfocus = mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Gamepad:");
			if(mu_button(mu, "Configure") || gfocus) DRIFT_LOG("Configure Gamepad");
			mu_end_group(mu);
		}
		
		{ mu_label(mu, "Graphics:");
			bool gfocus = mu_begin_group(mu, 0);
			mu_layout_row(mu, 2, widths, -1);
			mu_label(mu, "Fullscreen:");
			if(mu_button(mu, "Toggle") || gfocus) DriftAppToggleFullscreen(app);
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
				if(app->shell_func == shells[i]) item = i;
			}
			
			if(mu_droplist(mu, "GFX_SETTING", &item, item_count, items, gfocus)){
				DRIFT_LOG("changed to %s", items[item]);
				app->shell_restart = shells[item];
			}
			mu_end_group(mu);
		}
		mu_end_panel(mu);
		
		mu_end_window(mu);
	}
}

static void DriftPauseMenu(DriftDraw* draw, DriftPauseState* pause_state){
	mu_Context* mu = draw->ctx->mu;
	
	static const char* TITLE = "Pause";
	mu_Container* win = mu_get_container(mu, TITLE);
	win->open = (*pause_state == DRIFT_PAUSE_STATE_MENU);
	
	DriftVec2 extents = draw->internal_extent;
	mu_Vec2 win_size = {100, 120};
	win->rect = (mu_Rect){(int)(extents.x - win_size.x)/2, (int)(extents.y - win_size.y)/2, win_size.x, win_size.y};
	
	if(mu_begin_window_ex(mu, TITLE, win->rect, 0)){
		mu_layout_row(mu, 1, (int[]){-1}, 20);
		bool gfocus = false;
		int button_width[] = {-1};
		
		gfocus = mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, button_width, -1);
		if(mu_button(mu, "Resume") || gfocus || mu->key_down & MU_KEY_ESCAPE) *pause_state = DRIFT_PAUSE_STATE_NONE;
		mu_end_group(mu);
		
		gfocus = mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, button_width, -1);
		if(mu_button(mu, "Save") || gfocus){}
		mu_end_group(mu);
		
		gfocus = mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, button_width, -1);
		if(mu_button(mu, "Load") || gfocus){}
		mu_end_group(mu);
		
		gfocus = mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, button_width, -1);
		if(mu_button(mu, "Settings") || gfocus) *pause_state = DRIFT_PAUSE_STATE_SETTINGS;
		mu_end_group(mu);
		
		gfocus = mu_begin_group(mu, 0);
		mu_layout_row(mu, 1, button_width, -1);
		if(mu_button(mu, "Exit") || gfocus) draw->ctx->input.quit = true;
		mu_end_group(mu);
		
		mu_end_window(mu);
	}
}

void DriftPauseLoop(DriftGameContext* ctx, tina_job* job, DriftAffine vp_matrix){
	DriftDrawShared* draw_shared = ctx->draw_shared;
	DriftAffine vp_inverse = DriftAffineInverse(vp_matrix);
	DriftVec2 camera_pos = DriftAffineOrigin(vp_inverse);
	DriftAffine v_matrix = {1, 0, 0, 1, -camera_pos.x, -camera_pos.y};
	
	// Reset escape key event carried over from the previous screen.
	ctx->mu->key_down = 0;
	
	DriftPauseState pause_state = DRIFT_PAUSE_STATE_MENU;
	while(pause_state && !ctx->input.quit && !ctx->app->shell_restart){
		float dt = (float)DriftGameContextUpdateNanos(ctx)/1e9f;
		DriftInputEventsPoll(ctx, vp_inverse);
		if(pause_state == DRIFT_PAUSE_STATE_MENU && DriftInputButtonPress(&ctx->input.player, DRIFT_INPUT_PAUSE)) break;
		
		DriftDraw* draw = DriftDrawBegin(ctx, job, dt, 0, v_matrix, vp_matrix);
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
		
		DriftUIBegin(draw);
		DriftPauseMenu(draw, &pause_state);
		DriftSettingsPane(draw, &pause_state);
		DriftUIPresent(draw);
		
		// Present to the screen.
		DriftGfxRendererPushBindTargetCommand(draw->renderer, NULL, DRIFT_VEC4_CLEAR);
		DriftGfxPipelineBindings* present_bindings = DriftDrawQuads(draw, draw_shared->present_pipeline, 1);
		present_bindings->textures[1] = draw_shared->resolve_buffer;
		
		tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
		DriftAppPresentFrame(draw_shared->app, draw->renderer);
		tina_job_switch_queue(job, DRIFT_JOB_QUEUE_MAIN);
		DriftZoneMemRelease(draw->mem);
		
		ctx->current_frame = ++ctx->_frame_counter;
		tina_job_yield(job);
	}
}

void DriftGameContextMapLoop(DriftGameContext* ctx, tina_job* job, DriftAffine game_vp_matrix, DriftUIState ui_state, uintptr_t data){
	DriftDrawShared* draw_shared = ctx->draw_shared;
	DriftAffine prev_vp_matrix = game_vp_matrix;
	DriftVec2 player_pos = DriftAffineOrigin(DriftAffineInverse(game_vp_matrix));
	
	float zoom = 1;
	DriftVec2 camera_pos = player_pos;
	
	while(!ctx->input.quit){
		float dt = (float)DriftGameContextUpdateNanos(ctx)/1e9f;
		DriftInputEventsPoll(ctx, DriftAffineInverse(prev_vp_matrix));
		if(
			DriftInputButtonPress(&ctx->input.player, DRIFT_INPUT_OPEN_MAP) ||
			(ui_state == DRIFT_UI_STATE_NONE && DriftInputButtonPress(&ctx->input.player, DRIFT_INPUT_CANCEL))
		) break;
		if(DriftInputButtonPress(&ctx->input.player, DRIFT_INPUT_PREV)) ui_state = (ui_state - 1 + _DRIFT_UI_STATE_MAX)%_DRIFT_UI_STATE_MAX;
		if(DriftInputButtonPress(&ctx->input.player, DRIFT_INPUT_NEXT)) ui_state = (ui_state + 1 + _DRIFT_UI_STATE_MAX)%_DRIFT_UI_STATE_MAX;
		
		zoom *= expf(10*DriftInputJoystick(&ctx->input.player, DRIFT_INPUT_AXIS_LOOK_X, DRIFT_INPUT_AXIS_LOOK_Y).y*dt);
		zoom = DriftLogerp(DriftClamp(zoom, 1/64.0f, 0.25f), zoom, expf(-10*dt));
		
		DriftVec2 pan = DriftInputJoystick(&ctx->input.player, DRIFT_INPUT_AXIS_MOVE_X, DRIFT_INPUT_AXIS_MOVE_Y);
		camera_pos = DriftVec2FMA(camera_pos, pan, 600*dt/zoom);
		DriftAffine v_matrix = {1, 0, 0, 1, -camera_pos.x, -camera_pos.y};
		v_matrix = DriftAffineMul((DriftAffine){zoom, 0, 0, zoom, 0, 0}, v_matrix);
		DriftDraw* draw = DriftDrawBegin(ctx, job, 0, 0, v_matrix, prev_vp_matrix);
		
		// Draw terrain
		DriftTerrainDrawTiles(draw, true);
		DriftDrawPowerMap(draw, powf(zoom, 0.8f));
		
		// Draw player
		DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){.p0 = player_pos, .p1 = player_pos, .radii = {8/powf(zoom, 0.85f)}, .color = DRIFT_RGBA8_RED}));
		DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){.p0 = DRIFT_HOME_POSITION, .p1 = DRIFT_HOME_POSITION, .radii = {8/powf(zoom, 0.85f)}, .color = DRIFT_RGBA8_GREEN}));
		
		DriftAffine t = {1, 0, 0, 1, 8, roundf(draw->virtual_extent.y) - 18};
		t = DriftDrawText(draw, &draw->hud_sprites, t, DRIFT_VEC4_WHITE,
			"Pan & Zoom: {@MOVE} + {@LOOK}\n"
			"Exit: {@CANCEL}\n"
		);
		
		DriftDrawBindGlobals(draw);
		DriftGfxRendererPushBindTargetCommand(draw->renderer, draw_shared->resolve_target, DRIFT_VEC4_CLEAR);
		
		DriftGfxPipelineBindings terrain_bindings = draw->default_bindings;
		terrain_bindings.samplers[2] = draw_shared->repeat_sampler;
		terrain_bindings.textures[1] = draw_shared->terrain_tiles;
		
		// DRIFT_ARRAY(DriftGPUMatrix) blit = DRIFT_ARRAY_NEW(draw->mem, 1, DriftGPUMatrix);
		// DRIFT_ARRAY_PUSH(blit, DriftAffineToGPU(DriftAffineInverse(draw->reproj_matrix)));
		// DriftGfxPipelineBindings blit_bindings = draw->default_bindings;
		// blit_bindings.textures[1] = draw_shared->color_buffer[0];
		
		DriftGfxPipelineBindings hud_bindings = draw->default_bindings;
		hud_bindings.uniforms[0] = draw->ui_binding;
		
		DriftDrawBatches(draw, (DriftDrawBatch[]){
			{.arr = draw->terrain_chunks, .pipeline = draw_shared->terrain_map_pipeline, .bindings = &terrain_bindings},
			{.arr = draw->bg_prims, .pipeline = draw_shared->overlay_primitive_pipeline, .bindings = &draw->default_bindings},
			// {.arr = blit, .pipeline = draw_shared->map_blit_pipeline, .bindings = &blit_bindings},
			{.arr = draw->hud_sprites, .pipeline = draw_shared->overlay_sprite_pipeline, .bindings = &hud_bindings},
			{},
		});
		
		DriftUIBegin(draw);
		DriftScanUI(draw, &ui_state, data);
		DriftCraftUI(draw, &ui_state);
		DriftUIPresent(draw);
		
		// Present to the screen.
		DriftGfxRendererPushBindTargetCommand(draw->renderer, NULL, DRIFT_VEC4_CLEAR);
		DriftGfxPipelineBindings* present_bindings = DriftDrawQuads(draw, draw_shared->present_pipeline, 1);
		present_bindings->textures[1] = draw_shared->resolve_buffer;
		
		tina_job_switch_queue(job, DRIFT_JOB_QUEUE_GFX);
		DriftAppPresentFrame(draw_shared->app, draw->renderer);
		tina_job_switch_queue(job, DRIFT_JOB_QUEUE_MAIN);
		DriftZoneMemRelease(draw->mem);
		
		ctx->current_frame = ++ctx->_frame_counter;
		tina_job_yield(job);
	}
}
