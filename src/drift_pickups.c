#include <stdlib.h>

#include "drift_game.h"

static void PickupNull(DriftUpdate* update, DriftEntity e, DriftVec2 pos){}
static void PickupDrawNull(DriftDraw* draw, DriftVec2 pos){}

static DriftEntity ScrapMake(DriftGameState* state, DriftVec2 pos, DriftVec2 vel){
	DriftEntity e = DriftMakeEntity(state);
	
	uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
	uint sprite_idx = DriftComponentAdd(&state->sprites.c, e);
	state->sprites.data[sprite_idx].frame = DRIFT_SPRITE_NO_GFX;
	state->sprites.data[sprite_idx].color = DRIFT_RGBA8_WHITE;
	
	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	state->bodies.position[body_idx] = pos;
	state->bodies.velocity[body_idx] = DriftVec2Mul(vel, 0.25f);
	float a = pos.x;
	state->bodies.rotation[body_idx] = (DriftVec2){cosf(a*2*(float)M_PI), sinf(a*2*(float)M_PI)};
	state->bodies.angular_velocity[body_idx] = DriftVec2Length(vel)/1e5f*(2*a - 1);
	
	float radius = 4, mass = 0.1f;
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 1/(mass*0.5f*radius*radius);
	state->bodies.radius[body_idx] = radius;
	state->bodies.collision_type[body_idx] = DRIFT_COLLISION_TYPE_ITEM;
	
	return e;
}

static DriftEntity OreMake(DriftGameState* state, DriftVec2 pos, DriftVec2 vel){
	DriftEntity e = DriftMakeEntity(state);
	
	uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
	uint sprite_idx = DriftComponentAdd(&state->sprites.c, e);
	state->sprites.data[sprite_idx].frame = DRIFT_SPRITE_ORE_CHUNK0;
	state->sprites.data[sprite_idx].color = DRIFT_RGBA8_WHITE;
	
	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	state->bodies.position[body_idx] = pos;
	state->bodies.velocity[body_idx] = DriftVec2Mul(vel, 0.25f);
	float a = pos.x;
	state->bodies.rotation[body_idx] = (DriftVec2){cosf(a*2*(float)M_PI), sinf(a*2*(float)M_PI)};
	state->bodies.angular_velocity[body_idx] = DriftVec2Length(vel)/1e5f*(2*a - 1);
	
	float radius = 4, mass = 0.1f;
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 1/(mass*0.5f*radius*radius);
	state->bodies.radius[body_idx] = radius;
	state->bodies.collision_type[body_idx] = DRIFT_COLLISION_TYPE_ITEM;
	
	return e;
}

static DriftEntity LumiumMake(DriftGameState* state, DriftVec2 pos, DriftVec2 vel){
	DriftEntity e = DriftMakeEntity(state);
	
	uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
	uint sprite_idx = DriftComponentAdd(&state->sprites.c, e);
	state->sprites.data[sprite_idx].frame = DRIFT_SPRITE_LUMIUM_CHUNK0;
	state->sprites.data[sprite_idx].color = DRIFT_RGBA8_WHITE;
	state->sprites.data[sprite_idx].light = (DriftLight){
	 	.frame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .radius = 100000,
		.color = (DriftVec4){{0.15f, 0.20f, 0, 2}}, .matrix = {75, 0, 0, 75, 0, 0},
	};
	
	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	state->bodies.position[body_idx] = pos;
	state->bodies.velocity[body_idx] = DriftVec2Mul(vel, 0.25f);
	float a = pos.x;
	state->bodies.rotation[body_idx] = (DriftVec2){cosf(a*2*(float)M_PI), sinf(a*2*(float)M_PI)};
	state->bodies.angular_velocity[body_idx] = DriftVec2Length(vel)/1e5f*(2*a - 1);
	
	float radius = 4, mass = 0.1f;
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 1/(mass*0.5f*radius*radius);
	state->bodies.radius[body_idx] = radius;
	state->bodies.collision_type[body_idx] = DRIFT_COLLISION_TYPE_ITEM;
	
	return e;
}

static void PNodeGrab(DriftUpdate* update, DriftEntity e, DriftVec2 pos){
	DriftComponentRemove(&update->state->power_nodes.c, e);
}

static void PNodeDrop(DriftUpdate* update, DriftEntity e, DriftVec2 pos){
	DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(update->state, pos, update->mem);
	if(!info.valid_node) return;
	
	uint idx = DriftComponentAdd(&update->state->power_nodes.c, e);
	update->state->power_nodes.node[idx].x = (int)pos.x;
	update->state->power_nodes.node[idx].y = (int)pos.y;
	
	uint edges = info.nodes ? DriftArrayLength(info.nodes) : 0;
	for(uint i = 0; i < edges; i++){
		if(info.nodes[i].valid){
			uint edge_idx = DriftTablePushRow(&update->state->power_edges.t);
			update->state->power_edges.edge[edge_idx] = (DriftPowerNodeEdge){
				.e0 = e, .e1 = info.nodes[i].e,
				.x0 = (s16)info.pos.x, .x1 = (s16)info.nodes[i].pos.x,
				.y0 = (s16)info.pos.y, .y1 = (s16)info.nodes[i].pos.y,
			};
		}
	}
}

static void PNodeDraw(DriftDraw* draw, DriftVec2 pos){
	// TODO draw lines in parts to highlight min dist?
	DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(draw->state, pos, draw->mem);
	u8 v = info.valid_node ? 0xFF : 0x40;
	DRIFT_ARRAY_FOREACH(info.nodes, node){
		DriftRGBA8 color = {0, v, 0, v};
		if(node->is_too_close){
			color = DRIFT_RGBA8_RED;
		} else if(node->blocked_at < 1){
			color = (DriftRGBA8){0x40, 0x20, 0x00, 0x40};
			DriftVec2 p0 = node->pos, p1 = DriftVec2Lerp(p0, pos, node->blocked_at);
			DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){p0, p1, {DRIFT_POWER_BEAM_RADIUS, DRIFT_POWER_BEAM_RADIUS - 1}, color}));
		}
		
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){pos, node->pos, {1.5}, color}));
	}
}

static DriftEntity PNodeMake(DriftGameState* state, DriftVec2 pos, DriftVec2 vel){
	DriftEntity e = DriftMakeEntity(state);
	
	uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
	state->transforms.matrix[transform_idx] = (DriftAffine){1, 0, 0, 1, pos.x, pos.y};
	
	uint sprite_idx = DriftComponentAdd(&state->sprites.c, e);
	state->sprites.data[sprite_idx].frame = DRIFT_SPRITE_POWER_NODE;
	state->sprites.data[sprite_idx].color = DRIFT_RGBA8_WHITE;
	
	return e;
}

const DriftPickupItem DRIFT_PICKUPS[_DRIFT_ITEM_TYPE_COUNT] = {
	[DRIFT_ITEM_TYPE_SCRAP] = {.grab = PickupNull, .drop = PickupNull, .draw = PickupDrawNull, .make = ScrapMake},
	[DRIFT_ITEM_TYPE_ORE] = {.grab = PickupNull, .drop = PickupNull, .draw = PickupDrawNull, .make = OreMake},
	[DRIFT_ITEM_TYPE_LUMIUM] = {.grab = PickupNull, .drop = PickupNull, .draw = PickupDrawNull, .make = LumiumMake},
	[DRIFT_ITEM_TYPE_POWER_NODE] = {.grab = PNodeGrab, .drop = PNodeDrop, .draw = PNodeDraw, .make = PNodeMake},
};

DriftEntity DriftPickupMake(DriftGameState* state, DriftVec2 pos, DriftVec2 vel, DriftItemType type){
	if(DRIFT_PICKUPS[type].make == NULL) return (DriftEntity){};
	
	DriftEntity e = DRIFT_PICKUPS[type].make(state, pos, vel);
	uint pickup_idx = DriftComponentAdd(&state->pickups.c, e);
	state->pickups.type[pickup_idx] = type;
	
	return e;
}
