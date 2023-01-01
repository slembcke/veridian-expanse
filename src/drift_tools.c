#include "drift_game.h"

// TODO Does this need to go somewhere better?
static DriftVec2 MOUSE_POS = {};

static void draw_mouse(DriftDraw* draw, DriftRGBA8 color){
	if(draw->ctx->input.mouse_captured){
		DriftVec2 mouse = DriftVec2Add(DriftAffineOrigin(draw->vp_inverse), MOUSE_POS);
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = mouse, .p1 = mouse, .radii = {3}, .color = color}));
	}
}

static void update_base_move(DriftUpdate* update, DriftPlayerData* player){
	DriftPlayerInput* input = &update->ctx->input.player;
	player->desired_velocity = DriftVec2Mul(DriftInputJoystick(input, 0, 1), DRIFT_PLAYER_SPEED);
}

static void update_base_look(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	DriftPlayerInput* input = &update->ctx->input.player;
	MOUSE_POS = DriftVec2Clamp(DriftVec2FMA(MOUSE_POS, update->ctx->input.mouse_rel, 1), 150);
	
	if(update->ctx->input.mouse_captured){
		player->desired_rotation = DriftVec2Mul(MOUSE_POS, 1/fmaxf(DriftVec2Length(MOUSE_POS), 64.0f));
	} else {
		player->desired_rotation = DriftInputJoystick(input, 2, 3);
	}
	
	player->reticle = DriftVec2Mul((DriftVec2){transform.c, transform.d}, 64);
}

static void update_none(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	DriftGameState* state = update->state;
	DriftPlayerInput* input = &update->ctx->input.player;
	DriftVec2 pos = DriftAffineOrigin(transform);
	
	update_base_move(update, player);
	update_base_look(update, player, transform);
}

static void draw_base_look(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	DriftVec2 pos = DriftAffineOrigin(transform), rot = player->desired_rotation;
	
	draw_mouse(draw, (DriftRGBA8){0x00, 0xFF, 0x00, 0x80});

	DriftAffine m1 = (DriftAffine){rot.y, -rot.x, rot.x, rot.y, pos.x, pos.y};
	float r = 1.5f*DriftVec2Length(rot);
	DriftVec2 p1[] = {
		DriftAffinePoint(m1, (DriftVec2){-4, 36}),
		DriftAffinePoint(m1, (DriftVec2){ 0, 38}),
		DriftAffinePoint(m1, (DriftVec2){ 4, 36}),
	};
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p1[0], .p1 = p1[1], .radii = {r}, .color = {0x00, 0xFF, 0x00, 0x80}}));
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p1[1], .p1 = p1[2], .radii = {r}, .color = {0x00, 0xFF, 0x00, 0x80}}));
	
	DriftVec2 p0[] = {
		DriftAffinePoint(transform, (DriftVec2){-6, 37}),
		DriftAffinePoint(transform, (DriftVec2){ 0, 40}),
		DriftAffinePoint(transform, (DriftVec2){ 6, 37}),
	};
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p0[0], .p1 = p0[1], .radii = {1.5f}, .color = {0xFF, 0x00, 0x00, 0x80}}));
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p0[1], .p1 = p0[2], .radii = {1.5f}, .color = {0xFF, 0x00, 0x00, 0x80}}));
}

static void draw_indicator(DriftDraw* draw, DriftVec2 pos, DriftVec2 rot, float radius, DriftRGBA8 color){
	float i = rot.x*rot.x;
	rot = DriftVec2Mul(rot, radius/8);
	DRIFT_ARRAY_PUSH(draw->overlay_sprites, ((DriftSprite){
		.color = {(u8)(color.r*i), (u8)(color.g*i), (u8)(color.b*i), color.a},
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_SELECT_INDICATOR],
		.matrix = {rot.x, rot.y, -rot.y, rot.x, pos.x, pos.y},
	}));
}

static const float GRABBER_RADIUS = 10;

static uint find_nearest_item(DriftGameState* state, DriftVec2 position){
	uint nearest_idx = 0;
	float nearest_dist = 2*GRABBER_RADIUS;
	
	uint item_idx, transform_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&item_idx, &state->items.c},
		{&transform_idx, &state->transforms.c},
		{},
	});
	
	while(DriftJoinNext(&join)){
		DriftVec2 p = DriftAffineOrigin(state->transforms.matrix[transform_idx]);
		float dist = DriftVec2Distance(position, p);
		if(dist < nearest_dist){
			nearest_idx = item_idx;
			nearest_dist = (state->items.type[item_idx] != DRIFT_ITEM_POWER_NODE ? dist : 4*dist);
		}
	}
	
	return nearest_idx;
}

static bool grabber_grab(DriftUpdate* update, DriftPlayerData* player, DriftVec2 position){
	DriftGameState* state = update->state;
	
	uint idx = find_nearest_item(state, position);
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

DriftCargoSlot* DriftPlayerGetCargoSlot(DriftPlayerData* player, DriftItemType type){
	DriftCargoSlot* ret = NULL;
	for(uint i = 0; i < DRIFT_PLAYER_CARGO_SLOT_COUNT; i++){
		DriftCargoSlot* slot = player->cargo_slots + i;
		if(slot->type == type){
			return slot;
		} else if(slot->type == DRIFT_ITEM_NONE || (slot->count == 0 && slot->request == DRIFT_ITEM_NONE)){
			ret = slot;
		}
	}
	
	if(ret) ret->type = type;
	return ret;
}

static bool grabber_stash(DriftUpdate* update, DriftPlayerData* player){
	DriftEntity e = player->grabbed_entity;
	DRIFT_ASSERT(DriftEntitySetCheck(&update->state->entities, e), "Grabbed entity e%d does not exist.", e.id);
	
	DriftCargoSlot* slot = DriftPlayerGetCargoSlot(player, player->grabbed_type);
	if(slot){
		DriftContextPushToast(update->ctx, DRIFT_TEXT_GREEN"+1 %s", DRIFT_ITEMS[player->grabbed_type].name);
		DriftDestroyEntity(update, e);
		slot->count++;
	} else {
		DriftContextPushToast(update->ctx, DRIFT_TEXT_RED"Cargo Full");
		DriftItemDrop(update, e, player->grabbed_type);
	}
	
	player->grabbed_type = DRIFT_ITEM_NONE;
	player->grabbed_entity.id = 0;
	return slot != NULL;
}

static void update_grab(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	DriftGameState* state = update->state;
	DriftPlayerInput* input = &update->ctx->input.player;
	update_base_move(update, player);
	
	DriftVec2 pos = DriftAffineOrigin(transform);
	player->anim_state.hatch_l.target = 0;
	player->anim_state.hatch_r.target = 0;
	
	DriftAffine reticle_transform = {18*transform.a, 18*transform.b, 18*transform.c, 18*transform.d, 32*transform.c, 32*transform.d};
	
	bool mouse_look = update->ctx->input.mouse_captured;
	DriftVec2 local_look = {}, pull = {};
	if(mouse_look){
		// Clamp mouse to a slightly larger than normal radius.
		MOUSE_POS = DriftVec2Clamp(DriftVec2FMA(MOUSE_POS, update->ctx->input.mouse_rel, 1), 100);
		
		// Then pull it back in while pulling the player towards it.
		DriftVec2 clamped = DriftVec2Clamp(MOUSE_POS, 50);
		pull = DriftVec2Mul(DriftVec2Sub(MOUSE_POS, clamped), 1/update->dt);
		MOUSE_POS = DriftVec2LerpConst(MOUSE_POS, clamped, 2*DRIFT_PLAYER_SPEED*update->dt);
		
		local_look = DriftVec2Clamp(DriftAffinePoint(DriftAffineInverse(reticle_transform), MOUSE_POS), 1);
	} else {
		local_look = DriftAffineDirection(DriftAffineInverse(transform), DriftInputJoystick(input, 2, 3));
	}
	
	// Transform look coords to reticle coords.
	DriftVec2 reticle = DriftAffinePoint(reticle_transform, local_look);
	
	// Collide reticle with the terrain.
	float ray_t = DriftTerrainRaymarch(state->terra, pos, DriftVec2Add(pos, reticle), 0, 1);
	reticle = DriftVec2Mul(reticle, ray_t);
	DriftTerrainSampleInfo info = DriftTerrainSampleFine(state->terra, DriftVec2Add(pos, reticle));
	player->reticle = reticle = DriftVec2FMA(reticle, info.grad, fmaxf(0, GRABBER_RADIUS - info.dist));
	
	if(!mouse_look){
		// Pull towards the reticle in gamepad mode.
		float amount = DriftAffinePoint(DriftAffineInverse(reticle_transform), reticle).y;
		pull = DriftVec2Mul((DriftVec2){transform.c, transform.d}, fmaxf(0, 100*amount));
	}
	
	// Update desired velocity
	DriftVec2 velocity = DriftVec2Add(player->desired_velocity, pull);
	player->desired_velocity = DriftVec2Clamp(velocity, DRIFT_PLAYER_SPEED);
	
	static bool TMP_GRAB_LOCK = false;
	// Reset grab lock when not grabbing.
	TMP_GRAB_LOCK &= DriftInputButtonState(input, DRIFT_INPUT_GRAB);
	
	float rotation_rate = 1;
	// Ease the rotation rate when trying to pick something up
	if(TMP_GRAB_LOCK) rotation_rate *= (1 + local_look.y);
	player->desired_rotation = DriftAffineDirection(transform, (DriftVec2){rotation_rate*local_look.x, 1});
	
	DriftVec2 world_reticle = DriftVec2Add(pos, reticle);
	if(player->grabbed_type == DRIFT_ITEM_NONE){
		if(DriftInputButtonPress(input, DRIFT_INPUT_GRAB)){
			if(player->energy > 0){
				TMP_GRAB_LOCK = grabber_grab(update, player, world_reticle);
			} else {
				DriftAudioPlaySample(update->audio, DRIFT_SFX_CLICK, 1, 0, 1, false);
				DriftContextPushToast(update->ctx, DRIFT_TEXT_RED"Grabber has no power");
			}
		}
	} else {
		if(player->energy == 0){
			grabber_drop(update, player);
			DriftContextPushToast(update->ctx, DRIFT_TEXT_RED"Grabber has no power");
		} else if(DriftInputButtonRelease(input, DRIFT_INPUT_GRAB)){
			grabber_drop(update, player);
		} else if(local_look.y < - 0.80f){
			uint body_idx = DriftComponentFind(&state->bodies.c, player->grabbed_entity);
			if(!grabber_stash(update, player)){
				// Make it drift away if it can't be stashed.
				state->bodies.velocity[body_idx] = DriftAffineDirection(transform, (DriftVec2){0, 50});
				state->bodies.angular_velocity[body_idx] = 5;
			}
		} else if(DriftInputButtonState(input, DRIFT_INPUT_GRAB)){
			grabber_update(update, player, world_reticle);
		}
	}
	
	DriftCargoSlot* slot = DriftPlayerGetCargoSlot(player, DRIFT_ITEM_POWER_NODE);
	if(player->grabbed_type){
		// TODO force the player to drop here?
	} else if(DriftInputButtonState(input, DRIFT_INPUT_DROP)){
		DriftNearbyNodesInfo nodes = DriftSystemPowerNodeNearby(state, world_reticle, update->mem, DRIFT_POWER_BEAM_RADIUS);
		if(nodes.node_can_connect){
			player->last_valid_drop.pos = nodes.pos;
			player->last_valid_drop.frame = update->frame;
		}
		
		if(update->frame - player->last_valid_drop.frame == 1){
			DriftNearbyNodesInfo nodes = DriftSystemPowerNodeNearby(state, world_reticle, update->mem, DRIFT_POWER_BEAM_RADIUS);
			if(slot->count == 0){
				DriftContextPushToast(update->ctx, DRIFT_TEXT_RED"Cargo Empty");
			} else if(nodes.too_close_count == 0){
				DriftEntity e = DriftItemMake(update->state, slot->type, player->last_valid_drop.pos, DRIFT_VEC2_ZERO);
				DriftItemDrop(update, e, slot->type);
				slot->count--;
			}
		}
	} else if(DriftInputButtonRelease(input, DRIFT_INPUT_DROP)){
		DriftNearbyNodesInfo nodes = DriftSystemPowerNodeNearby(state, world_reticle, update->mem, DRIFT_POWER_BEAM_RADIUS);
		if(slot->count == 0){
			DriftContextPushToast(update->ctx, DRIFT_TEXT_RED"Cargo Empty");
		} else if(nodes.node_can_connect){
			DriftEntity e = DriftItemMake(update->state, slot->type, world_reticle, DRIFT_VEC2_ZERO);
			DriftItemDrop(update, e, slot->type);
			slot->count--;
		} else {
			DriftContextPushToast(update->ctx, DRIFT_TEXT_RED"Cannot Connect");
		}
	}
}

static void draw_arm_with_ik(DriftDraw* draw, DriftAffine transform, float anim, float grip, DriftArmPose* pose, DriftVec2 target, float err_smooth, float err_rate){
	DriftVec2 pos0 = DriftVec2Mul((DriftVec2){8, 8}, 1 - 2*anim);
	DriftVec2 pos = pos0, dir = {1, 0};
	err_rate *= (1 - anim)*(1 - anim);
	target.x += DriftLerp(18, 8, grip);
	
	// Calculate pose
	DriftVec2 x[3], q[4];
	for(uint i = 0; i < 3; i++){
		dir = DriftVec2Rotate(dir, (DriftVec2){cosf(pose->angle[i]), sinf(pose->angle[i])});
		x[i] = pos, q[i] = dir;
		
		const float len = 16;
		pos = DriftVec2FMA(pos, dir, len);
	}
	
	// Animate pose.
	if(anim == 0){
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
	}
	
	// Draw mount
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_GRABBER_MOUNT], .color = DRIFT_RGBA8_WHITE,
		.matrix = DriftAffineMul(transform, (DriftAffine){1, 0, 0, 1, pos0.x, pos0.y}),
	}));
	
	// Draw grippers
	float grip_angle = -0.7f*(1 - grip);
	DriftVec2 grip_q = {cosf(grip_angle), sinf(grip_angle)};
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_GRIPPER], .color = DRIFT_RGBA8_WHITE,
		.matrix = DriftAffineMul(transform, (DriftAffine){-grip_q.x, -grip_q.y, -grip_q.y,  grip_q.x, pos.x, pos.y}),
	}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_GRIPPER], .color = DRIFT_RGBA8_WHITE,
		.matrix = DriftAffineMul(transform, (DriftAffine){-grip_q.x,  grip_q.y, -grip_q.y, -grip_q.x, pos.x, pos.y}),
	}));
	
	// Draw arms
	static const DriftSpriteEnum frames[] = {DRIFT_SPRITE_GRABARM0, DRIFT_SPRITE_GRABARM1, DRIFT_SPRITE_GRABARM2};
	for(uint i = 0; i < 3; i++){
		DriftAffine t = DriftAffineMul(transform, (DriftAffine){q[i].x, q[i].y, -q[i].y, q[i].x, x[i].x, x[i].y});
		DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = DRIFT_FRAMES[frames[i]], .color = DRIFT_RGBA8_WHITE, .matrix = t}));
		// DriftDebugTransform(draw->state, t, 5);
	}
}

static DriftAffine draw_offset_label(DriftDraw* draw, DriftVec2 origin, DriftVec2 dir, DriftVec4 color, const char* label){
	DriftAABB2 bb = DriftDrawTextBounds(label, 0, draw->input_icons);
	bb.l -= 1, bb.b -= 1, bb.r += 1, bb.t += 1;
	DriftVec2 text_center = DriftAABB2Center(bb), text_extents = DriftAABB2Extents(bb);
	DriftVec2 text_origin = DriftVec2FMA(origin, dir, 1 + fminf((text_extents.x + 5)/fabsf(dir.x), (text_extents.y + 5)/fabsf(dir.y)));
	DriftAffine text_transform = {1, 0, 0, 1, text_origin.x - text_center.x, text_origin.y - text_center.y};
	DRIFT_ARRAY_PUSH(draw->overlay_sprites, ((DriftSprite){
		.color = {0x00, 0x00, 0x00, 0x60},
		.matrix = DriftAffineMul(text_transform, (DriftAffine){2*text_extents.x, 0, 0, 2*text_extents.y, bb.l, bb.b}),
	}));
	DriftDrawText(draw, &draw->overlay_sprites, text_transform, color, label);
	return (DriftAffine){1, 0, 0, 1, text_origin.x, text_origin.y + copysignf(text_extents.y + 4, dir.y)};
}

static void draw_grab(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	DriftVec2 player_pos = DriftAffineOrigin(transform), world_reticle = DriftVec2Add(player_pos, player->reticle);
	DriftRGBA8 grab_color = player->grabbed_entity.id ? (DriftRGBA8){0x00, 0x80, 0x00, 0x80} : (DriftRGBA8){0x80, 0x00, 0x00, 0x80};
	draw_mouse(draw, grab_color);
	
	// Draw grabber reticle.
	if(!player->grabbed_entity.id){
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){
			.p0 = world_reticle, .p1 = world_reticle, .radii = {GRABBER_RADIUS, GRABBER_RADIUS - 1}, .color = {0xC0, 0x00, 0x00, 0x80}
		}));
	}
	
	if(player->grabbed_entity.id){
		const char* label = DRIFT_ITEMS[player->grabbed_type].name;
		DriftItemDraw(draw, player->grabbed_type, world_reticle);
		
		draw_offset_label(draw, player_pos, player->reticle, (DriftVec4){{0.0, 0.5, 0.0, 0.25}}, label);
		
		float bob = 3*fabsf(DriftWaveComplex(draw->nanos, 1).x);
		DriftVec2 p[] = {
			DriftAffinePoint(transform, (DriftVec2){-4, 24 - bob}),
			DriftAffinePoint(transform, (DriftVec2){ 0, 21 - bob}),
			DriftAffinePoint(transform, (DriftVec2){ 4, 24 - bob}),
		};
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[0], .p1 = p[1], .radii = {1.5f}, .color = DRIFT_RGBA8_GREEN}));
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[1], .p1 = p[2], .radii = {1.5f}, .color = DRIFT_RGBA8_GREEN}));
	}
	
	// Draw ejecting item.
	DriftPlayerInput* input = &draw->ctx->input.player;
	DriftCargoSlot* slot = DriftPlayerGetCargoSlot(player, DRIFT_ITEM_POWER_NODE);
	if(DriftInputButtonState(input, DRIFT_INPUT_DROP) && slot->count && player->grabbed_type == DRIFT_ITEM_NONE){
		DriftItemDraw(draw, slot->type, DriftVec2Add(player_pos, player->reticle));
	}
	
	// Draw pickup indicators.
	DriftGameState* state = draw->state;
	uint transform_idx, item_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&item_idx, &state->items.c},
		{&transform_idx, &state->transforms.c},
		{},
	});
	
	const uint period = 1 << 29;
	float phase = (draw->ctx->update_nanos % period)*(float)M_PI/period;
	DriftVec2 rot = {cosf(phase), sinf(phase)}, inc = {cosf((float)(M_PI*DRIFT_PHI)), sinf((float)(M_PI*DRIFT_PHI))};
	DriftFrame frame = DRIFT_FRAMES[DRIFT_SPRITE_SELECT_INDICATOR];
	float ring = 5*(draw->tick % 120);
	
	while(DriftJoinNext(&join)){
		DriftAffine m = state->transforms.matrix[transform_idx];
		DriftVec2 item_pos = DriftAffineOrigin(m);
		if(!DriftAffineVisibility(draw->vp_matrix, item_pos, DRIFT_VEC2_ZERO)) continue;
		
		draw_indicator(draw, item_pos, rot, 8, (DriftRGBA8){0x00, 0x80, 0x00, 0x80});
		rot = DriftVec2Rotate(rot, inc);
		
		DriftItemType type = state->items.type[item_idx];
		float flash = 1 - fabsf(DriftVec2Distance(item_pos, player_pos) - ring)/40.0f;
		if(type != DRIFT_ITEM_POWER_NODE && flash > 0){
			DriftSprite sprite = DriftSpriteForItem(type, m);
			sprite.color = DriftRGBA8FromColor(DriftVec4Mul((DriftVec4){{0, 1, 0, 1}}, flash));
			DRIFT_ARRAY_PUSH(draw->flash_sprites, sprite);
		}
	}
	
	float err_smooth = 1 - expf(-30*draw->dt);
	float err_rate = 8*draw->dt;
	float grip = DriftInputButtonState(input, DRIFT_INPUT_GRAB);
	
	DriftPlayerAnimState* anim = &player->anim_state;
	DriftVec2 wobble = DriftWaveComplex(draw->nanos, 0.5f);
	wobble.y = 0.5f*DriftVec2Rotate(wobble, wobble).y;
	DriftVec2 desired = DriftVec2FMA(player->reticle, wobble, 3*(1 - grip));
	
	DriftAffine t_l = DriftAffineMul(transform, (DriftAffine){-1, 0, 0, 1, 0, 0});
	DriftVec2 desired_l = DriftAffineDirection(DriftAffineInverse(t_l), desired);
	draw_arm_with_ik(draw, t_l, anim->hatch_r.value, grip, &player->arm_l, desired_l, err_smooth, err_rate);
	
	DriftVec2 desired_r = DriftAffineDirection(DriftAffineInverse(transform), desired);
	draw_arm_with_ik(draw, transform, anim->hatch_l.value, grip, &player->arm_r, desired_r, err_smooth, err_rate);
}

// static void update_dig_doze(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
// 	DriftPlayerInput* input = &update->ctx->input.player;
	
// 	update_base_move(update, player);
// 	update_base_look(update, player, transform);
	
// 	const float DIG_SPEED = 15.0f;
// 	player->desired_velocity = DriftVec2FMA(player->desired_velocity, player->desired_rotation, DIG_SPEED);
// 	float speed = DriftVec2Length(player->desired_velocity);
	
// 	DriftTerrain* terra = update->state->terra;
	
// 	player->anim_state.laser.target = 1;
// 	player->nacelle_l = 0;
// 	player->nacelle_r = 1;
	
// 	player->is_digging = DriftInputButtonState(input, DRIFT_INPUT_LASER) && player->energy > 0 && !player->is_overheated;
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

static void update_dig_laze(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	player->anim_state.laser.target = 1;
	
	if(player->is_digging){
		player->nacelle_l = DriftLerpConst(player->nacelle_l, 0, update->dt/0.1f);
		player->nacelle_r = DriftLerpConst(player->nacelle_r, 0, update->dt/0.1f);
		player->desired_velocity = DRIFT_VEC2_ZERO;
	} else {
		update_base_move(update, player);
		update_base_look(update, player, transform);
	}
	
	DriftTerrain* terra = update->state->terra;
	DriftPlayerInput* input = &update->ctx->input.player;
	DriftVec2 p0 = DriftAffineOrigin(transform), p1 = DriftAffinePoint(transform, (DriftVec2){0, LASER_LENGTH});
	
	static float alpha = 0;
	if(DriftInputButtonPress(input, DRIFT_INPUT_LASER)){
		alpha = DriftTerrainRaymarch(terra, p0, p1, LASER_RADIUS - 1, 1);
		player->is_digging = true;
	}
	player->is_digging &= DriftInputButtonState(input, DRIFT_INPUT_LASER) && alpha < 1 && player->energy > 0 && !player->is_overheated;
	
	alpha = DriftLerpConst(alpha, 1, update->dt/8);
	DriftVec2 pos = DriftVec2Lerp(p0, p1, alpha);
	player->dig_pos = pos;
	
	if(player->is_digging){
		// TODO should be in fixed update?
		DriftTerrainDig(terra, pos, LASER_RADIUS + 2);
	} else {
		alpha = 0;
	}
}

static void draw_dig_laze(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	draw_base_look(draw, player, transform);
	DriftVec2 p0 = DriftAffineOrigin(transform), p1 = player->dig_pos;
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){
		.p0 = p0, .p1 = DriftAffinePoint(transform, (DriftVec2){0, LASER_LENGTH}),
		.radii = {LASER_RADIUS, LASER_RADIUS - 1}, .color = DRIFT_RGBA8_RED
	}));
	
	if(player->is_digging){
		DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteMake(DRIFT_SPRITE_LASER_DOT, (DriftRGBA8){0xFF, 0x60, 0x40, 0x00}, (DriftAffine){1, 0, 0, 1, p1.x, p1.y}));
		DRIFT_ARRAY_PUSH(draw->lights, DriftLightMake(DRIFT_SPRITE_LIGHT_RADIAL, (DriftVec4){{26.53f, 3.83f, 0.00f, 5.00f}}, (DriftAffine){250, 0, 0, 250, p1.x, p1.y}, LASER_RADIUS/4));
	}
}

static void update_dig(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){update_dig_laze(update, player, transform);}
static void draw_dig(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){draw_dig_laze(draw, player, transform);}

// TODO pull out commonalities to fly?
static void update_gun(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	player->anim_state.cannons.target = 1;
	
	update_base_move(update, player);
	update_base_look(update, player, transform);
	
	player->reticle = DriftVec2Mul((DriftVec2){transform.c, transform.d}, 64);

	static uint repeat = 0;
	static float timeout = 0, inc = 0;
	timeout -= update->dt;
	
	DriftPlayerInput* input = &update->ctx->input.player;
	if(timeout < 0 && DriftInputButtonPress(input, DRIFT_INPUT_FIRE)){
		timeout = 0, inc = 0.15f;
		
		if(player->energy == 0){
			DriftAudioPlaySample(update->audio, DRIFT_SFX_CLICK, 1, 0, 1, false);
			DriftContextPushToast(update->ctx, DRIFT_TEXT_RED"Cannon has no power");
		} else if(player->is_overheated){
			DriftAudioPlaySample(update->audio, DRIFT_SFX_CLICK, 1, 0, 1, false);
		} else {
			repeat = 1;
			if(update->state->inventory[DRIFT_ITEM_AUTOCANNON]) repeat = 4, inc = 0.1f;
			if(update->state->inventory[DRIFT_ITEM_ZIP_CANNON]) repeat = 8, inc = 0.05f;
		}
	}
	
	if(repeat && timeout <= 0){
		PlayerCannonTransforms cannons = CalculatePlayerCannonTransforms(1);
		static uint which = 0;
		uint cannon_idx = which++ % (repeat ? 4 : 2);
		DriftAffine cannon = cannons.arr[cannon_idx];
		cannon.c -= cannon.x/300;
		
		cannon = DriftAffineMul(transform, cannon);
		FireProjectile(update, DriftAffineOrigin(cannon), DriftAffineDirection(cannon, (DriftVec2){0, 2000}));
		repeat--, timeout += inc;
		
		player->energy -= 2;
		player->temp += 5e-2f;
	}
}

static void draw_gun(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	draw_base_look(draw, player, transform);

	PlayerCannonTransforms cannons = CalculatePlayerCannonTransforms(1);
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
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[0], .p1 = p[1], .radii = {1.0f}, .color = {0x80, 0x00, 0x00, 0x40}}));
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[2], .p1 = p[3], .radii = {1.2f}, .color = {0xFF, 0x00, 0x00, 0x80}}));
	}
}

#define SCANNER_INNER_RADIUS 10
#define SCANNER_OUTER_RADIUS 48

static void update_scan(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	DriftGameState* state = update->state;
	DriftPlayerInput* input = &update->ctx->input.player;
	update_base_move(update, player);
	
	DriftVec2 player_pos = DriftAffineOrigin(transform);
	player->anim_state.hatch_l.target = 0;
	player->anim_state.hatch_r.target = 0;
	
	DriftVec2 reticle = {};
	if(update->ctx->input.mouse_captured){
		reticle = MOUSE_POS = DriftVec2Clamp(DriftVec2FMA(MOUSE_POS, update->ctx->input.mouse_rel, 1), 120);
	} else {
		reticle = DriftVec2Clamp(DriftVec2FMA(player->reticle, DriftInputJoystick(input, 2, 3), 500*update->dt), 120);
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
	float closest_dist = INFINITY;
	DriftEntity closest_entity = {};
	DriftScanType closest_type = DRIFT_SCAN_NONE;
	
	DriftScanType scan_restrict = state->status.scan_restrict;
	while(DriftJoinNext(&join)){
		DriftScanType type = state->scan.type[scan_idx];
		
		// Restrict scaning types while in the tutorial.
		if(scan_restrict && type != scan_restrict) continue;
		
		DriftVec2 item_pos = DriftAffinePoint(state->transforms.matrix[transform_idx], DRIFT_SCANS[type].offset);
		float dist = DriftVec2Distance(world_reticle, item_pos);
		if(dist < SCANNER_OUTER_RADIUS && dist < closest_dist){
			closest_dist = dist;
			closest_entity = join.entity;
			closest_type = type;
		}
	}
	
	if(closest_type && update->ctx->ui_state == DRIFT_UI_STATE_NONE){
		DriftGameContext* ctx = update->ctx;
		float scan0 = state->scan_progress[closest_type];
		float scan1 = state->scan_progress[closest_type] = DriftLerpConst(scan0, 1, update->dt/2);
		if(scan0 < 1 && scan1 == 1){
			ctx->ui_state = DRIFT_UI_STATE_SCAN;
			ctx->last_scan = closest_type;
		}
		
		if(DriftInputButtonPress(&ctx->input.player, DRIFT_INPUT_SCAN)){
			if(closest_type == DRIFT_SCAN_FACTORY && state->scan_progress[DRIFT_SCAN_FACTORY] == 1){
				uint factory_node_idx = DriftComponentFind(&state->power_nodes.c, state->status.factory_node);
				if(state->power_nodes.active[factory_node_idx]){
					player->tool_idx = DRIFT_TOOL_NONE;
					ctx->ui_state = DRIFT_UI_STATE_CRAFT;
				}
			}
		}
	}
	
	player->scanned_entity = closest_entity;
}

static void draw_scan(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	DriftVec2 player_pos = DriftAffineOrigin(transform), world_reticle = DriftVec2Add(player_pos, player->reticle);
	DriftRGBA8 scan_color = {0x00, 0x40, 0x40, 0x00};
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
	
	const uint period = 1 << 29;
	float phase = (draw->ctx->update_nanos % period)*(float)M_PI/period;
	DriftVec2 rot = {cosf(phase), sinf(phase)}, inc = {cosf((float)(M_PI*DRIFT_PHI)), sinf((float)(M_PI*DRIFT_PHI))};
	
	uint closest_type = DRIFT_SCAN_NONE;
	DriftVec2 closest_pos = DRIFT_VEC2_ZERO;
	
	while(DriftJoinNext(&join)){
		DriftScanType type = state->scan.type[scan_idx];
		DriftVec2 item_pos = DriftAffinePoint(state->transforms.matrix[transform_idx], DRIFT_SCANS[type].offset);
		if(state->scan_progress[type] < 1){
			draw_indicator(draw, item_pos, rot, DRIFT_SCANS[type].radius, (DriftRGBA8){0x00, 0x80, 0x80, 0x80});
			rot = DriftVec2Rotate(rot, inc);
		}
		
		if(join.entity.id == player->scanned_entity.id){
			closest_type = type;
			closest_pos = item_pos;
		}
	}
	
	const char* label = DRIFT_SCANS[closest_type].name;
	DriftVec4 label_color = {{0.0, 0.5, 0.5, 0.25}};
	
	if(closest_type){
		if(closest_type == DRIFT_SCAN_FACTORY && state->scan_progress[DRIFT_SCAN_FACTORY] == 1){
			uint factory_node_idx = DriftComponentFind(&state->power_nodes.c, state->status.factory_node);
			if(state->power_nodes.active[factory_node_idx]) label = "Factory\nUse {@SCAN}";
		}
		
		DriftAffine t = draw_offset_label(draw, player_pos, DriftVec2Sub(closest_pos, player_pos), label_color, label);
		DriftSprite sprite = {.frame = DRIFT_FRAMES[DRIFT_SPRITE_SCAN0], .color = DRIFT_RGBA8_WHITE, .matrix = t};
		
		float progress = state->scan_progress[closest_type];
		if(progress < 1){
			scan_color = (DriftRGBA8){0x80, 0x00, 0x00, 0x00};
			
			uint inc = 2*(uint)roundf(14*progress);
			sprite.frame.bounds.t += inc;
			sprite.frame.bounds.b += inc;
			DRIFT_ARRAY_PUSH(draw->overlay_sprites, sprite);
		}
	}
	
	float retract = 14*(1 - player->anim_state.hatch_l.value);
	DriftVec2 origin[] = {
		DriftAffinePoint(transform, (DriftVec2){+retract, retract}),
		DriftAffinePoint(transform, (DriftVec2){-retract, retract}),
	};
	
	float phase0 = draw->tick/15.0f;//draw->ctx->update_nanos;
	float c = cosf(phase0), s = sinf(phase0);
	
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_GRABARM0], .color = DRIFT_RGBA8_WHITE,
		.matrix = DriftAffineMul(transform, (DriftAffine){-0.7f, -0.7f, -0.7f, 0.7f, +retract, retract}),
	}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_SCANNER], .color = DRIFT_RGBA8_WHITE,
		.matrix = DriftAffineTRS(origin[0], -phase0, DRIFT_VEC2_ONE),
	}));
	DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_TRI], .color = {{0, 6, 6, 0}},
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
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_TRI], .color = {{0, 6, 6, 0}},
		.matrix = DriftAffineTRS(origin[1], +phase0, (DriftVec2){30, 30}),
	}));
	
	if(player->anim_state.hatch_l.value > 0) return;
	
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
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = {{0, 0.5f, 0.5f, 5}},
		.matrix = DriftAffineMul((DriftAffine){80, 0, 0, 80, world_reticle.x, world_reticle.y}, (DriftAffine){c, s, -s, c, 0, 0}),
	}));
}

static const struct {
	DriftToolUpdateFunc* update;
	DriftToolDrawFunc* draw;
} DRIFT_TOOLS[_DRIFT_TOOL_COUNT] = {
	[DRIFT_TOOL_NONE] = {.update = update_none, .draw = draw_base_look},
	[DRIFT_TOOL_GRAB] = {.update = update_grab, .draw = draw_grab},
	[DRIFT_TOOL_SCAN] = {.update = update_scan, .draw = draw_scan},
	[DRIFT_TOOL_DIG] = {.update = update_dig, .draw = draw_dig},
	[DRIFT_TOOL_GUN] = {.update = update_gun, .draw = draw_gun},
};

void DriftToolUpdate(DriftUpdate* update, struct DriftPlayerData* player, DriftAffine transform){
	DriftToolType tool = player->tool_idx;
	// Drop anything the player was holding if not using the grabber.
	if(tool != DRIFT_TOOL_GRAB && player->grabbed_entity.id) grabber_drop(update, player);
	
	DRIFT_TOOLS[tool].update(update, player, transform);
}

void DriftToolDraw(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	if(draw->state->status.show_hud){
		DRIFT_TOOLS[player->tool_idx].draw(draw, player, transform);
	}
}
