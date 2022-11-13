#include <stdlib.h>
#include <string.h>

#include "tracy/TracyC.h"

#include "drift_game.h"

static void component_init(DriftGameState* state, DriftComponent* component, const char* name, DriftColumnSet columns, uint capacity){
	DriftMem* mem = DriftSystemMem;
	DriftComponentInit(component, (DriftTableDesc){.name = name, .mem = mem, .min_row_capacity = capacity, .columns = columns});
	DRIFT_ARRAY_PUSH(state->components, component);
}

static DriftVec3 DriftMul3xN(const DriftVec3 M[], float x[], uint n){
	DriftVec3 result = {};
	for(uint i = 0; i < n; i++) result = DriftVec3Add(result, DriftVec3Mul(M[i], x[i]));
	return result;
}

static void DriftPseudoInverseNx3(const DriftVec3 M[], DriftVec3 p_inv[], uint n){
	DriftVec3 m[3] = {};
	for(uint i = 0; i < n; i++){
		m[0] = DriftVec3Add(m[0], DriftVec3Mul(M[i], M[i].x));
		m[1] = DriftVec3Add(m[1], DriftVec3Mul(M[i], M[i].y));
		m[2] = DriftVec3Add(m[2], DriftVec3Mul(M[i], M[i].z));
	}
	
	float det_inv = 1/(
		+m[0].x*(m[1].y*m[2].z - m[2].y*m[1].z)
		-m[0].y*(m[1].x*m[2].z - m[2].x*m[1].z)
		+m[0].z*(m[1].x*m[2].y - m[2].x*m[1].y)
	);
	
	DriftVec3 m_inv[3] = {
		{{(m[1].y*m[2].z - m[2].y*m[1].z)*det_inv, (m[0].y*m[2].z - m[2].y*m[0].z)*det_inv, (m[0].y*m[1].z - m[1].y*m[0].z)*det_inv}},
		{{(m[1].x*m[2].z - m[2].x*m[1].z)*det_inv, (m[0].x*m[2].z - m[2].x*m[0].z)*det_inv, (m[0].x*m[1].z - m[1].x*m[0].z)*det_inv}},
		{{(m[1].x*m[2].y - m[2].x*m[1].y)*det_inv, (m[0].x*m[2].y - m[2].x*m[0].y)*det_inv, (m[0].x*m[1].y - m[1].x*m[0].y)*det_inv}},
	};
	
	for(uint i = 0; i < n; i++){
		p_inv[i] = (DriftVec3){{DriftVec3Dot(M[i], m_inv[0]), DriftVec3Dot(M[i], m_inv[1]), DriftVec3Dot(M[i], m_inv[2])}};
	}
}

static DriftVec3 DriftRCS(DriftVec3* matrix, DriftVec3* inverse, DriftVec3 desired, float solution[], uint n){
	for(uint i = 0; i < 4; i++){
		float ease = expf(-1.5f*i);
		DriftVec3 b_approx = DriftMul3xN(matrix, solution, n);
		DriftVec3 b_proj = DriftVec3Mul(desired, DriftVec3Dot(b_approx, desired)/(DriftVec3Dot(desired, desired) + FLT_MIN));
		DriftVec3 b_clip = DriftVec3Lerp(b_proj, desired, ease);
		DriftVec3 b_err = DriftVec3Sub(b_clip, b_approx);
		for(uint i = 0; i < n; i++) solution[i] += DriftVec3Dot(inverse[i], b_err);
		
		solution[0] = DriftClamp(solution[0], 0, 1);
    float coef_r = fminf(1.0f, 1/hypotf(solution[1], solution[2]));
    solution[1] *= coef_r;
    solution[2] *= coef_r;
    float coef_l = fminf(1.0f, 1/hypotf(solution[3], solution[4]));
    solution[3] *= coef_l;
    solution[4] *= coef_l;
	}
	
	return DriftMul3xN(matrix, solution, n);
}

static void DriftAnimStateUpdate(DriftAnimState* state, float dt){
	state->value = DriftLerpConst(state->value, state->target, dt*state->rate);
}

static const DriftVec2 nacelle_in = {12, -11}, nacelle_out = {28, -14};

DriftEntity DriftTempPlayerInit(DriftGameState* state, DriftEntity e, DriftVec2 position){
	uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	uint player_idx = DriftComponentAdd(&state->players.c, e);
	DriftPlayerData* player = state->players.data + player_idx;
	player->energy = 0;
	player->energy_cap = 100;
	player->cargo_slots[DRIFT_PLAYER_CARGO_SLOT_COUNT - 1].request = DRIFT_ITEM_POWER_NODE;
	player->headlight = true;
	player->tool_idx = DRIFT_TOOL_NONE;
	
	uint health_idx = DriftComponentAdd(&state->health.c, e);
	state->health.data[health_idx] = (DriftHealth){.value = 0, .maximum = 100, .damage_timeout = 0.2*DRIFT_TICK_HZ};
	
	float radius = DRIFT_PLAYER_SIZE, mass = 10;
	state->bodies.position[body_idx] = position;
	state->bodies.rotation[body_idx] = (DriftVec2){cosf(0.5f), sinf(0.5f)};
	state->bodies.radius[body_idx] = radius;
	state->bodies.collision_type[body_idx] = DRIFT_COLLISION_PLAYER,
	
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 1/(mass*0.5f*radius*radius);
	
	return e;
}

static void UpdatePlayer(DriftUpdate* update){
	DriftGameState* state = update->state;
	float dt = update->dt;
	float input_smoothing = expf(-20*dt);
	
	// TODO this really shouldn't go here
	DriftPlayerInput* input = &update->ctx->input.player;
	if(
		!DriftEntitySetCheck(&update->state->entities, update->ctx->player) &&
		DriftInputButtonRelease(input, DRIFT_INPUT_ACCEPT)
	){
		DriftEntity entity = update->ctx->player = DriftMakeEntity(state);
		DriftTempPlayerInit(state, entity, DRIFT_HOME_POSITION);
	}
	
	uint player_idx, transform_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&player_idx, &state->players.c},
		{&transform_idx, &state->transforms.c},
		{},
	});
	
	while(DriftJoinNext(&join)){
		DriftPlayerData* player = state->players.data + player_idx;
		DriftPlayerAnimState* anim = &player->anim_state;
		
		// Reset animation states
		anim->hatch_l.target = 1, anim->hatch_l.rate = 1/0.2f;
		anim->hatch_r.target = 1, anim->hatch_r.rate = 1/0.2f;
		anim->laser.target = 0, anim->laser.rate = 1/0.2f;
		anim->cannons.target = 0, anim->cannons.rate = 1/0.2f;
		
		if(state->status.enable_controls){
			if(DriftInputButtonPress(input, DRIFT_INPUT_LIGHT)){
				DriftAudioPlaySample(update->audio, DRIFT_SFX_CLICK, 1, 0, 1, false);
				player->headlight = !player->headlight;
			}
			
			// Is the button pressed, but ignore the first frame.
			u64 press_after = input->bstate & ~input->bpress;
			
			DriftToolType tool_idx = player->tool_idx;
			if(DriftInputButtonPress(input, DRIFT_INPUT_CANCEL)) tool_idx = DRIFT_TOOL_NONE;
			if(press_after & DRIFT_INPUT_FIRE) tool_idx = DRIFT_TOOL_GUN;
			if(press_after & DRIFT_INPUT_GRAB) tool_idx = DRIFT_TOOL_GRAB;
			if(press_after & DRIFT_INPUT_SCAN) tool_idx = DRIFT_TOOL_SCAN;
			if(press_after & DRIFT_INPUT_LASER && state->inventory[DRIFT_ITEM_MINING_LASER]) tool_idx = DRIFT_TOOL_DIG;
			if(DriftInputButtonPress(input, DRIFT_INPUT_DROP)) tool_idx = DRIFT_TOOL_GRAB;
			
			// TODO energy is a hack for the tutorial, is it annoying later?
			if(player->energy > 0 && tool_idx != player->tool_idx){
				DriftAudioPlaySample(update->audio, DRIFT_SFX_CLICK, 1, 0, 1, false);
				player->tool_idx = tool_idx;
				player->arm_l = (DriftArmPose){.angle = {(float)M_PI/4, (float)M_PI, (float)M_PI}};
				player->arm_r = (DriftArmPose){.angle = {(float)M_PI/4, (float)M_PI, (float)M_PI}};
			}
			
			// DriftVec2 prev_velocity = player->desired_velocity, prev_rotation = player->desired_rotation;
			DriftToolUpdate(update, player, state->transforms.matrix[transform_idx]);
			
			// // Apply input smoothing.
			// player->desired_velocity = DriftVec2Lerp(player->desired_velocity, prev_velocity, input_smoothing);
			// player->desired_rotation = DriftVec2Lerp(player->desired_rotation, prev_rotation, input_smoothing);
		} else {
			player->desired_velocity = DRIFT_VEC2_ZERO;
			player->desired_rotation = DRIFT_VEC2_ZERO;
		}
		
		DriftAnimStateUpdate(&anim->hatch_l, dt);
		DriftAnimStateUpdate(&anim->hatch_r, dt);
		DriftAnimStateUpdate(&anim->laser, dt);
		DriftAnimStateUpdate(&anim->cannons, dt);

		DriftAffine transform_r = state->transforms.matrix[transform_idx];
		DriftAffine transform_l = DriftAffineMul(transform_r, (DriftAffine){-1, 0, 0, 1, 0, 0});
		float nacelle_l = DriftTerrainRaymarch(state->terra, DriftAffinePoint(transform_l, nacelle_in), DriftAffinePoint(transform_l, nacelle_out), 4, 2);
		player->nacelle_l = fminf(nacelle_l, DriftLerpConst(player->nacelle_l, 1, dt/0.25f));
		
		float nacelle_r = DriftTerrainRaymarch(state->terra, DriftAffinePoint(transform_r, nacelle_in), DriftAffinePoint(transform_r, nacelle_out), 4, 2);
		player->nacelle_r = fminf(nacelle_r, DriftLerpConst(player->nacelle_r, 1, dt/0.25f));
		
		float volume = 0;
		for(uint i = 0; i < 5; i++) volume += player->thrusters[i]*player->thrusters[i];
		
		if(volume > 1e-2){
			if(DriftAudioSourceActive(update->audio, state->thruster_sampler.source)){
				DriftSamplerSetParams(update->audio, state->thruster_sampler, 3*volume, 0, 1, true);
			} else {
				state->thruster_sampler = DriftAudioPlaySample(update->audio, DRIFT_SFX_ENGINE, 0, 0, 1, true);
			}
		} else {
			DriftSamplerSetParams(update->audio, state->thruster_sampler, 0, 0, 1, false);
		}
	}
}

static void player_cargo_exchange(DriftUpdate* update, DriftPlayerData* player){
	bool is_request[DRIFT_PLAYER_CARGO_SLOT_COUNT] = {};
	
	// Fixup state for empty slots and decide if they are requests.
	for(uint i = 0; i < DRIFT_PLAYER_CARGO_SLOT_COUNT; i++){
		DriftCargoSlot* slot = player->cargo_slots + i;
		if(slot->count == 0) slot->type = slot->request;
		is_request[i] = slot->request && slot->request == slot->type;
	}

	// Fill one request slot
	for(uint i = 0; i < DRIFT_PLAYER_CARGO_SLOT_COUNT; i++){
		DriftCargoSlot* slot = player->cargo_slots + i;
		if(is_request[i] && update->state->inventory[slot->type]){
			update->state->inventory[slot->request]--;
			slot->count++;
			DriftAudioPlaySample(update->audio, DRIFT_SFX_PING, 0.5f, 0, 1, false);
			DriftContextPushToast(update->ctx, "Loaded %s", DRIFT_ITEMS[slot->type].name);
			break;
		}
	}

	// Empty one transfer slot.
	for(uint i = 0; i < DRIFT_PLAYER_CARGO_SLOT_COUNT; i++){
		DriftCargoSlot* slot = player->cargo_slots + i;
		if(!is_request[i] && slot->count > 0){
			slot->count--;
			update->state->inventory[slot->type]++;
			DriftAudioPlaySample(update->audio, DRIFT_SFX_PING, 0.5f, 0, 1, false);
			DriftContextPushToast(update->ctx, "Stored %s", DRIFT_ITEMS[slot->type].name);
			break;
		}
	}
}

static void TickPlayer(DriftUpdate* update){
	DriftGameState* state = update->state;	
	float tick_dt = update->tick_dt;
	float rcs_damping = expf(-20*tick_dt);
	float rcs_smoothing = expf(-30*tick_dt);
	
	float w_bias = 5;
	
	// TODO cache this and the inverse if it's going to be static.
	DriftVec3 rcs_matrix[] = {
		{{0.0e0f, 1.2e3f,  0.0e1f*w_bias}}, // main
		{{3.0e2f, 0.0e0f,  4.0e1f*w_bias}}, // right x
		{{0.0e0f, 3.0e2f, -8.0e1f*w_bias}}, // right y
		{{3.0e2f, 0.0e0f,  4.0e1f*w_bias}}, // left x
		{{0.0e0f, 3.0e2f,  8.0e1f*w_bias}}, // left y
	};
	
	DriftVec3 rcs_inverse[5];
	DriftPseudoInverseNx3(rcs_matrix, rcs_inverse, 5);
	
	uint player_idx, transform_idx, body_idx, health_idx, nav_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&player_idx, &state->players.c},
		{&body_idx, &state->bodies.c},
		{&transform_idx, &state->transforms.c},
		{&health_idx, &state->health.c},
		{&nav_idx, &state->navs.c, .optional = true},
		{},
	});
	
	while(DriftJoinNext(&join)){
		DriftPlayerData* player = &state->players.data[player_idx];
		DriftVec2 player_pos = state->bodies.position[body_idx];
		
		DriftVec2 desired_velocity = player->desired_velocity;
		DriftVec2 desired_rotation = player->desired_rotation;
		
		// TODO temp dock with factory code
		if(update->ctx->ui_state == DRIFT_UI_STATE_CRAFT){
			DriftVec2 dock_delta = DriftVec2Sub(DRIFT_FACTORY_POSITION, player_pos);
			desired_velocity = DriftVec2FMA(desired_velocity, dock_delta, 2);
			if(DriftVec2Length(dock_delta) < 32 && update->tick % 8 == 0) player_cargo_exchange(update, player);
		}

		DriftAffine m = state->transforms.matrix[transform_idx];
		DriftAffine m_inv = DriftAffineInverse(m);
		
		if(state->navs.data[nav_idx].next_node.id){
			DriftVec2 target_pos = state->navs.data[nav_idx].target_pos;
			DriftVec2 delta = DriftVec2Sub(target_pos, player_pos);
			desired_velocity = DriftVec2Clamp(DriftVec2Mul(delta, 0.15f/tick_dt), 1.5f*DRIFT_PLAYER_SPEED);
		}
		
		if(player->energy == 0) desired_velocity = DriftVec2Clamp(desired_velocity, 0.4f*DRIFT_PLAYER_SPEED);
		
		// Calculate the change in body relative velocity to hit the desired.
		DriftVec2 v = state->bodies.velocity[body_idx];
		DriftVec2 delta_v = DriftVec2Sub(desired_velocity, v);
		DriftVec2 local_delta_v = DriftAffineDirection(m_inv, delta_v);
		// local_delta_v = DriftVec2Clamp(local_delta_v, 2000.0f*dt);
		
		// Start by assuming we want te reduce the angular velocity to 0.
		float w = state->bodies.angular_velocity[body_idx];
		float delta_w = -w;
		
		// DriftVec2 v_mix = DriftVec2Lerp(delta_v, v, 0.75f);
		// DriftVec2 v_mix = DriftVec2Mul(desired_velocity, 0.01f);
		// DriftVec2 desired_rot = DriftVec2Lerp(v_mix, desired_rotation, DriftVec2Length(desired_rotation));
		if(DriftVec2LengthSq(desired_rotation) > 1e-3f){
			DriftVec2 local_rot = DriftAffineDirection(m_inv, desired_rotation);
			delta_w = -(1 - expf(-10*tick_dt))*atan2f(local_rot.x, local_rot.y)/tick_dt - w;
		}
		
		// Dampen the previous RCS impulse
		float thrusters[5];
		for(uint i = 0; i < 5; i++) thrusters[i] = player->thrusters[i]*rcs_damping;
		
		DriftVec3 impulse = {{local_delta_v.x/tick_dt, local_delta_v.y/tick_dt, delta_w*w_bias/tick_dt}};
		impulse = DriftVec3Clamp(impulse, 2000);
		
		//impulse
		DriftRCS(rcs_matrix, rcs_inverse, impulse, thrusters, 5);
		state->bodies.velocity[body_idx] = DriftVec2Add(v, DriftAffineDirection(m, (DriftVec2){impulse.x*tick_dt, impulse.y*tick_dt}));
		state->bodies.angular_velocity[body_idx] += impulse.z*tick_dt/w_bias;
		
		// Smooth out the RCS impulse
		for(uint i = 0; i < 5; i++) player->thrusters[i] = DriftLerp(thrusters[i], player->thrusters[i], rcs_smoothing);
		
		// Update shield/power/heat
		DriftHealth* health = state->health.data + health_idx;
		if(update->ctx->debug.godmode){
			player->is_powered = true;
			player->energy = player->energy_cap;
			player->power_timestamp = update->tick;
			player->is_overheated = false;
			player->temp = 0;
			health->value = health->maximum;
		} else {
			float shield_rate = -10;
			if(player->energy > 0){
				bool waiting_to_recharge = update->tick - health->damage_timestamp < 4*DRIFT_TICK_HZ;
				shield_rate = (waiting_to_recharge ? 0 : 30);
			}
			health->value = DriftClamp(health->value + shield_rate*tick_dt, 0, health->maximum);
			if(health->value > 0) player->shield_timestamp = update->tick;
			
			float power_draw = 5;
			if(player->headlight) power_draw += 5;
			if(player->is_digging) power_draw += 20;
			player->energy = fmaxf(player->energy - power_draw*tick_dt, 0);

			DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(state, player_pos, update->mem, 0);
			player->is_powered = info.player_can_connect;
			if(player->is_powered){
				player->energy = fminf(player->energy + 50*tick_dt, player->energy_cap);
				player->power_timestamp = update->tick;
			}
			
			if(player->is_digging) player->temp += 0.3f*tick_dt;
			if(player->temp >= 1) player->is_overheated = true;
			player->temp = DriftLerpConst(player->temp, 0, 0.20f*tick_dt);
			if(player->temp == 0) player->is_overheated = false;
		}
	}
}

static DriftAffine NacelleMatrix(DriftVec2 dir, DriftVec2 offset){
	float mag = DriftVec2Length(dir);
	dir = (mag > 0.1f ? DriftVec2Mul(dir, 0.5f/mag) : (DriftVec2){0, 0.5f});
	return (DriftAffine){dir.y, -dir.x, dir.x, dir.y, offset.x, offset.y};
}

static DriftAffine FlameMatrix(float mag){
	return (DriftAffine){0.5f*mag + 0.5f, 0, 0, 1.0f*mag + 0.5f, 0, -8.0f*DriftSaturate(mag)};
}

PlayerCannonTransforms CalculatePlayerCannonTransforms(float cannon_anim){
	DriftAffine matrix_gun0 = {1, 0, 0, 1,  8, -7 - -7*cannon_anim - 3};
	DriftAffine matrix_gun1 = {1, 0, 0, 1, 12, -5 - -5*cannon_anim - 5};
	
	return (PlayerCannonTransforms){{
		DriftAffineMul((DriftAffine){-1, 0, 0, 1, 0, 0}, matrix_gun0),
		DriftAffineMul((DriftAffine){+1, 0, 0, 1, 0, 0}, matrix_gun0),
		DriftAffineMul((DriftAffine){-1, 0, 0, 1, 0, 0}, matrix_gun1),
		DriftAffineMul((DriftAffine){+1, 0, 0, 1, 0, 0}, matrix_gun1),
	}};
}

static void DrawPlayer(DriftDraw* draw){
	DriftGameState* state = draw->state;
	uint player_idx, transform_idx, health_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&player_idx, &state->players.c},
		{&transform_idx, &state->transforms.c},
		{&health_idx, &state->health.c},
		{},
	});
	while(DriftJoinNext(&join)){
		DriftPlayerData* player = &state->players.data[player_idx];
		DriftPlayerAnimState* anim = &player->anim_state;
		
		float hatch_l = fmaxf(1 - anim->hatch_l.value, anim->cannons.value);
		float hatch_r = fmaxf(1 - anim->hatch_r.value, anim->cannons.value);
		
		const DriftAffine matrix_model = state->transforms.matrix[transform_idx];
		DriftToolDraw(draw, player, matrix_model);
		
		DriftAffine matrix_model_r = DriftAffineMul(matrix_model, (DriftAffine){ 1, 0, 0, 1, 0, 0});
		DriftAffine matrix_model_l = DriftAffineMul(matrix_model, (DriftAffine){-1, 0, 0, 1, 0, 0});
		
		float retract_l = 1 - player->nacelle_l;
		DriftVec2 nacelle_offset_l = DriftVec2Lerp(nacelle_out, nacelle_in, retract_l);
		DriftVec2 thrust_l = {-player->thrusters[1], player->thrusters[2]};
		float thrust_mag_l = DriftVec2Length(thrust_l);
		
		float retract_r = 1 - player->nacelle_r;
		DriftVec2 nacelle_offset_r = DriftVec2Lerp(nacelle_out, nacelle_in, retract_r);
		DriftVec2 thrust_r = {player->thrusters[3], player->thrusters[4]};
		float thrust_mag_r = DriftVec2Length(thrust_r);
				
		DriftAffine matrix_strut_l = DriftAffineTRS(nacelle_offset_l, 0.8f*retract_l, (DriftVec2){1, 1});
		DriftAffine matrix_strut_r = DriftAffineTRS(nacelle_offset_r, 0.8f*retract_r, (DriftVec2){1, 1});
		DriftAffine matrix_engine = DriftAffineMul(matrix_model_r, (DriftAffine){1, 0, 0, 1, 0, -10});
		DriftAffine matrix_nacelle_l = DriftAffineMul(matrix_model_l, NacelleMatrix(thrust_l, nacelle_offset_l));
		DriftAffine matrix_nacelle_r = DriftAffineMul(matrix_model_r, NacelleMatrix(thrust_r, nacelle_offset_r));
		DriftAffine matrix_laser = DriftAffineTRS((DriftVec2){0, 9 + 7*DriftHermite3(anim->laser.value)}, 0, (DriftVec2){1, 1});
		DriftAffine matrix_hatch_l = DriftAffineTRS((DriftVec2){0, 0}, 0.85f*hatch_l, (DriftVec2){DriftLerp(1, 0.9f, hatch_l), 1});
		DriftAffine matrix_hatch_r = DriftAffineTRS((DriftVec2){0, 0}, 0.85f*hatch_r, (DriftVec2){DriftLerp(1, 0.9f, hatch_r), 1});
		DriftAffine matrix_bay_l = DriftAffineTRS((DriftVec2){-3*retract_l, 3*retract_l}, 0, (DriftVec2){1, -1});
		DriftAffine matrix_bay_r = DriftAffineTRS((DriftVec2){-3*retract_r, 3*retract_r}, 0, (DriftVec2){1, -1});
		DriftAffine matrix_flame_light = (DriftAffine){128, 0, 0, -64, 0, 0};
		
		DriftAffine matrix_flame = FlameMatrix(player->thrusters[0]);
		DriftAffine matrix_flame_l = FlameMatrix(thrust_mag_l);
		DriftAffine matrix_flame_r = FlameMatrix(thrust_mag_r);
		uint flame_frame = DRIFT_SPRITE_FLAME0 + draw->frame%_DRIFT_SPRITE_FLAME_COUNT;
		
		DriftVec4 flame_glow = (DriftVec4){{1.14f, 0.78f, 0.11f, 1.00f}};
		const DriftRGBA8 color = {0xFF, 0xFF, 0xFF, 0xFF};
		const DriftRGBA8 glow = {0xC0, 0xC0, 0xC0, 0x80};
		
		DriftSprite* sprites = DRIFT_ARRAY_RANGE(draw->fg_sprites, 64);
		DriftLight* lights = DRIFT_ARRAY_RANGE(draw->lights, 64);
		
		DriftSpritePush(&sprites, flame_frame, glow, DriftAffineMul(matrix_engine, matrix_flame));
		DriftSpritePush(&sprites, DRIFT_SPRITE_NACELLE, color, matrix_engine);
		DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_HEMI, DriftVec4Mul(flame_glow, player->thrusters[0]), DriftAffineMul(matrix_engine, matrix_flame_light), 0);
		
		DriftSpritePush(&sprites, DRIFT_SPRITE_STRUT, color, DriftAffineMul(matrix_model_l, matrix_strut_l));
		DriftSpritePush(&sprites, flame_frame, glow, DriftAffineMul(matrix_nacelle_l, matrix_flame_l));
		DriftSpritePush(&sprites, DRIFT_SPRITE_NACELLE, color, matrix_nacelle_l);
		DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_HEMI, DriftVec4Mul(flame_glow, thrust_mag_l), DriftAffineMul(matrix_nacelle_l, matrix_flame_light), 0);
		// if(frame%256 < 4) DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_RADIAL, (DriftVec4){{0, 0.4f, 0, 1}}, DriftAffineMult(matrix_nacelle_l, (DriftAffine){256, 0, 0, 256, 0, 0}), 0);
		
		DriftSpritePush(&sprites, DRIFT_SPRITE_STRUT, color, DriftAffineMul(matrix_model_r, matrix_strut_r));
		DriftSpritePush(&sprites, flame_frame, glow, DriftAffineMul(matrix_nacelle_r, matrix_flame_r));
		DriftSpritePush(&sprites, DRIFT_SPRITE_NACELLE, color, matrix_nacelle_r);
		DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_HEMI, DriftVec4Mul(flame_glow, thrust_mag_r), DriftAffineMul(matrix_nacelle_r, matrix_flame_light), 0);
		// if(frame%256 < 4) DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_RADIAL, (DriftVec4){{1, 0, 0, 1}}, DriftAffineMult(matrix_nacelle_r, (DriftAffine){256, 0, 0, 256, 0, 0}), 0);
		
		DriftSpritePush(&sprites, DRIFT_SPRITE_LASER, color, DriftAffineMul(matrix_model, matrix_laser));
		PlayerCannonTransforms cannons = CalculatePlayerCannonTransforms(anim->cannons.value);
		DriftSpritePush(&sprites, DRIFT_SPRITE_GUN, color, DriftAffineMul(matrix_model, cannons.arr[0]));
		DriftSpritePush(&sprites, DRIFT_SPRITE_GUN, color, DriftAffineMul(matrix_model, cannons.arr[1]));
		DriftSpritePush(&sprites, DRIFT_SPRITE_GUN, color, DriftAffineMul(matrix_model, cannons.arr[2]));
		DriftSpritePush(&sprites, DRIFT_SPRITE_GUN, color, DriftAffineMul(matrix_model, cannons.arr[3]));
		
		DriftSpritePush(&sprites, DRIFT_SPRITE_HATCH, color, DriftAffineMul(matrix_model_l, matrix_hatch_l)); sprites[-1].shiny = 0.1f;
		DriftSpritePush(&sprites, DRIFT_SPRITE_HATCH, color, DriftAffineMul(matrix_model_r, matrix_hatch_r)); sprites[-1].shiny = 0.1f;
		DriftSpritePush(&sprites, DRIFT_SPRITE_HATCH, color, DriftAffineMul(matrix_model_l, matrix_bay_l)); sprites[-1].shiny = 0.1f;
		DriftSpritePush(&sprites, DRIFT_SPRITE_HATCH, color, DriftAffineMul(matrix_model_r, matrix_bay_r)); sprites[-1].shiny = 0.1f;
		DriftSpritePush(&sprites, DRIFT_SPRITE_HULL, color, matrix_model); sprites[-1].shiny = 0.1f;
		
		DriftVec4 dim_glow = (DriftVec4){{0.01f, 0.01f, 0.01f, 10.00f}};
		DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_RADIAL, dim_glow, DriftAffineMul(matrix_model, (DriftAffine){90, 0, 0, 90, 0, 0}), 0);
		
		DriftVec4 laser_glow = DriftVec4Mul((DriftVec4){{1.00f, 0.00f, 0.17f, 0.50f}}, anim->laser.value);
		DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_RADIAL, laser_glow, DriftAffineMul(matrix_model, (DriftAffine){54, 0, 0, 54, 0, 23}), 0);
		
		if(player->headlight){
			DriftVec4 headlight_color = (DriftVec4){{0.39f, 0.38f, 0.27f, 1.00f}};
			DriftAffine headlight_matrix = {150, 0, 0, 120, 0, 4};
			uint headlight_frame = DRIFT_SPRITE_LIGHT_HEMI;
			
			if(player->energy > 0 && state->inventory[DRIFT_ITEM_HEADLIGHT]){
				headlight_color = (DriftVec4){{2, 2, 2, 1.00f}};
				headlight_matrix = (DriftAffine){200, 0, 0, 300, 0, 4};
				headlight_frame = DRIFT_SPRITE_LIGHT_HEMI;
			}
			
			DriftLightPush(&lights, true, headlight_frame, headlight_color, DriftAffineMul(matrix_model, headlight_matrix), 10);
		}
		
		{ // Draw shield
			DriftHealth* health = state->health.data + health_idx;
			float value = health->value/health->maximum;
			float timeout = (draw->tick - player->shield_timestamp)/(0.10f*DRIFT_TICK_HZ);
			if(value > 0 || timeout < 1){
				float shield_fade = powf(1 - value, 0.2f);
				DriftVec4 color = {{shield_fade*shield_fade*shield_fade, value*shield_fade, value*value*value*shield_fade, 0}};
				DriftVec2 rot = DriftVec2Normalize(DriftRandomInUnitCircle());
				DriftAffine m = DriftAffineMul(matrix_model, (DriftAffine){rot.x, rot.y, -rot.y, rot.x});
				
				color = DriftVec4Mul(color, 1 + timeout*(4 - 5*timeout));
				float scale = DriftLogerp(1, 3, timeout*timeout*timeout);
				m = DriftAffineMul(m, (DriftAffine){scale, 0, 0, scale});
				
				DriftSpritePush(&sprites, DRIFT_SPRITE_SHIELD, DriftRGBA8FromColor(color), m);
				DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_RADIAL, color, DriftAffineMul(m, (DriftAffine){64, 0, 0, 64, 0, 0}), 0);
				
				DriftVec4 flash_color = (DriftVec4){{0.26f, 0.88f, 0.99f, 0.00f}};
				float flash_fade = fminf(1, (draw->tick - health->damage_timestamp)/(0.25f*DRIFT_TICK_HZ));
				flash_color = DriftVec4Mul(flash_color, 1 - flash_fade*flash_fade);
				DriftSpritePush(&sprites, DRIFT_SPRITE_SHIELD_FLASH, DriftRGBA8FromColor(flash_color), matrix_model);
			} else {
				uint period = 30, tick = draw->tick;
				
				float fade = 1 - (0.5f + 0.5f*cosf(tick))*(tick%period)/(float)period;
				DriftVec4 color = {{0, fade*fade*0.15f, fade*fade*0.30f, 0}};
				
				float flash_phase = (tick/period)*(float)(2*M_PI/DRIFT_PHI);
				DriftVec2 frot = {cosf(flash_phase), sinf(flash_phase)};
				DriftAffine m = DriftAffineMul(matrix_model, (DriftAffine){frot.x, frot.y, -frot.y, frot.x});
				DriftSpritePush(&sprites, DRIFT_SPRITE_SHIELD_FLASH, DriftRGBA8FromColor(color), m);
				DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_RADIAL, color, DriftAffineMul(matrix_model, (DriftAffine){64, 0, 0, 64, 0, 0}), 0);
			}
		}
		
		DriftArrayRangeCommit(draw->fg_sprites, sprites);
		DriftArrayRangeCommit(draw->lights, lights);
		
		DriftVec2 pos = DriftAffineOrigin(matrix_model);
		
		// Draw power beam
		DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(state, pos, draw->mem, 0);
		DriftRGBA8 beam_color = {0x00, 0x40, 0x40, 0x00};
		if(info.player_can_connect){
			DriftVec2 near_pos = {};
			float near_dist = INFINITY;

			DRIFT_ARRAY_FOREACH(info.nodes, node){
				float dist = DriftVec2Distance(pos, node->pos);
				if(node->player_can_connect && dist < near_dist){
					near_pos = node->pos;
					near_dist = dist;
				}
			}

			DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){
				.p0 = pos, .p1 = near_pos,
				.radii = {1.5f}, .color = beam_color,
			}));
		}
	}
}

bool DriftCheckSpawn(DriftUpdate* update, DriftVec2 pos, float terrain_dist){
	float dist = DriftTerrainSampleCoarse(update->state->terra, pos).dist;
	bool open_space = terrain_dist < dist && dist < 64;
	bool on_screen = DriftAABB2Test(DRIFT_AABB2_UNIT, DriftAffinePoint(update->prev_vp_matrix, pos));
	return !on_screen && open_space;
}

DriftNearbyNodesInfo DriftSystemPowerNodeNearby(DriftGameState* state, DriftVec2 pos, DriftMem* mem, float beam_radius){
	DriftNearbyNodesInfo info = {.pos = pos, .nodes = DRIFT_ARRAY_NEW(mem, 8, DriftNearbyNodeInfo)};
	uint connect_count = 0, too_close_count = 0, near_count = 0;
	
	DRIFT_COMPONENT_FOREACH(&state->power_nodes.c, i){
		DriftVec2 node_pos = state->power_nodes.position[i];
		float dist = DriftVec2Distance(pos, node_pos);
		if(dist < DRIFT_POWER_EDGE_MAX_LENGTH){
			DriftEntity e = state->power_nodes.entity[i];
			
			bool is_too_close = dist < DRIFT_POWER_EDGE_MIN_LENGTH && state->power_nodes.active[i];
			float t = DriftTerrainRaymarch(state->terra, node_pos, pos, beam_radius, 1);

			bool unblocked = t == 1;
			bool node_can_connect = !is_too_close && unblocked;
			connect_count += node_can_connect;
			too_close_count += is_too_close;
			near_count += unblocked;
			
			bool active = state->power_nodes.active[i];
			info.player_can_connect |= unblocked && active;
			DRIFT_ARRAY_PUSH(info.nodes, ((DriftNearbyNodeInfo){
				.e = e, .pos = node_pos, .player_can_connect = unblocked && active,
				.node_can_connect  = node_can_connect, .is_too_close = is_too_close, .blocked_at = t
			}));
		}
	}
	
	info.node_can_connect = connect_count > 0 && too_close_count == 0;
	info.node_can_reach = near_count > 0;
	info.too_close_count = too_close_count;
	return info;
}

#define DRIFT_JOIN(__join_var__, __joins__)for(DriftJoin __join_var__ = DriftJoinMake(__joins__)

static void flow_map_tick(DriftUpdate* update, uint fmap_idx){
	TracyCZoneN(ZONE_FLOW, "Flow Tick", true);
	DriftGameState* state = update->state;
	DriftComponentPowerNode* power_nodes = 	&state->power_nodes;
	DriftComponentFlowMap* fmap = state->flow_maps + fmap_idx;
	
	// Buffer the flow map nodes.
	TracyCZoneN(ZONE_BUFFER, "buffer", true);
	DriftFlowNode* copy = DriftAlloc(update->mem, power_nodes->c.table.row_count*sizeof(*copy));
	DRIFT_COMPONENT_FOREACH(&power_nodes->c, node_idx){
		DriftEntity e = power_nodes->entity[node_idx];
		// TODO can't components on secondary threads.
		uint flow_idx = DriftComponentFind(&fmap->c, e) ?: DriftComponentAdd(&fmap->c, e);
		copy[flow_idx] = fmap->flow[flow_idx];
	}
	TracyCZoneEnd(ZONE_BUFFER);
	
	// Propagate new links.
	TracyCZoneN(ZONE_PROPAGATE, "propagate", true);
	uint tick0 = update->tick - 1;
	DriftTablePowerNodeEdges* edges = &state->power_edges;
	for(uint i = 0, count = edges->t.row_count; i < count; i++){
		DriftPowerNodeEdge* edge = edges->edge + i;
		uint idx0 = DriftComponentFind(&fmap->c, edge->e0);
		uint idx1 = DriftComponentFind(&fmap->c, edge->e1);
		
		if(idx0 && idx1){
			float len = DriftVec2Distance(edge->p0, edge->p1);
			// Weight by distance and their current age.
			float d0 = copy[idx0].dist + 64*(tick0 - copy[idx0].mark);
			float d1 = copy[idx1].dist + 64*(tick0 - copy[idx1].mark);
			// Check both nodes connected by the link and propage values.
			if(d0 > d1 + len) copy[idx0] = (DriftFlowNode){.next = edge->e1, .mark = fmap->flow[idx1].mark, .dist = fmap->flow[idx1].dist + len};
			if(d1 > d0 + len) copy[idx1] = (DriftFlowNode){.next = edge->e0, .mark = fmap->flow[idx0].mark, .dist = fmap->flow[idx0].dist + len};
		}
	}
	TracyCZoneEnd(ZONE_PROPAGATE);
	
	// Write values back to the flow nodes.
	TracyCZoneN(ZONE_WRITE, "write", true);
	uint flow_idx, node_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &fmap->c, .variable = &flow_idx},
		{.component = &power_nodes->c, .variable = &node_idx, .optional = true},
		{},
	});
	while(DriftJoinNext(&join)){
		// A node is current if it's mark has been updated.
		// This can have false positives, but is consistent after a short delay.
		fmap->current[flow_idx] = copy[flow_idx].mark != fmap->flow[flow_idx].mark;
		// Zero out links to non-existant nodes.
		if(!node_idx) copy[flow_idx].next = (DriftEntity){0};
		// Copy back to the nodes.
		fmap->flow[flow_idx] = copy[flow_idx];
	}
	TracyCZoneEnd(ZONE_WRITE);
	
	if(fmap_idx == 0){
		// Set power nodes as active if flow0 is current.
		TracyCZoneN(ZONE_SYNC, "sync", true);
		DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
			{.component = &fmap->c, .variable = &flow_idx},
			{.component = &power_nodes->c, .variable = &node_idx},
			{},
		});
		while(DriftJoinNext(&join)) power_nodes->active[node_idx] = fmap->current[flow_idx];
		TracyCZoneEnd(ZONE_SYNC);
	}
	TracyCZoneEnd(ZONE_FLOW);
}

static void flow_map_job(tina_job* job){
	DriftUpdate* update = tina_job_get_description(job)->user_data;
	uint fmap_idx = tina_job_get_description(job)->user_idx;
	flow_map_tick(update, fmap_idx);
}

static void TickPower(DriftUpdate* update){
	flow_map_tick(update, 0);
	flow_map_tick(update, 1);
	
	DriftGameState* state = update->state;
	
	{ // Update power roots
		state->power_nodes.active[1] = true;
		state->flow_maps[0].flow[1].mark = update->tick;
	}
	
	{ // Update player path roots
		uint transform_idx = DriftComponentFind(&state->transforms.c, update->ctx->player);
		DriftVec2 player_pos = DriftAffineOrigin(state->transforms.matrix[transform_idx]);
		DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(state, player_pos, update->mem, 0);
		
		DriftComponentFlowMap* fmap = state->flow_maps + 1;
		DRIFT_ARRAY_FOREACH(info.nodes, node){
			if(node->blocked_at < 1) continue;
			
			uint fnode_idx = DriftComponentFind(&fmap->c, node->e);
			// if(fnode_idx == 0) fnode_idx = DriftComponentAdd(&fmap->c, node->e);
			DriftFlowNode* fnode = fmap->flow + fnode_idx;
			
			DriftVec2 p1 = state->power_nodes.position[DriftComponentFind(&state->power_nodes.c, node->e)];
			fnode->dist = DriftVec2Distance(player_pos, p1);
			fnode->mark = update->tick - 1;
			fnode->next = node->e;
			
			// DriftDebugSegment(state, player_pos, p1, 1, DRIFT_RGBA8_ORANGE);
		}
	}
}

static void DrawPower(DriftDraw* draw){
	DriftGameState* state = draw->state;
	DriftRGBA8 node_color = {0xC0, 0xC0, 0xC0, 0xFF}, beam_color = {0x00, 0x40, 0x40, 0x00};
	
	// TODO no culling?
	DriftPowerNodeEdge* edges = state->power_edges.edge;
	DriftSpriteFrame light_frame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL];
	
	DRIFT_COMPONENT_FOREACH(&state->power_nodes.c, idx){
		if(state->power_nodes.active[idx]){
			DriftVec2 pos = state->power_nodes.position[idx];
			DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
				.frame = light_frame, .color = (DriftVec4){{0.08f, 0.12f, 0.12f, 0.00f}}, .matrix = {128, 0, 0, 128, pos.x, pos.y},
			}));
		}
	}
	
	for(uint idx = 0; idx < state->power_edges.t.row_count; idx++){
		DriftPowerNodeEdge edge = edges[idx];
		DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){
			.p0 = DriftVec2FMA(DriftVec2Sub(edge.p0, DRIFT_VEC2_ONE), DriftNoiseR2(draw->frame + edge.e0.id), 2),
			.p1 = DriftVec2FMA(DriftVec2Sub(edge.p1, DRIFT_VEC2_ONE), DriftNoiseR2(draw->frame + edge.e1.id), 2),
			.radii = {1.5f}, .color = beam_color,
		}));
	}
}

void DriftDrawPowerMap(DriftDraw* draw, float scale){
	DriftGameState* state = draw->state;
	DriftRGBA8 node_color = {0xFF, 0x80, 0x00, 0xFF}, beam_color = {0x00, 0x40, 0x40, 0x00};
	
	// TODO no culling?
	DriftPowerNodeEdge* edges = state->power_edges.edge;
	
	for(uint i = 0; i < state->power_edges.t.row_count; i++){
		DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){.p0 = edges[i].p0, .p1 = edges[i].p1, .radii = {1.5f/scale}, .color = beam_color}));
	}
	
	DRIFT_COMPONENT_FOREACH(&state->power_nodes.c, i){
		if(state->power_nodes.active[i]){
			DriftVec2 pos = state->power_nodes.position[i];
			DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){
				.p0 = pos, .p1 = pos, .radii = {5/scale}, .color = node_color,
			}));
		}
	}
}

static void TickNavs(DriftUpdate* update){
	DriftGameState* state = update->state;
	
	DRIFT_VAR(navs, state->navs.data);
	
	uint nav_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&nav_idx, &state->navs.c},
		{&body_idx, &state->bodies.c},
		{},
	});
	
	while(DriftJoinNext(&join)){
		DriftComponentFlowMap* flow_map = state->flow_maps + navs[nav_idx].flow_map;
		DriftEntity next_e = navs[nav_idx].next_node;
		DriftVec2 pos = state->bodies.position[body_idx];
		
		uint pnode_idx = DriftComponentFind(&state->power_nodes.c, next_e);
		DriftVec2 next_pos = state->power_nodes.position[pnode_idx];
		DriftVec2 target_pos = navs[nav_idx].target_pos;
		
		if(pnode_idx == 0){
			// Target node does not exist. Try to find a new one.
			DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(state, pos, update->mem, 0);
			float min_dist = INFINITY;
			DRIFT_ARRAY_FOREACH(info.nodes, inode){
				float root_dist = flow_map->flow[DriftComponentFind(&flow_map->c, inode->e)].dist;
				if(inode->blocked_at == 1 && root_dist < min_dist){
					min_dist = root_dist;
					next_e = inode->e;
				}
			}
			
			pnode_idx = DriftComponentFind(&state->power_nodes.c, next_e);
			navs[nav_idx].next_node = next_e;
			target_pos = next_pos = state->power_nodes.position[pnode_idx];
		}
		
		{
			float radius = navs[nav_idx].radius, min = 0;
			float t = DriftTerrainRaymarch2(state->terra, pos, target_pos, 0.0f*radius, 2, &min);
			// DriftDebugSegment2(state, pos, target_pos, radius + min, radius - 1 + min, DRIFT_RGBA8_WHITE);
			target_pos = DriftVec2LerpConst(target_pos, next_pos, 0.5f*fmaxf(0, min - 1));
			
			if(t < 1) navs[nav_idx].next_node.id = 0;
		}
		
		if(DriftVec2Distance(target_pos, next_pos) == 0){
			uint fnode_idx = DriftComponentFind(&flow_map->c, next_e);
			navs[nav_idx].next_node = flow_map->flow[fnode_idx].next;
		}
		
		navs[nav_idx].target_pos = target_pos;
		// DriftDebugSegment(state, pos, next_pos, 1, DRIFT_RGBA8_RED);
		// DriftDebugSegment(state, pos, target_pos, 1, DRIFT_RGBA8_ORANGE);
	}
}

static void DrawSprites(DriftDraw* draw){
	DriftGameState* state = draw->state;
	uint transform_idx, sprite_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&sprite_idx, &state->sprites.c},
		{&transform_idx, &state->transforms.c},
		{},
	});
	
	DRIFT_VAR(datas, state->sprites.data);
	DRIFT_VAR(transforms, state->transforms.matrix);
	
	int i = 0;
	DriftSprite* sprites = DRIFT_ARRAY_RANGE(draw->fg_sprites, state->sprites.c.count);
	DriftLight* lights = DRIFT_ARRAY_RANGE(draw->lights, state->sprites.c.count);
	while(DriftJoinNext(&join)){
		DriftAffine m = transforms[transform_idx];
		DriftSpritePush(&sprites, datas[sprite_idx].frame, datas[sprite_idx].color, m);

		DriftLight light = datas[sprite_idx].light;
		light.matrix = DriftAffineMul(m, light.matrix);
		*(lights++) = light;
	}
	DriftArrayRangeCommit(draw->fg_sprites, sprites);
	DriftArrayRangeCommit(draw->lights, lights);
}

DriftEntity DriftDroneMake(DriftGameState* state, DriftVec2 pos){
	DriftEntity e = DriftMakeEntity(state);
	uint drone_idx = DriftComponentAdd(&state->drones.c, e);
	
	uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	state->bodies.position[body_idx] = pos;
	
	float radius = 7, mass = 2.0f;
	state->bodies.radius[body_idx] = radius;
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 1/(mass*0.5f*radius*radius);
	
	return e;
}

#define NEAR_COUNT 4
typedef struct {
	DriftEntity entity[NEAR_COUNT];
} Nears;

Nears NEARS[1000];

typedef struct {
	DriftEntity entity;
	DriftVec2 pos, rot;
	float dist;
} Entry;

static int drone_comp(const void* _a, const void* _b){
	const Entry* a = _a;
	const Entry* b = _b;
	return (a->dist == b->dist ? 0 : (a->dist < b->dist ? -1 : 1));
}

static void TickDrones(DriftUpdate* update){
	DriftGameState* state = update->state;
	float subtick_dt = update->tick_dt;
	
	static uint check_idx;
	DriftEntity check_e = state->drones.entity[check_idx];
	DriftVec2 check_pos = state->bodies.position[DriftComponentFind(&state->bodies.c, check_e)];
	
	float period = 2e9;
	float phase = 2*(float)M_PI/period*(update->ctx->tick_nanos % (u64)period), inc = (float)(2*M_PI/DRIFT_PHI);
	DriftVec2 rot = {cosf(phase), sinf(phase)}, rinc = {cosf(inc), sinf(inc)};
	
	uint drone_idx, transform_idx, body_idx, nav_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&drone_idx, &state->drones.c},
		{&body_idx, &state->bodies.c},
		{&transform_idx, &state->transforms.c},
		{&nav_idx, &state->navs.c, .optional = true},
		{},
	});
	
	while(DriftJoinNext(&join)){
		DriftAffine m = state->transforms.matrix[transform_idx];
		DriftAffine m_inv = DriftAffineInverse(m);
		
		DriftVec2 pos = state->bodies.position[body_idx];
		DriftVec2 target_pos = pos;
		
		// TODO Should drones always have a nav node?
		if(nav_idx == 0){
			uint nav_idx = DriftComponentAdd(&state->navs.c, join.entity);
			state->navs.data[nav_idx].flow_map = 1;
			state->navs.data[nav_idx].radius = 8;
		}
		
		// Fly towards the nav target if it has one.
		if(state->navs.data[nav_idx].next_node.id){
			target_pos = state->navs.data[nav_idx].target_pos;
			target_pos.y += 20;
		}
		
		Entry entries[NEAR_COUNT + 1];
		for(uint i = 0; i < NEAR_COUNT; i++){
			DriftEntity near_e = NEARS[drone_idx].entity[i];
			DriftVec2 near_pos = state->bodies.position[DriftComponentFind(&state->bodies.c, near_e)];
			entries[i] = (Entry){.entity = near_e, .pos = near_pos, .dist = DriftVec2Distance(pos, near_pos)};
		}
		entries[NEAR_COUNT] = (Entry){.entity = check_e, .pos = check_pos, .dist = DriftVec2Distance(pos, check_pos)};
		qsort(entries, NEAR_COUNT + 1, sizeof(*entries), drone_comp);
		
		uint cursor = 0;
		DriftEntity prev_e = join.entity;
		Nears foo = {};
		for(uint i = 0; i < NEAR_COUNT + 1; i++){
			if(entries[i].entity.id == prev_e.id || cursor >= NEAR_COUNT) continue;
			prev_e = entries[i].entity;
			
			foo.entity[cursor++] = entries[i].dist < 30 ? entries[i].entity : (DriftEntity){};
			DriftDebugRay(state, pos, DriftVec2Sub(entries[i].pos, pos), 0.3f, DRIFT_RGBA8_RED);
		}
		
		Nears* bar = NEARS + drone_idx;
		if(memcmp(&foo, bar, sizeof(foo))){
			DRIFT_LOG("What?");
			DriftBreakpoint();
			*bar = foo;
		}
		
		DRIFT_COMPONENT_FOREACH(&state->drones.c, check_idx){
			DriftEntity check_entity = state->drones.entity[check_idx];
			if(check_entity.id == join.entity.id) continue;
			DriftVec2 check_pos = state->bodies.position[DriftComponentFind(&state->bodies.c, check_entity)];
			
			DriftVec2 delta = DriftVec2Sub(pos, check_pos);
			float dist = DriftVec2Length(delta) + FLT_MIN;
			target_pos = DriftVec2FMA(target_pos, delta, fmaxf(0, 32/dist - 1));
		}
		
		// if(check_entity.id != join.entity.id){
		// }
		
		DriftVec2 target_delta = DriftVec2Sub(target_pos, pos);
		DriftVec2 target_velocity = DriftVec2Mul(target_delta, expf(-100*subtick_dt)/subtick_dt);
		target_velocity = DriftVec2Clamp(target_velocity, 0.75f*DRIFT_PLAYER_SPEED);
		state->bodies.velocity[body_idx] = DriftVec2LerpConst(state->bodies.velocity[body_idx], target_velocity, 500.0f*subtick_dt);
		
		DriftVec2 local_rot = DriftAffineDirection(m_inv, DriftVec2Normalize(target_delta));
		float desired_w = -(1 - expf(-10*subtick_dt))*atan2f(local_rot.x, local_rot.y)/subtick_dt;
		state->bodies.angular_velocity[body_idx] = DriftLerpConst(state->bodies.angular_velocity[body_idx], desired_w, 50*subtick_dt);
	}
	
	// if(update->tick % 30 == 0)
	check_idx++;
	if(check_idx > state->drones.c.count) check_idx = 1;
}

static void DrawDrones(DriftDraw* draw){
	DriftGameState* state = draw->state;
	
	DriftSpriteFrame frame_chasis = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_DRONE_CHASSIS];
	DriftSpriteFrame frame_hatch = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_DRONE_HATCH];
	DriftSpriteFrame frame_flood = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_LIGHT_FLOOD];
	DriftSpriteFrame frame_hemi = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_LIGHT_HEMI];
	DriftSpriteFrame frame_flame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_FLAME0 + draw->frame%_DRIFT_SPRITE_FLAME_COUNT];
	const DriftRGBA8 flame_color = {0xC0, 0xC0, 0xC0, 0x80};
	
	uint count = state->drones.c.count;
	DriftSprite* sprites = DRIFT_ARRAY_RANGE(draw->fg_sprites, 5*count);
	DriftLight* lights = DRIFT_ARRAY_RANGE(draw->lights, 3*count);
	
	uint drone_idx, transform_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&drone_idx, &state->drones.c},
		{&transform_idx, &state->transforms.c},
		{&body_idx, &state->bodies.c},
		{},
	});
	while(DriftJoinNext(&join)){
		DriftAffine m_r = state->transforms.matrix[transform_idx];
		// m_r = DriftAffineMul(m_r, (DriftAffine){5, 0, 0, 5, 0, 0});
		DriftAffine m_l = DriftAffineMul(m_r, (DriftAffine){-1, 0, 0, 1, 0, 0});
		
		DriftVec2 v = state->bodies.velocity[body_idx];
		float w = state->bodies.angular_velocity[body_idx]/10;
		float vn = DriftVec2Dot((DriftVec2){m_r.c, m_r.d}, v)/200;
		
		DriftAffine flame_l = {-0.25f, 0, 0, DriftSaturate(vn - w), -3, -7};
		*sprites++ = (DriftSprite){.frame = frame_flame, .color = flame_color, .matrix = DriftAffineMul(m_r, flame_l)};
		DriftAffine flame_r = {+0.25f, 0, 0, DriftSaturate(vn + w), +3, -7};
		*sprites++ = (DriftSprite){.frame = frame_flame, .color = flame_color, .matrix = DriftAffineMul(m_r, flame_r)};
		
		*sprites++ = (DriftSprite){.frame = frame_chasis, .color = DRIFT_RGBA8_WHITE, .matrix = m_r};
		
		float hatch_open = DriftSaturate(sinf(draw->tick/8.0f));
		hatch_open *= 0*hatch_open;
		DriftVec2 rot = {cosf(-hatch_open), sinf(-hatch_open)};
		DriftAffine m_hatch = DriftAffineTRS(DriftVec2Mul((DriftVec2){4, -2}, hatch_open), 0.8f*hatch_open, DRIFT_VEC2_ONE);
		DriftAffine m_hatchr = DriftAffineMul(m_r, m_hatch);
		DriftAffine m_hatchl = DriftAffineMul(m_l, m_hatch);
		*sprites++ = (DriftSprite){.frame = frame_hatch, .color = DRIFT_RGBA8_WHITE, .matrix = m_hatchr};
		*sprites++ = (DriftSprite){.frame = frame_hatch, .color = DRIFT_RGBA8_WHITE, .matrix = m_hatchl};
		
		DriftVec4 flame_glow = DriftVec4Mul((DriftVec4){{1.14f, 0.78f, 0.11f, 0}}, 2*DriftSaturate(vn));
		*lights++ = (DriftLight){.frame = frame_hemi, .color = flame_glow, .matrix = DriftAffineMul(m_r, (DriftAffine){30, 0, 0, -20, 0, -8})};
		*lights++ = (DriftLight){.frame = frame_flood, .color = {{1, 1, 1, 0}}, .matrix = DriftAffineMul(m_hatchl, (DriftAffine){40, 0, 0, 48, +4, 8})};
		*lights++ = (DriftLight){.frame = frame_flood, .color = {{1, 1, 1, 0}}, .matrix = DriftAffineMul(m_hatchr, (DriftAffine){40, 0, 0, 48, +4, 8})};
	}
	DriftArrayRangeCommit(draw->fg_sprites, sprites);
	DriftArrayRangeCommit(draw->lights , lights);
}

typedef struct {
	float alpha;
	DriftVec2 point, normal;
} RayHit;

static inline RayHit CircleSegmentQuery(DriftVec2 center, float r1, DriftVec2 a, DriftVec2 b, float r2){
	DriftVec2 da = DriftVec2Sub(a, center);
	DriftVec2 db = DriftVec2Sub(b, center);
	float rsum = r1 + r2;
	
	float qa = DriftVec2Dot(da, da) - 2*DriftVec2Dot(da, db) + DriftVec2Dot(db, db);
	float qb = DriftVec2Dot(da, db) - DriftVec2Dot(da, da);
	float det = qb*qb - qa*(DriftVec2Dot(da, da) - rsum*rsum);
	
	if(det >= 0.0f){
		float t = -(qb + sqrtf(det))/qa;
		if(0 <= t && t <= 1){
			DriftVec2 n = DriftVec2Normalize(DriftVec2Lerp(da, db, t));
			return (RayHit){.alpha = t, .point = DriftVec2Sub(DriftVec2Lerp(a, b, t), DriftVec2Mul(n, r2)), .normal = n};
		}
	}
	
	return (RayHit){.alpha = 1};
}

void DriftHealthApplyDamage(DriftUpdate* update, DriftEntity entity, float amount){
	DriftGameState* state = update->state;
	
	uint health_idx = DriftComponentFind(&state->health.c, entity);
	if(health_idx){
		DriftHealth* health = state->health.data + health_idx;
		if(update->tick - health->damage_timestamp > health->damage_timeout){
			health->damage_timestamp = update->tick;
			health->value -= amount;
		}
		
		uint enemy_idx = DriftComponentFind(&state->enemies.c, entity);
		if(enemy_idx) state->enemies.aggro_ticks[enemy_idx] = 10*DRIFT_TICK_HZ;
		
		if(health->value <= 0){
			health->value = 0;
			
			uint body_idx = DriftComponentFind(&state->bodies.c, entity);
			DriftVec2 pos = state->bodies.position[body_idx];
			DriftVec2 vel = state->bodies.velocity[body_idx];
			if(health->drop) DriftItemMake(update->state, health->drop, pos, vel);
			
			DriftDestroyEntity(update, entity);
			DriftAudioPlaySample(update->audio, DRIFT_SFX_EXPLODE, 1, 0, 1, false);
			MakeBlast(update, pos);
		} else {
			DriftAudioPlaySample(update->audio, DRIFT_SFX_BULLET_HIT, 1, 0, 1, false);
		}
	}
}

void FireProjectile(DriftUpdate* update, DriftVec2 pos, DriftVec2 vel){
	DriftGameState* state = update->state;
	DriftEntity e = DriftMakeEntity(state);
	uint bullet_idx = DriftComponentAdd(&update->state->projectiles.c, e);
	state->projectiles.origin[bullet_idx] = pos;
	state->projectiles.velocity[bullet_idx] = vel;
	state->projectiles.tick0[bullet_idx] = update->tick;
	state->projectiles.timeout[bullet_idx] = 2; // TODO should be in ticks?
	
	float pitch = expf(0.2f*(2*(float)rand()/(float)RAND_MAX - 1));
	DriftAudioPlaySample(update->audio, DRIFT_SFX_BULLET_FIRE, 0.5f, 0, pitch, false);
}

static void TickProjectiles(DriftUpdate* update){
	DriftGameState* state = update->state;
	
	DRIFT_COMPONENT_FOREACH(&state->projectiles.c, i){
		uint age = update->tick - state->projectiles.tick0[i];
		DriftVec2 origin = state->projectiles.origin[i], velocity = state->projectiles.velocity[i];
		DriftVec2 p0 = DriftVec2FMA(origin, velocity, (age + 0)/DRIFT_TICK_HZ);
		DriftVec2 p1 = DriftVec2FMA(origin, velocity, (age + 1)/DRIFT_TICK_HZ);
		
		float ray_t = DriftTerrainRaymarch(state->terra, p0, p1, 0, 1);
		DriftEntity hit_entity = {};
		
		DRIFT_COMPONENT_FOREACH(&state->bodies.c, body_idx){
			if(DriftCollisionFilter(DRIFT_COLLISION_PLAYER_BULLET, state->bodies.collision_type[body_idx])){
				DriftEntity entity = state->bodies.entity[body_idx];
				
				RayHit hit = CircleSegmentQuery(state->bodies.position[body_idx], state->bodies.radius[body_idx], p0, p1, 0);
				if(hit.alpha < ray_t){
					ray_t = hit.alpha;
					
					hit_entity = entity;
					// DriftVec2* body_v = state->bodies.velocity + body_idx;
					// *body_v = DriftVec2FMA(*body_v, velocity, 0.5e-1f);
				}
			}
		}
		
		if(ray_t < 1 || age/DRIFT_TICK_HZ > state->projectiles.timeout[i]){
			DriftHealthApplyDamage(update, hit_entity, 25); // TODO store on projectile?
			DriftDebugCircle(state, DriftVec2Lerp(p0, p1, ray_t), 8, DRIFT_RGBA8_RED);
			// TODO sound?
			DriftDestroyEntity(update, state->projectiles.entity[i]);
		}
	}
}

static void DrawProjectiles(DriftDraw* draw){
	DriftGameState* state = draw->state;
	float t0 = draw->dt_since_tick - draw->dt/2;
	float t1 = draw->dt_since_tick + draw->dt/2;
	
	DRIFT_COMPONENT_FOREACH(&state->projectiles.c, i){
		float t = (draw->tick - state->projectiles.tick0[i])/DRIFT_TICK_HZ;
		float timeout = state->projectiles.timeout[i];
		DriftVec2 origin = state->projectiles.origin[i], velocity = state->projectiles.velocity[i];
		DriftVec2 p0 = DriftVec2FMA(origin, velocity, fmaxf(0, t + t0));
		DriftVec2 p1 = DriftVec2FMA(origin, velocity, fmaxf(0, t + t1));
		DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){.p0 = p0, .p1 = p1, .radii[0] = 2, .color = {0xFF, 0xB0, 0x40, 0xFF}}));

		DriftVec4 color = (DriftVec4){{0.46f, 0.27f, 0.12f, 0.00f}};
		DRIFT_ARRAY_PUSH(draw->lights, DriftLightMake(false, DRIFT_SPRITE_LIGHT_RADIAL, color, (DriftAffine){64, 0, 0, 64, p0.x, p0.y}, 0));
	}
}

// TODO
typedef struct {
	DriftTable t;
	DriftVec2* position;
	uint* tick0;
} DriftTableBlast;
static DriftTableBlast blasts;

void MakeBlast(DriftUpdate* update, DriftVec2 position){
	uint idx = DriftTablePushRow(&blasts.t);
	blasts.position[idx] = position;
	blasts.tick0[idx] = update->tick;
}

static void DrawBlasts(DriftDraw* draw){
	DriftGameState* state = draw->state;
	uint tick = draw->tick;
	
	uint i = 0;
	while(i < blasts.t.row_count){
		uint frame = (tick - blasts.tick0[i])/2;
		if(frame < 8){
			DriftVec2 pos = blasts.position[i];
			DriftAffine transform = {3, 0, 0, 3, pos.x, pos.y};
			DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteMake(DRIFT_SPRITE_DOGSPLOSION0 + frame, DRIFT_RGBA8_WHITE, transform));
			
			DriftVec4 color = DriftVec4Mul((DriftVec4){{20.00f, 17.85f, 12.24f, 0.00f}}, 1 - frame/8.0f);
			DRIFT_ARRAY_PUSH(draw->lights, DriftLightMake(true, DRIFT_SPRITE_LIGHT_RADIAL, color, (DriftAffine){350, 0, 0, 350, pos.x, pos.y}, 8));
			
			i++;
		} else {
			DriftTableCopyRow(&blasts.t, i, --blasts.t.row_count);
		}
	}
}

void DriftSystemsUpdate(DriftUpdate* update){
	static bool needs_init = true;
	if(needs_init){
		needs_init = false;
		
		DriftTableInit(&blasts.t, (DriftTableDesc){
			.mem = DriftSystemMem, .name = "Blasts",
			.columns.arr = {
				DRIFT_DEFINE_COLUMN(blasts.position),
				DRIFT_DEFINE_COLUMN(blasts.tick0),
			},
		});
	}
	
	UpdatePlayer(update);
}

#define RUN_FUNC(_func_, _arg_) {TracyCZoneN(ZONE, #_func_, true); _func_(_arg_); TracyCZoneEnd(ZONE);}

void DriftSystemsTick(DriftUpdate* update){
	RUN_FUNC(TickNavs, update);
	RUN_FUNC(TickPlayer, update);
	RUN_FUNC(TickDrones, update);
	RUN_FUNC(TickPower, update);
	RUN_FUNC(TickProjectiles, update);
	RUN_FUNC(DriftTickItemSpawns, update);
	RUN_FUNC(DriftTickEnemies, update);
}

void DriftSystemsDraw(DriftDraw* draw){
	typedef struct {
		u8 fx, fy, fdist, idist;
	} PlacementNoise;
	static const PlacementNoise* pixels;
	static uint rando[64*64];
	
	if(pixels == NULL){
		pixels = DriftAssetLoad(DriftSystemMem, "bin/placement64.bin").ptr;
		srand(65418);
		for(uint i = 0; i < 64*64; i++) rando[i] = rand();
	}
	
	DriftGameState* state = draw->state;
	DriftAffine vp_inv = draw->vp_inverse;
	float hw = fabsf(vp_inv.a) + fabsf(vp_inv.c) + 64, hh = fabsf(vp_inv.b) + fabsf(vp_inv.d) + 64;
	DriftAABB2 bounds = {vp_inv.x - hw, vp_inv.y - hh, vp_inv.x + hw, vp_inv.y + hh};
	
	DriftAffine v_mat = draw->v_matrix;
	float v_scale = hypotf(v_mat.a, v_mat.b) + hypotf(v_mat.c, v_mat.d);
	
	TracyCZoneN(ZONE_BIOME, "Biome", true);
	float q = 16;
	for(int y = (int)floorf(bounds.b/q); y*q < bounds.t; y++){
		if(draw->ctx->debug.hide_terrain_decals) break;
		if(v_scale < 1.5f) break;
		for(int x = (int)floorf(bounds.l/q); x*q < bounds.r; x++){
			uint i = (x & 63) + (y & 63)*64;
			float pdist = pixels[i].idist + pixels[i].fdist/255.0f;
			if(pdist == 0) continue;
			
			DriftVec2 pos = DriftVec2FMA((DriftVec2){x*q, y*q}, (DriftVec2){pixels[i].fx, pixels[i].fy}, q/255);
			// DriftDebugCircle(state, pos, 5, DRIFT_RGBA8_RED);
			
			DriftTerrainSampleInfo info = DriftTerrainSampleFine(state->terra, pos);
			// if(0 < info.dist && info.dist < 50) DriftDebugCircle(state, pos, info.dist, (DriftRGBA8){64, 0, 0, 64});
			
			if(info.dist > 0){
				uint rnd = rando[i];
				if(info.dist < 16){
					pos = DriftVec2FMA(pos, info.grad, -info.dist);
					DriftVec2 g = DriftVec2Mul(info.grad, 0.75f + info.dist*0.5f/16);
					uint light[] = {
						DRIFT_SPRITE_LARGE_BUSHY_PLANTS1, DRIFT_SPRITE_LARGE_BUSHY_PLANTS2, DRIFT_SPRITE_LARGE_BUSHY_PLANTS3,
						DRIFT_SPRITE_LARGE_BUSHY_PLANTS4, DRIFT_SPRITE_LARGE_BUSHY_PLANTS5,
					};
					uint radio[] = {
						DRIFT_SPRITE_MEDIUM_CRYSTALS1, DRIFT_SPRITE_MEDIUM_CRYSTALS2, DRIFT_SPRITE_MEDIUM_CRYSTALS3,
						DRIFT_SPRITE_MEDIUM_FUNGI1, DRIFT_SPRITE_MEDIUM_FUNGI2, DRIFT_SPRITE_MEDIUM_FUNGI3,
						DRIFT_SPRITE_MEDIUM_FUNGI4, DRIFT_SPRITE_MEDIUM_FUNGI5, DRIFT_SPRITE_MEDIUM_FUNGI6,
					};
					uint cryo[] = {DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS1, DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS2, DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS3};
					uint dark[] = {
						DRIFT_SPRITE_LARGE_BURNED_PLANTS1, DRIFT_SPRITE_LARGE_BURNED_PLANTS2, DRIFT_SPRITE_LARGE_BURNED_PLANTS3,
						DRIFT_SPRITE_LARGE_BURNED_PLANTS4, DRIFT_SPRITE_LARGE_BURNED_PLANTS5
					};
					
					uint* biome_base[] = {light, radio, cryo, dark, light};
					uint biome_len[] = {sizeof(light)/sizeof(*light), sizeof(radio)/sizeof(*radio), sizeof(cryo)/sizeof(*cryo), 5, 1};
					
					uint biome = DriftTerrainSampleBiome(state->terra, pos).idx;
					uint frame = biome_base[biome][rnd % biome_len[biome]];
					DriftSprite sprite = {
						.matrix = {g.y, -g.x, g.x, g.y, pos.x, pos.y},
						.frame = DRIFT_SPRITE_FRAMES[frame], .color = DRIFT_RGBA8_WHITE,
					};
					
					if(frame == DRIFT_SPRITE_LARGE_BUSHY_PLANTS3 || frame == DRIFT_SPRITE_LARGE_BUSHY_PLANTS5){
						DriftLight light = {.frame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = {{0.01f, 0.18f, 0.22f, 0.00f}}, .matrix = {80, 0, 0, 80, pos.x, pos.y}};
						DRIFT_ARRAY_PUSH(draw->lights, light);
					} else if(frame == DRIFT_SPRITE_LARGE_BUSHY_PLANTS4){
						DriftLight light = {.frame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = {{0.12f, 0.10f, 0.01f, 0.00f}}, .matrix = {80, 0, 0, 80, pos.x, pos.y}};
						DRIFT_ARRAY_PUSH(draw->lights, light);
					} else if(DRIFT_SPRITE_MEDIUM_CRYSTALS1 <= frame && frame <= DRIFT_SPRITE_MEDIUM_CRYSTALS3){
						sprite.shiny = 0.6f;
						DriftLight light = {.frame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = {{0.34f, 0.40f, 0.00f, 0.00f}}, .matrix = {45, 0, 0, 45, pos.x, pos.y}};
						DRIFT_ARRAY_PUSH(draw->lights, light);
					} else if(DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS1 <= frame && frame <= DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS3){
						sprite.shiny = 0.6f;
						DriftLight light = {.frame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = {{0.10f, 0.14f, 0.19f, 0.00f}}, .matrix = {30, 0, 0, 30, pos.x, pos.y}};
						DRIFT_ARRAY_PUSH(draw->lights, light);
					}
					
					DRIFT_ARRAY_PUSH(draw->fg_sprites, sprite);
				} else if(info.dist > 48 && pdist > 2) {
					static const uint light0[] = {
						DRIFT_SPRITE_SMALL_MOSS_PATCHES1, DRIFT_SPRITE_SMALL_MOSS_PATCHES2, DRIFT_SPRITE_SMALL_MOSS_PATCHES3, DRIFT_SPRITE_SMALL_MOSS_PATCHES4,
						DRIFT_SPRITE_SMALL_ROCKS1, DRIFT_SPRITE_SMALL_ROCKS2, DRIFT_SPRITE_SMALL_ROCKS3,
					};
					static const uint light1[] = {
						DRIFT_SPRITE_MEDIUM_MOSS_PATCHES1, DRIFT_SPRITE_MEDIUM_MOSS_PATCHES2, DRIFT_SPRITE_MEDIUM_MOSS_PATCHES3, DRIFT_SPRITE_MEDIUM_MOSS_PATCHES4,
						DRIFT_SPRITE_MEDIUM_ROCKS1, DRIFT_SPRITE_MEDIUM_ROCKS2,
					};
					static const uint light2[] = {DRIFT_SPRITE_LARGE_MOSS_PATCHES1, DRIFT_SPRITE_LARGE_MOSS_PATCHES2, DRIFT_SPRITE_LARGE_ROCKS1, DRIFT_SPRITE_LARGE_ROCKS2};
					
					static const uint* light_base[] = {light0, light1, light2};
					static const uint light_div[] = {7, 6, 4};
					
					static const uint radio0[] = {
						DRIFT_SPRITE_SMALL_CRYSTALS1, DRIFT_SPRITE_SMALL_CRYSTALS2, DRIFT_SPRITE_SMALL_CRYSTALS3,
					};
					static const uint radio1[] = {
						DRIFT_SPRITE_MEDIUM_FUNGI1, DRIFT_SPRITE_MEDIUM_FUNGI2, DRIFT_SPRITE_MEDIUM_FUNGI3,
						DRIFT_SPRITE_MEDIUM_FUNGI4, DRIFT_SPRITE_MEDIUM_FUNGI5, DRIFT_SPRITE_MEDIUM_FUNGI6,
					};
					static const uint radio2[] = {DRIFT_SPRITE_LARGE_FUNGI1, DRIFT_SPRITE_LARGE_FUNGI2, DRIFT_SPRITE_LARGE_SLIME_PATCHES1, DRIFT_SPRITE_LARGE_SLIME_PATCHES2};

					static const uint* radio_base[] = {radio0, radio1, radio2};
					static const uint radio_div[] = {3, 6, 4};
					
					static const uint cryo0[] = {
						DRIFT_SPRITE_SMALL_ICE_CHUNKS1, DRIFT_SPRITE_SMALL_ICE_CHUNKS2, DRIFT_SPRITE_SMALL_ICE_CHUNKS3,
						DRIFT_SPRITE_SMALL_ROCKS_CRYO1, DRIFT_SPRITE_SMALL_ROCKS_CRYO2,
						DRIFT_SPRITE_CRYO_SMALL_ROCK1, DRIFT_SPRITE_CRYO_SMALL_ROCK2, DRIFT_SPRITE_CRYO_SMALL_ROCK3,
						DRIFT_SPRITE_CRYO_SMALL_ROCK4, DRIFT_SPRITE_CRYO_SMALL_ROCK5, DRIFT_SPRITE_CRYO_SMALL_ROCK6,
						DRIFT_SPRITE_CRYO_SMALL_ROCK7, DRIFT_SPRITE_CRYO_SMALL_ROCK8, DRIFT_SPRITE_CRYO_SMALL_ROCK9,
						DRIFT_SPRITE_CRYO_SMALL_ROCK10, DRIFT_SPRITE_CRYO_SMALL_ROCK11, DRIFT_SPRITE_CRYO_SMALL_ROCK12,
						DRIFT_SPRITE_CRYO_SMALL_ROCK13, DRIFT_SPRITE_CRYO_SMALL_ROCK14, DRIFT_SPRITE_CRYO_SMALL_ROCK15,
					};
					static const uint cryo1[] = {DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS1, DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS2, DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS3};
					static const uint cryo2[] = {
						DRIFT_SPRITE_LARGE_FOSSILS1, DRIFT_SPRITE_LARGE_FOSSILS2, DRIFT_SPRITE_LARGE_FOSSILS3, DRIFT_SPRITE_LARGE_FOSSILS4,
					};
					
					static const uint* cryo_base[] = {cryo0, cryo1, cryo2};
					static const uint cryo_div[] = {20, 3, 4};
					
					static const uint dark0[] = {DRIFT_SPRITE_SMALL_DARK_ROCKS1, DRIFT_SPRITE_SMALL_DARK_ROCKS2};
					static const uint dark1[] = {
						DRIFT_SPRITE_MEDIUM_DARK_ROCKS1, DRIFT_SPRITE_MEDIUM_DARK_ROCKS2,
						DRIFT_SPRITE_SMALL_BURNED_PLANTS1, DRIFT_SPRITE_SMALL_BURNED_PLANTS2,
						DRIFT_SPRITE_SMALL_BURNED_PLANTS3, DRIFT_SPRITE_SMALL_BURNED_PLANTS4, DRIFT_SPRITE_SMALL_BURNED_PLANTS5
					};
					static const uint* dark_base[] = {dark0, dark1, dark1};
					static const uint dark_div[] = {2, 7, 7};
					
					// static const uint space0[] = {DRIFT_SPRITE_TMP_STAR};
					// static const uint* space_base[] = {space0, space0, space0};
					// static const uint space_div[] = {1, 1, 1};
					
					static const uint** base[] = {light_base, radio_base, cryo_base, dark_base};
					static const uint* div[] = {light_div, radio_div, cryo_div, dark_div};
					
					uint asset_size = (uint)DriftClamp((pdist - 2)/1.5f, 0, 2.99f);
					uint biome = DriftTerrainSampleBiome(state->terra, pos).idx;
					if(biome < 4){
						uint frame = base[biome][asset_size][rnd%div[biome][asset_size]];
						DriftVec2 rot = {cosf(rnd*(float)(2*M_PI/RAND_MAX)), sinf(rnd*(float)(2*M_PI/RAND_MAX))};
						DriftSprite sprite = {
							.matrix = {rot.x, rot.y, -rot.y, rot.x, pos.x, pos.y}, .z = 1,
							.frame = DRIFT_SPRITE_FRAMES[frame], .color = DRIFT_RGBA8_WHITE,
						};
						DRIFT_ARRAY_PUSH(draw->bg_sprites, sprite);
					}
				}
			}
		}
	}
	TracyCZoneEnd(ZONE_BIOME);
	
	{ // TODO temp crashed ship
		DriftAffine m = DriftAffineTRS((DriftVec2)DRIFT_HOME_POSITION, -0.5, DRIFT_VEC2_ONE);
		DRIFT_ARRAY_PUSH(draw->bg_sprites, ((DriftSprite){
			.frame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_CRASHED_SHIP], .color = DRIFT_RGBA8_WHITE,
			.matrix = m, .shiny = 0.6f, .z = 0.7f,
		}));
		
		DRIFT_ARRAY_PUSH(draw->bg_sprites, ((DriftSprite){
			.frame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_LARGE_ROCKS1], .color = DRIFT_RGBA8_WHITE,
			.matrix = {1, 0, 0, 1, DRIFT_HOME_POSITION.x + 64, DRIFT_HOME_POSITION.y + 16}, .z = 0.5f,
		}));
		
		DRIFT_ARRAY_PUSH(draw->bg_sprites, ((DriftSprite){
			.frame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_LARGE_ROCKS2], .color = DRIFT_RGBA8_WHITE,
			.matrix = {1, 0, 0, 1, DRIFT_HOME_POSITION.x + 87, DRIFT_HOME_POSITION.y - 19}, .z = 0.5f,
		}));
		
		DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
			.frame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = (DriftVec4){{1.77f, 1.71f, 1.47f, 1.81f}},
			.matrix = DriftAffineMul(m, (DriftAffine){800, 0, 0, 800, 0, 48}),
			.shadow_caster = true, .radius = 8,
		}));
		
		DRIFT_ARRAY_PUSH(draw->bg_sprites, ((DriftSprite){
			.frame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_PRODUCTION_MODULE], .color = DRIFT_RGBA8_WHITE,
			.matrix = {1, 0, 0, 1, DRIFT_FACTORY_POSITION.x, DRIFT_FACTORY_POSITION.y}, .z = 0.7f,
		}));
	}
	
	RUN_FUNC(DrawPower, draw);
	RUN_FUNC(DrawProjectiles, draw);
	RUN_FUNC(DrawSprites, draw);
	RUN_FUNC(DriftDrawItems, draw);
	RUN_FUNC(DrawDrones, draw);
	RUN_FUNC(DriftDrawEnemies, draw);
	RUN_FUNC(DrawPlayer, draw);
	RUN_FUNC(DrawBlasts, draw);
}

void DriftSystemsInit(DriftGameState* state){
	component_init(state, &state->transforms.c, "@Transform", (DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->transforms.entity),
		DRIFT_DEFINE_COLUMN(state->transforms.matrix),
	}, 1024);
	state->transforms.matrix[0] = DRIFT_AFFINE_IDENTITY;
	
	component_init(state, &state->sprites.c, "@Sprite", (DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->sprites.entity),
		DRIFT_DEFINE_COLUMN(state->sprites.data),
	}, 1024);

	component_init(state, &state->bodies.c, "@RigidBody", (DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->bodies.entity),
		DRIFT_DEFINE_COLUMN(state->bodies.position),
		DRIFT_DEFINE_COLUMN(state->bodies.velocity),
		DRIFT_DEFINE_COLUMN(state->bodies.rotation),
		DRIFT_DEFINE_COLUMN(state->bodies.angular_velocity),
		DRIFT_DEFINE_COLUMN(state->bodies.mass_inv),
		DRIFT_DEFINE_COLUMN(state->bodies.moment_inv),
		DRIFT_DEFINE_COLUMN(state->bodies.offset),
		DRIFT_DEFINE_COLUMN(state->bodies.radius),
		DRIFT_DEFINE_COLUMN(state->bodies.collision_type),
	}, 1024);
	state->bodies.rotation[0] = (DriftVec2){1, 0};
	
	DriftTableInit(&state->rtree.table, (DriftTableDesc){
		.name = "DriftRTree", .mem = DriftSystemMem,
		.min_row_capacity = 256,
		.columns = {
			DRIFT_DEFINE_COLUMN(state->rtree.n_leaf),
			DRIFT_DEFINE_COLUMN(state->rtree.n_count),
			DRIFT_DEFINE_COLUMN(state->rtree.n_bound),
			DRIFT_DEFINE_COLUMN(state->rtree.n_child),
			DRIFT_DEFINE_COLUMN(state->rtree.pool_arr),
		},
	});
	state->rtree.root = DriftTablePushRow(&state->rtree.table);
	state->rtree.n_leaf[state->rtree.root] = true;

	component_init(state, &state->players.c, "@Player", (DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->players.entity),
		DRIFT_DEFINE_COLUMN(state->players.data),
	}, 0);

	component_init(state, &state->drones.c, "@Drone", (DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->drones.entity),
		DRIFT_DEFINE_COLUMN(state->drones.data),
	}, 0);

	// component_init(state, &state->ore_deposits.c, "@OreDeposit", (DriftColumnSet){
	// 	{DRIFT_DEFINE_COLUMN(state->ore_deposits.entity)},
	// }, 256);

	component_init(state, &state->items.c, "@Items", (DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->items.entity),
		DRIFT_DEFINE_COLUMN(state->items.type),
	}, 1024);

	component_init(state, &state->scan.c, "@Scan", (DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->scan.entity),
		DRIFT_DEFINE_COLUMN(state->scan.type),
	}, 1024);

	component_init(state, &state->power_nodes.c, "@PowerNode", (DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->power_nodes.entity),
		DRIFT_DEFINE_COLUMN(state->power_nodes.position),
		DRIFT_DEFINE_COLUMN(state->power_nodes.active),
	}, 0);
	
	DriftTableInit(&state->power_edges.t, (DriftTableDesc){
		.name = "PowerNodeEdges", .mem = DriftSystemMem,
		.columns.arr = {
			DRIFT_DEFINE_COLUMN(state->power_edges.edge),
		},
	});
	DRIFT_ARRAY_PUSH(state->tables, &state->power_edges.t);
	
	static const char* FLOW_NAMES[] = {"@FlowHome", "@FlowPlayer"};
	for(uint i = 0; i < DRIFT_FLOW_MAP_COUNT; i++){
		component_init(state, &state->flow_maps[i].c, FLOW_NAMES[i], (DriftColumnSet){
			DRIFT_DEFINE_COLUMN(state->flow_maps[i].entity),
			DRIFT_DEFINE_COLUMN(state->flow_maps[i].flow),
			DRIFT_DEFINE_COLUMN(state->flow_maps[i].current),
		}, 0);
		state->flow_maps[i].flow[0].dist = INFINITY;
	}	
	
	component_init(state, &state->navs.c, "@Nav", (DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->navs.entity),
		DRIFT_DEFINE_COLUMN(state->navs.data),
	}, 0);
	
	component_init(state, &state->projectiles.c, "@bullets", (DriftColumnSet){{
		DRIFT_DEFINE_COLUMN(state->projectiles.entity),
		DRIFT_DEFINE_COLUMN(state->projectiles.origin),
		DRIFT_DEFINE_COLUMN(state->projectiles.velocity),
		DRIFT_DEFINE_COLUMN(state->projectiles.tick0),
		DRIFT_DEFINE_COLUMN(state->projectiles.timeout),
	}}, 0);
	
	component_init(state, &state->health.c, "@health", (DriftColumnSet){{
		DRIFT_DEFINE_COLUMN(state->health.entity),
		DRIFT_DEFINE_COLUMN(state->health.data),
	}}, 0);
	
	component_init(state, &state->bug_nav.c, "@bug_nav", (DriftColumnSet){{
		DRIFT_DEFINE_COLUMN(state->bug_nav.entity),
		DRIFT_DEFINE_COLUMN(state->bug_nav.speed),
		DRIFT_DEFINE_COLUMN(state->bug_nav.accel),
		DRIFT_DEFINE_COLUMN(state->bug_nav.forward_bias),
	}}, 0);
	
	component_init(state, &state->enemies.c, "@enemies", (DriftColumnSet){{
		DRIFT_DEFINE_COLUMN(state->enemies.entity),
		DRIFT_DEFINE_COLUMN(state->enemies.type),
		DRIFT_DEFINE_COLUMN(state->enemies.aggro_ticks),
	}}, 0);
}
