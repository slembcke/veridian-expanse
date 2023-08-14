/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include "drift_game.h"

// TODO This should get moved to the player struct?
static DriftVec2 MOUSE_POS = {};

static void draw_mouse(DriftDraw* draw, DriftRGBA8 color){
	if(INPUT->mouse_captured){
		DriftVec2 mouse = DriftVec2Add(DriftAffineOrigin(draw->vp_inverse), MOUSE_POS);
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = mouse, .p1 = mouse, .radii = {3}, .color = color}));
	}
}

static void update_base_move(DriftUpdate* update, DriftPlayerData* player){
	player->desired_velocity = DriftVec2Mul(DriftInputJoystick(0, 1), DRIFT_PLAYER_SPEED);
}

static void update_base_look(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	MOUSE_POS = DriftVec2Clamp(DriftVec2FMA(MOUSE_POS, INPUT->mouse_rel, APP->prefs.mouse_sensitivity), 150);
	
	if(INPUT->mouse_captured){
		player->desired_rotation = DriftVec2Mul(MOUSE_POS, 1/fmaxf(DriftVec2Length(MOUSE_POS), 64.0f));
	} else {
		player->desired_rotation = DriftInputJoystick(2, 3);
	}
	
	player->reticle = DriftVec2Mul((DriftVec2){transform.c, transform.d}, 64);
}

static void draw_base_look(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	if(draw->state->status.disable_look) return;
	
	DriftVec2 pos = DriftAffineOrigin(transform), rot = player->desired_rotation;
	DriftAffine m1 = (DriftAffine){rot.y, -rot.x, rot.x, rot.y, pos.x, pos.y};
	float r = 1.5f*DriftVec2Length(rot);
	DriftVec2 p1[] = {
		DriftAffinePoint(m1, (DriftVec2){-4, 36}),
		DriftAffinePoint(m1, (DriftVec2){ 0, 38}),
		DriftAffinePoint(m1, (DriftVec2){ 4, 36}),
	};
	DriftRGBA8 green = DriftRGBA8FromColor(DriftVec4Mul((DriftVec4){{0, 1, 0, 0.5f}}, player->tool_anim));
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p1[0], .p1 = p1[1], .radii = {r}, .color = green}));
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p1[1], .p1 = p1[2], .radii = {r}, .color = green}));
	
	DriftVec2 p0[] = {
		DriftAffinePoint(transform, (DriftVec2){-6, 37}),
		DriftAffinePoint(transform, (DriftVec2){ 0, 40}),
		DriftAffinePoint(transform, (DriftVec2){ 6, 37}),
	};
	DriftRGBA8 red = DriftRGBA8FromColor(DriftVec4Mul((DriftVec4){{1, 0, 0, 0.5f}}, player->tool_anim));
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p0[0], .p1 = p0[1], .radii = {1.5f}, .color = red}));
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p0[1], .p1 = p0[2], .radii = {1.5f}, .color = red}));
	
	draw_mouse(draw, (DriftRGBA8){0x00, 0xFF, 0x00, 0x80});
}

static void update_none(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	DriftGameState* state = update->state;
	DriftPlayerInput* input = &INPUT->player;
	DriftVec2 pos = DriftAffineOrigin(transform);
	
	if(!state->status.disable_look) update_base_look(update, player, transform);
	if(!state->status.disable_move) update_base_move(update, player);
}

static void draw_none(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	draw_base_look(draw, player, transform);
	
	float anim = DriftSmoothstep(1, 0, player->tool_anim);
	DriftAffine m_hatch = DriftAffineTRS((DriftVec2){0, 0}, 0.85f*anim, (DriftVec2){DriftLerp(1, 0.9f, anim), 1});
	DriftAffine m_r = transform, m_l = DriftAffineMul(m_r, (DriftAffine){-1, 0, 0, 1, 0, 0});
	
	DriftFrame frame = DRIFT_FRAMES[DRIFT_SPRITE_HATCH];
	DriftRGBA8 color = DRIFT_RGBA8_WHITE;
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = frame, .color = color, .matrix = DriftAffineMul(m_l, m_hatch), .shiny = 30,}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = frame, .color = color, .matrix = DriftAffineMul(m_r, m_hatch), .shiny = 30,}));
}

static const float GRABBER_RADIUS = 10;

static uint find_nearest_grabbable(DriftGameState* state, DriftVec2 position, float limit){
	uint nearest_idx = 0;
	float nearest_dist = INFINITY, nearest_weight = INFINITY;
	float power_node_weight = (state->scan_progress[DRIFT_SCAN_POWER_NODE] >= 1 ? 4 : INFINITY);
	
	uint item_idx, scan_idx, transform_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&item_idx, &state->items.c},
		{&scan_idx, &state->scan.c},
		{&transform_idx, &state->transforms.c},
		{},
	});
	
	while(DriftJoinNext(&join)){
		DriftVec2 p = DriftAffineOrigin(state->transforms.matrix[transform_idx]);
		float dist = DriftVec2Distance(position, p), weight = dist;
		
		if(state->items.type[item_idx] == DRIFT_ITEM_POWER_NODE) weight *= power_node_weight;
		if(state->scan_progress[state->scan.type[scan_idx]] < 1) weight *= 8;
		
		if(weight < nearest_weight && dist < limit){
			nearest_idx = item_idx;
			nearest_dist = dist;
			nearest_weight = weight;
		}
	}
	
	return nearest_idx;
}

static bool grabber_grab(DriftUpdate* update, DriftPlayerData* player, DriftVec2 position){
	DriftGameState* state = update->state;
	float radius = 3*GRABBER_RADIUS;
	
	uint idx = find_nearest_grabbable(state, position, radius);
	player->grabbed_type = state->items.type[idx];
	player->grabbed_entity = state->items.entity[idx];
	
	if(player->grabbed_entity.id){
		DriftItemGrab(update, player->grabbed_entity, player->grabbed_type);
		return true;
	} else {
		return false;
	}
}

static void grabber_update(DriftUpdate* update, DriftPlayerData* player, DriftVec2 position){
	DriftGameState* state = update->state;
	DriftEntity e = player->grabbed_entity;
	DRIFT_ASSERT(DriftEntitySetCheck(&state->entities, e), "Grabbed entity e%d does not exist.", e.id);
	
	uint transform_idx = DriftComponentFind(&state->transforms.c, e);
	DRIFT_ASSERT(transform_idx, "Grabbed entity e%d has no transform.", e.id);
	float x = state->transforms.matrix[transform_idx].x;
	state->transforms.matrix[transform_idx].x = position.x;
	state->transforms.matrix[transform_idx].y = position.y;
	
	uint body_idx = DriftComponentFind(&state->bodies.c, e);
	if(body_idx){
		// TODO Need to sync to player.
		state->bodies.position[body_idx] = position;
		state->bodies.velocity[body_idx] = DRIFT_VEC2_ZERO;
		state->bodies.angular_velocity[body_idx] = 0;
	}
}

static void grabber_drop(DriftUpdate* update, DriftPlayerData* player){
	DriftGameState* state = update->state;
	DriftEntity e = player->grabbed_entity;
	DRIFT_ASSERT(DriftEntitySetCheck(&state->entities, e), "Grabbed entity e%d does not exist.", e.id);
	
	DriftItemDrop(update, e, player->grabbed_type);
	player->grabbed_type = DRIFT_ITEM_NONE;
	player->grabbed_entity.id = 0;
}

static bool grabber_deny(DriftUpdate* update, DriftEntity e, DriftItemType type){
	DriftAudioPlaySample(DRIFT_BUS_HUD, DRIFT_SFX_DENY, (DriftAudioParams){.gain = 1});
	DriftItemDrop(update, e, type);
	return false;
}

static bool grabber_stash(DriftUpdate* update, DriftPlayerData* player){
	DriftGameState* state = update->state;
	DriftItemType type = player->grabbed_type;
	DriftEntity e = player->grabbed_entity;
	DRIFT_ASSERT(DriftEntitySetCheck(&state->entities, e), "Grabbed entity e%d does not exist.", e.id);
	
	player->grabbed_type = DRIFT_ITEM_NONE;
	player->grabbed_entity.id = 0;
	
	DriftScanType scan_type = state->scan.type[DriftComponentFind(&state->scan.c, e)];
	if(state->scan_progress[scan_type] < 1){
		DriftHudPushToast(update->ctx, 0, "Scan item with {@SCAN}");
		return grabber_deny(update, e, type);
	}
	
	bool no_limit = update->ctx->debug.godmode;
	uint node_cap = DriftPlayerNodeCap(state);
	if(type == DRIFT_ITEM_POWER_NODE && state->inventory.cargo[DRIFT_ITEM_POWER_NODE] >= node_cap && !no_limit){
		DriftHudPushToast(update->ctx, node_cap, DRIFT_TEXT_RED"Power Nodes Full");
		return grabber_deny(update, e, type);
	}
	
	uint cargo_cap = DriftPlayerCargoCap(state);
	if(DRIFT_ITEMS[type].mass + DriftPlayerCalculateCargo(state) > cargo_cap){
		DriftHudPushToast(update->ctx, 0, DRIFT_TEXT_RED"Cargo Full (max %d kg)", cargo_cap);
		return grabber_deny(update, e, type);
	}
	
	uint item_cap = DriftPlayerItemCap(state, type);
	if(DriftPlayerItemCount(state, type) >= item_cap && !no_limit){
		DriftHudPushToast(update->ctx, 0, DRIFT_TEXT_RED"%s Full (max %d)", DriftItemName(type), item_cap);
		return grabber_deny(update, e, type);
	}
	
	DriftRumbleLow();
	state->inventory.cargo[type]++;
	DriftHudPushToast(update->ctx, DriftPlayerItemCount(state, type), DRIFT_TEXT_GREEN"%s", DriftItemName(type));
	// DriftAudioPlaySample(DRIFT_BUS_HUD, DRIFT_SFX_DENY, (DriftAudioParams){.gain = 1});
	DriftDestroyEntity(state, e);
	return true;
}

static const DriftVec4 GRAB_LABEL_COLOR = {{0.0, 0.5, 0.0, 0.25}};

static void update_grab(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	DriftGameState* state = update->state;
	DriftPlayerInput* input = &INPUT->player;
	update_base_move(update, player);
	
	DriftVec2 pos = DriftAffineOrigin(transform);
	DriftAffine reticle_transform = {18*transform.a, 18*transform.b, 18*transform.c, 18*transform.d, 32*transform.c, 32*transform.d};
	
	bool mouse_look = INPUT->mouse_captured;
	DriftVec2 local_look = {}, pull = {};
	if(DriftInputButtonState(DRIFT_INPUT_QUICK_GRAB)){
		// First frame should be centered to grab the correct object before pulling in.
		local_look = (DriftVec2){0, DriftInputButtonPress(DRIFT_INPUT_QUICK_GRAB) ? 0 : -1};
	} else if(mouse_look){
		// Clamp mouse to a slightly larger than normal radius.
		MOUSE_POS = DriftVec2Clamp(DriftVec2FMA(MOUSE_POS, INPUT->mouse_rel, APP->prefs.mouse_sensitivity), 100);
		
		// Then pull it back in while pulling the player towards it.
		DriftVec2 clamped = DriftVec2Clamp(MOUSE_POS, 50);
		pull = DriftVec2Mul(DriftVec2Sub(MOUSE_POS, clamped), 1/update->dt);
		MOUSE_POS = DriftVec2LerpConst(MOUSE_POS, clamped, 2*DRIFT_PLAYER_SPEED*update->dt);
		
		local_look = DriftVec2Clamp(DriftAffinePoint(DriftAffineInverse(reticle_transform), MOUSE_POS), 1);
	} else {
		local_look = DriftAffineDirection(DriftAffineInverse(transform), DriftInputJoystick(2, 3));
	}
	
	// Transform look coords to reticle coords.
	DriftVec2 reticle = DriftAffinePoint(reticle_transform, local_look);
	
	// Collide reticle with the terrain.
	float ray_t = DriftTerrainRaymarch(state->terra, pos, DriftVec2Add(pos, reticle), 0, 1);
	reticle = DriftVec2Mul(reticle, ray_t);
	DriftTerrainSampleInfo info = DriftTerrainSampleFine(state->terra, DriftVec2Add(pos, reticle));
	player->reticle = reticle = DriftVec2FMA(reticle, info.grad, fmaxf(0, GRABBER_RADIUS - info.dist));
	
	uint grab_mask = DRIFT_INPUT_GRAB | DRIFT_INPUT_QUICK_GRAB;
	bool grab_state = DriftInputButtonState(grab_mask);
	if(!mouse_look){
		// Pull towards the reticle in gamepad mode.
		float amount = DriftAffinePoint(DriftAffineInverse(reticle_transform), reticle).y;
		pull = DriftVec2Mul((DriftVec2){transform.c, transform.d}, 100*fmaxf(grab_state ? 0.0f : -0.5f, amount));
	}
	
	// Update desired velocity
	DriftVec2 velocity = DriftVec2Add(player->desired_velocity, pull);
	player->desired_velocity = DriftVec2Clamp(velocity, DRIFT_PLAYER_SPEED);
	
	float rotation_rate = local_look.x*DriftSmoothstep(grab_state ? 0.5f : 0.0f, 1.0f, fabsf(local_look.x));
	// Ease the rotation rate when trying to pick something up
	if(grab_state) rotation_rate *= (1 + local_look.y);
	player->desired_rotation = DriftAffineDirection(transform, (DriftVec2){rotation_rate, 1});
	
	DriftVec2 world_reticle = DriftVec2Add(pos, reticle);
	if(player->grabbed_type == DRIFT_ITEM_NONE){
		if(DriftInputButtonPress(grab_mask)) grabber_grab(update, player, world_reticle);
	} else {
		if(DriftInputButtonRelease(grab_mask)){
			grabber_drop(update, player);
		} else if(local_look.y < - 0.80f){
			// Player pulled the grabber in, stash the item.
			uint body_idx = DriftComponentFind(&state->bodies.c, player->grabbed_entity);
			if(grabber_stash(update, player)){
				DriftHUDPushSwoop(update->ctx, DriftAffineOrigin(transform), player->reticle, GRAB_LABEL_COLOR, player->grabbed_type);
			} else if(body_idx) {
				// Make it drift away if it can't be stashed, and has a physics body.
				state->bodies.velocity[body_idx] = DriftAffineDirection(transform, (DriftVec2){0, 50});
				state->bodies.angular_velocity[body_idx] = 5;
			}
		} else if(grab_state){
			grabber_update(update, player, world_reticle);
		}
	}
	
	if(player->grabbed_type){
		// TODO force the player to drop here?
	} else if(DriftInputButtonState(DRIFT_INPUT_DROP) && state->inventory.cargo[DRIFT_ITEM_POWER_NODE] > 0){
		DriftNearbyNodesInfo nodes = DriftSystemPowerNodeNearby(state, world_reticle, update->mem, DRIFT_POWER_BEAM_RADIUS);
		if(nodes.node_can_connect && nodes.active_count > 0){
			player->last_valid_drop.pos = nodes.pos;
			player->last_valid_drop.frame = update->frame;
		}
		
		// TODO Check terrain distance here too?
		if(update->frame - player->last_valid_drop.frame == 1){
			DriftNearbyNodesInfo nodes = DriftSystemPowerNodeNearby(state, world_reticle, update->mem, DRIFT_POWER_BEAM_RADIUS);
			if(nodes.too_close_count == 0){
				DriftEntity e = DriftItemMake(update->state, DRIFT_ITEM_POWER_NODE, player->last_valid_drop.pos, DRIFT_VEC2_ZERO, 0);
				DriftItemDrop(update, e, DRIFT_ITEM_POWER_NODE);
				state->inventory.cargo[DRIFT_ITEM_POWER_NODE]--;
				DriftHudPushToast(update->ctx, 0, "Node auto-placed");
			}
		}
	} else if(DriftInputButtonRelease(DRIFT_INPUT_DROP)){
		DriftNearbyNodesInfo nodes = DriftSystemPowerNodeNearby(state, world_reticle, update->mem, DRIFT_POWER_BEAM_RADIUS);
		if(state->inventory.cargo[DRIFT_ITEM_POWER_NODE] == 0){
			// TODO blegh this is still wonky. Error should be on press?
			DriftHudPushToast(update->ctx, 0, DRIFT_TEXT_RED"Build more power nodes");
		} else if(nodes.node_can_connect && nodes.active_count > 0){
			DriftEntity e = DriftItemMake(update->state, DRIFT_ITEM_POWER_NODE, world_reticle, DRIFT_VEC2_ZERO, 0);
			DriftItemDrop(update, e, DRIFT_ITEM_POWER_NODE);
			state->inventory.cargo[DRIFT_ITEM_POWER_NODE]--;
		} else {
			DriftHudPushToast(update->ctx, 0, DRIFT_TEXT_RED"No power, cannot place node");
			DriftAudioPlaySample(DRIFT_BUS_HUD, DRIFT_SFX_DENY, (DriftAudioParams){.gain = 1});
		}
	}
}

static DriftVec2 calculate_pose(const float angle[], DriftVec2 x[], DriftVec2 q[], DriftVec2 pos0){
	DriftVec2 pos = pos0, dir = {1, 0};
	for(uint i = 0; i < 3; i++){
		dir = DriftVec2Rotate(dir, DriftVec2ForAngle(angle[i]));
		x[i] = pos, q[i] = dir;
		
		const float len = 16;
		pos = DriftVec2FMA(pos, dir, len);
	}
	
	return pos;
}

static void draw_arm_with_ik(DriftDraw* draw, DriftAffine transform, float anim, float grip, DriftArmPose* pose, DriftVec2 target, float err_smooth, float err_rate){
	DriftVec2 pos0 = DriftVec2Lerp((DriftVec2){-5, -5}, (DriftVec2){9, 9}, DriftSmoothstep(0, 0.5f, anim));
	target.x += DriftLerp(18, 8, grip);
	
	DriftVec2 x[3], q[3];
	DriftVec2 pos = calculate_pose(pose->angle, x, q, pos0);
	
	pose->current = pos;
	DriftVec2 pose_err = DriftVec2Sub(target, pose->current);
	
	// Blend the last two joint angles for that bowed out look
	float blend = 0.5f*(pose->angle[2] - pose->angle[1])*err_smooth;
	pose->angle[1] += blend, pose->angle[2] -= blend;
	
	// Solve constraints
	static const float limit_min[] = {-1, 0.1f, 0.1f}, limit_max[] = {1.1f, 1.9f, 1.9f};
	for(uint i = 0; i < 3; i++){
		DriftVec2 delta = DriftVec2Sub(pose->current, x[i]);
		// Calculate error in radians.
		float err = DriftVec2Cross(delta, pose_err)/(DriftVec2LengthSq(delta) + FLT_MIN);
		// Apply smoothing, clamp, and interpolate at the max rate.
		pose->angle[i] = DriftLerpConst(pose->angle[i], DriftClamp(pose->angle[i] + err_smooth*err, limit_min[i], limit_max[i]), err_rate);
	}
	
	// Recalculate pose and lerp with retract animation.
	float angle[3];
	static const float RETRACT_POSE[] = {1.0f, 3.1f, 3.1f};
	static const float RETRACT_TMIN[] = {0.0f, 0.3f, 0.6f};
	static const float RETRACT_TMAX[] = {0.8f, 1.0f, 1.0f};
	for(uint i = 0; i < 3; i++) angle[i] = DriftLerp(RETRACT_POSE[i], pose->angle[i], DriftSmoothstep(RETRACT_TMIN[i], RETRACT_TMAX[i], anim));
	pos = calculate_pose(angle, x, q, pos0);
	
	// Draw mount
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_GRABBER_MOUNT], .color = DRIFT_RGBA8_WHITE,
		.matrix = DriftAffineMul(transform, (DriftAffine){1, 0, 0, 1, pos0.x, pos0.y}),
	}));
	
	// Draw grippers
	float angle_3 = angle[0] + angle[1] + angle[2];
	float grip_angle = DriftLerp(+2.9f - angle_3, 3.1f, grip);
	float grip_close = DriftLerp(-1.0f + DriftWaveComplex(draw->nanos, 0.9f).x*0.5f - angle_3, -0.15f, grip);
	float grip_retract = DriftSmoothstep(0.6f, 1.0f, anim);
	float claw_angle_b = DriftLerp(-2.5f - angle_3, grip_angle + grip_close, grip_retract);
	float claw_angle_t = DriftLerp(+8.4f - angle_3, grip_angle - grip_close, grip_retract);
	
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_GRIPPER], .color = DRIFT_RGBA8_WHITE,
		.matrix = DriftAffineMul(transform, DriftAffineTRS(pos, claw_angle_t, (DriftVec2){-1, -1})),
	}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_GRIPPER], .color = DRIFT_RGBA8_WHITE,
		.matrix = DriftAffineMul(transform, DriftAffineTRS(pos, claw_angle_b, (DriftVec2){-1, 1})),
	}));
	
	// Draw arms
	static const DriftSpriteEnum frames[] = {DRIFT_SPRITE_GRABARM0, DRIFT_SPRITE_GRABARM1, DRIFT_SPRITE_GRABARM2};
	for(uint i = 0; i < 3; i++){
		DriftVec2 r = q[i];
		DriftAffine t = DriftAffineMul(transform, (DriftAffine){r.x, r.y, -r.y, r.x, x[i].x, x[i].y});
		DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = DRIFT_FRAMES[frames[i]], .color = DRIFT_RGBA8_WHITE, .matrix = t}));
		// DriftDebugTransform(draw->state, t, 5);
	}
}

static void draw_grab(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	DriftRGBA8 grab_color = player->grabbed_entity.id ? (DriftRGBA8){0x00, 0x80, 0x00, 0x80} : (DriftRGBA8){0x80, 0x00, 0x00, 0x80};
	draw_mouse(draw, grab_color);
	
	DriftGameState* state = draw->state;
	DriftAffine transform_l = DriftAffineMul(transform, (DriftAffine){-1, 0, 0, 1, 0, 0});
	DriftVec2 claw_l = DriftAffinePoint(transform_l, player->arm_l.current);
	DriftVec2 claw_r = DriftAffinePoint(transform, player->arm_r.current);
	DriftVec2 gripper_location = DriftVec2Lerp(claw_l, claw_r, 0.5f);
	
	// Draw grabber reticle.
	if(!player->grabbed_entity.id){
		DriftRGBA8 red = DriftRGBA8FromColor(DriftVec4Mul((DriftVec4){{0.5f, 0, 0, 0.5f}}, player->tool_anim));
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){
			.p0 = gripper_location, .p1 = gripper_location, .radii = {GRABBER_RADIUS, GRABBER_RADIUS - 1}, .color = red
		}));
	}
	
	// Draw grabbed item and chevron.
	DriftEntity e = player->grabbed_entity;
	if(e.id){
		DriftScanType scan_type = state->scan.type[DriftComponentFind(&state->scan.c, e)];
		bool scanned = state->scan_progress[scan_type] >= 1;
		const char* name = scanned ? DriftItemName(player->grabbed_type) : "UNSCANNED";
		
		DriftVec4 color = scanned ? GRAB_LABEL_COLOR : DRIFT_VEC4_RED;
		if(player->grabbed_type != DRIFT_ITEM_POWER_NODE) DriftHudDrawOffsetLabel(draw, DriftAffineOrigin(transform), player->reticle, color, name);
		DriftItemDraw(draw, player->grabbed_type, gripper_location, player->grabbed_entity.id, 0);
		
		float bob = 3*fabsf(DriftWaveComplex(draw->nanos, 1).x);
		DriftVec2 p[] = {
			DriftAffinePoint(transform, (DriftVec2){-4, 24 - bob}),
			DriftAffinePoint(transform, (DriftVec2){ 0, 21 - bob}),
			DriftAffinePoint(transform, (DriftVec2){ 4, 24 - bob}),
		};
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[0], .p1 = p[1], .radii = {1.5f}, .color = DRIFT_RGBA8_GREEN}));
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[1], .p1 = p[2], .radii = {1.5f}, .color = DRIFT_RGBA8_GREEN}));
	}
	
	if(state->inventory.cargo[DRIFT_ITEM_POWER_NODE]){
		// Draw power node to be dropped.
		if(DriftInputButtonState(DRIFT_INPUT_DROP) && player->grabbed_type == DRIFT_ITEM_NONE){
			DriftItemDraw(draw, DRIFT_ITEM_POWER_NODE, gripper_location, 0, 0);
		}
	}
	
	DriftVec2 wobble = DriftWaveComplex(draw->nanos, 0.5f);
	wobble.y = 0.5f*DriftVec2Rotate(wobble, wobble).y;
	float grip = DriftInputButtonState(DRIFT_INPUT_GRAB | DRIFT_INPUT_QUICK_GRAB | DRIFT_INPUT_DROP);
	DriftVec2 desired = DriftVec2FMA(player->reticle, wobble, 3*(1 - grip));
	float err_rate = 8*draw->dt, err_smooth = 1 - expf(-30*draw->dt);
	
	DriftVec2 desired_l = DriftAffineDirection(DriftAffineInverse(transform_l), desired);
	draw_arm_with_ik(draw, transform_l, player->tool_anim, grip, &player->arm_l, desired_l, err_smooth, err_rate);
	
	DriftVec2 desired_r = DriftAffineDirection(DriftAffineInverse(transform), desired);
	draw_arm_with_ik(draw, transform, player->tool_anim, grip, &player->arm_r, desired_r, err_smooth, err_rate);
}

// static void update_dig_doze(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
// 	DriftPlayerInput* input = &INPUT->player;
	
// 	update_base_move(update, player);
// 	update_base_look(update, player, transform);
	
// 	const float DIG_SPEED = 15.0f;
// 	player->desired_velocity = DriftVec2FMA(player->desired_velocity, player->desired_rotation, DIG_SPEED);
// 	float speed = DriftVec2Length(player->desired_velocity);
	
// 	DriftTerrain* terra = update->state->terra;
	
// 	player->anim_state.laser.target = 1;
// 	player->nacelle_l = 0;
// 	player->nacelle_r = 1;
	
// 	player->is_digging = DriftInputButtonState(DRIFT_INPUT_LASER) && player->energy > 0 && !player->is_overheated;
// 	if(player->is_digging){
// 		// TODO should be in fixed update?
// 		DriftVec2 pos = DriftAffineOrigin(transform);
// 		DriftTerrainDig(terra, pos, 20);
// 		player->dig_pos = pos;
// 	}
// }

// static void draw_dig_doze(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
// 	draw_base_look(draw, player, transform);
	
// 	DriftVec2 p1 = player->dig_pos;
// 	if(player->is_digging){
// 		DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteMake(DRIFT_SPRITE_LASER_DOT, (DriftRGBA8){0xFF, 0x60, 0x40, 0x00}, (DriftAffine){1, 0, 0, 1, p1.x, p1.y}));
// 		DRIFT_ARRAY_PUSH(draw->lights, DriftLightMake(true, DRIFT_SPRITE_LIGHT_RADIAL, (DriftVec4){{5.22f, 2.75f, 1.49f, 0.00f}}, (DriftAffine){320, 0, 0, 320, p1.x, p1.y}, 16));
// 	}
// }

#define LASER_LENGTH 64
#define LASER_RADIUS 24
static float TMP_LASER_ALPHA = 0; // TODO!

static void update_dig_laze(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	DriftTerrain* terra = update->state->terra;
	DriftPlayerInput* input = &INPUT->player;
	DriftVec2 p0 = DriftAffineOrigin(transform), p1 = DriftAffinePoint(transform, (DriftVec2){0, LASER_LENGTH});
	
	if(update->ctx->debug.godmode){
		update_base_move(update, player);
		update_base_look(update, player, transform);
		player->dig_pos = p0;
		DriftTerrainDig(terra, p0, 2*LASER_RADIUS);
		return;
	}
	
	if(player->is_digging){
		player->nacelle_l = DriftLerpConst(player->nacelle_l, 0, update->dt/0.1f);
		player->nacelle_r = DriftLerpConst(player->nacelle_r, 0, update->dt/0.1f);
		player->desired_velocity = DRIFT_VEC2_ZERO;
	} else {
		update_base_move(update, player);
		update_base_look(update, player, transform);
	}
	
	if(DriftInputButtonPress(DRIFT_INPUT_LASER)){
		TMP_LASER_ALPHA = DriftTerrainRaymarch(terra, p0, p1, LASER_RADIUS - 1, 1);
		player->is_digging = true;
	}
	player->is_digging &= DriftInputButtonState(DRIFT_INPUT_LASER) && player->energy > 0 && !player->is_overheated;
	
	TMP_LASER_ALPHA = DriftLerpConst(TMP_LASER_ALPHA, 1, update->dt/8);
	DriftVec2 pos = DriftVec2Lerp(p0, p1, TMP_LASER_ALPHA);
	player->dig_pos = pos;
	
	if(player->is_digging){
		// TODO should be in fixed update?
		DriftTerrainDig(terra, pos, LASER_RADIUS + 2);
	} else {
		TMP_LASER_ALPHA = 0;
	}
}

static void draw_dig_laze(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	draw_base_look(draw, player, transform);
	
	DriftVec2 p0 = DriftAffineOrigin(transform), p1 = player->dig_pos;
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){
		.p0 = p0, .p1 = DriftAffinePoint(transform, (DriftVec2){0, LASER_LENGTH}),
		.radii = {LASER_RADIUS, LASER_RADIUS - 1}, .color = DriftRGBA8FromColor(DriftVec4Mul(DRIFT_VEC4_RED, player->tool_anim)),
	}));
	
	DriftVec4 laser_glow = DriftVec4Mul((DriftVec4){{1.00f, 0.00f, 0.17f, 0.50f}}, player->tool_anim);
	
	float anim = DriftHermite3(player->tool_anim);
	float anim0 = DriftSmoothstep(0.5f, 1.0f, player->tool_anim);
	float anim1 = DriftSmoothstep(0.0f, 0.5f, player->tool_anim);
	float x = DriftLerp(12, 18, anim0);
	float y = DriftLerp( 6,  13, anim1);
	
	DriftFrame frame_laser = DRIFT_FRAMES[DRIFT_SPRITE_LASER];
	DriftFrame frame_slide = DRIFT_FRAMES[DRIFT_SPRITE_LASER_EXTENSION];
	DriftFrame frame_radial = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL];
	
	DriftAffine m_r = transform;
	DriftAffine m_laser_r0 = DriftAffineMul(m_r, (DriftAffine){1, 0, 0, 1, x, y});
	DriftAffine m_laser_r1 = DriftAffineMul(m_r, (DriftAffine){1, 0, 0, 1, 10, y});
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = frame_laser, .color = DRIFT_RGBA8_WHITE, .matrix = m_laser_r0}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = frame_laser, .color = DRIFT_RGBA8_WHITE, .matrix = m_laser_r1}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
		.frame = frame_slide, .color = DRIFT_RGBA8_WHITE,
		.matrix = DriftAffineMul(transform, (DriftAffine){1, 0, 0, 1, x, 0}),
	}));
	DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
		.frame = frame_radial, .color = laser_glow, .matrix = DriftAffineMul(m_laser_r0, (DriftAffine){70, 0, 0, 70, 0, 5}),
	}));
	
	DriftAffine m_l = DriftAffineMul(transform, (DriftAffine){-1, 0, 0, 1, 0, 0});
	DriftAffine m_laser_l0 = DriftAffineMul(m_l, (DriftAffine){1, 0, 0, 1, x, y});
	DriftAffine m_laser_l1 = DriftAffineMul(m_l, (DriftAffine){1, 0, 0, 1, 10, y});
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = frame_laser, .color = DRIFT_RGBA8_WHITE, .matrix = m_laser_l0}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = frame_laser, .color = DRIFT_RGBA8_WHITE, .matrix = m_laser_l1}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
		.frame = frame_slide, .color = DRIFT_RGBA8_WHITE,
		.matrix = DriftAffineMul(m_l, (DriftAffine){1, 0, 0, 1, x, 0}),
	}));
	DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
		.frame = frame_radial, .color = laser_glow, .matrix = DriftAffineMul(m_laser_l0, (DriftAffine){70, 0, 0, 70, 0, 5}),
	}));
	
	if(player->is_digging){
		DriftVec2 lase_target = {0, TMP_LASER_ALPHA*LASER_LENGTH + 8};
		DriftRGBA8 lase_color = {0xFF, 0xC0, 0xC0, 0x00};
		DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){
			.p0 = DriftAffineOrigin(m_laser_l0), .p1 = DriftAffinePoint(m_laser_l0, lase_target), .color = lase_color, .radii = {2, 0},
		}));
		DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){
			.p0 = DriftAffineOrigin(m_laser_l1), .p1 = DriftAffinePoint(m_laser_l1, lase_target), .color = lase_color, .radii = {2, 0},
		}));
		DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){
			.p0 = DriftAffineOrigin(m_laser_r0), .p1 = DriftAffinePoint(m_laser_r0, lase_target), .color = lase_color, .radii = {2, 0},
		}));
		DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){
			.p0 = DriftAffineOrigin(m_laser_r1), .p1 = DriftAffinePoint(m_laser_r1, lase_target), .color = lase_color, .radii = {2, 0},
		}));
		
		DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteMake(DRIFT_SPRITE_LASER_DOT, (DriftRGBA8){0xFF, 0x60, 0x40, 0x00}, (DriftAffine){1, 0, 0, 1, p1.x, p1.y}));
		DRIFT_ARRAY_PUSH(draw->lights, DriftLightMake(DRIFT_SPRITE_LIGHT_RADIAL, (DriftVec4){{26.53f, 3.83f, 0.00f, 5.00f}}, (DriftAffine){250, 0, 0, 250, p1.x, p1.y}, LASER_RADIUS/4));
	}
}

static void update_dig(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){update_dig_laze(update, player, transform);}
static void draw_dig(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){draw_dig_laze(draw, player, transform);}

typedef struct {
	DriftAffine arr[4];
} PlayerCannonTransforms;

// TODO refactor this, at least pass in the player transform directly
static PlayerCannonTransforms CalculatePlayerCannonTransforms(float cannon_anim){
	cannon_anim = DriftSmoothstep(0, 1, cannon_anim);
	DriftAffine matrix_gun0 = {1, 0, 0, 1,  8, -7 - -7*cannon_anim - 3};
	DriftAffine matrix_gun1 = {1, 0, 0, 1, 12, -5 - -5*cannon_anim - 5};
	
	return (PlayerCannonTransforms){{
		DriftAffineMul((DriftAffine){-1, 0, 0, 1, 0, 0}, matrix_gun0),
		DriftAffineMul((DriftAffine){+1, 0, 0, 1, 0, 0}, matrix_gun0),
		DriftAffineMul((DriftAffine){-1, 0, 0, 1, 0, 0}, matrix_gun1),
		DriftAffineMul((DriftAffine){+1, 0, 0, 1, 0, 0}, matrix_gun1),
	}};
}

// TODO pull out commonalities to fly?
static void update_gun(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	update_base_move(update, player);
	update_base_look(update, player, transform);
	
	player->reticle = DriftVec2Mul((DriftVec2){transform.c, transform.d}, 64);

	static uint repeat = 0;
	static float timeout = 0, inc = 0;
	timeout -= update->dt;
	
	if(player->tool_anim == 0){
		DriftAudioPlaySample(DRIFT_BUS_SFX, DRIFT_SFX_GUN_LOAD, (DriftAudioParams){.gain = 0.5f});
	}
	
	DriftPlayerInput* input = &INPUT->player;
	if(timeout < 0 && DriftInputButtonPress(DRIFT_INPUT_FIRE)){
		timeout = 0;
		
		if(player->energy == 0){
			DriftAudioPlaySample(DRIFT_BUS_HUD, DRIFT_SFX_DENY, (DriftAudioParams){.gain = 1});
			DriftHudPushToast(update->ctx, 0, DRIFT_TEXT_RED"Cannon has no power");
		} else if(player->is_overheated){
			DriftAudioPlaySample(DRIFT_BUS_HUD, DRIFT_SFX_DENY, (DriftAudioParams){.gain = 1});
		} else {
			repeat = 2, inc = 6/DRIFT_TICK_HZ;
			if(update->state->inventory.skiff[DRIFT_ITEM_AUTOCANNON]) repeat = 6, inc = 4/DRIFT_TICK_HZ;
			if(update->state->inventory.skiff[DRIFT_ITEM_ZIP_CANNON]) repeat = 12, inc = 2/DRIFT_TICK_HZ;
		}
	}
	
	if(repeat && timeout <= 0){
		PlayerCannonTransforms cannons = CalculatePlayerCannonTransforms(1);
		static uint which = 0;
		uint cannon_idx = which++ % (repeat ? 4 : 2);
		DriftAffine cannon = cannons.arr[cannon_idx];
		cannon.c -= cannon.x/300;
		
		cannon = DriftAffineMul(transform, cannon);
		DriftFireProjectile(update, DRIFT_PROJECTILE_PLAYER, DriftAffineOrigin(cannon), DriftAffineDirection(cannon, (DriftVec2){0, 1}));
		DriftRumble();
		repeat--, timeout += inc;
		
		player->energy -= 2;
		player->temp += 4e-2f;
	}
}

static void draw_gun(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	draw_base_look(draw, player, transform);
	
	PlayerCannonTransforms cannons = CalculatePlayerCannonTransforms(player->tool_anim);
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = DRIFT_FRAMES[DRIFT_SPRITE_GUN], .color = DRIFT_RGBA8_WHITE, .matrix = DriftAffineMul(transform, cannons.arr[0])}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = DRIFT_FRAMES[DRIFT_SPRITE_GUN], .color = DRIFT_RGBA8_WHITE, .matrix = DriftAffineMul(transform, cannons.arr[1])}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = DRIFT_FRAMES[DRIFT_SPRITE_GUN], .color = DRIFT_RGBA8_WHITE, .matrix = DriftAffineMul(transform, cannons.arr[2])}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = DRIFT_FRAMES[DRIFT_SPRITE_GUN], .color = DRIFT_RGBA8_WHITE, .matrix = DriftAffineMul(transform, cannons.arr[3])}));
	
	for(uint i = 0; i < 4; i++){
		DriftAffine cannon = cannons.arr[i];
		cannon.c -= cannon.x/300;
		
		DriftAffine t = DriftAffineMul(transform, cannon);
		DriftVec2 p[4] = {
			DriftAffinePoint(t, (DriftVec2){0, 60}),
			DriftAffinePoint(t, (DriftVec2){0, 85}),
			DriftAffinePoint(t, (DriftVec2){0, 140}),
			DriftAffinePoint(t, (DriftVec2){0, 150}),
		};
		DriftRGBA8 red0 = DriftRGBA8FromColor(DriftVec4Mul((DriftVec4){{0.5f, 0, 0, 0.25f}}, player->tool_anim));
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[0], .p1 = p[1], .radii = {1.0f}, .color = red0}));
		DriftRGBA8 red1 = DriftRGBA8FromColor(DriftVec4Mul((DriftVec4){{1, 0, 0, 0.5f}}, player->tool_anim));
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[2], .p1 = p[3], .radii = {1.2f}, .color = red1}));
	}
}

#define SCANNER_INNER_RADIUS 10
#define SCANNER_OUTER_RADIUS 48

static void update_scan(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	DriftGameState* state = update->state;
	DriftPlayerInput* input = &INPUT->player;
	update_base_move(update, player);
	
	DriftVec2 player_pos = DriftAffineOrigin(transform);
	DriftVec2 reticle = {};
	if(INPUT->mouse_captured){
		reticle = MOUSE_POS = DriftVec2Clamp(DriftVec2FMA(MOUSE_POS, INPUT->mouse_rel, APP->prefs.mouse_sensitivity), 120);
	} else {
		reticle = DriftVec2Clamp(DriftVec2FMA(player->reticle, DriftInputJoystick(2, 3), 500*update->dt), 120);
	}
	
	// Collide reticle with the terrain.
	float ray_t = DriftTerrainRaymarch(state->terra, player_pos, DriftVec2Add(player_pos, reticle), 0, 2);
	reticle = DriftVec2Mul(reticle, ray_t);
	player->reticle = reticle;
	
	// Ease the rotation rate when trying to pick something up
	player->desired_rotation = DriftVec2Normalize(player->reticle);
	
	uint scan_idx, transform_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &state->scan.c, .variable = &scan_idx},
		{.component = &state->transforms.c, .variable = &transform_idx},
		{},
	});
	
	DriftVec2 world_reticle = DriftVec2Add(player_pos, player->reticle);
	DriftEntity closest_entity = {};
	float closest_dist = INFINITY;
	DriftVec2 closest_pos = DRIFT_VEC2_ZERO;
	DriftScanType closest_type = DRIFT_SCAN_NONE;
	
	DriftScanType scan_restrict = state->status.scan_restrict;
	while(DriftJoinNext(&join)){
		DriftScanType type = state->scan.type[scan_idx];
		
		// Restrict scaning types while in the tutorial.
		if(scan_restrict && type != scan_restrict) continue;
		// Power nodes should be researched, not scanned.
		if(type == DRIFT_SCAN_POWER_NODE) continue;
		
		DriftVec2 scan_pos = DriftAffinePoint(state->transforms.matrix[transform_idx], DRIFT_SCANS[type].offset);
		float dist = DriftVec2Distance(world_reticle, scan_pos);
		if(dist < SCANNER_OUTER_RADIUS && dist < closest_dist){
			closest_entity = join.entity;
			closest_dist = dist;
			closest_pos = scan_pos;
			closest_type = type;
		}
	}
	
	player->scanned_entity = closest_entity;
	player->scanned_type = closest_type;
	
	float scan0 = state->scan_progress[closest_type];
	if(closest_type && update->ctx->ui_state == DRIFT_UI_STATE_NONE){
		DriftGameContext* ctx = update->ctx;
		float scan1 = state->scan_progress[closest_type] = DriftLerpConst(scan0, 1, update->dt/DRIFT_SCANS[closest_type].duration);
		if(scan0 < 1 && scan1 == 1){
			ctx->ui_state = DRIFT_UI_STATE_SCAN;
			ctx->last_scan = closest_type;
		}
		
		bool skiff = (state->scan_progress[DRIFT_SCAN_CONSTRUCTION_SKIFF] >= 1);
		if(skiff && DriftInputButtonPress(DRIFT_INPUT_SCAN)){
			DriftScanUIType sui_type = state->scan_ui.type[DriftComponentFind(&state->scan_ui.c, closest_entity)];
			if(sui_type == DRIFT_SCAN_UI_FABRICATOR){
				player->tool_select = DRIFT_TOOL_NONE;
				ctx->ui_state = DRIFT_UI_STATE_CRAFT;
				ctx->is_docked = true;
			}
			if(sui_type == DRIFT_SCAN_UI_DEPOSIT){
				DriftVec2 vel = DriftVec2Mul(DriftVec2Normalize(DriftVec2Sub(player_pos, closest_pos)), 100);
				DriftItemMake(state, DRIFT_ITEM_COPPER, closest_pos, vel, 0);
			}
		}
	}
	
	static DriftAudioSampler sampler;
	static float pitch = 1;
	pitch = DriftLerpConst(pitch, 1 + fmodf(scan0, 1), update->dt/0.25f);
	float pan = DriftClamp(DriftAffinePoint(update->prev_vp_matrix, world_reticle).x, -1, 1);
	DriftImAudioSet(DRIFT_BUS_SFX, DRIFT_SFX_SCANNER, &sampler, (DriftAudioParams){
		.gain = 0.25f*player->tool_anim, .pitch = pitch, .pan = pan, .loop = true,
	});
}

static void draw_scan(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	DriftVec2 player_pos = DriftAffineOrigin(transform), world_reticle = DriftVec2Add(player_pos, player->reticle);
	
	float anim_brightness = DriftSaturate(3*player->tool_anim - 2);
	DriftRGBA8 scan_color = DriftRGBA8FromColor(DriftVec4Mul((DriftVec4){{0, 0.25f, 0.25f, 0}}, anim_brightness));
	draw_mouse(draw, scan_color);
	
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){
		.p0 = world_reticle, .p1 = world_reticle, .radii = {SCANNER_OUTER_RADIUS, SCANNER_OUTER_RADIUS - 0.5f}, .color = scan_color,
	}));
	
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){
		.p0 = world_reticle, .p1 = world_reticle, .radii = {SCANNER_INNER_RADIUS, SCANNER_INNER_RADIUS - 0.5f}, .color = scan_color,
	}));
	
	DriftGameState* state = draw->state;
	uint scan_idx, transform_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &state->scan.c, .variable = &scan_idx},
		{.component = &state->transforms.c, .variable = &transform_idx},
		{},
	});
	
	DriftEntity closest_entity = {};
	uint closest_type = DRIFT_SCAN_NONE;
	DriftVec2 closest_pos = DRIFT_VEC2_ZERO;
	
	while(DriftJoinNext(&join)){
		DriftScanType type = state->scan.type[scan_idx];
		DriftAffine m = state->transforms.matrix[transform_idx];
		DriftVec2 item_pos = DriftAffinePoint(m, DRIFT_SCANS[type].offset);
		if(state->scan_progress[type] < 1){
			DriftVec2 rot = DriftWaveComplex(draw->nanos - (uint64_t)(1.5e6*DriftVec2Length(item_pos)), 1);
			// draw_indicator(draw, item_pos, rot, DRIFT_SCANS[type].radius, (DriftRGBA8){0x00, 0x80, 0x80, 0x80});
		}
		
		if(join.entity.id == player->scanned_entity.id){
			closest_entity = join.entity;
			closest_type = type;
			closest_pos = item_pos;
		}
	}
	
	static const DriftVec4 label_color = {{0.0, 0.5, 0.5, 0.25}};
	float scan_progress = state->scan_progress[closest_type];
	DriftVec2 label_delta = DriftVec2Sub(closest_pos, player_pos);
	if(closest_type){
		const char* label = DriftSMPrintf(draw->mem, "{#00808040}%s", DRIFT_SCANS[closest_type].name);
		if(scan_progress < 1){
			DriftAffine t = DriftHudDrawOffsetLabel(draw, player_pos, label_delta, label_color, label);
			DriftSprite sprite = {.frame = DRIFT_FRAMES[DRIFT_SPRITE_SCAN0], .color = DRIFT_RGBA8_WHITE, .matrix = t};
			uint inc = 2*(uint)roundf(14*scan_progress);
			sprite.frame.bounds.t += inc;
			sprite.frame.bounds.b += inc;
			DRIFT_ARRAY_PUSH(draw->overlay_sprites, sprite);
		} else {
			DriftScanUIType sui_type = state->scan_ui.type[DriftComponentFind(&state->scan_ui.c, closest_entity)];
			if(scan_progress >= 1 && sui_type == DRIFT_SCAN_UI_FABRICATOR){
				uint node_idx = DriftComponentFind(&state->power_nodes.c, closest_entity);
				label = "Fabricator\nUse {@SCAN}";
			}
			if(scan_progress >= 1 && sui_type == DRIFT_SCAN_UI_DEPOSIT){
				label = "Mineral Deposit\nUse {@SCAN}";
			}
			
			if(label) DriftHudDrawOffsetLabel(draw, player_pos, label_delta, DRIFT_VEC4_WHITE, label);
		}
	}
	
	float retract = DriftLerp(4, 14, DriftSmoothstep(0, 1, player->tool_anim));
	DriftVec2 origin[] = {
		DriftAffinePoint(transform, (DriftVec2){+retract, retract}),
		DriftAffinePoint(transform, (DriftVec2){-retract, retract}),
	};
	
	float phase0 = 2*(float)M_PI*DriftWaveSaw(draw->nanos, 0.7f);
	DriftVec4 light_color = DriftVec4Mul((DriftVec4){{0, 6, 6, 0}}, anim_brightness);
	
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_GRABARM0], .color = DRIFT_RGBA8_WHITE,
		.matrix = DriftAffineMul(transform, (DriftAffine){-0.7f, -0.7f, -0.7f, 0.7f, +retract, retract}),
	}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_SCANNER], .color = DRIFT_RGBA8_WHITE,
		.matrix = DriftAffineTRS(origin[0], -phase0, DRIFT_VEC2_ONE),
	}));
	DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_TRI], .color = light_color,
		.matrix = DriftAffineTRS(origin[0], -phase0, (DriftVec2){30, 30}),
	}));
	
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_GRABARM0], .color = DRIFT_RGBA8_WHITE,
		.matrix = DriftAffineMul(transform, (DriftAffine){+0.7f, -0.7f, +0.7f, 0.7f, -retract, retract}),
	}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_SCANNER], .color = DRIFT_RGBA8_WHITE,
		.matrix = DriftAffineTRS(origin[1], +phase0, DRIFT_VEC2_ONE),
	}));
	DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_TRI], .color = light_color,
		.matrix = DriftAffineTRS(origin[1], +phase0, (DriftVec2){30, 30}),
	}));
	
	static float scan_radius = 0;
	static DriftVec2 scan_offset = DRIFT_VEC2_ZERO;
	const float rate = 100*draw->dt;
	if(closest_type){
		scan_offset = DriftVec2LerpConst(scan_offset, DriftVec2Sub(closest_pos, world_reticle), rate);
		scan_radius = DriftLerpConst(scan_radius, DRIFT_SCANS[closest_type].radius/2, rate);
	} else {
		scan_offset = DriftVec2LerpConst(scan_offset, DRIFT_VEC2_ZERO, rate);
		scan_radius = DriftLerpConst(fminf(scan_radius, SCANNER_OUTER_RADIUS), SCANNER_OUTER_RADIUS, rate);
	}
	DriftVec2 scan_pos = DriftVec2Add(world_reticle, scan_offset);
	
	for(uint i = 0; i < 3; i++){
		float phase = phase0 + i*(float)(2*M_PI/3);
		float dx = cosf(phase), dy = sinf(phase);
		
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){
			.p0 = DriftVec2FMA(origin[0], (DriftVec2){dx, +dy}, 4),
			.p1 = DriftVec2FMA(scan_pos, (DriftVec2){dx, -dy}, scan_radius),
			.radii = {1.2f}, .color = scan_color
		}));
		
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){
			.p0 = DriftVec2FMA(origin[1], (DriftVec2){dx, -dy}, 4),
			.p1 = DriftVec2FMA(scan_pos, (DriftVec2){dx, +dy}, scan_radius),
			.radii = {1.2f}, .color = scan_color
		}));
	}
	
	DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = DriftVec4Mul((DriftVec4){{0, 0.5f, 0.5f, 5}}, anim_brightness),
		.matrix = DriftAffineMul((DriftAffine){80, 0, 0, 80, world_reticle.x, world_reticle.y}, (DriftAffine){1, 0, 0, 1, 0, 0}),
	}));
}

static const struct {
	DriftToolUpdateFunc* update;
	DriftToolDrawFunc* draw;
} DRIFT_TOOLS[_DRIFT_TOOL_COUNT] = {
	[DRIFT_TOOL_NONE] = {.update = update_none, .draw = draw_none},
	[DRIFT_TOOL_GRAB] = {.update = update_grab, .draw = draw_grab},
	[DRIFT_TOOL_SCAN] = {.update = update_scan, .draw = draw_scan},
	[DRIFT_TOOL_DIG] = {.update = update_dig, .draw = draw_dig},
	[DRIFT_TOOL_GUN] = {.update = update_gun, .draw = draw_gun},
};

void DriftToolUpdate(DriftUpdate* update, struct DriftPlayerData* player, DriftAffine transform){
	DriftToolType tool = player->tool_idx;
	
	// Drop anything the player was holding if not using the grabber.
	if(tool != DRIFT_TOOL_GRAB && player->grabbed_entity.id) grabber_drop(update, player);
	
	// Reset scan state if using other tools.
	if(tool != DRIFT_TOOL_SCAN){
		player->scanned_entity = (DriftEntity){};
		player->scanned_type = DRIFT_SCAN_NONE;
	}
	
	
	DRIFT_TOOLS[tool].update(update, player, transform);
}

void DriftToolDraw(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	DRIFT_TOOLS[player->tool_idx].draw(draw, player, transform);
}
