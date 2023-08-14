/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "drift_game.h"
#include "microui/microui.h"

const DriftItem DRIFT_ITEMS[_DRIFT_ITEM_COUNT] = {
	[DRIFT_ITEM_NONE] = {},
	
	// Scrap
	[DRIFT_ITEM_SCRAP] = {.scan = DRIFT_SCAN_SCRAP, .mass = 10, .limit = 10, .is_cargo = true, .from_biomass = true},
	[DRIFT_ITEM_ADVANCED_SCRAP] = {.mass = 10, .limit = 10, .is_cargo = true, .from_biomass = true},
	
	// Ore
	[DRIFT_ITEM_VIRIDIUM] = {.scan = DRIFT_SCAN_VIRIDIUM, .mass = 5, .limit = 30, .is_cargo = true},
	[DRIFT_ITEM_BORONITE] = {.scan = DRIFT_SCAN_BORONITE, .mass = 5, .limit = 30, .is_cargo = true},
	[DRIFT_ITEM_RADONITE] = {.scan = DRIFT_SCAN_RADONITE, .mass = 5, .limit = 30, .is_cargo = true},
	[DRIFT_ITEM_METRIUM] = {.scan = DRIFT_SCAN_METRIUM, .mass = 5, .limit = 30, .is_cargo = true},
	
	// Biomech
	[DRIFT_ITEM_LUMIUM] = {.scan = DRIFT_SCAN_LUMIUM, .mass = 5, .limit = 20, .is_cargo = true, .from_biomass = true},
	[DRIFT_ITEM_FLOURON] = {.scan = DRIFT_SCAN_FLOURON, .mass = 5, .limit = 20, .is_cargo = true, .from_biomass = true},
	[DRIFT_ITEM_FUNGICITE] = {.scan = DRIFT_SCAN_FUNGICITE, .mass = 5, .limit = 20, .is_cargo = true, .from_biomass = true},
	[DRIFT_ITEM_MORPHITE] = {.scan = DRIFT_SCAN_MORPHITE, .mass = 5, .limit = 20, .is_cargo = true, .from_biomass = true},
	
	// Rare
	[DRIFT_ITEM_COPPER] = {.scan = DRIFT_SCAN_COPPER, .mass = 20, .limit = 10, .is_cargo = true},
	[DRIFT_ITEM_SILVER] = {.scan = DRIFT_SCAN_SILVER, .mass = 20, .limit = 10, .is_cargo = true},
	[DRIFT_ITEM_GOLD] = {.scan = DRIFT_SCAN_GOLD, .mass = 20, .limit = 10, .is_cargo = true},
	[DRIFT_ITEM_GRAPHENE] = {.scan = DRIFT_SCAN_GRAPHENE, .mass = 20, .limit = 10, .is_cargo = true},
	
	// Intermediate
	[DRIFT_ITEM_POWER_SUPPLY] = {
		.scan = DRIFT_SCAN_POWER_SUPPLY, .limit = 10, .is_part = true, .duration = 20,
		.ingredients = {
			{.type = DRIFT_ITEM_VIRIDIUM, .count = 8},
			{.type = DRIFT_ITEM_SCRAP, .count = 2},
			{.type = DRIFT_ITEM_COPPER, .count = 1},
		}
	},
	[DRIFT_ITEM_OPTICS] = {
		.scan = DRIFT_SCAN_OPTICS, .limit = 10, .is_part = true, .duration = 20,
		.ingredients = {
			{.type = DRIFT_ITEM_VIRIDIUM, .count = 8},
			{.type = DRIFT_ITEM_LUMIUM, .count = 2},
			{.type = DRIFT_ITEM_SCRAP, .count = 1},
		},
	},
	
	// Tools and upgrades
	[DRIFT_ITEM_HEADLIGHT] = {
		.scan = DRIFT_SCAN_HEADLIGHT, .limit = 1, .duration = 10,
		.ingredients = {
			{.type = DRIFT_ITEM_VIRIDIUM, .count = 10},
			{.type = DRIFT_ITEM_LUMIUM, .count = 5},
		},
	},
	
	[DRIFT_ITEM_AUTOCANNON] = {
		.scan = DRIFT_SCAN_AUTOCANNON, .limit = 1, .duration = 30,
		.ingredients = {
			{.type = DRIFT_ITEM_VIRIDIUM, .count = 6},
			{.type = DRIFT_ITEM_SCRAP, .count = 3},
		},
	},
	
	[DRIFT_ITEM_ZIP_CANNON] = {
		.scan = DRIFT_SCAN_ZIP_CANNON, .limit = 1, .duration = 30,
	},
	
	[DRIFT_ITEM_MINING_LASER] = {
		.scan = DRIFT_SCAN_LASER, .limit = 1, .duration = 60,
		.ingredients = {
			{.type = DRIFT_ITEM_LUMIUM, .count = 10},
			{.type = DRIFT_ITEM_OPTICS, .count = 2},
			{.type = DRIFT_ITEM_POWER_SUPPLY, .count = 1},
		},
	},
	
	[DRIFT_ITEM_FAB_RADIO] = {
		.scan = DRIFT_SCAN_FAB_RADIO, .limit = 1, .duration = 30,
		.ingredients = {
			{.type = DRIFT_ITEM_VIRIDIUM, .count = 10},
			{.type = DRIFT_ITEM_SCRAP, .count = 5},
			{.type = DRIFT_ITEM_COPPER, .count = 2},
		},
	},
	
	[DRIFT_ITEM_SHIELD_L2] = {
		.scan = DRIFT_SCAN_SHIELD_L2, .limit = 1, .duration = 60,
		.ingredients = {
			{.type = DRIFT_ITEM_VIRIDIUM, .count = 20},
			{.type = DRIFT_ITEM_OPTICS, .count = 3},
			{.type = DRIFT_ITEM_POWER_SUPPLY, .count = 2},
		},
	},
	
	[DRIFT_ITEM_SHIELD_L3] = {
		.scan = DRIFT_SCAN_SHIELD_L3, .limit = 1, .duration = 60,
	},
	
	[DRIFT_ITEM_CARGO_L2] = {
		.scan = DRIFT_SCAN_CARGO_L2, .limit = 1, .duration = 60,
		.ingredients = {
			{.type = DRIFT_ITEM_VIRIDIUM, .count = 30},
			{.type = DRIFT_ITEM_BORONITE, .count = 30},
			{.type = DRIFT_ITEM_COPPER, .count = 1},
		},
	},
	
	[DRIFT_ITEM_CARGO_L3] = {
		.scan = DRIFT_SCAN_CARGO_L3, .limit = 1, .duration = 60,
		.ingredients = {
			{.type = DRIFT_ITEM_BORONITE, .count = 45},
			{.type = DRIFT_ITEM_RADONITE, .count = 45},
			{.type = DRIFT_ITEM_SILVER, .count = 1},
		},
	},
	
	[DRIFT_ITEM_NODES_L2] = {
		.scan = DRIFT_SCAN_NODES_L2, .limit = 1, .duration = 60,
		.ingredients = {
			{.type = DRIFT_ITEM_VIRIDIUM, .count = 30},
			{.type = DRIFT_ITEM_BORONITE, .count = 30},
			{.type = DRIFT_ITEM_COPPER, .count = 1},
		},
	},
	
	[DRIFT_ITEM_NODES_L3] = {
		.scan = DRIFT_SCAN_NODES_L3, .limit = 1, .duration = 60,
		.ingredients = {
			{.type = DRIFT_ITEM_BORONITE, .count = 45},
			{.type = DRIFT_ITEM_RADONITE, .count = 45},
			{.type = DRIFT_ITEM_SILVER, .count = 1},
		},
	},
	
	[DRIFT_ITEM_STORAGE_L2] = {
		.scan = DRIFT_SCAN_STORAGE_L2, .limit = 1, .duration = 60,
		.ingredients = {
			{.type = DRIFT_ITEM_VIRIDIUM, .count = 30},
			{.type = DRIFT_ITEM_BORONITE, .count = 30},
			{.type = DRIFT_ITEM_COPPER, .count = 1},
		},
	},
	
	[DRIFT_ITEM_STORAGE_L3] = {
		.scan = DRIFT_SCAN_STORAGE_L3, .limit = 1, .duration = 60,
		.ingredients = {
			{.type = DRIFT_ITEM_BORONITE, .count = 45},
			{.type = DRIFT_ITEM_RADONITE, .count = 45},
			{.type = DRIFT_ITEM_SILVER, .count = 1},
		},
	},
	
	
	// Consumables
	[DRIFT_ITEM_POWER_NODE] = {
		.scan = DRIFT_SCAN_POWER_NODE, .limit = 40, .is_cargo = true, .duration = 20, .makes = 10,
		.ingredients = {
			{.type = DRIFT_ITEM_VIRIDIUM, .count = 10},
			{.type = DRIFT_ITEM_LUMIUM, .count = 1},
			{.type = DRIFT_ITEM_SCRAP, .count = 1},
		},
	},
	[DRIFT_ITEM_DRONE] = {
		.scan = DRIFT_SCAN_DRONE, .limit = 10, .duration = 30,
		.ingredients = {
			{.type = DRIFT_ITEM_VIRIDIUM, .count = 10},
			{.type = DRIFT_ITEM_BORONITE, .count = 5},
			{.type = DRIFT_ITEM_SCRAP, .count = 2},
		},
	},
};

static DriftEntity pnode_make(DriftGameState* state, DriftItemType type, DriftVec2 pos, DriftVec2 vel){
	DriftEntity e = DriftMakeEntity(state);
	
	uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
	state->transforms.matrix[transform_idx] = (DriftAffine){1, 0, 0, 1, pos.x, pos.y};
	uint scan_idx = DriftComponentAdd(&state->scan.c, e);
	state->scan.type[scan_idx] = DRIFT_SCAN_POWER_NODE;
	
	return e;
}

static void pnode_draw(DriftDraw* draw, DriftVec2 pos){
	bool flash = DriftWaveSaw(draw->nanos, 3) < 0.75f;
	
	// TODO draw lines in parts to highlight min dist?
	DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(draw->state, pos, draw->mem, DRIFT_POWER_BEAM_RADIUS);
	bool can_connect = info.node_can_connect && info.active_count > 0;
	
	DRIFT_ARRAY_FOREACH(info.nodes, node){
		if(node->is_too_close){
			// Flash nodes too close in red.
			if(flash){
				DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){pos, node->pos, {1.5}, DRIFT_RGBA8_RED}));
				DriftDrawTextFull(draw, &draw->overlay_sprites, "TOO CLOSE", (DriftTextOptions){
					.tint = DRIFT_VEC4_RED, .matrix = {1, 0, 0, 1, node->pos.x + 12, node->pos.y - 4},
				});
			}
		} else if(node->blocked_at < 1){
			// TODO cleanup
			// Draw blocked nodes in orange.
			DriftVec2 p0 = node->pos, p1 = DriftVec2Lerp(p0, pos, node->blocked_at);
			DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){pos, p1, {1.5}, {0x80, 0x00, 0x00, 0x80}}));
			DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){p1, node->pos, {1.5}, {0x80, 0x40, 0x00, 0x80}}));
			
			float pulse = 1 - fabsf(DriftWaveComplex(draw->nanos, 1).x);
			DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){p0, DriftVec2Lerp(p0, pos, pulse*node->blocked_at), {DRIFT_POWER_BEAM_RADIUS, DRIFT_POWER_BEAM_RADIUS - 1.5f}, {0x80, 0x40, 0x00, 0x80}}));
		} else {
			// Draw good links in green.
			u8 v = can_connect ? 0xFF : 0x20;
			DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){pos, node->pos, {1.5}, {0, v, 0, v}}));
		}
	}
	
	if(!can_connect){
		DriftDrawTextFull(draw, &draw->overlay_sprites, "NO POWER", (DriftTextOptions){
			.tint = DRIFT_VEC4_RED, .matrix = {1, 0, 0, 1, pos.x + 12, pos.y - 4},
		});
	}
}

static void pnode_grab(DriftUpdate* update, DriftEntity e){
	DriftComponentRemove(&update->state->power_nodes.c, e);
	for(uint i = 0; i < 1; i++) DriftComponentRemove(&update->state->flow_maps[i].c, e);
	// update->ctx->debug.pause = true;
}

void DriftPowerNodeActivate(DriftGameState* state, DriftEntity e, DriftMem* mem){
	uint idx = DriftComponentFind(&state->power_nodes.c, e);
	
	// Create the component if it doesn't already have one.
	if(idx == 0){
		idx = DriftComponentAdd(&state->power_nodes.c, e);
		
		uint transform_idx = DriftComponentFind(&state->transforms.c, e);
		state->power_nodes.position[idx] = DriftAffineOrigin(state->transforms.matrix[transform_idx]);
	}
	
	DriftVec2 pos = state->power_nodes.position[idx];
	DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(state, pos, mem, DRIFT_POWER_BEAM_RADIUS);
	if(info.node_can_connect){
		// Make connections to other nodes.
		DRIFT_ARRAY_FOREACH(info.nodes, node_info){
			// Don't connect to itself.
			if(e.id == node_info->e.id) continue;
			
			if(node_info->node_can_connect){
				uint edge_idx = DriftTablePushRow(&state->power_edges.t);
				state->power_edges.edge[edge_idx] = (DriftPowerNodeEdge){.e0 = e, .e1 = node_info->e, .p0 = pos, .p1 = node_info->pos};
			}
		}
		
		// // Try activating nearby inactive nodes.
		// DRIFT_ARRAY_FOREACH(info.nodes, node_info){
		// 	if(node_info->e.id != e.id && !state->power_nodes.active[DriftComponentFind(&state->power_nodes.c, node_info->e)]){
		// 		DriftPowerNodeActivate(state, node_info->e, mem);
		// 	}
		// }
	}
}

static void pnode_drop(DriftUpdate* update, DriftEntity e){
	DriftGameState* state = update->state;
	uint transform_idx = DriftComponentFind(&state->transforms.c, e);
	DriftVec2 pos = DriftAffineOrigin(state->transforms.matrix[transform_idx]);
	
	DriftPowerNodeActivate(update->state, e, update->mem);
}

typedef DriftEntity DriftItemMakeFunc(DriftGameState* state, DriftItemType type, DriftVec2 pos, DriftVec2 vel);
typedef void DriftItemDrawPickupFunc(DriftDraw* draw, DriftVec2 pos);
typedef void DriftItemPickupFunc(DriftUpdate* update, DriftEntity e);

static DriftItemMakeFunc item_make_generic;

static const struct {
	DriftItemMakeFunc* make;
	DriftItemDrawPickupFunc* draw;
	DriftItemPickupFunc* grab;
	DriftItemPickupFunc* drop;
	
	uint frame0, frame_n, light_frame;
	float scale[2], light_size;
	u8 shiny;
	DriftVec4 light_color;
	DriftLight light;
} DRIFT_PICKUP_ITEMS[_DRIFT_ITEM_COUNT] = {
	[DRIFT_ITEM_POWER_NODE] = {
		.make = pnode_make, .draw = pnode_draw, .grab = pnode_grab, .drop = pnode_drop,
		.frame0 = DRIFT_SPRITE_POWER_NODE, .scale = {1, 1},
	},
	
	[DRIFT_ITEM_SCRAP] = {.make = item_make_generic, .frame0 = DRIFT_SPRITE_SCRAP, .scale = {1, 1}, .shiny = 0x80},
	[DRIFT_ITEM_ADVANCED_SCRAP] = {.make = item_make_generic, .frame0 = DRIFT_SPRITE_ADVANCED_SCRAP, .scale = {1, 1}, .shiny = 0xC0},
	
	[DRIFT_ITEM_VIRIDIUM] = {.make = item_make_generic, .frame0 = DRIFT_SPRITE_VIRIDIUM00, .frame_n = DRIFT_SPRITE_VIRIDIUM11, .scale = {0.6f, 0.8f}, .shiny = 0x80},
	[DRIFT_ITEM_BORONITE] = {.make = item_make_generic, .frame0 = DRIFT_SPRITE_BORON00, .scale = {0.6f, 0.8f}, .shiny = 0x80},
	[DRIFT_ITEM_RADONITE] = {.make = item_make_generic, .frame0 = DRIFT_SPRITE_RADONITE00, .scale = {0.6f, 0.8f}, .shiny = 0x80},
	[DRIFT_ITEM_METRIUM] = {.make = item_make_generic, .frame0 = DRIFT_SPRITE_METRIUM00, .scale = {0.6f, 0.8f}, .shiny = 0x80},
	
	[DRIFT_ITEM_LUMIUM] = {
		.make = item_make_generic, .frame0 = DRIFT_SPRITE_LUMIUM, .scale = {1, 1},
		.light_frame = DRIFT_SPRITE_LIGHT_RADIAL, .light_color = (DriftVec4){{0.24f, 0.16f, 0.04f, 2.00f}}, .light_size = 100,
	},
	[DRIFT_ITEM_FLOURON] = {.make = item_make_generic, .frame0 = DRIFT_SPRITE_FLOURON, .scale = {1, 1}},
	[DRIFT_ITEM_FUNGICITE] = {.make = item_make_generic, .frame0 = DRIFT_SPRITE_FUNGICITE, .scale = {1, 1}},
	[DRIFT_ITEM_MORPHITE] = {.make = item_make_generic, .frame0 = DRIFT_SPRITE_MORPHITE, .scale = {1, 1}},
	
	[DRIFT_ITEM_COPPER] = {.make = item_make_generic, .frame0 = DRIFT_SPRITE_COPPER00, .scale = {1, 1}, .shiny = 0x80},
	[DRIFT_ITEM_SILVER] = {.make = item_make_generic, .frame0 = DRIFT_SPRITE_SILVER00, .scale = {1, 1}, .shiny = 0xFF},
	[DRIFT_ITEM_GOLD] = {.make = item_make_generic, .frame0 = DRIFT_SPRITE_GOLD00, .scale = {1, 1}, .shiny = 0xFF},
	[DRIFT_ITEM_GRAPHENE] = {.make = item_make_generic, .frame0 = DRIFT_SPRITE_GRAPHENE, .scale = {1, 1}, .shiny = 0xFF},
};

static float scale_for_item(DriftItemType type, uint id){
	return DriftLerp(DRIFT_PICKUP_ITEMS[type].scale[0], DRIFT_PICKUP_ITEMS[type].scale[1], fmodf((float)DRIFT_PHI*id, 1));
}

static DriftEntity item_make_generic(DriftGameState* state, DriftItemType type, DriftVec2 pos, DriftVec2 vel){
	DriftEntity e = DriftMakeEntity(state);
	uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
	
	uint scan_idx = DriftComponentAdd(&state->scan.c, e);
	state->scan.type[scan_idx] = DRIFT_ITEMS[type].scan;
	
	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	state->bodies.position[body_idx] = pos;
	state->bodies.velocity[body_idx] = DriftVec2Mul(vel, 0.25f);
	float a = pos.x;
	state->bodies.rotation[body_idx] = DriftVec2ForAngle(a*2*(float)M_PI);
	state->bodies.angular_velocity[body_idx] = 0; // TODO need better random spin?
	
	DriftFrame frame = DRIFT_FRAMES[DRIFT_PICKUP_ITEMS[type].frame0];
	float radius = scale_for_item(type, e.id)*(frame.bounds.r - frame.bounds.l)/2;
	float mass = radius*radius/5;
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 1/(mass*0.5f*radius*radius);
	state->bodies.radius[body_idx] = radius;
	state->bodies.collision_type[body_idx] = DRIFT_COLLISION_ITEM;
	
	return e;
}

DriftSprite DriftSpriteForItem(DriftItemType type, DriftAffine transform, uint id, uint tick){
	DriftSpriteEnum frame0 = DRIFT_PICKUP_ITEMS[type].frame0;
	uint frame_n = DRIFT_PICKUP_ITEMS[type].frame_n ?: frame0;
	float scale = scale_for_item(type, id);
	return (DriftSprite){
		.frame = DRIFT_FRAMES[frame0 + (tick/5)%(frame_n - frame0 + 1)], .color = DRIFT_RGBA8_WHITE,
		.matrix = DriftAffineMul(transform, (DriftAffine){scale, 0, 0, scale, 0, 0}),
		.shiny = DRIFT_PICKUP_ITEMS[type].shiny,
	};
}

static DriftLight light_for_item(DriftItemType type, DriftAffine matrix){
	DriftSpriteEnum frame = DRIFT_PICKUP_ITEMS[type].light_frame;
	float size = DRIFT_PICKUP_ITEMS[type].light_size;
	return (DriftLight){
		.frame = DRIFT_FRAMES[frame], .color = DRIFT_PICKUP_ITEMS[type].light_color,
		.matrix = DriftAffineMul(matrix, (DriftAffine){size, 0, 0, size, 0, 0}),
	};
}

DriftEntity DriftItemMake(DriftGameState* state, DriftItemType type, DriftVec2 pos, DriftVec2 vel, uint tile_idx){
	DRIFT_ASSERT(DRIFT_PICKUP_ITEMS[type].make, "Item is not a pickup.");
	
	DriftEntity e = DRIFT_PICKUP_ITEMS[type].make(state, type, pos, vel);
	uint pickup_idx = DriftComponentAdd(&state->items.c, e);
	state->items.type[pickup_idx] = type;
	state->items.tile_idx[pickup_idx] = tile_idx;
	
	return e;
}

void DriftItemDraw(DriftDraw* draw, DriftItemType type, DriftVec2 pos, uint id, uint tick){
	DRIFT_ASSERT(DRIFT_PICKUP_ITEMS[type].make, "Item is not a pickup.");
	
	DriftAffine matrix = (DriftAffine){1, 0, 0, 1, pos.x, pos.y};
	DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteForItem(type, matrix, id, tick));
	if(DRIFT_PICKUP_ITEMS[type].light_frame) DRIFT_ARRAY_PUSH(draw->lights, light_for_item(type, matrix));
	
	DriftItemDrawPickupFunc* func = DRIFT_PICKUP_ITEMS[type].draw;
	if(func) func(draw, pos);
}

void DriftItemGrab(DriftUpdate* update, DriftEntity entity, DriftItemType type){
	DRIFT_ASSERT(DRIFT_PICKUP_ITEMS[type].make, "Item is not a pickup.");
	DriftComponentRemove(&update->state->items.c, entity);
	
	DriftItemPickupFunc* func = DRIFT_PICKUP_ITEMS[type].grab;
	if(func) func(update, entity);
}

void DriftItemDrop(DriftUpdate* update, DriftEntity entity, DriftItemType type){
	DRIFT_ASSERT(DRIFT_PICKUP_ITEMS[type].make, "Item is not a pickup.");
	if(DriftComponentFind(&update->state->items.c, entity) == 0){
		uint pickup_idx = DriftComponentAdd(&update->state->items.c, entity);
		update->state->items.type[pickup_idx] = type;
	}
	
	DriftItemPickupFunc* func = DRIFT_PICKUP_ITEMS[type].drop;
	if(func) func(update, entity);
}

void DriftDrawItems(DriftDraw* draw){
	DriftGameState* state = draw->state;
	uint tick = draw->tick;
	DriftRGBA8 scan_color = {0x00, 0x80, 0x80, 0x80};
	if(state->status.disable_scan) scan_color = DRIFT_RGBA8_CLEAR;
	
	DriftAffine vp_matrix = draw->vp_matrix;
	
	DriftPlayerData* player = state->players.data + DriftComponentFind(&state->players.c, draw->state->player);
	float grab_fade = player->tool_idx == DRIFT_TOOL_GRAB ? player->tool_anim : 0;
	const DriftRGBA8 grab_color = DriftRGBA8Fade((DriftRGBA8){0x00, 0x80, 0x00, 0x80}, grab_fade);
	
	uint item_idx, transform_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&item_idx, &state->items.c},
		{&transform_idx, &state->transforms.c},
		{},
	});
	while(DriftJoinNext(&join)){
		DriftAffine m = state->transforms.matrix[transform_idx];
		DriftVec2 pos = DriftAffineOrigin(m);
		if(!DriftAffineVisibility(vp_matrix, pos, (DriftVec2){100, 100})) continue;
		
		DriftItemType type = state->items.type[item_idx];
		if(type == DRIFT_ITEM_POWER_NODE){
			// Draw just the grab indicators for power nodes.
			if(state->scan_progress[DRIFT_SCAN_POWER_NODE] >= 1) DriftHUDIndicator(draw, pos, grab_color);
		} else {
			uint scan_idx = DriftComponentFind(&state->scan.c, join.entity);
			bool needs_scan = state->scan_progress[state->scan.type[scan_idx]] < 1;
			DriftRGBA8 flash = DriftHUDIndicator(draw, pos, needs_scan ? scan_color : grab_color);
			
			DriftSprite sprite = DriftSpriteForItem(type, m, join.entity.id, tick + join.entity.id);
			DRIFT_ARRAY_PUSH(draw->fg_sprites, sprite);
			if(DRIFT_PICKUP_ITEMS[type].light_frame) DRIFT_ARRAY_PUSH(draw->lights, light_for_item(type, m));
				
			if(flash.a > 0){
				sprite.color = flash;
				DRIFT_ARRAY_PUSH(draw->flash_sprites, sprite);
			}
		}
	}
}

void DriftTickItemSpawns(DriftUpdate* update){
	static DriftRandom rand[1];
	DriftGameState* state = update->state;
	DriftTerrain* terra = state->terra;

	DriftVec2 player_pos = ({
		uint body_idx = DriftComponentFind(&state->bodies.c, update->state->player);
		state->bodies.position[body_idx];
	});
	
	uint indexes[200];
	uint tile_count = DriftTerrainSpawnTileIndexes(terra, indexes, 200, player_pos, DRIFT_SPAWN_RADIUS);
	
	for(uint i = 0; i < tile_count; i++){
		uint tile_idx = indexes[i];
		uint spawn_count = DriftTerrainTileResources(terra, tile_idx);
		if(spawn_count == 0) continue;
		
		DRIFT_ASSERT(spawn_count <= 6, "spawn count overflow on tile %d of %d.", tile_idx, spawn_count);
		DriftVec2 locations[6];
		uint location_count = DriftTerrainSpawnLocations(terra, locations, 6, tile_idx, tile_idx, 4);
		if(location_count < spawn_count) spawn_count = location_count;
		
		for(uint i = 0; i < spawn_count; i++){
			DriftVec2 pos = locations[i];
			
			DriftItemType type = DRIFT_ITEM_NONE;
			DriftReservoir res = DriftReservoirMake(rand);
			switch(DriftTerrainSampleBiome(terra, pos).idx){
				case 0:{
					if(DriftReservoirSample(&res, 100)) type = DRIFT_ITEM_VIRIDIUM;
					// if(DriftReservoirSample(&res, 5)) type = DRIFT_ITEM_LUMIUM;
					// if(DriftReservoirSample(&res, 5)) type = DRIFT_ITEM_COPPER;
					// if(DriftReservoirSample(&res, 1)) type = DRIFT_ITEM_SCRAP;
				} break;
				
				case 2:{
					if(DriftReservoirSample(&res, 5)) type = DRIFT_ITEM_BORONITE;
					// if(DriftReservoirSample(&res, 5)) type = DRIFT_ITEM_FLOURON;
					if(DriftReservoirSample(&res, 1)) type = DRIFT_ITEM_SILVER;
					// if(DriftReservoirSample(&res, 1)) type = DRIFT_ITEM_SCRAP;
				} break;
				
				case 1:{
					if(DriftReservoirSample(&res, 5)) type = DRIFT_ITEM_RADONITE;
					// if(DriftReservoirSample(&res, 5)) type = DRIFT_ITEM_FUNGICITE;
					if(DriftReservoirSample(&res, 1)) type = DRIFT_ITEM_GOLD;
				// 	if(DriftReservoirSample(&res, 1)) type = DRIFT_ITEM_ADVANCED_SCRAP;
				} break;
				
				case 3:{
					if(DriftReservoirSample(&res, 5)) type = DRIFT_ITEM_METRIUM;
				// 	if(DriftReservoirSample(&res, 5)) type = DRIFT_ITEM_MORPHITE;
				// 	if(DriftReservoirSample(&res, 5)) type = DRIFT_ITEM_GRAPHENE;
				// 	if(DriftReservoirSample(&res, 1)) type = DRIFT_ITEM_ADVANCED_SCRAP;
				} break;
				
				// TODO
				default:{
					if(DriftReservoirSample(&res, 100)) type = DRIFT_ITEM_VIRIDIUM;
				} break;
			}
			
			DriftEntity e = DriftItemMake(state, type, pos, DRIFT_VEC2_ZERO, tile_idx);
		}
	}

	uint item_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &state->items.c, .variable = &item_idx},
		{.component = &state->bodies.c, .variable = &body_idx},
		{},
	});
	
	while(DriftJoinNext(&join)){
		DriftVec2 delta = DriftVec2Sub(player_pos, state->bodies.position[body_idx]);
		if(DriftVec2Length(delta) > DRIFT_SPAWN_RADIUS){
			bool from_biomass = DRIFT_ITEMS[state->items.type[item_idx]].from_biomass;
			uint tile_idx = state->items.tile_idx[item_idx];
			(from_biomass ? DriftTerrainTileBiomassInc : DriftTerrainTileResourcesInc)(terra, tile_idx);
			DriftDestroyEntity(state, join.entity);
		}
	}
}

#define UI_LINE_HEIGHT 10
#define STR_CORRUPT_DATA "MISSING DATA"

typedef struct {
	DriftDraw* draw;
	DriftGameState* state;
	mu_Context* mu;
	DriftItemType selected;
	bool focused;
} RowContext;

static const char* TextColor(DriftVec4 color){
	DriftRGBA8 c = DriftRGBA8FromColor(color);
	static char buffer[16];
	sprintf(buffer, "%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
	return buffer;
}

#define TEXT_ENABLED "{#CBCBCA95}"
#define TEXT_ENABLED_GREEN "{#2EC20098}"
#define TEXT_DISABLED "{#5353534B}"
#define TEXT_DISABLED_RED "{#8418185E}"

static void craft_item_row(RowContext* ctx, DriftItemType item_type){
	DriftGameState* state = ctx->state;
	
	const DriftItem* item = DRIFT_ITEMS + item_type;
	DRIFT_ASSERT_WARN(item->ingredients[0].type, "Item is not craftable");
	
	const char* name = DriftItemName(item_type);
	DRIFT_ASSERT_WARN(item->scan, "Scan type not set for '%s'.", name);
	uint item_count = DriftPlayerItemCount(state, item_type);
	bool is_recipe_known = state->scan_progress[item->scan] >= 1;
	bool can_craft = is_recipe_known;
	bool ingredients_known = true;
	
	const DriftIngredient* ingredients = item->ingredients;
	uint ingredient_count = 0, known_count = 0;
	while(ingredient_count < DRIFT_ITEM_MAX_INGREDIENTS){
		DriftItemType ingredient_type = ingredients[ingredient_count].type;
		if(ingredient_type == DRIFT_ITEM_NONE) break;
		
		uint have = state->inventory.skiff[ingredient_type], need = ingredients[ingredient_count].count;
		can_craft &= have >= need;
		
		bool ingredient_known = (state->scan_progress[DRIFT_ITEMS[ingredient_type].scan] == 1);
		known_count += ingredient_known;
		ingredients_known &= ingredient_known;
		ingredient_count++;
	}
	
	mu_layout_row(ctx->mu, 1, (int[]){-1}, 16);
	bool gfocus = mu_begin_group(ctx->mu, 0);{
		if(mu_group_is_hovered(ctx->mu)){
			ctx->selected = item_type;
			ctx->focused = gfocus;
		}
		
		mu_layout_row(ctx->mu, 2, (int[]){-30, -1}, UI_LINE_HEIGHT);
		if(item->limit == 1 && item_count){
			mu_labelf(ctx->mu, TEXT_DISABLED"%s", name);
			mu_labelf(ctx->mu, "{#23760151} {!%d}", DRIFT_SPRITE_TEXT_CHECK);
		} else if(can_craft){
			mu_labelf(ctx->mu, TEXT_ENABLED"%s", name);
			if(item_count) mu_labelf(ctx->mu, TEXT_ENABLED"% 3d", item_count);
		} else if(is_recipe_known){
			mu_labelf(ctx->mu, TEXT_DISABLED"%s", name);
			if(item_count) mu_labelf(ctx->mu, TEXT_DISABLED"% 3d", item_count);
		} else {
			if(ingredients_known){
				mu_labelf(ctx->mu, TEXT_ENABLED_GREEN"%s", name);
				mu_label(ctx->mu, ctx->draw->tick/10%2 ? TEXT_ENABLED_GREEN"NEW" : TEXT_DISABLED"NEW");
			} else {
				mu_labelf(ctx->mu, TEXT_DISABLED_RED"%s", STR_CORRUPT_DATA);
				mu_labelf(ctx->mu, TEXT_DISABLED_RED"%d/%d", known_count, ingredient_count);
			}
		}
	} mu_end_group(ctx->mu);
}

static void missing_ingredient(mu_Context* mu, DriftItemType type){
	mu_layout_row(mu, 1, (int[]){-1}, UI_LINE_HEIGHT);
	static const char* missing_scan = TEXT_DISABLED"- "TEXT_DISABLED_RED"MISSING SCAN";
	static const char* missing_research = TEXT_DISABLED"- "TEXT_DISABLED_RED"MISSING RESEARCH";
	mu_label(mu, DRIFT_ITEMS[type].is_part ? missing_research : missing_scan);
}

static bool fab_button(mu_Context* mu, const char* label, DriftGameState* state){
	if(state->fab.progress > 0){
		mu_Rect r = mu_layout_next(mu);
		mu->draw_frame(mu, r, MU_COLOR_GROUPBG);
		mu_draw_control_text(mu, "Busy...", r, MU_COLOR_TEXT, MU_OPT_ALIGNCENTER);
		return false;
	} else {
		return mu_button(mu, label);
	}
}

static void craft_pane(RowContext* ctx){
	mu_Context* mu = ctx->mu;
	DriftGameState* state = ctx->state;
	const DriftItem* item = DRIFT_ITEMS + ctx->selected;
	
	DriftScanType scan_type = item->scan;
	const DriftScan* scan = DRIFT_SCANS + scan_type;
	float progress = state->scan_progress[scan_type];
	uint item_count = DriftPlayerItemCount(state, ctx->selected);
	bool is_recipe_known = progress == 1;
	bool already_full = item_count + (item->makes ?: 1) > item->limit;
	bool can_craft = is_recipe_known && !already_full;
	bool ingredients_known = true;
	
	const DriftIngredient* ingredients = item->ingredients;
	uint ingredient_count = 0, known_count = 0;
	while(ingredient_count < DRIFT_ITEM_MAX_INGREDIENTS){
		DriftItemType ingredient_type = ingredients[ingredient_count].type;
		if(ingredient_type == DRIFT_ITEM_NONE) break;
		
		uint have = state->inventory.skiff[ingredient_type], need = ingredients[ingredient_count].count;
		bool have_enough = have >= need;
		bool ingredient_known = state->scan_progress[DRIFT_ITEMS[ingredient_type].scan] == 1;
		known_count += ingredient_known;
		can_craft &= have_enough && ingredient_known;
		ingredients_known &= ingredient_known;
		
		ingredient_count++;
	}
	
	const char* img_name = DriftItemName(progress == 1 ? ctx->selected : DRIFT_ITEM_NONE);
	static const char* LOADED;
	if(LOADED != img_name){
		DriftDraw* draw = ctx->draw;
		
		LOADED = img_name;
		char* filename = DriftSMPrintf(draw->mem, "gfx/scans/%s.qoi", img_name);
		
		// Normalize file name.
		for(char* c = filename; *c; c++){
			if('A' <= *c && *c <='Z') *c += 0x20;
			if(*c == ' ') *c = '_';
		}
		DRIFT_LOG("loading scan image '%s'", filename);
		
		DriftImage img = DriftAssetLoadImage(DriftSystemMem, filename);
		img.pixels = DriftRealloc(draw->mem, img.pixels, img.w*img.h*4, DRIFT_ATLAS_SIZE*DRIFT_ATLAS_SIZE*4);
		
		const DriftGfxDriver* driver = draw->shared->driver;
		uint queue = tina_job_switch_queue(draw->job, DRIFT_JOB_QUEUE_GFX);
		driver->load_texture_layer(driver, draw->shared->atlas_texture, DRIFT_ATLAS_SCAN, img.pixels);
		tina_job_switch_queue(draw->job, queue);
	}
	
	mu_layout_row(mu, 1, (int[]){256}, 64); {
		mu_Rect r = mu_layout_next(mu);
		mu_draw_icon(mu, -DRIFT_SPRITE_SCAN_IMAGE, r, (mu_Color){0xFF, 0xFF, 0xFF, 0xFF});
	}
	
	const char* item_name = DriftItemName(ctx->selected);
	mu_layout_row(mu, 3, (int[]){-160, -90, -2}, 0);
	if(item->limit == 1 && item_count){
		// Already equipped
		mu_labelf(mu, "{#808080FF}%s", item_name);
		mu_label(mu, TEXT_ENABLED_GREEN"equipped");
		
		mu_layout_row(mu, 1, (int[]){-1}, UI_LINE_HEIGHT);
		mu_text(mu, scan->description ?: "TODO add description to scan table");
		
		const char* usage = scan->usage;
		if(usage){
			mu_label(mu, "{#808080FF}Controls:");
			mu_text(mu, usage);
		}
	} else if(is_recipe_known){
		// Item can be built.
		if(item->makes){
			mu_labelf(mu, "{#808080FF}%s x%d", item_name, item->makes);
		} else {
			mu_labelf(mu, "{#808080FF}%s", item_name);
		}
		mu_labelf(mu, DRIFT_TEXT_GRAY"(have % 2d)", item_count);
		
		// TODO apply limit here?
		if(can_craft && (fab_button(mu, "Build {@ACCEPT}", state) || ctx->focused)){
			state->fab.is_research = false;
			state->fab.item = ctx->selected;
			for(uint p = 0; p < ingredient_count; p++){
				state->inventory.skiff[ingredients[p].type] -= ingredients[p].count;
			}
		}
		
		mu_layout_row(mu, 2, (int[]){-160, -2}, UI_LINE_HEIGHT);
		mu_label(mu, DRIFT_TEXT_GRAY"  Time");
		mu_labelf(mu, DRIFT_TEXT_GRAY"    %2d:%02d", item->duration/60, item->duration%60);
		for(uint i = 0; i < ingredient_count; i++){
			uint ingredient_type = ingredients[i].type;
			const char* ingredient_name = DriftItemName(ingredient_type);
			uint have = state->inventory.skiff[ingredient_type], need = ingredients[i].count;
			bool enough = have >= need;
			mu_labelf(mu, "%s- %s", enough ? TEXT_ENABLED_GREEN : TEXT_DISABLED, ingredient_name);
			mu_labelf(mu, "%s %3d / %2d", (enough ? TEXT_ENABLED_GREEN : TEXT_DISABLED), have, need);
		}
		
		mu_layout_row(mu, 1, (int[]){-1}, UI_LINE_HEIGHT);
		mu_text(mu, scan->description ?: "TODO add description to scan table");
	} else if(ingredients_known){
		// Item needs to be researched.
		mu_labelf(mu, "{#808080FF}%s", item_name);
		mu_layout_next(mu);
		
		if(known_count == ingredient_count && progress < 1){
			if(fab_button(mu, "Research {@ACCEPT}", state) || ctx->focused){
				state->fab.is_research = true;
				state->fab.item = ctx->selected;
			}
		}
		
		mu_layout_row(mu, 1, (int[]){-1}, UI_LINE_HEIGHT);
		mu_labelf(mu, DRIFT_TEXT_GRAY"  Time: %d:%02d", scan->duration/60, scan->duration%60);
		for(uint i = 0; i < ingredient_count; i++){
			uint ingredient_type = ingredients[i].type;
			const char* ingredient_name = DriftItemName(ingredient_type);
			if(state->scan_progress[DRIFT_ITEMS[ingredient_type].scan] == 1){
				mu_labelf(mu, TEXT_ENABLED_GREEN"- %s", ingredient_name);
			}
		}
		
		mu_label(mu, "Ready to {#008080FF}research"DRIFT_TEXT_WHITE".");
	} else {
		// Haven't discovered ingredients yet.
		mu_label(mu, DRIFT_TEXT_RED STR_CORRUPT_DATA);
		mu_layout_next(mu);
		
		mu_layout_row(mu, 1, (int[]){-1}, UI_LINE_HEIGHT);
		for(uint i = 0; i < ingredient_count; i++){
			uint ingredient_type = ingredients[i].type;
			const char* ingredient_name = DriftItemName(ingredient_type);
			if(state->scan_progress[DRIFT_ITEMS[ingredient_type].scan] == 1){
				mu_labelf(mu, TEXT_ENABLED_GREEN"- %s", ingredient_name);
			} else {
				missing_ingredient(mu, ingredient_type);
			}
		}
		
		mu_layout_row(mu, 1, (int[]){-1}, UI_LINE_HEIGHT);
		mu_label(mu, "{#008080FF}Scan"DRIFT_TEXT_WHITE" or {#008080FF}research"DRIFT_TEXT_WHITE" to rediscover materials.");
	}
}

static void progress_bar(mu_Context* mu, mu_Rect r, float progress){
	mu->draw_frame(mu, r, MU_COLOR_GROUPBG);
	// Start the width at the smallest sliceable size.
	int min_width = 9, w = r.w - min_width;
	r.w = min_width + (int)DriftClamp(w*progress, 0, w);
	mu->draw_frame(mu, r, MU_COLOR_GROUPHOVER);
}

static int item_compare(const void* a, const void* b){
	return strcmp(DriftItemName(*(DriftItemType*)a), DriftItemName(*(DriftItemType*)b));
}

static void craft_ui(mu_Context* mu, DriftDraw* draw){
	DriftGameState* state = draw->state;
	RowContext ctx = {.draw = draw, .mu = mu, .state = state, .selected = DRIFT_ITEM_NONE};
	mu_layout_row(mu, 2, (int[]){-260, -1}, -1);
	
	mu_begin_panel(mu, "list");
	if(draw->ctx->debug.show_ui){
		mu_layout_row(mu, 1, (int[]){-1}, 0);
		if(mu_button(mu, "cheaty cheater")){
			state->inventory.skiff[DRIFT_ITEM_VIRIDIUM] += 100;
			state->inventory.skiff[DRIFT_ITEM_LUMIUM] += 100;
			state->inventory.skiff[DRIFT_ITEM_SCRAP] += 100;
			state->inventory.skiff[DRIFT_ITEM_POWER_SUPPLY] += 100;
			state->inventory.skiff[DRIFT_ITEM_OPTICS] += 100;
			state->inventory.skiff[DRIFT_ITEM_POWER_NODE] += 100;
		}
	}
	
	mu_label(mu, "Items:");
	craft_item_row(&ctx, DRIFT_ITEM_POWER_NODE);
	mu_label(mu, "Parts:");
	craft_item_row(&ctx, DRIFT_ITEM_OPTICS);
	craft_item_row(&ctx, DRIFT_ITEM_POWER_SUPPLY);
	mu_label(mu, "Tool Upgrades:");
	craft_item_row(&ctx, DRIFT_ITEM_HEADLIGHT);
	craft_item_row(&ctx, DRIFT_ITEM_MINING_LASER);
	mu_label(mu, "Weapon Upgrades:");
	craft_item_row(&ctx, DRIFT_ITEM_AUTOCANNON);
	craft_item_row(&ctx, DRIFT_ITEM_SHIELD_L2);
	mu_label(mu, "Cargo Upgrades:");
	craft_item_row(&ctx, DRIFT_ITEM_CARGO_L2);
	craft_item_row(&ctx, DRIFT_ITEM_CARGO_L3);
	mu_label(mu, "Skiff Upgrades:");
	craft_item_row(&ctx, DRIFT_ITEM_FAB_RADIO);
	craft_item_row(&ctx, DRIFT_ITEM_STORAGE_L2);
	craft_item_row(&ctx, DRIFT_ITEM_STORAGE_L3);
	mu_label(mu, "Node Upgrades:");
	craft_item_row(&ctx, DRIFT_ITEM_NODES_L2);
	craft_item_row(&ctx, DRIFT_ITEM_NODES_L3);
	mu_label(mu, "Drone Upgrades:");
	craft_item_row(&ctx, DRIFT_ITEM_DRONE);
	mu_end_panel(mu);
	
	mu_layout_begin_column(mu);
	craft_pane(&ctx);
	mu_layout_end_column(mu);
	
	mu_Container* win = mu_get_current_container(mu);
	mu_Rect prog_rect = {win->rect.x, win->rect.y + win->rect.h + 10, 330, 20};
	
	mu_Container* prog_win = mu_get_container(mu, "FABTASK");
	prog_win->rect = prog_rect;
	prog_win->open = state->fab.progress > 0;
	
	if(mu_begin_window_ex(mu, "FABTASK", prog_win->rect, MU_OPT_NOTITLE | MU_OPT_NOFRAME)){
		DriftItemType item = state->fab.item;
		bool is_research = state->fab.is_research;
		
		uint duration = is_research ? DriftItemResearchDuration(item) : DriftItemBuildDuration(item);
		progress_bar(mu, prog_rect, state->fab.progress);
		
		const char* prog_text = DriftSMPrintf(draw->mem, TEXT_ENABLED"%s: "DRIFT_TEXT_GRAY"%s", is_research ? "Researching" : "Building", DriftItemName(item));
		mu_draw_control_text(mu, prog_text, prog_rect, MU_COLOR_TEXT, MU_OPT_ALIGNCENTER);
		
		uint remain = (uint)(duration - duration*state->fab.progress);
		const char* remain_text = DriftSMPrintf(draw->mem, DRIFT_TEXT_GRAY"%d:%02d", remain/60, remain%60);
		mu_draw_control_text(mu, remain_text, prog_rect, MU_COLOR_TEXT, MU_OPT_ALIGNRIGHT);
		
		mu_end_window(mu);
	}
	
	static DriftItemType transfer_items[_DRIFT_ITEM_COUNT];
	static uint transfer_count;
	static float transfer_timeout;
	bool did_transfer = false;
	
	while(true){
		bool can_transfer_node = state->inventory.skiff[DRIFT_ITEM_POWER_NODE] && state->inventory.cargo[DRIFT_ITEM_POWER_NODE] < DriftPlayerNodeCap(state);
		if(transfer_count == 0 && draw->ctx->is_docked){
			for(uint i = 0; i < _DRIFT_ITEM_COUNT; i++){
				if(i == DRIFT_ITEM_POWER_NODE) continue;
				if(state->inventory.cargo[i] > 0) transfer_items[transfer_count++] = i;
			}
			
			qsort(transfer_items, transfer_count, sizeof(*transfer_items), item_compare);
			if(can_transfer_node) transfer_items[transfer_count++] = DRIFT_ITEM_POWER_NODE;
			
			transfer_timeout = 1;
		} else if(transfer_count){
			transfer_timeout -= draw->dt;
			if(transfer_timeout < 0){
				transfer_timeout += 0.125f;
				
				// transfer an item
				for(uint i = 0; i <transfer_count; i++){
					DriftItemType item = transfer_items[i];
					if(state->inventory.cargo[item] > 0 && item != DRIFT_ITEM_POWER_NODE){
						state->inventory.skiff[item]++;
						state->inventory.cargo[item]--;
						DriftAudioPlaySample(DRIFT_BUS_UI, DRIFT_SFX_PING, (DriftAudioParams){.gain = 0.5f});
						DriftHudPushToast(draw->ctx, 0, "Stored %s", DriftItemName(item));
						goto transfer_done;
					}
				}
				
				// transfer a node
				if(can_transfer_node){
					state->inventory.skiff[DRIFT_ITEM_POWER_NODE]--;
					state->inventory.cargo[DRIFT_ITEM_POWER_NODE]++;
					DriftAudioPlaySample(DRIFT_BUS_UI, DRIFT_SFX_PING, (DriftAudioParams){.gain = 0.5f});
					DriftHudPushToast(draw->ctx, 0, "Loaded %s", DriftItemName(DRIFT_ITEM_POWER_NODE));
				}
			}
		}
		
		transfer_done: break;
	}
	
	mu_Rect popup_rect = win->body;
	mu_Vec2 pad = {20, 20};
	popup_rect.x += pad.x, popup_rect.w -= 2*pad.x;
	popup_rect.y += pad.y, popup_rect.h -= 2*pad.y;
	
	if(transfer_count) mu_open_popup_at(mu, "FABTRANS", popup_rect);
	if(mu_begin_popup(mu, "FABTRANS")){
		mu_layout_row(mu, 1, (int[]){-1}, 30);
		mu_draw_control_text(mu, "Transferring Cargo:", mu_layout_next(mu), MU_COLOR_TEXT, MU_OPT_ALIGNCENTER);
		
		mu_layout_row(mu, 4, (int[]){120, 40, 40, 40}, 10);
		
		mu_layout_next(mu);
		mu_draw_control_text(mu, DRIFT_TEXT_GRAY"Pod", mu_layout_next(mu), MU_COLOR_TEXT, MU_OPT_ALIGNRIGHT);
		mu_draw_control_text(mu, DRIFT_TEXT_GRAY"Skiff", mu_layout_next(mu), MU_COLOR_TEXT, MU_OPT_ALIGNRIGHT);
		mu_draw_control_text(mu, DRIFT_TEXT_GRAY"Max", mu_layout_next(mu), MU_COLOR_TEXT, MU_OPT_ALIGNRIGHT);
		
		for(uint i = 0; i < transfer_count; i++){
			DriftItemType item = transfer_items[i];
			const char* name = DriftItemName(item);
			uint cargo = state->inventory.cargo[item], skiff = state->inventory.skiff[item], max = DRIFT_ITEMS[item].limit;
			
			mu_draw_control_text(mu, DriftSMPrintf(draw->mem, DRIFT_TEXT_GRAY"%s:", name), mu_layout_next(mu), MU_COLOR_TEXT, MU_OPT_ALIGNRIGHT);
			mu_draw_control_text(mu, DriftSMPrintf(draw->mem, "%d", cargo), mu_layout_next(mu), MU_COLOR_TEXT, MU_OPT_ALIGNRIGHT);
			mu_draw_control_text(mu, DriftSMPrintf(draw->mem, "%d", skiff), mu_layout_next(mu), MU_COLOR_TEXT, MU_OPT_ALIGNRIGHT);
			mu_draw_control_text(mu, DriftSMPrintf(draw->mem, "%d", max), mu_layout_next(mu), MU_COLOR_TEXT, MU_OPT_ALIGNRIGHT);
		}
		
		// TODO power nodes
		
		mu_Vec2 size = {80, 20};
		mu_layout_set_next(mu, (mu_Rect){(popup_rect.w - size.x)/2, popup_rect.h - size.y - 10, size.x, size.y}, true);
		if(mu_button(mu, "Done {@ACCEPT}") || (mu->key_down & (MU_KEY_RETURN | MU_KEY_ESCAPE))){
			mu_get_current_container(mu)->open = false;
			transfer_count = 0;
			
			// transfer remaining stock immediately
			for(uint i = 0; i < _DRIFT_ITEM_COUNT; i++){
				if(i == DRIFT_ITEM_POWER_NODE) continue;
				state->inventory.skiff[i] += state->inventory.cargo[i];
				state->inventory.cargo[i] = 0;
			}
			
			uint want = DriftPlayerNodeCap(state), have = state->inventory.cargo[DRIFT_ITEM_POWER_NODE];
			if(want > have){
				uint need = want - have, available = state->inventory.skiff[DRIFT_ITEM_POWER_NODE];
				if(need > available) need = available;
				state->inventory.cargo[DRIFT_ITEM_POWER_NODE] += need;
				state->inventory.skiff[DRIFT_ITEM_POWER_NODE] -= need;
			}
		}
		
		mu_end_popup(mu);
	}
}

static const mu_Vec2 UI_SIZE = {400, 200};

static void reboot_ui(mu_Context* mu, DriftDraw* draw){
	DriftGameState* state = draw->state;
	
	static bool show_crash = true;
	if(show_crash){
		static uint line_max = 0;
		static const char* crash_txt[] = {
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Started System Logging Service.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Finished Create Volatile Files and Directories.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Started LSB: automatic crash report generation.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Started crash report submission.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Started RPC bind portmap service.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Reached target System Initialization.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Started ACPI Events Check.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Reached target Path Units.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Finished Initialize hardware monitoring sensors.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Started Thermal Daemon Service.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Finished Raise network interfaces.\n",
			DRIFT_TEXT_GRAY"[  "DRIFT_TEXT_GREEN"OK"DRIFT_TEXT_GRAY"  ] Started IIO Sensor Proxy service.\n",
			DRIFT_TEXT_GRAY"["DRIFT_TEXT_RED"FAILED"DRIFT_TEXT_GRAY"] Initialize blueprint database.\n",
			DRIFT_TEXT_RED"         Device storage corrupt!",
			NULL,
		};
	
		mu_layout_row(mu, 1, (int[]){-1}, -1);
		mu_Rect r = mu_layout_next(mu);
		for(uint i = 0; i < line_max/6; i++){
			mu_draw_text(mu, mu->style->font, crash_txt[i], -1, (mu_Vec2){r.x, r.y}, (mu_Color){0xFF, 0xFF, 0xFF, 0xFF});
			r.y += 10;
		}
		
		if(crash_txt[line_max/6]){
			line_max++;
		} else {
			static bool beep;
			if(!beep){
				DriftAudioPlaySample(DRIFT_BUS_UI, DRIFT_SFX_BOOT_FAIL, (DriftAudioParams){.gain = 0.25f});
				beep = true;
			}
			
			mu_Vec2 size = {160, 20};
			mu_layout_set_next(mu, (mu_Rect){(UI_SIZE.x - size.x)/2, (UI_SIZE.y - size.y - 8), size.x, size.y}, true);
			if(mu_button(mu, "Reboot Fabricator {@ACCEPT}") || mu->key_down & MU_KEY_RETURN) show_crash = false;
		}
	} else {
		static char* reboot_txt;
		if(reboot_txt == NULL){
			DriftData data = DriftAssetLoad(DriftSystemMem, "bin/reboot.txt");
			reboot_txt = DriftRealloc(DriftSystemMem, data.ptr, data.size, data.size + 1);
			reboot_txt[data.size] = 0;
		}
		
		mu_layout_row(mu, 1, (int[]){-1}, -1);
		mu_begin_panel_ex(mu, "reboot", MU_OPT_NOSCROLL);
		static int scroll = 0;
		mu_Container* cnt = mu_get_current_container(mu);
		cnt->scroll = (mu_Vec2){0, scroll};
		scroll += (int)(900*draw->dt);
		if(scroll > 1200) state->status.factory_needs_reboot = false;
		
		mu_layout_row(mu, 1, (int[]){-1}, -1);
		mu_text(mu, reboot_txt);
		mu_end_panel(mu);
	}
}

void DriftCraftUI(mu_Context* mu, DriftDraw* draw, DriftUIState* ui_state){
	DriftVec2 extents = draw->internal_extent;
	
	static const char* TITLE = "Fabricator";
	mu_Container* win = mu_get_container(mu, TITLE);
	win->rect = (mu_Rect){(int)(extents.x - UI_SIZE.x)/2, (int)(extents.y - UI_SIZE.y)/2, UI_SIZE.x, UI_SIZE.y};
	win->open = (*ui_state == DRIFT_UI_STATE_CRAFT);
	
	int opts = MU_OPT_NOSCROLL;
	bool needs_reboot = draw->state->status.factory_needs_reboot;
	if(needs_reboot) opts |= MU_OPT_NOCLOSE;
	
	if(mu_begin_window_ex(mu, TITLE, win->rect, opts)){
		mu_bring_to_front(mu, win);
		
		if(needs_reboot){
			reboot_ui(mu, draw);
		} else if(draw->ctx->is_docked || draw->state->inventory.skiff[DRIFT_ITEM_FAB_RADIO]){
			craft_ui(mu, draw);
			DriftUICloseIndicator(mu, win);
		} else {
			mu_draw_control_text(mu, "Build the Fab Radio to connect to your fabricator remotely ...", win->rect, MU_COLOR_TEXT, MU_OPT_ALIGNCENTER);
			DriftUICloseIndicator(mu, win);
		}
		
		if(!win->open){
			*ui_state = DRIFT_UI_STATE_NONE;
			draw->ctx->is_docked = false;
		}
		mu_end_window(mu);
	}
}
