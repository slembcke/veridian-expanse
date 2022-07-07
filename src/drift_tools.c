#include "drift_game.h"

static void draw_mouse(DriftDraw* draw, DriftRGBA8 color){
	if(draw->ctx->input.mouse_captured){
		DriftVec2 mouse = draw->ctx->input.mouse_pos;
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = mouse, .p1 = mouse, .radii = {3}, .color = color}));
	}
}

static void ToolUpdateFly(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	DriftGameState* state = update->state;
	DriftPlayerInput* input = &update->ctx->input.player;
	DriftVec2 pos = DriftAffineOrigin(transform);
	
	float boost = DRIFT_PLAYER_SPEED*input->axes[DRIFT_INPUT_AXIS_ACTION1];
	if(update->ctx->input.mouse_captured){
		DriftVec2 mouse_look = DriftVec2Sub(update->ctx->input.mouse_pos, pos);
		player->desired_rotation = DriftVec2Mul(mouse_look, 1/fmaxf(DriftVec2Length(mouse_look), 64.0f));
	} else {
		player->desired_rotation = DriftInputJoystick(input, 2, 3);
	}
	
	player->reticle = DriftVec2Mul((DriftVec2){transform.c, transform.d}, 64);
	DriftVec2 velocity = DriftVec2FMA(player->desired_velocity, player->desired_rotation, boost);
	player->desired_velocity = DriftVec2Clamp(velocity, 1.5f*DRIFT_PLAYER_SPEED);
	
	bool home = DriftInputButtonState(input, DRIFT_INPUT_ACTION2);
	DriftEntity entity = update->ctx->player;
	uint nav_idx = DriftComponentFind(&state->navs.c, entity);
	
	if(!home && nav_idx) DriftComponentRemove(&state->navs.c, entity);
	if(home && !nav_idx){
		uint nav_idx = DriftComponentAdd(&state->navs.c, entity);
		state->navs.data[nav_idx].radius = DRIFT_PLAYER_SIZE + 2;
	}
	
	// Left trigger return to home?
	// Repel walls?
	// DriftTerrainSampleInfo info = DriftTerrainSampleFine(update->ctx->terra, DriftAffineOrigin(transform));
	// thrust = DriftVec2Add(thrust, DriftVec2Mul(info.grad, fmaxf(0, (32 - info.dist))/32));
}

static void ToolDrawFly(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	DriftVec2 pos = DriftAffineOrigin(transform), rot = player->desired_rotation;
	
	draw_mouse(draw, DRIFT_RGBA8_GREEN);

	DriftAffine m1 = (DriftAffine){rot.y, -rot.x, rot.x, rot.y, pos.x, pos.y};
	float r = 1.5f*DriftVec2Length(rot);
	DriftVec2 p1[] = {
		DriftAffinePoint(m1, (DriftVec2){-4, 36}),
		DriftAffinePoint(m1, (DriftVec2){ 0, 38}),
		DriftAffinePoint(m1, (DriftVec2){ 4, 36}),
	};
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p1[0], .p1 = p1[1], .radii = {r}, .color = DRIFT_RGBA8_GREEN}));
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p1[1], .p1 = p1[2], .radii = {r}, .color = DRIFT_RGBA8_GREEN}));
	
	DriftVec2 p0[] = {
		DriftAffinePoint(transform, (DriftVec2){-6, 37}),
		DriftAffinePoint(transform, (DriftVec2){ 0, 40}),
		DriftAffinePoint(transform, (DriftVec2){ 6, 37}),
	};
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p0[0], .p1 = p0[1], .radii = {1.5f}, .color = DRIFT_RGBA8_RED}));
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p0[1], .p1 = p0[2], .radii = {1.5f}, .color = DRIFT_RGBA8_RED}));
}

static const float GRABBER_RADIUS = 10;

static void grabber_grab(DriftUpdate* update, DriftPlayerData* player, DriftVec2 reticle){
	DriftGameState* state = update->state;
	float min_dist = GRABBER_RADIUS;
	
	uint pickup_idx, transform_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&pickup_idx, &state->pickups.c},
		{&transform_idx, &state->transforms.c},
		{},
	});
	while(DriftJoinNext(&join)){
		DriftVec2 p = DriftAffineOrigin(state->transforms.matrix[transform_idx]);
		float dist = DriftVec2Distance(reticle, p);
		if(dist < min_dist){
			player->grabbed_type = state->pickups.type[pickup_idx];
			player->grabbed_entity = join.entity;
			min_dist = dist;
		}
	}
	
	if(player->grabbed_entity.id){
		DriftComponentRemove(&state->pickups.c, player->grabbed_entity);
		DRIFT_PICKUPS[player->grabbed_type].grab(update, player->grabbed_entity, reticle);
	}
}

static void grabber_make(DriftUpdate* update, DriftPlayerData* player, DriftVec2 reticle){
	DriftCargoSlot* slot = player->cargo_slots + player->cargo_idx;
	if(slot->count > 0){
		player->grabbed_type = slot->type;
		player->grabbed_entity = DRIFT_PICKUPS[player->grabbed_type].make(update->state, reticle, DRIFT_VEC2_ZERO);
		slot->count--;
	} else {
		DRIFT_LOG("Cargo empty. (This should be a toast notification)");
	}
}

static void grabber_update(DriftUpdate* update, DriftPlayerData* player, DriftVec2 reticle){
	DriftGameState* state = update->state;
	DriftEntity e = player->grabbed_entity;
	DRIFT_ASSERT(DriftEntitySetCheck(&state->entities, e), "Grabbed entity e%d does not exist.", e.id);
	
	uint transform_idx = DriftComponentFind(&state->transforms.c, e);
	DRIFT_ASSERT(transform_idx, "Grabbed entity e%d has no transform.", e.id);
	float x = state->transforms.matrix[transform_idx].x;
	state->transforms.matrix[transform_idx].x = reticle.x;
	state->transforms.matrix[transform_idx].y = reticle.y;
	
	uint body_idx = DriftComponentFind(&state->bodies.c, e);
	if(body_idx){
		// TODO Need to sync to player.
		state->bodies.position[body_idx] = reticle;
		state->bodies.velocity[body_idx] = DRIFT_VEC2_ZERO;
		state->bodies.angular_velocity[body_idx] = 0;
	}
}

static void grabber_drop(DriftUpdate* update, DriftPlayerData* player, DriftVec2 reticle){
	DriftGameState* state = update->state;
	DriftEntity e = player->grabbed_entity;
	DRIFT_ASSERT(DriftEntitySetCheck(&state->entities, e), "Grabbed entity e%d does not exist.", e.id);
	
	uint pickup_idx = DriftComponentAdd(&state->pickups.c, e);
	state->pickups.type[pickup_idx] = player->grabbed_type;
	
	DRIFT_PICKUPS[player->grabbed_type].drop(update, e, reticle);
	player->grabbed_type = DRIFT_ITEM_TYPE_NONE;
	player->grabbed_entity.id = 0;
}

DriftCargoSlot* DriftPlayerGetCargoSlot(DriftPlayerData* player, DriftItemType type){
	DriftCargoSlot* ret = NULL;
	for(uint i = 0; i < DRIFT_PLAYER_CARGO_SLOT_COUNT; i++){
		DriftCargoSlot* slot = player->cargo_slots + i;
		if(slot->type == type){
			return slot;
		} else if(!slot->type){
			ret = slot;
		}
	}
	
	if(ret) ret->type = type;
	return ret;
}

static void grabber_stash(DriftUpdate* update, DriftPlayerData* player){
	DriftEntity e = player->grabbed_entity;
	
	DRIFT_ASSERT(DriftEntitySetCheck(&update->state->entities, e), "Grabbed entity e%d does not exist.", e.id);
	DriftDestroyEntity(update, e);
	
	DriftItemType type = player->grabbed_type;
	DriftCargoSlot* slot = DriftPlayerGetCargoSlot(player, type);
	
	if(slot){
		slot->count++;
	} else {
		DRIFT_LOG("Cargo full. (This should be a toast notification)");
	}
	
	player->grabbed_type = DRIFT_ITEM_TYPE_NONE;
	player->grabbed_entity.id = 0;
}

static const DriftVec2 HATCH_POS = {-14, 14};
static const float HATCH_RADIUS = 10;

static void ToolUpdateGrab(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	DriftGameState* state = update->state;
	DriftPlayerInput* input = &update->ctx->input.player;
	
	player->cargo_hatch_open ^= DriftInputButtonPress(input, DRIFT_INPUT_ACTION2);
	player->anim_state.hatch_l.target = !player->cargo_hatch_open;
	
	DriftVec2 pos = DriftAffineOrigin(transform);
	float grabber_length = 48;
	
	float pull_coef = 1;
	DriftVec2 reticle = player->reticle;
	
	if(update->ctx->input.mouse_captured){
		pull_coef = 2e-2f;
		reticle = DriftVec2Sub(update->ctx->input.mouse_pos, pos);
	} else {
		reticle = DriftVec2FMA(reticle, DriftInputJoystick(input, 2, 3), 200*update->dt);
	}
	
	DriftVec2 clamped_reticle = DriftVec2Clamp(reticle, grabber_length);
	DriftVec2 pull = DriftVec2Mul(DriftVec2Sub(reticle, clamped_reticle), pull_coef/update->dt);
	player->desired_velocity = DriftVec2Clamp(DriftVec2Add(player->desired_velocity, pull), DRIFT_PLAYER_SPEED);
	// TODO clamp to a certain angle for aesthetic reasons?
	player->desired_rotation = DRIFT_VEC2_ZERO;
	
	DriftVec2 hatch_pos = DriftAffineDirection(transform, HATCH_POS);
	float hatch_dist = player->cargo_hatch_open ? DriftVec2Distance(hatch_pos, clamped_reticle) : INFINITY;
	if(hatch_dist < HATCH_RADIUS + GRABBER_RADIUS) clamped_reticle = DriftVec2LerpConst(clamped_reticle, hatch_pos, 50*update->dt);
	
	float ray_t = DriftTerrainRaymarch(state->terra, pos, DriftVec2Add(pos, clamped_reticle), 0, 1);
	clamped_reticle = DriftVec2Mul(clamped_reticle, ray_t);
	DriftTerrainSampleInfo info = DriftTerrainSampleFine(state->terra, DriftVec2Add(pos, clamped_reticle));
	clamped_reticle = DriftVec2FMA(clamped_reticle, info.grad, fmaxf(0, GRABBER_RADIUS - info.dist));
	
	player->reticle = clamped_reticle;
	DriftVec2 world_reticle = DriftVec2Add(pos, clamped_reticle);
	if(player->grabbed_entity.id == 0){
		if(hatch_dist < HATCH_RADIUS && DriftInputButtonPress(input, DRIFT_INPUT_ACTION1)){
			grabber_make(update, player, world_reticle);
			player->cargo_hatch_open = DriftInputButtonState(input, DRIFT_INPUT_ACTION2);
		} else if(DriftInputButtonPress(input, DRIFT_INPUT_ACTION1)){
			grabber_grab(update, player, world_reticle);
		}
	} else {
		if(DriftInputButtonState(input, DRIFT_INPUT_ACTION1)){
			grabber_update(update, player, world_reticle);
		}
		
		if(hatch_dist < HATCH_RADIUS && DriftInputButtonRelease(input, DRIFT_INPUT_ACTION1)){
			grabber_stash(update, player);
			player->cargo_hatch_open = DriftInputButtonState(input, DRIFT_INPUT_ACTION2);
		} else if(DriftInputButtonRelease(input, DRIFT_INPUT_ACTION1)){
			grabber_drop(update, player, world_reticle);
		}
	}
}

static void ToolDrawGrab(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	DriftVec2 pos = DriftAffineOrigin(transform), reticle = DriftVec2Add(pos, player->reticle);
	DriftRGBA8 grab_color = player->grabbed_entity.id ? (DriftRGBA8){0x00, 0x80, 0x00, 0x80} : (DriftRGBA8){0x80, 0x00, 0x00, 0x80};
	draw_mouse(draw, grab_color);
	
	DriftGameState* state = draw->state;
	uint transform_idx, pickup_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&pickup_idx, &state->pickups.c},
		{&transform_idx, &state->transforms.c},
		{},
	});
	
	float period = 0.5e9;
	uint pulse = (uint)(64 + 32*sinf((draw->ctx->update_nanos % (u64)period)*(2*(float)M_PI/period)));
	DriftRGBA8 highlight = {pulse, pulse/2, 0, pulse};
	
	while(DriftJoinNext(&join)){
		DriftVec2 p = DriftAffineOrigin(state->transforms.matrix[transform_idx]);
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p, .p1 = p, .radii = {HATCH_RADIUS, HATCH_RADIUS - 1}, .color = highlight}));
	}
	
	float hatch_anim = 1 - player->anim_state.hatch_l.value;
	DriftVec2 p = DriftVec2Mul(HATCH_POS, hatch_anim);
	DriftAffine t = DriftAffineMult(transform, (DriftAffine){1, 0, 0, 1, p.x, p.y});
	
	DriftCargoSlot* slot = player->cargo_slots + player->cargo_idx;
	if(player->grabbed_entity.id){
		DRIFT_PICKUPS[player->grabbed_type].draw(draw, reticle);
	} else if(slot->count){
		DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteMake(DRIFT_ITEMS[slot->type].icon, DRIFT_RGBA8_WHITE, t));
	}
	
	if(player->cargo_hatch_open){
		DriftRGBA8 hatch_color = {0x00, (u8)(0x80*hatch_anim), 0x00, (u8)(0x80*hatch_anim)};
		DriftVec2 hatch_pos = DriftAffineOrigin(t);
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = hatch_pos, .p1 = hatch_pos, .radii = {HATCH_RADIUS, HATCH_RADIUS - 1}, .color = hatch_color}));
	}

	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = reticle, .p1 = reticle, .radii = {GRABBER_RADIUS, GRABBER_RADIUS - 1}, .color = grab_color}));
}

#define DIG_RADIUS 20

static void ToolUpdateDig(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	DriftTerrain* terra = update->state->terra;
	DriftPlayerInput* input = &update->ctx->input.player;
	
	player->anim_state.laser.target = 1;
	
	float pull_coef = 1;
	DriftVec2 reticle = player->reticle;
	DriftVec2 pos = DriftAffineOrigin(transform);
	
	if(update->ctx->input.mouse_captured){
		pull_coef = 2e-2f;
		reticle = DriftVec2Sub(update->ctx->input.mouse_pos, pos);
	} else {
		reticle = DriftVec2FMA(reticle, DriftInputJoystick(input, 2, 3), 200*update->dt);
	}
	
	DriftVec2 clamped_reticle = DriftVec2Clamp(reticle, 96);
	player->reticle = clamped_reticle;
	
	DriftTerrainSampleInfo info = DriftTerrainSampleFine(terra, pos);
	DriftVec2 repel = DriftVec2Mul(info.grad, 20.0f*fmaxf(0, (24 - info.dist)));
	DriftVec2 pull = DriftVec2Mul(DriftVec2Sub(reticle, player->reticle), pull_coef/update->dt);
	player->desired_velocity = DriftVec2Add(player->desired_velocity, DriftVec2Add(repel, pull));
	player->desired_velocity = DriftVec2Clamp(player->desired_velocity, DRIFT_PLAYER_SPEED);
	player->desired_rotation = DriftVec2Normalize(player->reticle);
	
	player->is_digging = input->axes[DRIFT_INPUT_AXIS_ACTION1] > 0.75f;
	if(player->is_digging){
		// TODO should be in fixed update?
		DriftVec2 world_reticle = DriftVec2Add(pos, player->reticle);
		float alpha = DriftTerrainRaymarch(terra, pos, world_reticle, DIG_RADIUS, 1);
		float t = 3.0f*(update->ctx->update_nanos*1e-9f);
		DriftVec2 sincos = {0.75f*DIG_RADIUS*cosf(t), 0.75f*DIG_RADIUS*sinf(t)};
		
		// Reflect across the look direction.
		DriftVec2 n = DriftVec2Normalize(player->reticle);
		float dot = DriftVec2Dot(n, sincos);
		if(dot < 0) sincos = DriftVec2Sub(sincos, DriftVec2Mul(n, 2*dot));
		
		DriftVec2 dig_pos = DriftVec2Add(DriftVec2Lerp(pos, world_reticle, alpha), sincos);
		DriftTerrainSampleInfo info = DriftTerrainSampleFine(terra, dig_pos);
		if(alpha < 1) DriftTerrainDig(terra, dig_pos, 0.75f*DIG_RADIUS);
		
		player->dig_pos = dig_pos;
		player->desired_rotation = DriftVec2Normalize(DriftVec2Sub(dig_pos, pos));
	}
}

static void ToolDrawDig(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	DriftVec2 pos = DriftAffineOrigin(transform), reticle = DriftVec2Add(pos, player->reticle);
	DriftRGBA8 color = {0x80, 0x00, 0x00, 0x80};
	draw_mouse(draw, color);
	
	DriftRGBA8 color2 = {color.r/2, color.g/2, color.b/2, color.a/2};
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = pos, .p1 = reticle, .radii = {DIG_RADIUS, DIG_RADIUS - 1.0f}, .color = color2}));
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = reticle, .p1 = reticle, .radii = {DIG_RADIUS, DIG_RADIUS - 1.5f}, .color = color}));
	
	if(player->is_digging){
		DriftVec2 p0 = DriftAffinePoint(transform, (DriftVec2){0, 24});
		DriftVec2 p1 = player->dig_pos;
		
		DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteMake(DRIFT_SPRITE_LASER_DOT, (DriftRGBA8){0xFF, 0x60, 0x40, 0x00}, (DriftAffine){1, 0, 0, 1, p1.x, p1.y}));
		DRIFT_ARRAY_PUSH(draw->lights, DriftLightMake(true, DRIFT_SPRITE_LIGHT_RADIAL, (DriftVec4){{5, 2, 1, 2}}, (DriftAffine){256, 0, 0, 256, p1.x, p1.y}, 4));
		DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){.p0 = p0, .p1 = p1, .radii = {3}, .color = (DriftRGBA8){0xFF, 0x60, 0x40, 0x00}}));
	}
}

// TODO pull out commonalities to fly?
static void ToolUpdateGun(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	player->anim_state.cannons.target = 1;
	
	DriftPlayerInput* input = &update->ctx->input.player;
	DriftVec2 pos = DriftAffineOrigin(transform);
	
	if(update->ctx->input.mouse_captured){
		DriftVec2 mouse_look = DriftVec2Sub(update->ctx->input.mouse_pos, pos);
		player->desired_rotation = DriftVec2Mul(mouse_look, 1/fmaxf(DriftVec2Length(mouse_look), 64.0f));
	} else {
		player->desired_rotation = DriftInputJoystick(input, 2, 3);
	}
	
	player->reticle = DriftVec2Mul((DriftVec2){transform.c, transform.d}, 64);

	static uint repeat = 0;
	static float timeout = 0, inc = 0;
	timeout -= update->dt;
	
	if(timeout < 0 && DriftInputButtonPress(input, DRIFT_INPUT_ACTION1)){
		repeat = 1, timeout = 0, inc = 0.25f;
		
		if(update->ctx->inventory[DRIFT_ITEM_TYPE_CANNON]) repeat = 4, inc = 0.1f;
		if(update->ctx->inventory[DRIFT_ITEM_TYPE_AUTOCANNON]) repeat = 8, inc = 0.05f;
	}
	
	// if(DriftInputButtonState(input, DRIFT_INPUT_ACTION1)){
	// 	repeat = 1;
	// }
	
	if(repeat && timeout <= 0){
		PlayerCannonTransforms cannons = CalculatePlayerCannonTransforms(1);
		static uint which = 0;
		DriftAffine cannon = cannons.arr[which++ & 3];
		cannon.c -= cannon.x/400;
		
		cannon = DriftAffineMult(transform, cannon);
		void FireBullet(DriftUpdate* update, DriftVec2 pos, DriftVec2 vel);
		FireBullet(update, DriftAffineOrigin(cannon), DriftAffineDirection(cannon, (DriftVec2){0, 2000}));
		repeat--, timeout += inc;
	}
}

static void ToolDrawGun(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){
	DriftVec2 pos = DriftAffineOrigin(transform), rot = player->desired_rotation;
	
	draw_mouse(draw, DRIFT_RGBA8_GREEN);

	DriftAffine m1 = (DriftAffine){rot.y, -rot.x, rot.x, rot.y, pos.x, pos.y};
	float r = 1.5f*DriftVec2Length(rot);
	DriftVec2 p[] = {
		DriftAffinePoint(m1, (DriftVec2){-4, 36}),
		DriftAffinePoint(m1, (DriftVec2){ 0, 38}),
		DriftAffinePoint(m1, (DriftVec2){ 4, 36}),
	};
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[0], .p1 = p[1], .radii = {r}, .color = DRIFT_RGBA8_GREEN}));
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[1], .p1 = p[2], .radii = {r}, .color = DRIFT_RGBA8_GREEN}));
	
	PlayerCannonTransforms cannons = CalculatePlayerCannonTransforms(1);
	for(uint i = 0; i < 4; i++){
		DriftAffine cannon = cannons.arr[i];
		cannon.c -= cannon.x/400;
		
		DriftAffine t = DriftAffineMult(transform, cannon);
		DriftVec2 p[2] = {
			DriftAffinePoint(t, (DriftVec2){0,  120}),
			DriftAffinePoint(t, (DriftVec2){0, 150}),
		};
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[0], .p1 = p[1], .radii = {1.5f}, .color = {0x40, 0x00, 0x00, 0x40}}));
	}
}

static void ToolDrawNull(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform){}

const DriftTool DRIFT_TOOLS[] = {
	[DRIFT_TOOL_FLY] = {.name = "Fly", .update = ToolUpdateFly, .draw = ToolDrawFly},
	[DRIFT_TOOL_GRAB] = {.name = "Grab", .update = ToolUpdateGrab, .draw = ToolDrawGrab},
	[DRIFT_TOOL_DIG] = {.name = "Dig", .update = ToolUpdateDig, .draw = ToolDrawDig},
	[DRIFT_TOOL_GUN] = {.name = "Shoot", .update = ToolUpdateGun, .draw = ToolDrawGun},
};
