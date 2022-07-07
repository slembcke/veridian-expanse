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

DriftVec3 DriftRCS(DriftVec3* matrix, DriftVec3* inverse, DriftVec3 desired, float solution[], uint n){
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

static inline const DriftTool* player_tool(DriftPlayerData* player){
	return DRIFT_TOOLS + player->quickslots[player->quickslot_idx];
}

static void DriftAnimStateUpdate(DriftAnimState* state, float dt){
	state->value = DriftLerpConst(state->value, state->target, dt*state->rate);
}

static const DriftVec2 nacelle_in = {12, -11}, nacelle_out = {28, -14};

static DriftEntity TempPlayerInit(DriftGameState* state, DriftEntity e){
	uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	uint player_idx = DriftComponentAdd(&state->players.c, e);
	DriftPlayerData* player = state->players.data + player_idx;
	player->local = true;
	player->power_reserve = 300;
	player->power_capacity = 300;
	
	uint health_idx = DriftComponentAdd(&state->health.c, e);
	state->health.data[health_idx] = (DriftHealth){.value = 100, .maximum = 100};
	
	float radius = DRIFT_PLAYER_SIZE, mass = 10;
	state->bodies.position[body_idx] = (DriftVec2){256, 0};
	state->bodies.radius[body_idx] = radius;
	state->bodies.collision_type[body_idx] = DRIFT_COLLISION_TYPE_PLAYER,
	
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 1/(mass*0.5f*radius*radius);
	
	return e;
}

static void DestroyPlayer(DriftComponent* component, uint idx){
	DriftComponentPlayer* players = (DriftComponentPlayer*)component;
	DriftPlayerData* player = players->data + idx;
	// DriftSamplerSetParams(player->thruster_sampler, 0, 0, 1, false);
}

static void UpdatePlayer(DriftUpdate* update){
	DriftGameState* state = update->state;
	float dt = update->dt;
	float input_smoothing = expf(-20*dt);
	
	uint player_idx, transform_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&player_idx, &state->players.c},
		{&transform_idx, &state->transforms.c},
		{},
	});
	
	while(DriftJoinNext(&join)){
		DriftPlayerData* player = state->players.data + player_idx;
		DriftPlayerAnimState* anim = &player->anim_state;

		if(player->local){
			DriftPlayerInput* input = &update->ctx->input.player;

			bool toggle_headlight = DriftInputButtonPress(input, DRIFT_INPUT_TOGGLE_HEADLIGHT);
			if(toggle_headlight){
				DriftAudioPlaySample(update->audio, DRIFT_SFX_CLICK, 1, 0, 1, false);
				player->headlight = !player->headlight;
			}
			
			uint slot_idx = 0;
			if(DriftInputButtonPress(input, DRIFT_INPUT_QUICK_SLOT1)) slot_idx = 1;
			if(DriftInputButtonPress(input, DRIFT_INPUT_QUICK_SLOT2)) slot_idx = 2;
			if(DriftInputButtonPress(input, DRIFT_INPUT_QUICK_SLOT3)) slot_idx = 3;
			if(DriftInputButtonPress(input, DRIFT_INPUT_QUICK_SLOT4)) slot_idx = 4;
			
			if(slot_idx && player->quickslots[slot_idx]){
				DriftAudioPlaySample(update->audio, DRIFT_SFX_CLICK, 1, 0, 1, false);
				player->quickslot_idx = (player->quickslot_idx != slot_idx ? slot_idx : 0);
			}
			
			int cargo_change = 0;
			if(DriftInputButtonPress(input, DRIFT_INPUT_CARGO_PREV)) cargo_change = -1;
			if(DriftInputButtonPress(input, DRIFT_INPUT_CARGO_NEXT)) cargo_change = +1;
			
			if(cargo_change){
				DriftAudioPlaySample(update->audio, DRIFT_SFX_CLICK, 1, 0, 1, false);
				for(uint i = 0; i < DRIFT_PLAYER_CARGO_SLOT_COUNT; i++){
					player->cargo_idx += cargo_change;
					player->cargo_idx = (player->cargo_idx + DRIFT_PLAYER_CARGO_SLOT_COUNT) % DRIFT_PLAYER_CARGO_SLOT_COUNT;
					if(player->cargo_slots[player->cargo_idx].count != 0) break;
				}
			}

			DriftVec2 prev_velocity = player->desired_velocity;
			DriftVec2 prev_rotation = player->desired_rotation;

			player->desired_velocity = DriftVec2Mul(DriftInputJoystick(input, 0, 1), DRIFT_PLAYER_SPEED);

			anim->hatch_l.target = 1, anim->hatch_l.rate = 1/0.2f;
			anim->hatch_r.target = 1, anim->hatch_r.rate = 1/0.2f;
			anim->laser.target = 0, anim->laser.rate = 1/0.2f;
			anim->cannons.target = 0, anim->cannons.rate = 1/0.2f;
			
			player_tool(player)->update(update, player, state->transforms.matrix[transform_idx]);

			player->desired_velocity = DriftVec2Lerp(player->desired_velocity, prev_velocity, input_smoothing);
			player->desired_rotation = DriftVec2Lerp(player->desired_rotation, prev_rotation, input_smoothing);
		}
		
		DriftAnimStateUpdate(&anim->hatch_l, dt);
		DriftAnimStateUpdate(&anim->hatch_r, dt);
		DriftAnimStateUpdate(&anim->laser, dt);
		DriftAnimStateUpdate(&anim->cannons, dt);

		DriftAffine transform_r = state->transforms.matrix[transform_idx];
		DriftAffine transform_l = DriftAffineMult(transform_r, (DriftAffine){-1, 0, 0, 1, 0, 0});
		float nacelle_l = DriftTerrainRaymarch(state->terra, DriftAffinePoint(transform_l, nacelle_in), DriftAffinePoint(transform_l, nacelle_out), 4, 2);
		player->nacelle_l = fmaxf(nacelle_l, DriftLerpConst(player->nacelle_l, 0, dt/0.25f));
		
		float nacelle_r = DriftTerrainRaymarch(state->terra, DriftAffinePoint(transform_r, nacelle_in), DriftAffinePoint(transform_r, nacelle_out), 4, 2);
		player->nacelle_r = fmaxf(nacelle_r, DriftLerpConst(player->nacelle_r, 0, dt/0.25f));
		
		float volume = 0;
		for(uint i = 0; i < 5; i++) volume += player->thrusters[i]*player->thrusters[i];
		
		if(volume > 1e-2){
			if(DriftAudioSourceActive(update->audio, player->thruster_sampler.source)){
				DriftSamplerSetParams(update->audio, player->thruster_sampler, 3*volume, 0, 1, true);
			} else {
				player->thruster_sampler = DriftAudioPlaySample(update->audio, DRIFT_SFX_ENGINE, 0, 0, 1, true);
			}
		} else {
			DriftSamplerSetParams(update->audio, player->thruster_sampler, 0, 0, 1, false);
		}
	}
}

static void PlayerCargoExchange(DriftGameContext* ctx, DriftPlayerData* player){
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
		if(is_request[i] && ctx->inventory[slot->type]){
			ctx->inventory[slot->request]--;
			slot->count++;
			DriftAudioPlaySample(ctx->audio, DRIFT_SFX_PING, 0.5f, 0, 1, false);
			break;
		}
	}

	// Empty one transfer slot.
	for(uint i = 0; i < DRIFT_PLAYER_CARGO_SLOT_COUNT; i++){
		DriftCargoSlot* slot = player->cargo_slots + i;
		if(!is_request[i] && slot->count > 0){
			slot->count--;
			ctx->inventory[slot->type]++;
			DriftAudioPlaySample(ctx->audio, DRIFT_SFX_PING, 0.5f, 0, 1, false);
			break;
		}
	}
}

void TickPlayer(DriftUpdate* update){
	DriftGameState* state = update->state;
	DriftGameContext* ctx = update->ctx;
	
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
	
	uint player_idx, transform_idx, body_idx, nav_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&player_idx, &state->players.c},
		{&body_idx, &state->bodies.c},
		{&transform_idx, &state->transforms.c},
		{&nav_idx, &state->navs.c, .optional = true},
		{},
	});
	
	while(DriftJoinNext(&join)){
		DriftPlayerData* player = &state->players.data[player_idx];
		DriftVec2 player_pos = state->bodies.position[body_idx];
		
		// Temporary.
		DriftVec2 home_pos = {128, 0};
		if(DriftVec2Distance(player_pos, home_pos) < 128 &&  ctx->current_tick % 16 == 0){
			PlayerCargoExchange(ctx, player);
		}

		DriftAffine m = state->transforms.matrix[transform_idx];
		DriftAffine m_inv = DriftAffineInverse(m);
		
		if(state->navs.data[nav_idx].next_node.id){
			DriftVec2 target_pos = state->navs.data[nav_idx].target_pos;
			DriftVec2 delta = DriftVec2Sub(target_pos, player_pos);
			player->desired_velocity = DriftVec2Clamp(DriftVec2Mul(delta, 0.15f/tick_dt), 1.5f*DRIFT_PLAYER_SPEED);
		}
		
		// Calculate the change in body relative velocity to hit the desired.
		DriftVec2 v = state->bodies.velocity[body_idx];
		DriftVec2 delta_v = DriftVec2Sub(player->desired_velocity, v);
		DriftVec2 local_delta_v = DriftAffineDirection(m_inv, delta_v);
		// local_delta_v = DriftVec2Clamp(local_delta_v, 2000.0f*dt);
		
		// Start by assuming we want te reduce the angular velocity to 0.
		float w = state->bodies.angular_velocity[body_idx];
		float delta_w = -w;
		
		// DriftVec2 v_mix = DriftVec2Lerp(delta_v, v, 0.75f);
		DriftVec2 v_mix = DriftVec2Mul(player->desired_velocity, 0.01f);
		DriftVec2 desired_rot = DriftVec2Lerp(v_mix, player->desired_rotation, DriftVec2Length(player->desired_rotation));
		if(DriftVec2LengthSq(desired_rot) > 1e-3f){
			DriftVec2 local_rot = DriftAffineDirection(m_inv, desired_rot);
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

		player->power_reserve = fmaxf(player->power_reserve - 5*update->tick_dt, 0);

		DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(state, player_pos, update->mem);
		if(info.valid_power){
			player->power_reserve = fminf(player->power_reserve + 30*update->tick_dt, player->power_capacity);
		}

		if(player->power_reserve == 0){
			state->bodies.position[body_idx] = DRIFT_VEC2_ZERO;

			void* u_ded_func(tina* coro, void* value);
			if(!ctx->script.coro) ctx->script.coro = tina_init(ctx->script.buffer, sizeof(ctx->script.buffer), u_ded_func, &ctx->script);
		}
	}
	
	if(!DriftEntitySetCheck(&update->state->entities, ctx->player)){
		DriftPlayerData* data = state->players.data + DriftComponentFind(&state->players.c, ctx->player);
		DriftEntity entity = ctx->player = DriftMakeEntity(state);
		TempPlayerInit(state, entity);
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
	DriftAffine matrix_gun0 = DriftAffineTRS((DriftVec2){8, -7 - -7*cannon_anim - 3}, 0, (DriftVec2){1, 1});
	DriftAffine matrix_gun1 = DriftAffineTRS((DriftVec2){12, -5 - -5*cannon_anim - 5}, 0, (DriftVec2){1, 1});
	
	return (PlayerCannonTransforms){{
		DriftAffineMult((DriftAffine){-1, 0, 0, 1, 0, 0}, matrix_gun0),
		DriftAffineMult((DriftAffine){+1, 0, 0, 1, 0, 0}, matrix_gun0),
		DriftAffineMult((DriftAffine){-1, 0, 0, 1, 0, 0}, matrix_gun1),
		DriftAffineMult((DriftAffine){+1, 0, 0, 1, 0, 0}, matrix_gun1),
	}};
}

void DrawPlayer(DriftDraw* draw){
	DriftGameState* state = draw->state;
	uint player_idx, transform_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&player_idx, &state->players.c},
		{&transform_idx, &state->transforms.c},
		{},
	});
	while(DriftJoinNext(&join)){
		DriftPlayerData* player = &state->players.data[player_idx];
		DriftPlayerAnimState* anim = &player->anim_state;
		static int frame = 0; frame++;
		
		float hatch_l = fmaxf(1 - anim->hatch_l.value, anim->cannons.value);
		float hatch_r = fmaxf(1 - anim->hatch_r.value, anim->cannons.value);
		
		const DriftAffine matrix_model = state->transforms.matrix[transform_idx];
		if(player->local) player_tool(player)->draw(draw, player, matrix_model);
		
		DriftAffine matrix_model_r = DriftAffineMult(matrix_model, (DriftAffine){ 1, 0, 0, 1, 0, 0});
		DriftAffine matrix_model_l = DriftAffineMult(matrix_model, (DriftAffine){-1, 0, 0, 1, 0, 0});
		
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
		DriftAffine matrix_engine = DriftAffineMult(matrix_model_r, (DriftAffine){1, 0, 0, 1, 0, -10});
		DriftAffine matrix_nacelle_l = DriftAffineMult(matrix_model_l, NacelleMatrix(thrust_l, nacelle_offset_l));
		DriftAffine matrix_nacelle_r = DriftAffineMult(matrix_model_r, NacelleMatrix(thrust_r, nacelle_offset_r));
		DriftAffine matrix_laser = DriftAffineTRS((DriftVec2){0, 9 + 7*DriftHermite3(anim->laser.value)}, 0, (DriftVec2){1, 1});
		DriftAffine matrix_hatch_l = DriftAffineTRS((DriftVec2){0, 0}, 0.85f*hatch_l, (DriftVec2){DriftLerp(1, 0.9f, hatch_l), 1});
		DriftAffine matrix_hatch_r = DriftAffineTRS((DriftVec2){0, 0}, 0.85f*hatch_r, (DriftVec2){DriftLerp(1, 0.9f, hatch_r), 1});
		DriftAffine matrix_bay_l = DriftAffineTRS((DriftVec2){-3*retract_l, 3*retract_l}, 0, (DriftVec2){1, -1});
		DriftAffine matrix_bay_r = DriftAffineTRS((DriftVec2){-3*retract_r, 3*retract_r}, 0, (DriftVec2){1, -1});
		DriftAffine matrix_flame_light = (DriftAffine){128, 0, 0, -64, 0, 0};
		
		DriftAffine matrix_flame = FlameMatrix(player->thrusters[0]);
		DriftAffine matrix_flame_l = FlameMatrix(thrust_mag_l);
		DriftAffine matrix_flame_r = FlameMatrix(thrust_mag_r);
		uint flame_frame = DRIFT_SPRITE_FLAME0 + (frame & (_DRIFT_SPRITE_FLAME_COUNT - 1));
		
		DriftVec4 flame_glow = {{2, 1, 0, 1}};
		const DriftRGBA8 color = {0xFF, 0xFF, 0xFF, 0xFF};
		const DriftRGBA8 glow = {0xC0, 0xC0, 0xC0, 0x00};
		
		DriftSprite* sprites = DRIFT_ARRAY_RANGE(draw->fg_sprites, 64);
		DriftLight* lights = DRIFT_ARRAY_RANGE(draw->lights, 64);
		
		DriftSpritePush(&sprites, flame_frame, glow, DriftAffineMult(matrix_engine, matrix_flame));
		DriftSpritePush(&sprites, DRIFT_SPRITE_NACELLE, color, matrix_engine);
		DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_HEMI, DriftVec4Mul(flame_glow, player->thrusters[0]), DriftAffineMult(matrix_engine, matrix_flame_light), 0);
		
		DriftSpritePush(&sprites, DRIFT_SPRITE_STRUT, color, DriftAffineMult(matrix_model_l, matrix_strut_l));
		DriftSpritePush(&sprites, flame_frame, glow, DriftAffineMult(matrix_nacelle_l, matrix_flame_l));
		DriftSpritePush(&sprites, DRIFT_SPRITE_NACELLE, color, matrix_nacelle_l);
		DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_HEMI, DriftVec4Mul(flame_glow, thrust_mag_l), DriftAffineMult(matrix_nacelle_l, matrix_flame_light), 0);
		if(frame%256 < 4) DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_RADIAL, (DriftVec4){{0, 0.4f, 0, 1}}, DriftAffineMult(matrix_nacelle_l, (DriftAffine){256, 0, 0, 256, 0, 0}), 0);
		
		DriftSpritePush(&sprites, DRIFT_SPRITE_STRUT, color, DriftAffineMult(matrix_model_r, matrix_strut_r));
		DriftSpritePush(&sprites, flame_frame, glow, DriftAffineMult(matrix_nacelle_r, matrix_flame_r));
		DriftSpritePush(&sprites, DRIFT_SPRITE_NACELLE, color, matrix_nacelle_r);
		DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_HEMI, DriftVec4Mul(flame_glow, thrust_mag_r), DriftAffineMult(matrix_nacelle_r, matrix_flame_light), 0);
		if(frame%256 < 4) DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_RADIAL, (DriftVec4){{1, 0, 0, 1}}, DriftAffineMult(matrix_nacelle_r, (DriftAffine){256, 0, 0, 256, 0, 0}), 0);
		
		DriftSpritePush(&sprites, DRIFT_SPRITE_LASER, color, DriftAffineMult(matrix_model, matrix_laser));
		PlayerCannonTransforms cannons = CalculatePlayerCannonTransforms(anim->cannons.value);
		DriftSpritePush(&sprites, DRIFT_SPRITE_GUN, color, DriftAffineMult(matrix_model, cannons.arr[0]));
		DriftSpritePush(&sprites, DRIFT_SPRITE_GUN, color, DriftAffineMult(matrix_model, cannons.arr[1]));
		DriftSpritePush(&sprites, DRIFT_SPRITE_GUN, color, DriftAffineMult(matrix_model, cannons.arr[2]));
		DriftSpritePush(&sprites, DRIFT_SPRITE_GUN, color, DriftAffineMult(matrix_model, cannons.arr[3]));
		
		DriftSpritePush(&sprites, DRIFT_SPRITE_HATCH, color, DriftAffineMult(matrix_model_l, matrix_hatch_l));
		DriftSpritePush(&sprites, DRIFT_SPRITE_HATCH, color, DriftAffineMult(matrix_model_r, matrix_hatch_r));
		DriftSpritePush(&sprites, DRIFT_SPRITE_HATCH, color, DriftAffineMult(matrix_model_l, matrix_bay_l));
		DriftSpritePush(&sprites, DRIFT_SPRITE_HATCH, color, DriftAffineMult(matrix_model_r, matrix_bay_r));
		DriftSpritePush(&sprites, DRIFT_SPRITE_HULL, color, matrix_model);
		
		DriftVec4 dim_glow = {{0.1f, 0.1f, 0.1f, 10}};
		DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_RADIAL, dim_glow, DriftAffineMult(matrix_model, (DriftAffine){64, 0, 0, 64, 0, 0}), 0);
		
		DriftVec4 laser_glow = {{0.8f*anim->laser.value, 0, 0, 0}};
		DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_RADIAL, laser_glow, DriftAffineMult(matrix_model, (DriftAffine){64, 0, 0, 64, 0, 20}), 0);
		
		if(player->headlight){
			DriftVec4 headlight_color = {{0.5f, 0.3f, 0.0f, 5}};
			DriftAffine headlight_matrix = {128, 0, 0, 96, 0, 4};
			uint headlight_frame = DRIFT_SPRITE_LIGHT_FLOOD;
			
			if(draw->ctx->inventory[DRIFT_ITEM_TYPE_HEADLIGHT]){
				headlight_color = (DriftVec4){{1, 1, 1.5f, 5}};
				headlight_matrix = (DriftAffine){192, 0, 0, 192, 0, 4};
				headlight_frame = DRIFT_SPRITE_LIGHT_HEMI;
			}
			
			DriftLightPush(&lights, true, headlight_frame, headlight_color, DriftAffineMult(matrix_model, headlight_matrix), 12);
		}
		
		DriftArrayRangeCommit(draw->fg_sprites, sprites);
		DriftArrayRangeCommit(draw->lights, lights);

		DriftVec2 player_pos = DriftAffineOrigin(matrix_model);
		DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(state, player_pos, draw->mem);
		DriftRGBA8 beam_color = {0x00, 0x40, 0x40, 0x00};
		if(info.valid_power){
			DriftVec2 near_pos = {};
			float near_dist = INFINITY;

			DRIFT_ARRAY_FOREACH(info.nodes, node){
				float dist = DriftVec2Distance(player_pos, node->pos);
				if(node->blocked_at == 1 && dist < near_dist){
					near_pos = node->pos;
					near_dist = dist;
				}
			}

			DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){
				.p0 = player_pos, .p1 = near_pos,
				.radii = {1.5f}, .color = beam_color,
			}));
		}
	}
}

static bool check_spawn(DriftUpdate* update, DriftVec2 pos, float terrain_dist){
	bool open_space = DriftTerrainSampleCoarse(update->state->terra, pos).dist > terrain_dist;
	bool on_screen = DriftAABB2Test(DRIFT_AABB2_UNIT, DriftAffinePoint(update->prev_vp_matrix, pos));
	return !on_screen && open_space;
}

#define SPAWN_RADIUS 2048

#define DRIFT_MAX_BIOME 4

typedef struct {
	u64 rand, sum;
} SelectionContext;

static bool select_weight(SelectionContext* ctx, u64 weight){
	ctx->sum += weight;
	ctx->rand *= ctx->sum;
	if(ctx->rand <= weight*RAND_MAX){
		ctx->rand /= weight;
		return true;
	} else {
		ctx->rand = (ctx->rand - weight*RAND_MAX)/(ctx->sum - weight);
		return false;
	}
}

static void TickItemSpawns(DriftUpdate* update){
	DriftGameState* state = update->state;

	DriftVec2 player_pos = ({
		uint body_idx = DriftComponentFind(&state->bodies.c, update->ctx->player);
		state->bodies.position[body_idx];
	});
	
	for(uint retries = 0; retries < 10; retries++){
		if(state->pickups.c.count >= 300) break;
		
		DriftVec2 pos = DriftVec2FMA(player_pos, DriftRandomInUnitCircle(), SPAWN_RADIUS);
		if(check_spawn(update, pos, 4)){
			uint item_idx = DRIFT_ITEM_TYPE_NONE;
			SelectionContext ctx = {.rand = rand()};
			switch(DriftTerrainSampleBiome(state->terra, pos)){
				default:
				case 0:{
					if(select_weight(&ctx, 100)) item_idx = DRIFT_ITEM_TYPE_ORE;
					if(select_weight(&ctx, 1)) item_idx = DRIFT_ITEM_TYPE_SCRAP;
				} break;
				case 1:{
					if(select_weight(&ctx, 10)) item_idx = DRIFT_ITEM_TYPE_LUMIUM;
				} break;
			}
			DriftPickupMake(state, pos, DRIFT_VEC2_ZERO, item_idx);
		}
	}

	uint pickup_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &state->pickups.c, .variable = &pickup_idx},
		{.component = &state->bodies.c, .variable = &body_idx},
		{},
	});
	
	while(DriftJoinNext(&join)){
		DriftVec2 delta = DriftVec2Sub(player_pos, state->bodies.position[body_idx]);
		if(DriftVec2Length(delta) > SPAWN_RADIUS){
			DriftDestroyEntity(update, join.entity);
		}
	}
}

static void DrawOre(DriftDraw* draw){
	DriftGameState* state = draw->state;
	DriftSprite* sprites = DRIFT_ARRAY_RANGE(draw->fg_sprites, state->ore_deposits.c.count);
	
	// TODO just use a sprite component?
	uint ore_idx, transform_idx;
	DriftJoin deposits = DriftJoinMake((DriftComponentJoin[]){
		{&ore_idx, &state->ore_deposits.c},
		{&transform_idx, &state->transforms.c},
		{},
	});
	while(DriftJoinNext(&deposits)){
		DriftSpritePush(&sprites, DRIFT_SPRITE_ORE_DEPOSIT_SM0, DRIFT_RGBA8_WHITE, state->transforms.matrix[transform_idx]);
	}
	DriftArrayRangeCommit(draw->fg_sprites, sprites);
}

DriftNearbyNodesInfo DriftSystemPowerNodeNearby(DriftGameState* state, DriftVec2 pos, DriftMem* mem){
	DriftNearbyNodesInfo info = {.pos = pos, .nodes = DRIFT_ARRAY_NEW(mem, 8, DriftNearbyNodeInfo)};
	uint valid_count = 0, invalid_count = 0;
	
	DriftPowerNode* nodes = state->power_nodes.node;
	DRIFT_COMPONENT_FOREACH(&state->power_nodes.c, i){
		DriftVec2 node_pos = {nodes[i].x, nodes[i].y};
		float dist = DriftVec2Distance(pos, node_pos);
		if(dist < 2*DRIFT_POWER_EDGE_MIN_LENGTH){
			DriftEntity e = state->power_nodes.entity[i];
			if(!DriftEntitySetCheck(&state->entities, e)) continue;
			
			bool is_close = dist < DRIFT_POWER_EDGE_MIN_LENGTH;
			float t = DriftTerrainRaymarch(state->terra, node_pos, pos, DRIFT_POWER_BEAM_RADIUS - 1, 1);

			bool unblocked = t == 1;
			bool valid = !is_close && unblocked;
			valid_count += valid;
			invalid_count += is_close;
			
			info.valid_power |= unblocked;
			DRIFT_ARRAY_PUSH(info.nodes, ((DriftNearbyNodeInfo){.e = e, .pos = node_pos, .valid = valid, .is_too_close = is_close, .blocked_at = t}));
		}
	}
	
	info.valid_node = valid_count > 0 && invalid_count == 0;
	return info;
}

void flow_map_tick(tina_job* job){
	// DriftStopwatch sw = DRIFT_STOPWATCH_START("power tick");
	DriftGameState* state = tina_job_get_description(job)->user_data;
	DriftComponentFlowMap* fmap = state->flow_maps + tina_job_get_description(job)->user_idx;
	
	// TODO Limit the number of updates per tick.
	DriftPowerNode* nodes = state->power_nodes.node;
	DriftTablePowerNodeEdges* edges = &state->power_edges;
	
	uint node_idx, flow_idx0;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&node_idx, &state->power_nodes.c},
		{&flow_idx0, &fmap->c, .optional = true},
		{}
	});
	
	while(DriftJoinNext(&join)){
		// Create the flow node if it doesn't exist yet.
		if(flow_idx0 == 0) flow_idx0 = DriftComponentAdd(&fmap->c, join.entity);
		
		// Propagate the distance from the next node.
		// Deleted links will be replaced by the default node with infinite root distance.
		DriftFlowMapNode* flow0 = fmap->node + flow_idx0;
		uint flow_idx1 = DriftComponentFind(&fmap->c, flow0->next);
		flow0->root_dist = fmap->node[flow_idx1].root_dist + flow0->next_dist;
		if(flow_idx0 == flow_idx1) flow0->root_dist = fminf(flow0->root_dist, 1e5);
	}
	// DRIFT_STOPWATCH_MARK(sw, "broken");
	
	// Propagate new links.
	for(uint i = 0, count = edges->t.row_count; i < count; i++){
		DriftPowerNodeEdge* edge = edges->edge + i;
		float len = hypotf(edge->x1 - edge->x0, edge->y1 - edge->y0);
		
		uint idx0 = DriftComponentFind(&fmap->c, edge->e0);
		uint idx1 = DriftComponentFind(&fmap->c, edge->e1);
		if(idx0 == 0 || idx1 == 0) continue;
		
		DriftFlowMapNode* flow0 = fmap->node + idx0;
		DriftFlowMapNode* flow1 = fmap->node + idx1;
		
		if(flow0->root_dist > flow1->root_dist + len){
			flow0->next_dist = len;
			flow0->next = edge->e1;
		}
		
		if(flow1->root_dist > flow0->root_dist + len){
			flow1->next_dist = len;
			flow1->next = edge->e0;
		}
	}
	// DRIFT_STOPWATCH_STOP(sw, "edges");
}

void TickPower(DriftUpdate* update){
	DriftGameState* state = update->state;
	DriftEntity e = {};//update->ctx->client->player_entity;
	uint transform_idx = DriftComponentFind(&state->transforms.c, e);
	DriftVec2 p0 = DriftAffineOrigin(state->transforms.matrix[transform_idx]);
	
	DriftComponentFlowMap* flow = state->flow_maps + 1;
	
	// if(ctx->input.players[0].axes[DRIFT_INPUT_AXIS_ACTION1] > 0.5f)
	{
	DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(state, p0, update->mem);
	DRIFT_ARRAY_FOREACH(info.nodes, node){
		if(node->blocked_at < 1) continue;
		
		DriftEntity e = node->e;
		DriftPowerNode* pnode = state->power_nodes.node + DriftComponentFind(&state->power_nodes.c, e);
		DriftVec2 p1 = {pnode->x, pnode->y};
		
		uint fnode_idx = DriftComponentFind(&flow->c, e);
		if(fnode_idx == 0) fnode_idx = DriftComponentAdd(&flow->c, e);
		
		DriftFlowMapNode* fnode = flow->node + fnode_idx;
		float dist = DriftVec2Distance(p0, p1);
		fnode->next = e;
		fnode->next_dist = 1e4f*update->tick_dt;
		fnode->root_dist = dist;
		
		// DriftDebugSegment(state, p0, p1, 1, DRIFT_RGBA8_ORANGE);
	}
	}
	
	DriftThrottledParallelFor(update->job, "FlowMapTick", flow_map_tick, update->state, DRIFT_FLOW_MAP_COUNT);
}

void DrawPower(DriftDraw* draw){
	DriftGameState* state = draw->state;
	float light_radius = 1.0f*DRIFT_POWER_EDGE_MIN_LENGTH;
	DriftVec4 light_color = {{0.7f, 1.0f, 1.0f, 3}};
	DriftRGBA8 node_color = {0xC0, 0xC0, 0xC0, 0xFF}, beam_color = {0x00, 0x40, 0x40, 0x00};
	
	// TODO no culling?
	DriftPowerNode* nodes = state->power_nodes.node;
	DriftPowerNodeEdge* edges = state->power_edges.edge;
	
	for(uint i = 0; i < state->power_edges.t.row_count; i++){
		DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){
			.p0 = {edges[i].x0, edges[i].y0},
			.p1 = {edges[i].x1, edges[i].y1},
			.radii = {1.5f}, .color = beam_color,
		}));
	}
	
	DriftLight* lights = DRIFT_ARRAY_RANGE(draw->lights, state->power_nodes.c.count);
	DRIFT_COMPONENT_FOREACH(&state->power_nodes.c, i){
		DriftVec2 pos = {nodes[i].x, nodes[i].y};
		DriftLightPush(&lights, false, DRIFT_SPRITE_LIGHT_RADIAL, light_color, (DriftAffine){light_radius, 0, 0, light_radius, pos.x, pos.y}, 5);
	}
	DriftArrayRangeCommit(draw->lights, lights);
}

static DriftVec2 node_pos(DriftGameState* state, uint idx){
	DriftPowerNode* next_node = state->power_nodes.node + idx;
	return (DriftVec2){next_node->x, next_node->y};
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
		DriftVec2 next_pos = node_pos(state, pnode_idx);
		DriftVec2 target_pos = navs[nav_idx].target_pos;
		
		if(pnode_idx == 0){
			// Target node does not exist. Try to find a new one.
			DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(state, pos, update->mem);
			float min_dist = INFINITY;
			DRIFT_ARRAY_FOREACH(info.nodes, inode){
				DriftFlowMapNode* fnode = flow_map->node + DriftComponentFind(&flow_map->c, inode->e);
				if(inode->blocked_at == 1 && fnode->root_dist < min_dist){
					min_dist = fnode->root_dist;
					next_e = inode->e;
				}
			}
			
			pnode_idx = DriftComponentFind(&state->power_nodes.c, next_e);
			navs[nav_idx].next_node = next_e;
			target_pos = next_pos = node_pos(state, pnode_idx);
		}
		
		{
			float radius = navs[nav_idx].radius, min = 0;
			DriftTerrainRaymarch2(state->terra, pos, target_pos, 0.75f*radius, 2, &min);
			// DriftDebugSegment2(state, pos, target_pos, radius + min, radius - 1 + min, DRIFT_RGBA8_WHITE);
			target_pos = DriftVec2LerpConst(target_pos, next_pos, 0.5f*(min - 1));
		}
		
		if(DriftVec2Distance(target_pos, next_pos) == 0){
			uint fnode_idx = DriftComponentFind(&flow_map->c, next_e);
			navs[nav_idx].next_node = flow_map->node[fnode_idx].next;
		}
		
		navs[nav_idx].target_pos = target_pos;
		// DriftDebugSegment(state, pos, target_pos, 2, DRIFT_RGBA8_ORANGE);
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
		light.matrix = DriftAffineMult(m, light.matrix);
		*(lights++) = light;
	}
	DriftArrayRangeCommit(draw->fg_sprites, sprites);
	DriftArrayRangeCommit(draw->lights, lights);
}

DriftEntity DriftDroneMake(DriftGameState* state, DriftVec2 pos){
	DriftEntity e = DriftMakeEntity(state);
	uint drone_idx = DriftComponentAdd(&state->drones.c, e);
	
	uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
	uint sprite_idx = DriftComponentAdd(&state->sprites.c, e);
	state->sprites.data[sprite_idx].frame = DRIFT_SPRITE_DRONE;
	state->sprites.data[sprite_idx].color = DRIFT_RGBA8_WHITE;
	
	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	state->bodies.position[body_idx] = pos;
	
	float radius = 7, mass = 2.0f;
	state->bodies.radius[body_idx] = radius;
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 1/(mass*0.5f*radius*radius);
	
	return e;
}

static void TickDrones(DriftUpdate* update){
	DriftGameState* state = update->state;
	float subtick_dt = update->tick_dt;
	
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
		
		if(nav_idx == 0){
			uint nav_idx = DriftComponentAdd(&state->navs.c, join.entity);
			state->navs.data[nav_idx].flow_map = 1;
		}
		
		if(state->navs.data[nav_idx].next_node.id){
			DriftVec2 target_pos = state->navs.data[nav_idx].target_pos;
			DriftVec2 delta = DriftVec2Sub(target_pos, state->bodies.position[body_idx]);
			
			DriftVec2 target_velocity = DriftVec2Clamp(DriftVec2Mul(delta, expf(-100*subtick_dt)/subtick_dt), 0.75f*DRIFT_PLAYER_SPEED);;
			state->bodies.velocity[body_idx] = DriftVec2LerpConst(state->bodies.velocity[body_idx], target_velocity, 500.0f*subtick_dt);
			
			DriftVec2 desired_rot = DriftVec2Normalize(delta);
			DriftVec2 local_rot = DriftAffineDirection(m_inv, desired_rot);
			float desired_w = -(1 - expf(-10*subtick_dt))*atan2f(local_rot.x, local_rot.y)/subtick_dt;
			state->bodies.angular_velocity[body_idx] = DriftLerpConst(state->bodies.angular_velocity[body_idx], desired_w, 50*subtick_dt);
		} else {
			// state->navs.data[nav_idx].
		}
	}
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
	uint health_idx = DriftComponentFind(&update->state->health.c, entity);
	if(health_idx){
		DriftHealth* health = update->state->health.data + health_idx;
		health->value -= amount;
		
		if(health->value <= 0){
			health->value = 0;
			DriftDestroyEntity(update, entity);
			
			DriftGameState* state = update->state;
			uint body_idx = DriftComponentFind(&state->bodies.c, entity);
			DriftVec2 pos = state->bodies.position[body_idx];
			DriftVec2 vel = state->bodies.velocity[body_idx];
			if(health->drop) DriftPickupMake(update->state, pos, vel, health->drop);
			
			void MakeBlast(DriftUpdate* update, DriftVec2 position);
			MakeBlast(update, pos);
			
			DriftAudioPlaySample(update->audio, DRIFT_SFX_EXPLODE, 1, 0, 1, false);
		} else {
			DriftAudioPlaySample(update->audio, DRIFT_SFX_BULLET_HIT, 1, 0, 1, false);
		}
	}
}

void FireBullet(DriftUpdate* update, DriftVec2 pos, DriftVec2 vel){
	DriftGameState* state = update->state;
	DriftEntity e = DriftMakeEntity(state);
	uint bullet_idx = DriftComponentAdd(&update->state->projectiles.c, e);
	state->projectiles.origin[bullet_idx] = pos;
	state->projectiles.velocity[bullet_idx] = vel;
	state->projectiles.tick0[bullet_idx] = update->ctx->current_tick;
	state->projectiles.timeout[bullet_idx] = 2; // TODO should be in ticks?
	
	float pitch = expf(0.2f*(2*(float)rand()/(float)RAND_MAX - 1));
	DriftAudioPlaySample(update->audio, DRIFT_SFX_BULLET_FIRE, 0.5f, 0, pitch, false);
}

static void TickProjectiles(DriftUpdate* update){
	DriftGameState* state = update->state;
	
	DRIFT_COMPONENT_FOREACH(&state->projectiles.c, i){
		uint age = update->ctx->current_tick - state->projectiles.tick0[i];
		DriftVec2 origin = state->projectiles.origin[i], velocity = state->projectiles.velocity[i];
		DriftVec2 p0 = DriftVec2FMA(origin, velocity, (age + 0)/DRIFT_TICK_HZ);
		DriftVec2 p1 = DriftVec2FMA(origin, velocity, (age + 1)/DRIFT_TICK_HZ);
		
		float ray_t = DriftTerrainRaymarch(state->terra, p0, p1, 0, 1);
		DriftEntity hit_entity = {};
		
		DRIFT_COMPONENT_FOREACH(&state->bodies.c, body_idx){
			if(DriftCollisionFilter(DRIFT_COLLISION_TYPE_PLAYER_BULLET, state->bodies.collision_type[body_idx])){
				DriftEntity entity = state->bodies.entity[body_idx];
				
				RayHit hit = CircleSegmentQuery(state->bodies.position[body_idx], state->bodies.radius[body_idx], p0, p1, 0);
				if(hit.alpha < ray_t){
					ray_t = hit.alpha;
					
					hit_entity = entity;
					DriftVec2* body_v = state->bodies.velocity + body_idx;
					*body_v = DriftVec2FMA(*body_v, velocity, 0.5e-1f);
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
	float t0 = draw->dt_since_tick - draw->dt/3;
	float t1 = draw->dt_since_tick + draw->dt/3;
	
	DRIFT_COMPONENT_FOREACH(&state->projectiles.c, i){
		float t = (draw->ctx->current_tick - state->projectiles.tick0[i])/DRIFT_TICK_HZ;
		float timeout = state->projectiles.timeout[i];
		DriftVec2 origin = state->projectiles.origin[i], velocity = state->projectiles.velocity[i];
		DriftVec2 p0 = DriftVec2FMA(origin, velocity, fmaxf(0, t + t0));
		DriftVec2 p1 = DriftVec2FMA(origin, velocity, fmaxf(0, t + t1));
		DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){.p0 = p0, .p1 = p1, .radii[0] = 2, .color = {0xFF, 0xB0, 0x40, 0xFF}}));

		DriftVec4 color = {{0.5, 0.25, 0, 3}};
		DRIFT_ARRAY_PUSH(draw->lights, DriftLightMake(false, DRIFT_SPRITE_LIGHT_RADIAL, color, (DriftAffine){128, 0, 0, 128, p0.x, p0.y}, 0));
	}
}

typedef struct {
	DriftComponent c;
	DriftEntity* entity;

	float* speed;
	float* accel;
} DriftComponentBugNav;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	
	uint spawn_counter;
} DriftComponentGlowBug;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	
	uint spawn_counter;
} DriftComponentWorkerBug;

DriftComponentBugNav BUG_NAV;
DriftComponentGlowBug GLOW_BUGS;
DriftComponentWorkerBug WORKER_BUGS;

static void TickBugNav(DriftUpdate* update){
	DriftGameState* state = update->state;
	
	DriftVec2 player_pos = ({
		uint body_idx = DriftComponentFind(&state->bodies.c, update->ctx->player);
		state->bodies.position[body_idx];
	});
	
	uint bug_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &BUG_NAV.c, .variable = &bug_idx},
		{.component = &state->bodies.c, .variable = &body_idx},
		{},
	});
	
	float period = 2e9;
	float phase = 2*(float)M_PI/period*(update->ctx->tick_nanos % (u64)period), inc = 0.2f;
	DriftVec2 rot = {cosf(phase), sinf(phase)}, rinc = {cosf(inc), sinf(inc)};

	while(DriftJoinNext(&join)){
		DriftVec2 pos = state->bodies.position[body_idx];
		DriftVec2 forward = DriftVec2Perp(state->bodies.rotation[body_idx]);
		
		// Push the forward vector away from terrain.
		DriftTerrainSampleInfo info = DriftTerrainSampleFine(state->terra, pos);
		float bias = DriftClamp(1.5f - info.dist/35, -1, 1);
		DriftVec2 forward_bias = DriftVec2FMA(forward, info.grad, bias*bias);
		forward_bias = DriftVec2FMA(forward_bias, rot, 0.3f);
		forward_bias = DriftVec2Normalize(forward_bias);
		// forward_bias = DriftVec2Clamp(forward_bias, 1);

		// DriftDebugSegment(state, pos, DriftVec2FMA(pos, rot, 20), 1, DRIFT_RGBA8_RED);
		// DriftDebugSegment(state, pos, DriftVec2FMA(pos, forward_bias, 20), 1, DRIFT_RGBA8_RED);
		
		DriftVec2* v = state->bodies.velocity + body_idx;
		float* w = state->bodies.angular_velocity + body_idx;
		// Accelerate towards forward bias.
		const float speed = BUG_NAV.speed[bug_idx], accel = BUG_NAV.accel[bug_idx];
		*v = DriftVec2LerpConst(*v, DriftVec2Mul(forward_bias, speed), accel*update->tick_dt);
		// Rotate to align with motion.
		*w = (*w)*0.7f + DriftVec2Cross(forward, *v)/50;

		rot = DriftVec2Rotate(rot, rinc);
		
		// TODO push these checks into a list.
		DriftVec2 delta = DriftVec2Sub(player_pos, pos);
		if(DriftVec2Length(delta) > 1024){
			// DRIFT_LOG("far out");
			DriftDestroyEntity(update, join.entity);
		}
	}
}

static DriftEntity SpawnGlowBug(DriftGameState* state, DriftVec2 pos, DriftVec2 rot){
	DriftEntity e = DriftMakeEntity(state);
	DriftComponentAdd(&GLOW_BUGS.c, e);
	DriftComponentAdd(&state->transforms.c, e);

	uint nav_idx = DriftComponentAdd(&BUG_NAV.c, e);
	BUG_NAV.speed[nav_idx] = 50;
	BUG_NAV.accel[nav_idx] = 150;

	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	float mass = 2, radius = 8;
	state->bodies.position[body_idx] = pos;
	state->bodies.rotation[body_idx] = rot;
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 2/(mass*radius*radius);
	state->bodies.radius[body_idx] = radius;
	state->bodies.collision_type[body_idx] = DRIFT_COLLISION_TYPE_BUG;
	
	uint health_idx = DriftComponentAdd(&state->health.c, e);
	state->health.data [health_idx] = (DriftHealth){.value = 50, .maximum = 50, .drop = DRIFT_ITEM_TYPE_LUMIUM};
	
	return e;
}

static DriftEntity SpawnWorkerDrone(DriftGameState* state, DriftVec2 pos, DriftVec2 rot){
	DriftEntity e = DriftMakeEntity(state);
	DriftComponentAdd(&WORKER_BUGS.c, e);
	DriftComponentAdd(&state->transforms.c, e);

	uint nav_idx = DriftComponentAdd(&BUG_NAV.c, e);
	BUG_NAV.speed[nav_idx] = 100;
	BUG_NAV.accel[nav_idx] = 300;

	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	float mass = 2, radius = 8;
	state->bodies.position[body_idx] = pos;
	state->bodies.rotation[body_idx] = rot;
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 2/(mass*radius*radius);
	state->bodies.radius[body_idx] = radius;
	state->bodies.collision_type[body_idx] = DRIFT_COLLISION_TYPE_BUG;

	uint health_idx = DriftComponentAdd(&state->health.c, e);
	state->health.data [health_idx] = (DriftHealth){.value = 100, .maximum = 100, .drop = DRIFT_ITEM_TYPE_SCRAP};
	
	return e;
}

static void TickEnemySpawns(DriftUpdate* update){
	DriftGameState* state = update->state;
	float dt = update->tick_dt;
	
	DriftVec2 player_pos = ({
		uint body_idx = DriftComponentFind(&state->bodies.c, update->ctx->player);
		state->bodies.position[body_idx];
	});
	
	if(update->ctx->current_tick % (uint)(1*DRIFT_TICK_HZ) == 0){
		int count = 20 - GLOW_BUGS.c.count;
		GLOW_BUGS.spawn_counter = count > 0 ? count : 0;
		// if(GLOW_BUGS.spawn_counter) DRIFT_LOG("spawning %d glow bugs", GLOW_BUGS.spawn_counter);
	}
	
	while(GLOW_BUGS.spawn_counter){
		DriftVec2 offset = DriftRandomInUnitCircle();
		DriftVec2 pos = DriftVec2FMA(player_pos, offset, SPAWN_RADIUS);
		if(check_spawn(update, pos, 8)){
			SelectionContext ctx = {.rand = rand()};
			DriftEntity (*spawn_func)(DriftGameState*, DriftVec2, DriftVec2) = NULL;
			switch(DriftTerrainSampleBiome(state->terra, pos)){
				default:
				case 0:{
					if(select_weight(&ctx, 100)) spawn_func = SpawnGlowBug;
					if(select_weight(&ctx, 20)) spawn_func = SpawnWorkerDrone;
				} break;
			}
			
			spawn_func(state, pos, DriftVec2Normalize(offset));
			GLOW_BUGS.spawn_counter--;
		}
	}
}

static uint anim_loop(uint tick, uint div, uint f0, uint f1){
	return f0 + tick/div%(f1 - f0);
}

static void DrawGlowBugs(DriftDraw* draw){
	DriftGameState* state = draw->state;
	
	uint bug_idx, transform_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &GLOW_BUGS.c, .variable = &bug_idx},
		{.component = &state->transforms.c, .variable = &transform_idx},
		{},
	});
	
	uint tick = draw->ctx->current_tick;
	while(DriftJoinNext(&join)){
		uint frame = anim_loop(tick++, 8, DRIFT_SPRITE_GLOW_BUG1, DRIFT_SPRITE_GLOW_BUG6);
		DriftAffine transform = state->transforms.matrix[transform_idx];
		DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteMake(frame, DRIFT_RGBA8_WHITE, transform));
		
		DriftAffine m = DriftAffineMult(transform, (DriftAffine){100, 0, 0, 100, 0, -8});
		DRIFT_ARRAY_PUSH(draw->lights, DriftLightMake(false, DRIFT_SPRITE_LIGHT_RADIAL, (DriftVec4){{0.3f, 0.4f, 0, 2}}, m, 0));
	}
}

static void DrawWorkerBugs(DriftDraw* draw){
	DriftGameState* state = draw->state;
	
	uint bug_idx, transform_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &WORKER_BUGS.c, .variable = &bug_idx},
		{.component = &state->transforms.c, .variable = &transform_idx},
		{},
	});
	
	uint tick = draw->ctx->current_tick;
	while(DriftJoinNext(&join)){
		uint frame = anim_loop(tick++, 8, DRIFT_SPRITE_WORKER_DRONE1, DRIFT_SPRITE_WORKER_DRONE4);//DRIFT_SPRITE_WORKER_DRONE1 + (tick++)/8%4;
		DriftAffine transform = state->transforms.matrix[transform_idx];
		DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteMake(frame, DRIFT_RGBA8_WHITE, transform));
		
		DriftAffine m = DriftAffineMult(transform, (DriftAffine){30, 0, 0, 30, 0, -8});
		DRIFT_ARRAY_PUSH(draw->lights, DriftLightMake(false, DRIFT_SPRITE_LIGHT_RADIAL, (DriftVec4){{0.2f, 0.2f, 0.4f, 7}}, m, 0));
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
	blasts.tick0[idx] = update->ctx->current_tick;
}

static void DrawBlasts(DriftDraw* draw){
	DriftGameState* state = draw->state;
	uint tick = draw->ctx->current_tick;
	
	uint i = 0;
	while(i < blasts.t.row_count){
		uint frame = (tick - blasts.tick0[i])/2;
		if(frame < 8){
			DriftVec2 pos = blasts.position[i];
			DriftAffine transform = {3, 0, 0, 3, pos.x, pos.y};
			DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteMake(DRIFT_SPRITE_DOGSPLOSION0 + frame, DRIFT_RGBA8_WHITE, transform));
			
			DriftVec4 color = DriftVec4Mul((DriftVec4){{8, 6, 4, 4}}, 1 - frame/8.0f);
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
		
		component_init(update->state, &BUG_NAV.c, "@bug_nav", (DriftColumnSet){{
			DRIFT_DEFINE_COLUMN(BUG_NAV.entity),
			DRIFT_DEFINE_COLUMN(BUG_NAV.speed),
			DRIFT_DEFINE_COLUMN(BUG_NAV.accel),
		}}, 256);
		BUG_NAV.c.reset_on_hotload = true;
		
		component_init(update->state, &GLOW_BUGS.c, "@glow_bugs", (DriftColumnSet){{
			DRIFT_DEFINE_COLUMN(GLOW_BUGS.entity),
		}}, 256);
		GLOW_BUGS.c.reset_on_hotload = true;
		
		component_init(update->state, &WORKER_BUGS.c, "@worker_bugs", (DriftColumnSet){{
			DRIFT_DEFINE_COLUMN(WORKER_BUGS.entity),
		}}, 256);
		WORKER_BUGS.c.reset_on_hotload = true;
	}
	
	UpdatePlayer(update);
}

#define RUN_FUNC(_func_, _arg_) {TracyCZoneN(ZONE, #_func_, true); _func_(_arg_); TracyCZoneEnd(ZONE);}

void DriftSystemsTick(DriftUpdate* update){
	RUN_FUNC(TickItemSpawns, update);
	RUN_FUNC(TickNavs, update);
	RUN_FUNC(TickPlayer, update);
	RUN_FUNC(TickDrones, update);
	RUN_FUNC(TickPower, update);
	RUN_FUNC(TickProjectiles, update);
	RUN_FUNC(TickEnemySpawns, update);
	RUN_FUNC(TickBugNav, update);
}

void DriftSystemsDraw(DriftDraw* draw){
	typedef struct {
		u8 fx, fy, fdist, idist;
	} PlacementNoise;
	static const PlacementNoise* pixels;
	static uint rando[64*64];
	
	if(pixels == NULL){
		pixels = DriftAssetGet("bin/placement64.bin")->data;
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
					};
					uint cryo[] = {DRIFT_SPRITE_MEDIUM_CRYSTALS1};
					
					uint* biome_base[] = {light, radio, cryo};
					uint biome_len[] = {sizeof(light)/sizeof(*light), sizeof(radio)/sizeof(*radio), sizeof(cryo)/sizeof(*cryo)};
					
					uint biome = DriftTerrainSampleBiome(state->terra, pos);
					uint frame = biome_base[biome][rnd % biome_len[biome]];
					DriftSprite sprite = {
						.matrix = {g.y, -g.x, g.x, g.y, pos.x, pos.y},
						.frame = DRIFT_SPRITE_FRAMES[frame], .color = DRIFT_RGBA8_WHITE,
					};
					DRIFT_ARRAY_PUSH(draw->fg_sprites, sprite);
					
					if(frame == DRIFT_SPRITE_LARGE_BUSHY_PLANTS3 || frame == DRIFT_SPRITE_LARGE_BUSHY_PLANTS5){
						DriftLight light = {.frame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = {{0.0f, 0.15f, 0.3f, 5}}, .matrix = {96, 0, 0, 96, pos.x, pos.y}};
						DRIFT_ARRAY_PUSH(draw->lights, light);
					} else if(frame == DRIFT_SPRITE_LARGE_BUSHY_PLANTS4){
						DriftLight light = {.frame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = {{0.3f, 0.25f, 0.0f, 5}}, .matrix = {96, 0, 0, 96, pos.x, pos.y}};
						DRIFT_ARRAY_PUSH(draw->lights, light);
					} else if(frame == DRIFT_SPRITE_MEDIUM_CRYSTALS1){
						DriftLight light = {.frame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = {{0.06f, 0.1f, 0.0f, 5}}, .matrix = {96, 0, 0, 96, pos.x, pos.y}};
						DRIFT_ARRAY_PUSH(draw->lights, light);
					}
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
					
					static const uint* cryo_base[] = {radio0, radio0, radio0};
					static const uint cryo_div[] = {3, 3, 3};
					
					static const uint** base[] = {light_base, radio_base, cryo_base};
					static const uint* div[] = {light_div, radio_div, cryo_div};
					
					uint asset_size = (uint)DriftClamp((pdist - 2)/1.5f, 0, 2.99f);
					uint biome = DriftTerrainSampleBiome(state->terra, pos);
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
	TracyCZoneEnd(ZONE_BIOME);
	
	RUN_FUNC(DrawPower, draw);
	RUN_FUNC(DrawOre, draw);
	RUN_FUNC(DrawProjectiles, draw);
	RUN_FUNC(DrawSprites, draw);
	RUN_FUNC(DrawGlowBugs, draw);
	RUN_FUNC(DrawWorkerBugs, draw);
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
	state->players.c.cleanup = DestroyPlayer;

	component_init(state, &state->drones.c, "@Drone", (DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->drones.entity),
		DRIFT_DEFINE_COLUMN(state->drones.data),
	}, 0);

	component_init(state, &state->ore_deposits.c, "@OreDeposit", (DriftColumnSet){
		{DRIFT_DEFINE_COLUMN(state->ore_deposits.entity)},
	}, 256);

	component_init(state, &state->pickups.c, "@Pickup", (DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->pickups.entity),
		DRIFT_DEFINE_COLUMN(state->pickups.type),
	}, 1024);

	component_init(state, &state->power_nodes.c, "@PowerNode", (DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->power_nodes.entity),
		DRIFT_DEFINE_COLUMN(state->power_nodes.node),
	}, 0);
	
	DriftTableInit(&state->power_edges.t, (DriftTableDesc){
		.name = "PowerNodeEdges", .mem = DriftSystemMem,
		.columns.arr = {
			DRIFT_DEFINE_COLUMN(state->power_edges.edge),
		},
	});
	DRIFT_ARRAY_PUSH(state->tables, &state->power_edges.t);
	
	component_init(state, &state->flow_maps[0].c, "@FlowMap", (DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->flow_maps[0].entity),
		DRIFT_DEFINE_COLUMN(state->flow_maps[0].node),
	}, 0);
	
	DriftNameCopy(&state->flow_maps[0].name, "Home");
	state->flow_maps[0].node[0].root_dist = INFINITY;
	
	component_init(state, &state->flow_maps[1].c, "@FlowMap", (DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->flow_maps[1].entity),
		DRIFT_DEFINE_COLUMN(state->flow_maps[1].node),
	}, 0);
	
	DriftNameCopy(&state->flow_maps[1].name, "Player");
	state->flow_maps[1].node[0].root_dist = INFINITY;
	
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
	}}, 1024);
	
	component_init(state, &state->health.c, "@health", (DriftColumnSet){{
		DRIFT_DEFINE_COLUMN(state->health.entity),
		DRIFT_DEFINE_COLUMN(state->health.data),
	}}, 1024);
}
