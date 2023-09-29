/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "tracy/TracyC.h"

#include "drift_game.h"

static void draw_indicator(DriftDraw* draw, DriftVec2 pos, DriftVec2 rot, float radius, DriftRGBA8 color){
	float i = rot.x*rot.x;
	rot = DriftVec2Mul(rot, radius/8);
	DRIFT_ARRAY_PUSH(draw->overlay_sprites, ((DriftSprite){
		.color = {(u8)(color.r*i), (u8)(color.g*i), (u8)(color.b*i), color.a},
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_SELECT_INDICATOR],
		.matrix = {rot.x, rot.y, -rot.y, rot.x, pos.x, pos.y},
	}));
}

DriftRGBA8 DriftHUDIndicator(DriftDraw* draw, DriftVec2 pos, DriftRGBA8 color){
	DriftVec2 rot = DriftWaveComplex(draw->update_nanos - (uint64_t)(1.5e6*DriftVec2Length(pos)), 1);
	draw_indicator(draw, pos, rot, 8, color);
	return DriftRGBA8Fade(color, DriftSaturate(4*rot.x - 3));
}

static float toast_alpha(u64 current_nanos, DriftToast* toast){
	return 1 - DriftSaturate((current_nanos - toast->timestamp)*1e-9f - 4);
}

void DriftHudPushToast(DriftGameContext* ctx, uint num, const char* format, ...){
	DriftToast toast = {.timestamp = ctx->update_nanos, .count = 1, .num = num};
	
	va_list arg;
	va_start(arg, format);
	char* end = toast.message + vsnprintf(toast.message, sizeof(toast.message), format, arg);
	va_end(arg);
	
	// Find a matching toast and update it's count
	uint i = 0;
	while(i < DRIFT_MAX_TOASTS){
		bool visible = toast_alpha(ctx->update_nanos, ctx->toasts + i) > 0;
		if(visible && strncmp(toast.message, ctx->toasts[i].message, sizeof(toast.message)) == 0){
			toast.count += ctx->toasts[i].count;
			break;
		}
		
		i++;
	}
	
	// Reuse last if no match.
	if(i == DRIFT_MAX_TOASTS) i--;
	
	push_up:
	for(; i; i--) ctx->toasts[i] = ctx->toasts[i - 1];
	ctx->toasts[0] = toast;
}

typedef struct {
	DriftAffine text_matrix, bg_matrix, next_matrix;
} LabelInfo;

static LabelInfo calc_label_info(DriftVec2 origin, DriftVec2 dir, const char* label){
	DriftAABB2 bb = DriftDrawTextBounds(label, 0);
	bb.l -= 1, bb.b -= 1, bb.r += 1, bb.t += 1;
	DriftVec2 text_center = DriftAABB2Center(bb), text_extents = DriftAABB2Extents(bb);
	DriftVec2 text_origin = DriftVec2FMA(origin, dir, 1 + fminf((text_extents.x + 5)/fabsf(dir.x), (text_extents.y + 5)/fabsf(dir.y)));
	DriftAffine text_matrix = {1, 0, 0, 1, text_origin.x - text_center.x, text_origin.y - text_center.y};
	return (LabelInfo){
		.text_matrix = text_matrix,
		.bg_matrix = DriftAffineMul(text_matrix, (DriftAffine){2*text_extents.x, 0, 0, 2*text_extents.y, bb.l, bb.b}),
		.next_matrix = {1, 0, 0, 1, text_origin.x, text_origin.y + copysignf(text_extents.y + 4, dir.y)},
	};
}

DriftAffine DriftHudDrawOffsetLabel(DriftDraw* draw, DriftVec2 origin, DriftVec2 dir, DriftVec4 color, const char* label){
	LabelInfo info = calc_label_info(origin, dir, label);
	DRIFT_ARRAY_PUSH(draw->overlay_sprites, ((DriftSprite){.color = DRIFT_PANEL_COLOR, .matrix = info.bg_matrix}));
	DriftDrawTextFull(draw, &draw->overlay_sprites, label, (DriftTextOptions){
		.tint = color, .matrix = info.text_matrix,
	});
	return info.next_matrix;
}

static uint SWOOP_CURSOR = 0;
static struct {
	LabelInfo info;
	DriftVec4 color;
	DriftItemType type;
	float anim;
} SWOOPS[2] = {
	[0].anim = 1,
	[1].anim = 1,
};

void DriftHUDPushSwoop(DriftGameContext* ctx, DriftVec2 origin, DriftVec2 dir, DriftVec4 color, DriftItemType type){
	SWOOPS[SWOOP_CURSOR % 2].info = calc_label_info(origin, dir, DriftItemName(type));
	SWOOPS[SWOOP_CURSOR % 2].color = color;
	SWOOPS[SWOOP_CURSOR % 2].type = type;
	SWOOPS[SWOOP_CURSOR % 2].anim = 0;
	SWOOP_CURSOR++;
}

static void update_swoops(DriftDraw* draw){
	for(uint i = 0; i < 2; i++){
		if(SWOOPS[i].anim == 0){
			// Convert world->hud matrix for swoops.
			DriftAffine world_to_ui = DriftAffineMul(DriftAffineInverse(draw->ui_matrix), draw->vp_matrix);
			SWOOPS[i].info.text_matrix = DriftAffineMul(world_to_ui, SWOOPS[i].info.text_matrix);
			SWOOPS[i].info.bg_matrix = DriftAffineMul(world_to_ui, SWOOPS[i].info.bg_matrix);
		}
		
		SWOOPS[i].anim = DriftSaturate(SWOOPS[i].anim + draw->dt/0.5f);
	}
}

static void draw_swoops(DriftDraw* draw, DriftVec2 p){
	for(uint i = 0; i < 2; i++){
		// This is a bit of a mess, but whatever.
		if(SWOOPS[i].anim < 1){
			p.x += 8*6;
			
			DriftAffine text_matrix = SWOOPS[i].info.text_matrix, bg_matrix = SWOOPS[i].info.bg_matrix;
			DriftVec2 offset = DriftVec2Mul(DriftVec2Sub(p, DriftAffineOrigin(text_matrix)), DriftHermite3(SWOOPS[i].anim));
			text_matrix.x += offset.x;
			text_matrix.y += offset.y;
			bg_matrix.x += offset.x;
			bg_matrix.y += offset.y;
			
			DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){
				.color = DriftRGBA8Fade(DRIFT_PANEL_COLOR, DriftSmoothstep(0.5f, 0.0f, SWOOPS[i].anim)),
				.matrix = bg_matrix,
			}));
			DriftDrawTextFull(draw, &draw->hud_sprites, DriftItemName(SWOOPS[i].type), (DriftTextOptions){
				.tint = DriftVec4Mul(SWOOPS[i].color, DriftSmoothstep(1.0f, 0.9f, SWOOPS[i].anim)), .matrix = text_matrix,
			});
		}
	}
}

void DriftDrawHud(DriftDraw* draw){
	TracyCZoneN(ZONE_HUD, "HUD", true);
	DriftGameContext* ctx = draw->ctx;
	DriftGameState* state = draw->state;
	if(state->status.disable_hud) return;
	
	DriftVec2 screen_size = {roundf(draw->virtual_extent.x), roundf(draw->virtual_extent.y)};
	DriftPlayerData* player = state->players.data + DriftComponentFind(&state->players.c, state->player);
	DriftVec2 player_pos = DriftAffineOrigin(state->transforms.matrix[DriftComponentFind(&state->transforms.c, state->player)]);
	
	static const char BAR[] = "###############---------------";
	
	uint health_idx = DriftComponentFind(&state->health.c, state->player);
	DriftHealth* health = state->health.data + health_idx;
	
	DriftVec2 status_cursor = {8, roundf(draw->virtual_extent.y) - 8};
	float panel_width = state->status.disable_scan ? 154 : 262;
	status_cursor = DriftHUDDrawPanel(draw, status_cursor, (DriftVec2){panel_width, 40}, 1);
	
	DriftVec2 inv_cursor = status_cursor;
	inv_cursor.x += 150;
	
	// Draw temp status bars
	const char* health_bar = BAR + 15 -  (uint)(15*health->value/health->maximum);
	status_cursor = DriftDrawTextF(draw, &draw->hud_sprites, status_cursor, DRIFT_TEXT_GRAY"Shield |%.15s|\n", health_bar);
	
	const char* energy_bar = BAR + 15 -  (uint)(15*player->energy/DriftPlayerEnergyCap(state));
	status_cursor = DriftDrawTextF(draw, &draw->hud_sprites, status_cursor, "{#23ADB4FF}Energy |%.15s|\n", energy_bar);
	
	const char* temp_bar = BAR + 15 -  (uint)(15*player->temp);
	status_cursor = DriftDrawTextF(draw, &draw->hud_sprites, status_cursor, "{#DA4C4CFF}Temp   |%.15s|\n", temp_bar);
	
	uint cargo_mass = DriftPlayerCalculateCargo(state), cargo_max = DriftPlayerCargoCap(state);
	if(!state->status.disable_scan){
		inv_cursor = DriftDrawTextF(draw, &draw->hud_sprites, inv_cursor, DRIFT_TEXT_GRAY"Power Nodes:"DRIFT_TEXT_WHITE" %2d\n", state->inventory.cargo[DRIFT_ITEM_POWER_NODE]);
		
		update_swoops(draw);
		draw_swoops(draw, inv_cursor);
		
		inv_cursor = DriftDrawTextF(draw, &draw->hud_sprites, inv_cursor, DRIFT_TEXT_GRAY"Cargo:"DRIFT_TEXT_WHITE" %3d/%3d kg\n", cargo_mass, cargo_max);
		
		const char* bar = BAR + (cargo_mass > cargo_max ? 0 : 15 -  15*cargo_mass/cargo_max);
		inv_cursor = DriftDrawTextF(draw, &draw->hud_sprites, inv_cursor, DRIFT_TEXT_GRAY"|%.15s|\n", bar);
	}
	
	// Temporary prompt for unfinished areas
	DriftBiomeType biome = DriftTerrainSampleBiome(state->terra, player_pos).idx;
	if(biome != DRIFT_BIOME_LIGHT){
		DriftDrawText(draw, &draw->hud_sprites, (DriftVec2){roundf(draw->virtual_extent.x/2 - 100), roundf(draw->virtual_extent.y*0.65f)}, "{#FF0000FF}NOTE: This area of the game is\nunfinished and may be unplayable.");
	}
	
	// Draw prompts near player
	
	// Save the current to insert the panel under the text.
	uint panel_idx = DriftArrayLength(draw->hud_sprites);
	const float info_advance = 12;
	float info_flash = (DriftWaveSaw(draw->update_nanos, 1) < 0.9f ? 1 : 0.25);
	DriftVec2 info_cursor = {screen_size.x/2, screen_size.y/3}, info_origin = info_cursor;
	float info_width = 0;
	
	const char* prompt_text = NULL;
	if(player->tool_idx == DRIFT_TOOL_DIG) prompt_text = "{@LASER} TO DIG";
	if(player->grabbed_type && !DriftInputButtonState(DRIFT_INPUT_STASH)){
		const DriftItem* grabbed_item = DRIFT_ITEMS + player->grabbed_type;
		DriftScanType scan_type = grabbed_item->scan;
		if(state->scan_progress[scan_type] >= 1){
			bool has_space = cargo_mass + grabbed_item->mass <= cargo_max;
			prompt_text = has_space ? DRIFT_TEXT_GRAY"STASH WITH {@LOOK} or {@STASH}" : DRIFT_TEXT_RED"CARGO FULL";
		} else {
			prompt_text = DRIFT_TEXT_GRAY"SCAN WITH {@SCAN}";
		}
	}
	
	if(prompt_text){
		float width = DriftDrawTextSize(prompt_text, 0).x;
		DriftDrawTextFull(draw, &draw->hud_sprites, prompt_text, (DriftTextOptions){
			.tint = DriftVec4Mul(DRIFT_VEC4_WHITE, info_flash),
			.matrix = {1, 0, 0, 1, info_cursor.x - width/2, info_cursor.y},
		});
		info_cursor.y -= info_advance;
		info_width = fmaxf(info_width, width);
	}
	
	if(!player->is_powered && state->inventory.cargo[DRIFT_ITEM_POWER_NODE] > 0){
		const char* text = DRIFT_TEXT_GRAY"DEPLOY NODE {@DROP}";
		float width = DriftDrawTextSize(text, 0).x;
		DriftDrawTextFull(draw, &draw->hud_sprites, text, (DriftTextOptions){
			.tint = DriftVec4Mul(DRIFT_VEC4_WHITE, info_flash),
			.matrix = {1, 0, 0, 1, info_cursor.x - width/2, info_cursor.y},
		});
		info_cursor.y -= info_advance;
		info_width = fmaxf(info_width, width);
	}
	
	if(player->energy == 0){
		const char* text = "EMERGENCY POWER";
		float width = DriftDrawTextSize(text, 0).x;
		DriftDrawTextFull(draw, &draw->hud_sprites, text, (DriftTextOptions){
			.tint = DriftVec4Mul(DRIFT_VEC4_RED, info_flash),
			.matrix = {1, 0, 0, 1, info_cursor.x - width/2, info_cursor.y},
		});
		info_cursor.y -= info_advance;
		info_width = fmaxf(info_width, width);
	}
	
	float shield = health->value/health->maximum;
	if(shield == 0){
		const char* text = DRIFT_TEXT_RED"SHIELDS DOWN";
		float width = DriftDrawTextSize(text, 0).x;
		DriftDrawTextFull(draw, &draw->hud_sprites, text, (DriftTextOptions){
			.tint = DriftVec4Mul(DRIFT_VEC4_WHITE, info_flash),
			.matrix = {1, 0, 0, 1, info_cursor.x - width/2, info_cursor.y},
		});
		info_cursor.y -= info_advance;
		info_width = fmaxf(info_width, width);
	}
	
	if(player->is_overheated){
		const char* text = "OVERHEAT";
		float width = DriftDrawTextSize(text, 0).x + 10;
		DriftDrawTextFull(draw, &draw->hud_sprites, text, (DriftTextOptions){
			.tint = DriftVec4Mul(DRIFT_VEC4_RED, info_flash),
			.matrix = {1, 0, 0, 1, info_cursor.x - width/2 + 10, info_cursor.y},
		});
		
		DriftFrame temp_frame = DRIFT_FRAMES[DRIFT_SPRITE_HUD_TEMP];
		uint w = temp_frame.bounds.r - temp_frame.bounds.l + 1;
		w *= (uint)(DriftSaturate(player->temp)*9);
		temp_frame.bounds.l += w;
		temp_frame.bounds.r += w;
		DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){
			.frame = temp_frame, .color = DRIFT_RGBA8_WHITE,
			.matrix = (DriftAffine){1, 0, 0, 1, info_cursor.x - width/2 + 4, info_cursor.y + 3},
		}));
		
		info_cursor.y -= info_advance;
		info_width = fmaxf(info_width, width);
	} else if(player->temp > 0.15f){
		const char* text = "TEMPERATURE";
		float width = DriftDrawTextSize(text, 0).x + 10;
		DriftDrawTextFull(draw, &draw->hud_sprites, text, (DriftTextOptions){
			.tint = DriftVec4Mul(DRIFT_VEC4_ORANGE, info_flash),
			.matrix = {1, 0, 0, 1, info_cursor.x - width/2 + 10, info_cursor.y},
		});
		
		DriftFrame temp_frame = DRIFT_FRAMES[DRIFT_SPRITE_HUD_TEMP];
		uint w = temp_frame.bounds.r - temp_frame.bounds.l + 1;
		w *= (uint)(DriftSaturate(player->temp)*9);
		temp_frame.bounds.l += w;
		temp_frame.bounds.r += w;
		DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){
			.frame = temp_frame, .color = DRIFT_RGBA8_WHITE,
			.matrix = (DriftAffine){1, 0, 0, 1, info_cursor.x - width/2 + 4, info_cursor.y + 3},
		}));
		
		info_cursor.y -= info_advance;
		info_width = fmaxf(info_width, width);
	}
	
	if(info_width){
		// Copy sprite at panel index to end, and overwrite with panel index.
		DRIFT_ARRAY_PUSH(draw->hud_sprites, draw->hud_sprites[panel_idx]);
		draw->hud_sprites[panel_idx] = (DriftSprite){
			.matrix = {info_width + 4, 0, 0, info_cursor.y - info_origin.y - 4, info_origin.x - info_width/2 - 2, info_origin.y + 11},
			.color = DRIFT_PANEL_COLOR,
		};
	}
	
	// Draw toasts
	const char* messages[DRIFT_MAX_TOASTS] = {};
	DriftVec2 messages_size = {.y = 16};
	for(uint i = 0; i < DRIFT_MAX_TOASTS; i++){
		DriftToast* toast = ctx->toasts + i;
		if(toast_alpha(draw->update_nanos, toast) > 0){
			if(toast->num > 0){
				messages[i] = DriftSMPrintf(draw->mem, "%s "DRIFT_TEXT_GRAY"(%d)", toast->message, toast->num);
			} else if(toast->count > 1){
				messages[i] = DriftSMPrintf(draw->mem, "%s "DRIFT_TEXT_GRAY"x%d", toast->message, toast->count);
			} else {
				messages[i] = toast->message;
			}
			
			messages_size.x = fmaxf(messages_size.x, DriftDrawTextSize(messages[i], 0).x);
			messages_size.y += 10;
		}
	}
	messages_size.x = fmaxf(120, messages_size.x + 10);
	
	float alpha = toast_alpha(draw->update_nanos, ctx->toasts + 0);
	DriftVec2 toast_cursor = {8, roundf(draw->virtual_extent.y/2)};
	toast_cursor = DriftHUDDrawPanel(draw, toast_cursor, messages_size, alpha);
	DriftDrawTextFull(draw, &draw->hud_sprites, DRIFT_TEXT_GRAY"Messages:", (DriftTextOptions){
		.tint = {{alpha, alpha, alpha, alpha}},
		.matrix = {1, 0, 0, 1, toast_cursor.x, toast_cursor.y},
	});
	toast_cursor.y -= 10;
	
	for(uint i = 0; i < DRIFT_MAX_TOASTS; i++){
		if(messages[i]){
			DriftDrawTextFull(draw, &draw->hud_sprites, messages[i], (DriftTextOptions){
				.tint = DriftVec4Mul(DRIFT_VEC4_WHITE, toast_alpha(draw->update_nanos, ctx->toasts + i)),
				.matrix = {1, 0, 0, 1, toast_cursor.x, toast_cursor.y},
			});
			toast_cursor.y -= 10;
		}
	}
	
	if(!player->is_powered){
		float nearest_dist = INFINITY;
		DriftVec2 nearest_pos = DRIFT_VEC2_ZERO;
		
		DRIFT_COMPONENT_FOREACH(&state->power_nodes.c, node_idx){
			if(!state->power_nodes.active[node_idx]) continue;
			
			DriftVec2 pos = state->power_nodes.position[node_idx];
			float dist = DriftVec2Distance(pos, player_pos);
			if(dist < nearest_dist){
				nearest_dist = dist;
				nearest_pos = pos;
			}
		}
		
		float anim = DriftSaturate((draw->tick - player->power_tick0)/8.0f);
		DriftVec4 color = {{0, anim, anim, anim}};
		DriftRGBA8 color8 = DriftRGBA8FromColor(color);
		float wobble = 4*fabsf(DriftWaveComplex(draw->update_nanos, 1).x), r = 8 + wobble;
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = nearest_pos, .p1 = nearest_pos, .radii = {r, r - 1}, .color = color8}));
		
		DriftVec2 dir = DriftVec2Normalize(DriftVec2Sub(nearest_pos, player_pos));
		DriftVec2 chevron_pos = DriftVec2FMA(player_pos, dir, 96 - wobble);
		float scale = DriftLogerp(4, 1, anim);
		DriftAffine m = {scale, 0, 0, scale, chevron_pos.x, chevron_pos.y};
		
		// Draw chevron
		DriftAffine m_chev = DriftAffineMul(m, (DriftAffine){dir.x, dir.y, -dir.y, dir.x, 0, 0});
		DriftVec2 p[] = {
			DriftAffinePoint(m_chev, (DriftVec2){0, -4}),
			DriftAffinePoint(m_chev, (DriftVec2){2,  0}),
			DriftAffinePoint(m_chev, (DriftVec2){0,  4}),
		};
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[0], .p1 = p[1], .radii = {1.5f*scale}, .color = color8}));
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[1], .p1 = p[2], .radii = {1.5f*scale}, .color = color8}));
		
		// Draw battery icon
		DriftFrame power_frame = DRIFT_FRAMES[DRIFT_SPRITE_HUD_POWER];
		uint off_x = (power_frame.bounds.r - power_frame.bounds.l + 1);
		off_x *= (uint)floorf(8*DriftSaturate(player->energy/DriftPlayerEnergyCap(state)));
		power_frame.bounds.l += off_x;
		power_frame.bounds.r += off_x;
		DRIFT_ARRAY_PUSH(draw->overlay_sprites, ((DriftSprite){
			.frame = power_frame, .color = DriftRGBA8FromColor(DriftVec4Mul(DRIFT_VEC4_WHITE, info_flash)),
			.matrix = DriftAffineMul(m, (DriftAffine){1, 0, 0, 1, -10*dir.x, -10*dir.y}),
		}));
	}
	
	// DriftDrawControls(draw);
	
	TracyCZoneEnd(ZONE_HUD);
}

void DriftDrawControls(DriftDraw* draw){
	DriftVec2 panel_size = {120, 158};
	DriftVec2 p = {roundf((draw->virtual_extent.x/2 - panel_size.y)/2), roundf((draw->virtual_extent.y + panel_size.y)/2)};
	p = DriftHUDDrawPanel(draw, p, panel_size, 1);
	float adv = -3;
	
	p = DriftDrawText(draw, &draw->hud_sprites, p, "Controls:\n"); p.y += adv;
	p = DriftDrawText(draw, &draw->hud_sprites, p, "{@MOVE} Move\n"); p.y += adv;
	p = DriftDrawText(draw, &draw->hud_sprites, p, "{@LOOK} Aim\n"); p.y += adv;
	p = DriftDrawText(draw, &draw->hud_sprites, p, "{@FIRE} Shoot\n"); p.y += adv;
	p = DriftDrawText(draw, &draw->hud_sprites, p, "{@GRAB} Grab Object\n"); p.y += adv;
	p = DriftDrawText(draw, &draw->hud_sprites, p, "{@STASH} Quick Stash\n"); p.y += adv;
	p = DriftDrawText(draw, &draw->hud_sprites, p, "{@DROP} Place Node\n"); p.y += adv;
	p = DriftDrawText(draw, &draw->hud_sprites, p, "{@SCAN} Scan Object\n"); p.y += adv;
	p = DriftDrawText(draw, &draw->hud_sprites, p, "{@LASER} Laser\n"); p.y += adv;
	p = DriftDrawText(draw, &draw->hud_sprites, p, "{@MAP} Show Map\n"); p.y += adv;
	p = DriftDrawText(draw, &draw->hud_sprites, p, "{@LIGHT} Toggle Light\n"); p.y += adv;
}

DriftVec2 DriftHUDDrawPanel(DriftDraw* draw, DriftVec2 panel_origin, DriftVec2 panel_size, float alpha){
	DriftAffine panel_transform = {panel_size.x, 0, 0, -panel_size.y, panel_origin.x, panel_origin.y};
	DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){.color = DriftRGBA8Fade(DRIFT_PANEL_COLOR, alpha), .matrix = panel_transform}));
	
	DRIFT_ARRAY_PUSH(draw->hud_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_PORTRAIT_CORNER], .color = DriftRGBA8Fade(DRIFT_RGBA8_WHITE, alpha),
		.matrix = {1, 0, 0, 1, panel_origin.x, panel_origin.y},
	}));
	
	return DriftVec2Add(panel_origin, (DriftVec2){8, -12});
}
