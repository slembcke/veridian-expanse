/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <string.h>

#include "tracy/TracyC.h"

#include "drift_game.h"

// TODO
DriftEntity TEMP_WAYPOINT_NODE;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	struct {
		u8 flow_map;
		DriftEntity node;
		bool is_valid;
		DriftVec2 target_pos, target_dir;
	}* data;
} DriftComponentNav;

static float spline_ratio(DriftVec2 p0, DriftVec2 v0, DriftVec2 p1, DriftVec2 v1, float speed, float cutoff){
	float dist = DriftVec2Distance(p0, p1) + FLT_MIN;
	float v_avg = 0.5f*(DriftVec2Length(v0) + DriftVec2Length(v1));
	return fmaxf(v_avg/dist, 0.5f*speed/dist);
}

static DriftVec2 hermite_pos(float t, float ratio, DriftVec2 p0, DriftVec2 v0, DriftVec2 p1, DriftVec2 v1){
	t = DriftSaturate(t*ratio);
	DriftVec2 p = DRIFT_VEC2_ZERO;
	p = DriftVec2FMA(p, p0, (( 2*t - 3)*t*t + 1));
	p = DriftVec2FMA(p, v0, (( 1*t - 2)*t*t + t)/ratio);
	p = DriftVec2FMA(p, p1, ((-2*t + 3)*t*t));
	p = DriftVec2FMA(p, v1, (( 1*t - 1)*t*t)/ratio);
	return p;
}

static DriftVec2 hermite_vel(float t, float ratio, DriftVec2 p0, DriftVec2 v0, DriftVec2 p1, DriftVec2 v1){
	t = DriftSaturate(t*ratio);
	DriftVec2 p = DRIFT_VEC2_ZERO;
	p = DriftVec2FMA(p, p0, (( 6*t - 6)*t + 0)*ratio);
	p = DriftVec2FMA(p, v0, (( 3*t - 4)*t + 1));
	p = DriftVec2FMA(p, p1, ((-6*t + 6)*t + 0)*ratio);
	p = DriftVec2FMA(p, v1, (( 3*t - 2)*t + 0));
	return p;
}

static DriftVec2 hermite_acc(float t, float ratio, DriftVec2 p0, DriftVec2 v0, DriftVec2 p1, DriftVec2 v1){
	t = DriftSaturate(t*ratio);
	DriftVec2 p = DRIFT_VEC2_ZERO;
	p = DriftVec2FMA(p, p0, ( 12*t - 6)*ratio*ratio);
	p = DriftVec2FMA(p, v0, (  6*t - 4)*ratio);
	p = DriftVec2FMA(p, p1, (-12*t + 6)*ratio*ratio);
	p = DriftVec2FMA(p, v1, (  6*t - 2)*ratio);
	return p;
}

bool DRIFT_DEBUG_SHOW_PATH = false;
static void vis_path(DriftGameState* state, DriftEntity e, float dt, float desired_speed, float cutoff){
	DriftComponentNav* navs = DRIFT_GET_TYPED_COMPONENT(state, DriftComponentNav);
	uint nav_idx = DriftComponentFind(&navs->c, e);
	if(nav_idx){
		uint body_idx = DriftComponentFind(&state->bodies.c, e);
		DriftVec2 pos = state->bodies.position[body_idx];
		DriftVec2 vel = state->bodies.velocity[body_idx];
		
		DriftVec2 target_pos = navs->data[nav_idx].target_pos;
		DriftVec2 target_vel = DriftVec2Mul(navs->data[nav_idx].target_dir, desired_speed);
		float ratio = spline_ratio(pos, vel, target_pos, target_vel, desired_speed, cutoff);
		DriftVec2 p1 = pos;
		float t = dt;
		for(uint i = 0; i < 100; i++){
			if(t > 1/ratio) break;
			DriftVec2 p1 = hermite_pos(t, ratio, pos, vel, target_pos, target_vel);
			DriftVec2 v1 = hermite_vel(t, ratio, pos, vel, target_pos, target_vel);
			DriftDebugRay(state, target_pos, v1, 0.2f, DRIFT_RGBA8_RED);
			DriftDebugCircle(state, p1, 3, DRIFT_RGBA8_GREEN);
			t += dt;
		}
		
		DriftDebugRay(state, target_pos, vel, 0.2f, DRIFT_RGBA8_GREEN);
		DriftDebugRay(state, target_pos, target_vel, 0.2f, DRIFT_RGBA8_CYAN);
	}
}

static lifft_complex_t PLASMA_SPECTRUM[16] = {
	[0] = {+0.000f, +0.000f}, {-0.107f, +0.037f}, {+0.518f, -0.159f}, {-0.133f, +1.001f},
	[4] = {+1.091f, +0.625f}, {+0.515f, +1.146f}, {+0.699f, -0.841f}, {-0.613f, -0.616f},
	[8] = {-0.292f, +0.577f}, {-0.330f, +0.317f}, {-0.269f, -0.157f}, {-0.071f, -0.193f},
};

float* DriftGenPlasma(DriftDraw* draw){
	float phase = draw->ctx->update_nanos*1e-9f/1.5f;
	float* wave = DRIFT_ARRAY_NEW(draw->mem, DRIFT_PLASMA_N, typeof(*wave));
	
	// Calculate phases.
	lifft_complex_t phases[DRIFT_PLASMA_N/2 + 1] = {};
	for(uint i = 0; i < 16; i++){
		phases[i] = lifft_cmul(PLASMA_SPECTRUM[i], lifft_cispi(-powf(i, 1.5f)*phase));
	}
	
	// Calculate wave and apply cosine window.
	lifft_complex_t scratch[DRIFT_PLASMA_N];
	lifft_inverse_real(phases, 1, wave, 1, scratch, DRIFT_PLASMA_N);
	for(uint i = 0; i < DRIFT_PLASMA_N; i++) wave[i] *= DRIFT_PLASMA_N*sinf(3.14f*i/(DRIFT_PLASMA_N - 1));
	
	return wave;
}

void DriftDrawPlasma(DriftDraw* draw, DriftVec2 start, DriftVec2 end, float* plasma_wave){
	DRIFT_ARRAY_PUSH(draw->plasma_strands, ((DriftSegment){start, end}));
	
	DriftVec4 plasma_color = {{0.12f, 0.35f, 1.00f, 0.2f}};
	DriftVec2 t = DriftVec2Perp(DriftVec2Normalize(DriftVec2Sub(end, start)));
	DriftLight* lights = DRIFT_ARRAY_RANGE(draw->lights, DRIFT_PLASMA_N/2);
	for(uint i = 0; i < DRIFT_PLASMA_N; i += 2){
		DriftVec2 p0 = DriftVec2FMA(DriftVec2Lerp(start, end, (float)i/(DRIFT_PLASMA_N - 1)), t, -plasma_wave[i]);
		*lights++ = ((DriftLight){
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .matrix = {25, 0, 0, 25, p0.x, p0.y},
			.color = DriftVec4Mul(plasma_color, fabsf(plasma_wave[i])*0.12f),
		});
	}
	DriftArrayRangeCommit(draw->lights, lights);
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
	for(uint i = 0; i < 6; i++){
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

static const DriftVec2 nacelle_in = {12, -11}, nacelle_out = {28, -14};

DriftEntity DriftTempPlayerInit(DriftGameState* state, DriftEntity e, DriftVec2 position){
	uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	uint player_idx = DriftComponentAdd(&state->players.c, e);
	DriftPlayerData* player = state->players.data + player_idx;
	player->energy = 0;
	player->headlight = true;
	player->tool_idx = player->tool_select = DRIFT_TOOL_NONE;
	
	uint health_idx = DriftComponentAdd(&state->health.c, e);
	state->health.data[health_idx] = (DriftHealth){
		.value = 0, .maximum = 100, .timeout = 0.4f,
		.hit_sfx = DRIFT_SFX_SHIELD_SPARK, .die_sfx = DRIFT_SFX_EXPLODE,
	};
	
	float radius = DRIFT_PLAYER_SIZE, mass = 10;
	state->bodies.position[body_idx] = position;
	state->bodies.rotation[body_idx] = DriftVec2ForAngle(0.5);
	state->bodies.radius[body_idx] = radius;
	state->bodies.collision_type[body_idx] = DRIFT_COLLISION_PLAYER,
	
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 1/(mass*0.5f*radius*radius);
	
	return e;
}

static void tool_select(DriftUpdate* update, DriftPlayerData* player, u64 button_mask, DriftToolType tool, const char* name){
	if(DriftInputButtonPress(button_mask)){
		if(tool == DRIFT_TOOL_NONE){
			player->tool_select = tool;
		} else if(tool == DRIFT_TOOL_DIG && update->state->inventory.skiff[DRIFT_ITEM_MINING_LASER] == 0){
			DriftAudioPlaySample(DRIFT_BUS_HUD, DRIFT_SFX_DENY, (DriftAudioParams){.gain = 1});
			DriftHudPushToast(update->ctx, 0, DRIFT_TEXT_RED"%s not available", name);
		} else if(player->energy == 0){
			DriftAudioPlaySample(DRIFT_BUS_HUD, DRIFT_SFX_DENY, (DriftAudioParams){.gain = 1});
			DriftHudPushToast(update->ctx, 0, DRIFT_TEXT_RED"%s is unpowered", name);
		} else {
			DriftToolType tool_restrict = update->state->status.tool_restrict;
			if(tool_restrict == DRIFT_TOOL_NONE || tool_restrict == tool) player->tool_select = tool;
		}
	}
}

static void UpdatePlayer(DriftUpdate* update){
	DriftGameState* state = update->state;
	float dt = update->dt;
	float input_smoothing = expf(-20*dt);
	
	// TODO Player respawning really shouldn't go here
	if(
		!DriftEntitySetCheck(&update->state->entities, update->state->player) &&
		DriftInputButtonRelease(DRIFT_INPUT_ACCEPT)
	){
		DriftEntity entity = update->state->player = DriftMakeEntity(state);
		DriftTempPlayerInit(state, entity, state->status.spawn_at_start ? DRIFT_START_POSITION : DRIFT_SKIFF_POSITION);
		
		for(uint i = 0; i < _DRIFT_ITEM_COUNT; i++){
			// Lose half your cargo, but not power nodes.
			if(i == DRIFT_ITEM_POWER_NODE) continue;
			state->inventory.cargo[i] = (state->inventory.cargo[i] + 1)/2;
		}
	}
	
	uint player_idx, transform_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&player_idx, &state->players.c},
		{&transform_idx, &state->transforms.c},
		{},
	});
	
	while(DriftJoinNext(&join)){
		DriftPlayerData* player = state->players.data + player_idx;
		
		if(DriftInputButtonPress(DRIFT_INPUT_LIGHT) && state->inventory.skiff[DRIFT_ITEM_HEADLIGHT]){
			DriftAudioPlaySample(DRIFT_BUS_SFX, DRIFT_SFX_CLICK, (DriftAudioParams){.gain = 1});
			player->headlight = !player->headlight;
		}
		
		u64 grabber_mask = DRIFT_INPUT_GRAB | DRIFT_INPUT_STASH | DRIFT_INPUT_DROP;
		if(DriftInputButtonPress(-1)){
			tool_select(update, player, DRIFT_INPUT_CANCEL, DRIFT_TOOL_NONE, NULL);
			tool_select(update, player, DRIFT_INPUT_FIRE, DRIFT_TOOL_GUN, "Gun");
			tool_select(update, player, grabber_mask, DRIFT_TOOL_GRAB, "Grabber");
			tool_select(update, player, DRIFT_INPUT_SCAN, DRIFT_TOOL_SCAN, "Scanner");
			tool_select(update, player, DRIFT_INPUT_LASER, DRIFT_TOOL_DIG, "Laser");
		}
		
		DriftToolType tool_target = (player->energy >0 && !player->is_overheated ? player->tool_select : DRIFT_TOOL_NONE);
		float tool_anim_target = player->tool_idx == tool_target;
		player->tool_anim = DriftLerpConst(player->tool_anim, tool_anim_target, dt/0.4f);
		if(player->tool_anim == 0){
			player->tool_idx = tool_target;
			// Push the reticle out further to avoid it being really close on tools like the scanner.
			player->reticle = DriftVec2Mul(player->reticle, 200/(DriftVec2Length	(player->reticle) + FLT_MIN));
		}
		
		DriftToolUpdate(update, player, state->transforms.matrix[transform_idx]);
		
		float retract_target = (player->energy == 0 || player->tool_idx == DRIFT_TOOL_DIG ? 0.25f : 1);
		
		DriftAffine transform_r = state->transforms.matrix[transform_idx];
		DriftAffine transform_l = DriftAffineMul(transform_r, (DriftAffine){-1, 0, 0, 1, 0, 0});
		float nacelle_l = DriftTerrainRaymarch(state->terra, DriftAffinePoint(transform_l, nacelle_in), DriftAffinePoint(transform_l, nacelle_out), 4, 2);
		player->nacelle_l = fminf(nacelle_l, DriftLerpConst(player->nacelle_l, retract_target, dt/0.25f));
		
		float nacelle_r = DriftTerrainRaymarch(state->terra, DriftAffinePoint(transform_r, nacelle_in), DriftAffinePoint(transform_r, nacelle_out), 4, 2);
		player->nacelle_r = fminf(nacelle_r, DriftLerpConst(player->nacelle_r, retract_target, dt/0.25f));
		
		DriftAudioParams params = {.loop = true};
		for(uint i = 0; i < 5; i++) params.gain += player->thrusters[i]*player->thrusters[i];
		if(params.gain > 1e-2) DriftImAudioSet(DRIFT_BUS_SFX, DRIFT_SFX_ENGINE, &state->thruster_sampler, params);
		
		if(DRIFT_DEBUG_SHOW_PATH) vis_path(state, join.entity, update->tick_dt, DRIFT_PLAYER_AUTOPILOT_SPEED, 200);
	}
}

uint DriftPlayerEnergyCap(DriftGameState* state){
	return 100;
}

uint DriftPlayerNodeCap(DriftGameState* state){
	if(state->inventory.skiff[DRIFT_ITEM_NODES_L3]) return 20;
	if(state->inventory.skiff[DRIFT_ITEM_NODES_L2]) return 15;
	return 10;
}

uint DriftPlayerCargoCap(DriftGameState* state){
	if(state->inventory.skiff[DRIFT_ITEM_CARGO_L3]) return 200;
	if(state->inventory.skiff[DRIFT_ITEM_CARGO_L2]) return 150;
	return 100;
}

uint DriftPlayerItemCount(DriftGameState* state, DriftItemType item){
	return state->inventory.cargo[item] + state->inventory.transit[item] + state->inventory.skiff[item];
}

uint DriftPlayerItemCap(DriftGameState* state, DriftItemType item){
	uint limit = DRIFT_ITEMS[item].limit;
	if(state->inventory.skiff[DRIFT_ITEM_STORAGE_L3]) return 2*limit/1;
	if(state->inventory.skiff[DRIFT_ITEM_STORAGE_L2]) return 3*limit/2;
	return limit;
}

uint DriftPlayerCalculateCargo(DriftGameState* state){
	uint sum = 0;
	for(uint i = 0; i < _DRIFT_ITEM_COUNT; i++) sum += state->inventory.cargo[i]*DRIFT_ITEMS[i].mass;
	return sum;
}

#define RCS_ROWS 5

static void TickPlayer(DriftUpdate* update){
	DriftGameState* state = update->state;	
	float tick_dt = update->tick_dt;
	float rcs_damping = expf(-20*tick_dt);
	float rcs_smoothing = expf(-30*tick_dt);
	
	float w_bias = 5;
	// TODO cache this and the inverse if it's going to be static.
	DriftVec3 rcs_matrix[RCS_ROWS] = {
		// {{0.0e0f, 1.2e3f,  0.0e1f*w_bias}}, // main
		{{0.0e0f, 0.0e0f,  0.0e0f*w_bias}}, // main
		{{1.2e3f, 0.0e0f,  4.0e1f*w_bias}}, // right x
		{{0.0e0f, 1.2e3f, -8.0e1f*w_bias}}, // right y
		{{1.2e3f, 0.0e0f,  4.0e1f*w_bias}}, // left x
		{{0.0e0f, 1.2e3f,  8.0e1f*w_bias}}, // left y
	};
	
	DriftVec3 rcs_inverse[RCS_ROWS];
	DriftPseudoInverseNx3(rcs_matrix, rcs_inverse, RCS_ROWS);
	
	DriftComponentNav* navs = DRIFT_GET_TYPED_COMPONENT(state, DriftComponentNav);
	
	uint player_idx, transform_idx, body_idx, health_idx, nav_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&player_idx, &state->players.c},
		{&body_idx, &state->bodies.c},
		{&transform_idx, &state->transforms.c},
		{&health_idx, &state->health.c},
		{&nav_idx, &navs->c, .optional = true},
		{},
	});
	
	while(DriftJoinNext(&join)){
		DriftPlayerData* player = &state->players.data[player_idx];
		DriftVec2 player_pos = state->bodies.position[body_idx];
		DriftVec2 player_vel = state->bodies.velocity[body_idx];
		
		DriftVec2 desired_velocity = player->desired_velocity;
		DriftVec2 desired_rotation = player->desired_rotation;

		DriftAffine m = state->transforms.matrix[transform_idx];
		DriftAffine m_inv = DriftAffineInverse(m);
		
		// TODO temporary pathing stuff
		bool do_nav = (
			TEMP_WAYPOINT_NODE.id != 0 &&
			DriftVec2Length(DriftInputJoystick(DRIFT_INPUT_AXIS_MOVE_X, DRIFT_INPUT_AXIS_MOVE_Y)) < 0.25f
		);
		if(do_nav){
			DriftVec2 nav_pos = state->power_nodes.position[DriftComponentFind(&state->power_nodes.c, TEMP_WAYPOINT_NODE)];
			do_nav &= !DriftVec2Near(player_pos, nav_pos, 5);
		}
			
		if(do_nav && nav_idx == 0){
			nav_idx = DriftComponentAdd(&navs->c, state->player);
			navs->data[nav_idx].flow_map = DRIFT_FLOW_MAP_WAYPOINT;
		} else if(!do_nav && nav_idx != 0){
			DriftComponentRemove(&navs->c, state->player);
			TEMP_WAYPOINT_NODE = (DriftEntity){};
		}
		
		if(nav_idx && navs->data[nav_idx].is_valid){
			float speed = DRIFT_PLAYER_AUTOPILOT_SPEED;
			DriftVec2 target_pos = navs->data[nav_idx].target_pos;
			DriftVec2 target_vel = DriftVec2Mul(navs->data[nav_idx].target_dir, speed);
			
			if(DriftVec2Length(target_vel)){
				float ratio = spline_ratio(player_pos, player_vel, target_pos, target_vel, speed, 120);
				desired_velocity = hermite_vel(tick_dt, ratio, player_pos, player_vel, target_pos, target_vel);
				DriftVec2 thrust = hermite_acc(tick_dt, ratio, player_pos, player_vel, target_pos, target_vel);
				desired_rotation = DriftVec2Normalize(DriftVec2FMA(desired_velocity, thrust, 0.05f));
			} else {
				DriftVec2 delta = DriftVec2Sub(target_pos, player_pos);
				desired_velocity = DriftVec2Mul(delta, speed/fmaxf(150, DriftVec2Length(delta)));
				desired_rotation = DriftVec2Normalize(delta);
			}
		}
		
		if(player->energy == 0 || player->tool_idx == DRIFT_TOOL_DIG){
			desired_velocity = DriftVec2Clamp(desired_velocity, 0.4f*DRIFT_PLAYER_SPEED);
		}
		
		// Calculate the change in body relative velocity to hit the desired.
		DriftVec2 delta_v = DriftVec2Sub(desired_velocity, player_vel);
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
		impulse = DriftRCS(rcs_matrix, rcs_inverse, impulse, thrusters, RCS_ROWS);
		state->bodies.velocity[body_idx] = DriftVec2Add(player_vel, DriftAffineDirection(m, (DriftVec2){impulse.x*tick_dt, impulse.y*tick_dt}));
		state->bodies.angular_velocity[body_idx] += impulse.z*tick_dt/w_bias;
		
		// Smooth out the RCS impulse
		for(uint i = 0; i < 5; i++) player->thrusters[i] = DriftLerp(thrusters[i], player->thrusters[i], rcs_smoothing);
		
		// Update shield/power/heat
		DriftHealth* health = state->health.data + health_idx;
		if(update->ctx->debug.godmode){
			player->is_powered = true;
			player->energy = DriftPlayerEnergyCap(state);
			player->power_tick0 = update->tick;
			player->is_overheated = false;
			player->temp = 0;
			health->value = health->maximum;
			player->shield_tick0 = update->tick;
		} else {
			float shield_rate = -10;
			if(player->energy > 0){
				bool waiting_to_recharge = update->tick - health->damage_tick0 < 4*DRIFT_TICK_HZ;
				shield_rate = (waiting_to_recharge ? 0 : 30);
			}
			if(state->inventory.skiff[DRIFT_ITEM_SHIELD_L2]) health->maximum = 200;
			float prev_health = health->value, max = health->maximum;
			health->value = DriftClamp(health->value + shield_rate*tick_dt, 0, max);
			if(health->value > 0) player->shield_tick0 = update->tick;
			if(prev_health < max && health->value == max){
				DriftAudioPlaySample(DRIFT_BUS_HUD, DRIFT_SFX_SHIELD_NOTIFY, (DriftAudioParams){.gain = 1.0f});
			}
			
			float power_draw = 5;
			if(player->headlight) power_draw += 5;
			if(player->is_digging) power_draw += 20;
			player->energy = fmaxf(player->energy - power_draw*tick_dt, 0);

			DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(state, player_pos, update->mem, 0);
			player->is_powered = info.player_can_connect;
			if(player->is_powered){
				uint cap = DriftPlayerEnergyCap(state);
				player->energy = fminf(player->energy + cap*tick_dt/1, cap);
				player->power_tick0 = update->tick;
			}
			
			if(player->is_digging) player->temp += 0.3f*tick_dt;
			if(player->temp >= 1) player->is_overheated = true;
			if(player->temp == 0) player->is_overheated = false;
			
			static const float HEAT_RATE[_DRIFT_BIOME_COUNT] = {
				[DRIFT_BIOME_LIGHT] = -0.20f,
				[DRIFT_BIOME_RADIO] = 0.30f,
				[DRIFT_BIOME_CRYO] = -0.45f,
				[DRIFT_BIOME_DARK] = -0.20f,
				[DRIFT_BIOME_SPACE] = -0.20f,
			};
			
			uint biome = DriftTerrainSampleBiome(state->terra, player_pos).idx;
			player->temp = DriftSaturate(player->temp + HEAT_RATE[biome]*tick_dt);
		}
	}
}

static DriftAffine NacelleMatrix(DriftVec2 dir, DriftVec2 offset){
	float mag = DriftVec2Length(dir);
	dir = (mag > 0.1f ? DriftVec2Mul(dir, 1/mag) : (DriftVec2){0, 1});
	return (DriftAffine){dir.y, -dir.x, dir.x, dir.y, offset.x, offset.y};
}

static DriftAffine FlameMatrix(float mag){
	return (DriftAffine){0.25f*mag + 0.25f, 0, 0, 0.5f*mag + 0.25f, 0, -4.0f*DriftSaturate(mag)};
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
				
		DriftAffine matrix_strut_l = DriftAffineMul(matrix_model_l, DriftAffineTRS(nacelle_offset_l, 0.8f*retract_l, (DriftVec2){1, 1}));
		DriftAffine matrix_strut_r = DriftAffineMul(matrix_model_r, DriftAffineTRS(nacelle_offset_r, 0.8f*retract_r, (DriftVec2){1, 1}));
		DriftAffine matrix_nacelle_l = DriftAffineMul(matrix_model_l, NacelleMatrix(thrust_l, nacelle_offset_l));
		DriftAffine matrix_nacelle_r = DriftAffineMul(matrix_model_r, NacelleMatrix(thrust_r, nacelle_offset_r));
		DriftAffine matrix_bay_l = DriftAffineTRS((DriftVec2){-3*retract_l, 3*retract_l}, 0, (DriftVec2){1, -1});
		DriftAffine matrix_bay_r = DriftAffineTRS((DriftVec2){-3*retract_r, 3*retract_r}, 0, (DriftVec2){1, -1});
		DriftAffine matrix_flame_light = (DriftAffine){128, 0, 0, -64, 0, 0};
		
		DriftAffine matrix_flame_l = FlameMatrix(thrust_mag_l);
		DriftAffine matrix_flame_r = FlameMatrix(thrust_mag_r);
		uint flame_frame = DRIFT_SPRITE_FLAME0 + draw->frame%_DRIFT_SPRITE_FLAME_COUNT;
		
		const DriftVec4 flame_glow = (DriftVec4){{1.14f, 0.78f, 0.11f, 1.00f}};
		const DriftRGBA8 color = {0xFF, 0xFF, 0xFF, 0xFF};
		const DriftRGBA8 glow = {0xC0, 0xC0, 0xC0, 0x80};
		const DriftAffine matrix_nav = {22, 27, -35, 16, 0*-8, 0*3};
		
		
		DriftSprite* sprites = DRIFT_ARRAY_RANGE(draw->fg_sprites, 64);
		DriftLight* lights = DRIFT_ARRAY_RANGE(draw->lights, 64);
		
		DriftSpritePush(&sprites, DRIFT_SPRITE_STRUT, color, matrix_strut_l);
		DriftSpritePush(&sprites, flame_frame, glow, DriftAffineMul(matrix_nacelle_l, matrix_flame_l));
		DriftSpritePush(&sprites, DRIFT_SPRITE_NACELLE, color, matrix_nacelle_l);
		DriftLightPush(&lights, DRIFT_SPRITE_LIGHT_HEMI, DriftVec4Mul(flame_glow, thrust_mag_l), DriftAffineMul(matrix_nacelle_l, matrix_flame_light), 0);
		DriftLightPush(&lights, DRIFT_SPRITE_LIGHT_FLOOD, (DriftVec4){{0.29f, 0.04f, 0.04f, 0.00f}}, DriftAffineMul(matrix_strut_l, matrix_nav), 0);
		
		DriftSpritePush(&sprites, DRIFT_SPRITE_STRUT, color, matrix_strut_r);
		DriftSpritePush(&sprites, flame_frame, glow, DriftAffineMul(matrix_nacelle_r, matrix_flame_r));
		DriftSpritePush(&sprites, DRIFT_SPRITE_NACELLE, color, matrix_nacelle_r);
		DriftLightPush(&lights, DRIFT_SPRITE_LIGHT_HEMI, DriftVec4Mul(flame_glow, thrust_mag_r), DriftAffineMul(matrix_nacelle_r, matrix_flame_light), 0);
		DriftLightPush(&lights, DRIFT_SPRITE_LIGHT_FLOOD, (DriftVec4){{0.06f, 0.25f, 0.04f, 0.00f}}, DriftAffineMul(matrix_strut_r, matrix_nav), 0);
		
		DriftSpritePush(&sprites, DRIFT_SPRITE_HATCH, color, DriftAffineMul(matrix_model_l, matrix_bay_l));
		DriftSpritePush(&sprites, DRIFT_SPRITE_HATCH, color, DriftAffineMul(matrix_model_r, matrix_bay_r));
		DriftSpritePush(&sprites, DRIFT_SPRITE_HULL, color, matrix_model);
		
		DriftVec4 dim_glow = (DriftVec4){{0.01f, 0.01f, 0.01f, 10.00f}};
		DriftLightPush(&lights, DRIFT_SPRITE_LIGHT_RADIAL, dim_glow, DriftAffineMul(matrix_model, (DriftAffine){90, 0, 0, 90, 0, 0}), 0);
		
		float headlight_brightness = player->energy/DriftPlayerEnergyCap(state);
		static DriftRandom rand[1]; // TODO static global
		{ // Draw shield
			DriftHealth* health = state->health.data + health_idx;
			float value = health->value/health->maximum;
			float timeout = (draw->tick - player->shield_tick0)/(0.10f*DRIFT_TICK_HZ);
			if((0 < value && value < 1) || timeout < 1){ // Draw a normal shield bubble.
				float shield_fade = powf(1 - value, 0.2f);
				DriftVec4 color = {{shield_fade*shield_fade*shield_fade, value*shield_fade, value*value*value*shield_fade, 0}};
				DriftVec2 rot = DriftVec2Mul(DriftRandomOnUnitCircle(rand), 0.5f);
				DriftAffine m = DriftAffineMul(matrix_model, (DriftAffine){rot.x, rot.y, -rot.y, rot.x});
				
				color = DriftVec4Mul(color, 1 + timeout*(4 - 5*timeout));
				float scale = DriftLogerp(1, 3, timeout*timeout*timeout);
				m = DriftAffineMul(m, (DriftAffine){scale, 0, 0, scale});
				
				DriftSpritePush(&sprites, DRIFT_SPRITE_SHIELD, DriftRGBA8FromColor(color), m);
				DriftLightPush(&lights, DRIFT_SPRITE_LIGHT_RADIAL, color, DriftAffineMul(m, (DriftAffine){96, 0, 0, 96, 0, 0}), 0);
				
				DriftVec4 flash_color = (DriftVec4){{0.26f, 0.88f, 0.99f, 0.00f}};
				float flash_fade = fminf(1, (draw->tick - health->damage_tick0)/(0.25f*DRIFT_TICK_HZ));
				flash_color = DriftVec4Mul(flash_color, 1 - flash_fade*flash_fade);
				DriftSpritePush(&sprites, DRIFT_SPRITE_SHIELD_FLASH, DriftRGBA8FromColor(flash_color), matrix_model);
				
				headlight_brightness *= DriftSaturate((draw->tick - health->damage_tick0)/(0.9f*DRIFT_TICK_HZ) + 0.08f*sinf(draw->tick));
			} else { // Draw the flickering for a failed shield.
				uint period = 30, tick = draw->tick;
				float fade = 1 - (0.5f + 0.5f*cosf(tick))*(tick%period)/(float)period;
				DriftVec4 color = {{0, fade*fade*0.20f, fade*fade*0.40f, 0}};
				
				float flash_phase = (tick/period)*(float)(2*M_PI/DRIFT_PHI);
				DriftVec2 frot = DriftVec2ForAngle(flash_phase);
				DriftAffine m = DriftAffineMul(matrix_model, (DriftAffine){frot.x, frot.y, -frot.y, frot.x});
				DriftSpritePush(&sprites, DRIFT_SPRITE_SHIELD_FLASH, DriftRGBA8FromColor(color), m);
				DriftLightPush(&lights, DRIFT_SPRITE_LIGHT_RADIAL, color, DriftAffineMul(matrix_model, (DriftAffine){96, 0, 0, 96, 8*frot.x, 8*frot.y}), 0);
			}
		}
		
		// Draw headlight.
		if(player->headlight && state->inventory.skiff[DRIFT_ITEM_HEADLIGHT]){
			DriftVec4 headlight_color = DriftVec4Mul((DriftVec4){{2, 2, 2, 1.00f}}, headlight_brightness);
			DriftLightPush(&lights, DRIFT_SPRITE_LIGHT_HEMI, headlight_color, DriftAffineMul(matrix_model, (DriftAffine){200, 0, 0, 300, 0, 4}), 10);
		}
		
		DriftArrayRangeCommit(draw->fg_sprites, sprites);
		DriftArrayRangeCommit(draw->lights, lights);
		
		DriftVec2 pos = DriftAffineOrigin(matrix_model);
		
		// Draw power beam
		DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(state, pos, draw->mem, 0);
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
			
			DriftDrawPlasma(draw, near_pos, pos, DriftGenPlasma(draw));
		}
	}
}

bool DriftCheckSpawn(DriftUpdate* update, DriftVec2 pos, float terrain_dist){
	float dist = DriftTerrainSampleCoarse(update->state->terra, pos).dist;
	bool open_space = terrain_dist < dist && dist < 64;
	bool on_screen = DriftAffineVisibility(update->prev_vp_matrix, pos, DRIFT_VEC2_ZERO);
	return !on_screen && open_space;
}

DriftNearbyNodesInfo DriftSystemPowerNodeNearby(DriftGameState* state, DriftVec2 pos, DriftMem* mem, float beam_radius){
	uint connect_count = 0, near_count = 0;
	DriftNearbyNodesInfo info = {.pos = pos, .nodes = DRIFT_ARRAY_NEW(mem, 8, DriftNearbyNodeInfo)};
	
	DRIFT_COMPONENT_FOREACH(&state->power_nodes.c, i){
		DriftVec2 node_pos = state->power_nodes.position[i];
		float dist = DriftVec2Distance(pos, node_pos);
		if(dist < DRIFT_POWER_EDGE_MAX_LENGTH){
			bool active = state->power_nodes.active[i];
			info.active_count += active;
			
			float t = DriftTerrainRaymarch(state->terra, node_pos, pos, beam_radius, 1);
			bool unblocked = t == 1;
			near_count += unblocked;
			
			bool is_too_close = dist < DRIFT_POWER_EDGE_MIN_LENGTH && active;
			info.too_close_count += is_too_close;
			
			bool node_can_connect = unblocked && !is_too_close;
			connect_count += node_can_connect;
			
			info.player_can_connect |= unblocked && active;
			DRIFT_ARRAY_PUSH(info.nodes, ((DriftNearbyNodeInfo){
				.e = state->power_nodes.entity[i], .pos = node_pos, .player_can_connect = unblocked && active,
				.node_can_connect = node_can_connect, .is_too_close = is_too_close, .blocked_at = t,
			}));
		}
	}
	
	info.node_can_connect = connect_count > 0 && info.too_close_count == 0;
	info.node_can_reach = near_count > 0;
	return info;
}

#define DRIFT_JOIN(__join_var__, __joins__)for(DriftJoin __join_var__ = DriftJoinMake(__joins__)

static void flow_map_tick(DriftUpdate* update, DriftFlowMapID fmap_id){
	TracyCZoneN(ZONE_FLOW, "Flow Tick", true);
	DriftGameState* state = update->state;
	DriftComponentPowerNode* power_nodes = 	&state->power_nodes;
	DriftComponentFlowMap* fmap = state->flow_maps + fmap_id;
	
	// Buffer the flow map nodes.
	TracyCZoneN(ZONE_BUFFER, "buffer", true);
	DriftFlowNode* copy = DRIFT_ARRAY_NEW(update->mem, power_nodes->c.table.row_count, typeof(*copy));
	DRIFT_COMPONENT_FOREACH(&power_nodes->c, node_idx){
		DriftEntity e = power_nodes->entity[node_idx];
		// TODO can't components on secondary threads.
		uint flow_idx = DriftComponentFind(&fmap->c, e) ?: DriftComponentAdd(&fmap->c, e);
		copy[flow_idx] = fmap->flow[flow_idx];
	}
	TracyCZoneEnd(ZONE_BUFFER);
	
	// Propagate new links.
	TracyCZoneN(ZONE_PROPAGATE, "propagate", true);
	uint stamp = fmap->stamp++;
	DriftTablePowerNodeEdges* edges = &state->power_edges;
	for(uint i = 0, count = edges->t.row_count; i < count; i++){
		DriftPowerNodeEdge* edge = edges->edge + i;
		uint idx0 = DriftComponentFind(&fmap->c, edge->e0);
		uint idx1 = DriftComponentFind(&fmap->c, edge->e1);
		
		if(idx0 && idx1){
			float len = DriftVec2Distance(edge->p0, edge->p1);
			// Weight by distance and their current age.
			float d0 = copy[idx0].dist + 64*(stamp - copy[idx0].stamp);
			float d1 = copy[idx1].dist + 64*(stamp - copy[idx1].stamp);
			// Check both nodes connected by the link and propage values.
			if(d0 > d1 + len) copy[idx0] = (DriftFlowNode){.next = edge->e1, .stamp = fmap->flow[idx1].stamp, .dist = fmap->flow[idx1].dist + len};
			if(d1 > d0 + len) copy[idx1] = (DriftFlowNode){.next = edge->e0, .stamp = fmap->flow[idx0].stamp, .dist = fmap->flow[idx0].dist + len};
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
		// A node is valid if it's stamp has been updated.
		// This can have false positives, but is consistent after a short delay.
		fmap->is_valid[flow_idx] = copy[flow_idx].stamp != fmap->flow[flow_idx].stamp;
		// Zero out links to non-existant nodes.
		if(!node_idx) copy[flow_idx].next = (DriftEntity){0};
		// Copy back to the nodes.
		fmap->flow[flow_idx] = copy[flow_idx];
	}
	TracyCZoneEnd(ZONE_WRITE);
	
	if(fmap_id == 0){
		TracyCZoneN(ZONE_SYNC, "sync", true);
		// Set power nodes as active if flow0 is current.
		DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
			{.component = &fmap->c, .variable = &flow_idx},
			{.component = &power_nodes->c, .variable = &node_idx},
			{},
		});
		while(DriftJoinNext(&join)) power_nodes->active[node_idx] = fmap->is_valid[flow_idx];
		
		// Enforce e0 is closer than e1, swap if necessary.
		for(uint i = 0, count = edges->t.row_count; i < count; i++){
			DriftPowerNodeEdge* edge = edges->edge + i;
			uint idx0 = DriftComponentFind(&fmap->c, edge->e0);
			uint idx1 = DriftComponentFind(&fmap->c, edge->e1);
			if(copy[idx1].dist < copy[idx0].dist) *edge = (DriftPowerNodeEdge){.e0 = edge->e1, .e1 = edge->e0, .p0 = edge->p1, .p1 = edge->p0};
		}
		TracyCZoneEnd(ZONE_SYNC);
	}
	TracyCZoneEnd(ZONE_FLOW);
}

static void flow_map_job(tina_job* job){
	DriftUpdate* update = tina_job_get_description(job)->user_data;
	uint fmap_idx = tina_job_get_description(job)->user_idx;
	flow_map_tick(update, fmap_idx);
}

static void TickFlows(DriftUpdate* update){
	for(uint i = 0; i < _DRIFT_FLOW_MAP_COUNT; i++) flow_map_tick(update, i);
	
	DriftGameState* state = update->state;
	
	{ // Update power roots
		state->power_nodes.active[1] = true;
		DriftComponentFlowMap* fmap = state->flow_maps + DRIFT_FLOW_MAP_POWER;
		fmap->flow[1].stamp = fmap->stamp;
		fmap->is_valid[1] = true;
	}
	
	{ // Update player path roots
		DriftComponentFlowMap* fmap = state->flow_maps + DRIFT_FLOW_MAP_PLAYER;
		uint body_idx = DriftComponentFind(&state->bodies.c, update->state->player);
		DriftVec2 player_pos = state->bodies.position[body_idx];
		
		DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(state, player_pos, update->mem, 0);
		DRIFT_ARRAY_FOREACH(info.nodes, node){
			if(node->blocked_at < 1) continue;
			
			uint fnode_idx = DriftComponentFind(&fmap->c, node->e);
			DriftFlowNode* fnode = fmap->flow + fnode_idx;
			DriftVec2 p1 = state->power_nodes.position[DriftComponentFind(&state->power_nodes.c, node->e)];
			fnode->stamp = fmap->stamp;
			fnode->dist = DriftVec2Distance(player_pos, p1);
			fnode->next = node->e;
			fmap->is_valid[fnode_idx] = true;
		}
	}
	
	{ // Update waypoint
		DriftComponentFlowMap* fmap = state->flow_maps + DRIFT_FLOW_MAP_WAYPOINT;
		uint fnode_idx = DriftComponentFind(&fmap->c, TEMP_WAYPOINT_NODE);
		if(fnode_idx){
			fmap->flow[fnode_idx].stamp = fmap->stamp;
			fmap->flow[fnode_idx].dist = 0;
			fmap->flow[fnode_idx].next = TEMP_WAYPOINT_NODE;
			fmap->is_valid[fnode_idx] = true;
		}
	}
}

static void DrawPower(DriftDraw* draw){
	static const DriftRGBA8 node_color = {0xC0, 0xC0, 0xC0, 0xFF};
	
	DriftGameState* state = draw->state;
	DriftPowerNodeEdge* edges = state->power_edges.edge;
	DriftFrame light_frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL];
	float* plasma_wave = DriftGenPlasma(draw);
	
	DriftComponentFlowMap* fmap = state->flow_maps + 0;
	float smoothing = expf(-10.0f*draw->dt);
	
	DRIFT_COMPONENT_FOREACH(&state->power_nodes.c, idx){
		DriftVec2 pos = state->power_nodes.position[idx];
		if(!DriftAffineVisibility(draw->vp_matrix, pos, (DriftVec2){64, 64})) continue;
			
		DriftVec2 rot = state->power_nodes.rotation[idx];
		DriftAffine m = {rot.y, -rot.x, rot.x, rot.y, pos.x, pos.y};
		
		float clam = state->power_nodes.clam[idx];
		DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_POWER_NODE_CORE], .color = DRIFT_RGBA8_WHITE,
			.matrix = DriftAffineMul(m, (DriftAffine){1, 0, 0, 1, 0, 2*clam}),
		}));
		DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_POWER_NODE_SHELL], .color = DRIFT_RGBA8_WHITE,
			.matrix = DriftAffineMul(m, (DriftAffine){+1, 0, 0, 1, +2*clam, 0}),
		}));
		DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_POWER_NODE_SHELL], .color = DRIFT_RGBA8_WHITE,
			.matrix = DriftAffineMul(m, (DriftAffine){-1, 0, 0, 1, -2*clam, 0}),
		}));
		
		DriftEntity e = state->power_nodes.entity[idx];
		unsigned flow_idx = DriftComponentFind(&fmap->c, e);
		DriftFlowNode* flow = fmap->flow + flow_idx;
		
		float pulse = powf(0.5f + 0.5f*DriftWaveComplex(draw->update_nanos - (u64)(10e5f*flow->dist), 0.8f).x, 5);
		float dimming = clam*(5*pulse + 1);
		DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
			.frame = light_frame, .color = DriftVec4Mul((DriftVec4){{0.08f, 0.20f, 0.28f, 0.00f}}, dimming),
			.matrix = {100, 0, 0, 100, pos.x, pos.y},
		}));
		
		if(state->power_nodes.active[idx]){
			DriftEntity next_e = flow->next;
			if(e.id != next_e.id){
				DriftVec2 next_pos = state->power_nodes.position[DriftComponentFind(&state->power_nodes.c, next_e)];
				DriftVec2 dir = DriftVec2Normalize(DriftVec2Sub(next_pos, pos));
				rot = state->power_nodes.rotation[idx] = DriftVec2Normalize(DriftVec2Lerp(dir, rot, smoothing));
				state->power_nodes.clam[idx] = DriftLerpConst(state->power_nodes.clam[idx], 1, draw->dt/0.5f);
			}
		} else {
			state->power_nodes.clam[idx] = DriftLerpConst(clam, 0, draw->dt/0.5f);
		}
	}
	
	DriftRGBA8 flicker_color = DriftRGBA8FromColor(DriftVec4Mul((DriftVec4){{0.02f, 0.11f, 0.22f, 0.21f}}, 0.3f*plasma_wave[DRIFT_PLASMA_N/2]));
	// Draw power links. (scale model matrix by 1/2 to avoid divide in center and extents)
	DriftAffine mvp = DriftAffineMul(draw->vp_matrix, (DriftAffine){0.5f, 0, 0, 0.5f, 0, 0});
	for(uint idx = 0; idx < state->power_edges.t.row_count; idx++){
		DriftPowerNodeEdge edge = edges[idx];
		if(DriftAffineVisibility(mvp, DriftVec2Add(edge.p0, edge.p1), DriftVec2Sub(edge.p0, edge.p1))){
			uint node_idx0 = DriftComponentFind(&state->power_nodes.c, edge.e0);
			if(state->power_nodes.active[node_idx0]){
				DriftDrawPlasma(draw, edge.p0, edge.p1, plasma_wave);
			} else {
				DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){
					.p0 = edge.p0, .p1 = edge.p1, .radii = {0.8f}, .color = flicker_color
				}));
			}
		}
	}
}

void DriftDrawPowerMap(DriftDraw* draw, float scale){
	static const DriftRGBA8 beam_color = {0x00, 0xB0, 0xB0, 0xC0}, node_color = {0xFF, 0x80, 0x00, 0xFF};
	
	DriftGameState* state = draw->state;
	DriftPowerNodeEdge* edges = state->power_edges.edge;
	bool* node_active = state->power_nodes.active;
	for(uint i = 0; i < state->power_edges.t.row_count; i++){
		bool active = node_active[DriftComponentFind(&state->power_nodes.c, edges[i].e0)];
		DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){
			.p0 = edges[i].p0, .p1 = edges[i].p1, .radii = {2.0f*scale}, .color = DriftRGBA8Fade(beam_color, active ? 1.0f : 0.25f),
		}));
	}
	
	DriftVec2* node_pos = state->power_nodes.position;
	DRIFT_COMPONENT_FOREACH(&state->power_nodes.c, i){
		DriftVec2 pos = state->power_nodes.position[i];
		DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){
			.p0 = pos, .p1 = pos, .radii = {5*scale}, .color = DriftRGBA8Fade(node_color, node_active[i] ? 1.0f : 0.25f),
		}));
	}
}

static void TickNavs(DriftUpdate* update){
	DriftGameState* state = update->state;
	DriftComponentNav* nav_component = DRIFT_GET_TYPED_COMPONENT(state, DriftComponentNav);
	DRIFT_VAR(navs, nav_component->data);
	
	uint nav_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&nav_idx, &nav_component->c},
		{&body_idx, &state->bodies.c},
		{},
	});
	
	while(DriftJoinNext(&join)){
		DriftComponentFlowMap* fmap = state->flow_maps + navs[nav_idx].flow_map;
		
		DriftEntity node = navs[nav_idx].node;
		DriftVec2 pos0 = state->bodies.position[body_idx];
		
		uint node_idx = DriftComponentFind(&state->power_nodes.c, node);
		if(node_idx == 0){
			DRIFT_LOG("attempting to find new node");
			
			// Target node does not exist. Try to find a new one.
			DriftNearbyNodesInfo info = DriftSystemPowerNodeNearby(state, pos0, update->mem, 0);
			float min_dist = INFINITY;
			DRIFT_ARRAY_FOREACH(info.nodes, inode){
				float root_dist = fmap->flow[DriftComponentFind(&fmap->c, inode->e)].dist;
				if(inode->blocked_at == 1 && root_dist < min_dist){
					min_dist = root_dist;
					node = inode->e;
				}
			}
			
			node_idx = DriftComponentFind(&state->power_nodes.c, node);
			navs[nav_idx].node = node;
		}
		
		DriftVec2 node_pos = navs[nav_idx].target_pos = state->power_nodes.position[node_idx];
		uint fnode_idx = DriftComponentFind(&fmap->c, node);
		navs[nav_idx].is_valid = fmap->is_valid[fnode_idx];
		
		DriftEntity next_node = fmap->flow[fnode_idx].next;
		uint next_idx = DriftComponentFind(&state->power_nodes.c, next_node);
		DriftVec2 next_pos = state->power_nodes.position[next_idx];
		float d0 = DriftVec2Distance(pos0, node_pos), d1 = DriftVec2Distance(pos0, next_pos);
		if(d0 < DRIFT_POWER_EDGE_MIN_LENGTH || d1 < d0) navs[nav_idx].node = next_node;
			
		if(node.id != next_node.id){
			float dsum = d0 + DriftVec2Distance(node_pos, next_pos);
			navs[nav_idx].target_dir = DriftVec2Mul(DriftVec2Sub(next_pos, pos0), 1/(dsum + FLT_MIN));
		} else {
			navs[nav_idx].target_dir = DRIFT_VEC2_ZERO;
		}
	}
}

#define DRIFT_DRONE_SPEED (DRIFT_PLAYER_SPEED)

DriftEntity DriftDroneMake(DriftGameState* state, DriftVec2 pos, DriftDroneState drone_state, DriftItemType item, uint count){
	DriftEntity e = DriftMakeEntity(state);
	uint drone_idx = DriftComponentAdd(&state->drones.c, e);
	state->drones.data[drone_idx].state = drone_state;
	state->drones.data[drone_idx].item = item;
	state->drones.data[drone_idx].count = count;
	
	uint transform_idx = DriftComponentAdd(&state->transforms.c, e);
	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	state->bodies.position[body_idx] = pos;
	
	float radius = 7, mass = 2.0f;
	state->bodies.radius[body_idx] = radius;
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 1/(mass*0.5f*radius*radius);
	state->bodies.collision_type[body_idx] = DRIFT_COLLISION_PLAYER_DRONE;
	
	return e;
}

static const DriftFlowMapID DRONE_FLOW[_DRIFT_DRONE_STATE_COUNT] = {
	[DRIFT_DRONE_STATE_TO_POD] = DRIFT_FLOW_MAP_PLAYER,
	[DRIFT_DRONE_STATE_TO_SKIFF] = DRIFT_FLOW_MAP_POWER,
};

static void TickDrones(DriftUpdate* update){
	DriftGameState* state = update->state;
	float tick_dt = update->tick_dt;
	
	uint pod_requests = 0;
	for(uint i = 0; i < _DRIFT_ITEM_COUNT; i++){
		if(i != DRIFT_ITEM_POWER_NODE) pod_requests += state->inventory.cargo[i];
	}
	
	uint node_cargo = state->inventory.cargo[DRIFT_ITEM_POWER_NODE];
	uint node_skiff = state->inventory.skiff[DRIFT_ITEM_POWER_NODE];
	uint node_cap = node_cap = DriftPlayerNodeCap(state);
	uint nodes_wanted = node_cap - node_cargo;
	if(nodes_wanted > node_skiff) nodes_wanted = node_skiff;
	if(node_cargo < node_cap && nodes_wanted > pod_requests){
		pod_requests = nodes_wanted;
	}
	
	if(state->dispatch.count < state->inventory.skiff[DRIFT_ITEM_DRONE] && state->dispatch.pod < pod_requests){
		DriftItemType item = DRIFT_ITEM_NONE; uint count = 0;
		if(nodes_wanted) item = DRIFT_ITEM_POWER_NODE, count = 1;
		
		state->inventory.skiff[item] -= count;
		state->inventory.transit[item] += count;
		DriftDroneMake(state, DRIFT_SKIFF_POSITION, DRIFT_DRONE_STATE_TO_POD, item, count);
		state->dispatch.count++;
		state->dispatch.pod++;
	}
	
	uint player_body_idx = DriftComponentFind(&state->bodies.c, state->player);
	DriftVec2 player_pos = state->bodies.position[player_body_idx];
	DriftVec2 player_vel = state->bodies.velocity[player_body_idx];
	const float action_radius = 10;
	
	DriftComponentNav* navs = DRIFT_GET_TYPED_COMPONENT(state, DriftComponentNav);
	
	uint drone_idx, transform_idx, body_idx, nav_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&drone_idx, &state->drones.c},
		{&body_idx, &state->bodies.c},
		{&transform_idx, &state->transforms.c},
		{&nav_idx, &navs->c, .optional = true},
		{},
	});
	while(DriftJoinNext(&join)){
		DriftAffine m = state->transforms.matrix[transform_idx];
		DriftAffine m_inv = DriftAffineInverse(m);
		
		DriftVec2 pos = state->bodies.position[body_idx];
		DriftVec2 vel = state->bodies.velocity[body_idx];
		
		// TODO Should drones always have a nav node?
		if(nav_idx == 0) DriftComponentAdd(&navs->c, join.entity);
		
		// Fly towards the nav target if it has one.
		DriftVec2 desired_velocity = DRIFT_VEC2_ZERO;
		DriftVec2 desired_rotation = vel;
		if(navs->data[nav_idx].node.id){
			float speed = DRIFT_DRONE_SPEED;
			DriftVec2 dir = navs->data[nav_idx].target_dir;
			float seed = fmodf((join.entity.id | navs->data[nav_idx].node.id)*(float)DRIFT_PHI, 256)/256;
			dir = DriftVec2Rotate(dir, DriftVec2ForAngle(0.4f*(2*seed - 1)));
			
			DriftVec2 target_pos = DriftVec2FMA(navs->data[nav_idx].target_pos, DriftVec2Perp(dir), -8);
			DriftVec2 target_vel = DriftVec2Mul(dir, speed);
			
			if(DriftVec2LengthSq(navs->data[nav_idx].target_dir) == 0){
				DriftDroneState drone_state = state->drones.data[drone_idx].state;
				if(drone_state == DRIFT_DRONE_STATE_TO_POD){
					float t0 = DriftTerrainRaymarch(state->terra, pos, target_pos, 10, 2);
					float t1 = DriftTerrainRaymarch(state->terra, pos, player_pos, 10, 2);
					// DriftDebugSegment2(state, pos, DriftVec2Lerp(pos, target_pos, t0), 10, 9, DRIFT_RGBA8_GREEN);
					// DriftDebugSegment2(state, pos, DriftVec2Lerp(pos, player_pos, t1), 10, 9, DRIFT_RGBA8_GREEN);
					
					if(t0*t1 == 1 && DriftVec2Distance(player_pos, target_pos) < DRIFT_POWER_EDGE_MAX_LENGTH){
						target_pos = player_pos;
						target_vel = DriftVec2Clamp(player_vel, speed);
					}
					
					if(DriftVec2Distance(pos, player_pos) < action_radius){
						DriftItemType item = state->drones.data[drone_idx].item;
						uint count = state->drones.data[drone_idx].count;
						if(item == DRIFT_ITEM_POWER_NODE){
							// if the cargo is power nodes, make sure it's not overflowing the capacity
							uint sum = state->inventory.cargo[DRIFT_ITEM_POWER_NODE] + count;
							uint node_cap = DriftPlayerNodeCap(state);
							if(sum > node_cap){
								uint overflow = sum - node_cap;
								count -= overflow;
								
								// transfer the extras back to the skiff instantaneously
								state->inventory.transit[DRIFT_ITEM_POWER_NODE] -= overflow;
								state->inventory.skiff[DRIFT_ITEM_POWER_NODE] += overflow;
							}
						} else if(count > 0) {
							DRIFT_NYI();
						}
						
						state->inventory.transit[item] -= count;
						state->inventory.cargo[item] += count;
						
						// find items to take back to the skiff
						item = DRIFT_ITEM_NONE, count = 0;
						for(uint i = 0; i < _DRIFT_ITEM_COUNT; i++){
							if(i != DRIFT_ITEM_POWER_NODE && state->inventory.cargo[i] > 0){
								item = i, count = 1;
								break;
							}
						}
						
						if(item){
							state->inventory.cargo[item] -= count;
							state->inventory.transit[item] += count;
						}
						
						state->drones.data[drone_idx].item = item;
						state->drones.data[drone_idx].count = count;
						state->drones.data[drone_idx].state = DRIFT_DRONE_STATE_TO_SKIFF;
						state->dispatch.pod--;
					}
				}
				
				if(drone_state == DRIFT_DRONE_STATE_TO_SKIFF && DriftVec2Distance(pos, target_pos) < action_radius){
					DriftDestroyEntity(state, join.entity);
					DriftItemType item = state->drones.data[drone_idx].item;
					uint count = state->drones.data[drone_idx].count;
					state->inventory.transit[item] -= count;
					state->inventory.skiff[item] += count;
					state->dispatch.count--;
				}
				
				DriftVec2 delta = DriftVec2Sub(target_pos, pos);
				desired_velocity = DriftVec2FMA(target_vel, delta, DRIFT_DRONE_SPEED/fmaxf(30, DriftVec2Length(delta)));
				desired_velocity = DriftVec2Clamp(desired_velocity, DRIFT_DRONE_SPEED);
				desired_rotation = desired_velocity;
			} else {
				float ratio = spline_ratio(pos, vel, target_pos, target_vel, speed, 2e-2f);
				desired_velocity = hermite_vel(tick_dt, ratio, pos, vel, target_pos, target_vel);
				DriftVec2 thrust = hermite_acc(tick_dt, ratio, pos, vel, target_pos, target_vel);
				desired_rotation = DriftVec2FMA(desired_velocity, thrust, 2);
			}
		}
		
		state->bodies.velocity[body_idx] = DriftVec2LerpConst(state->bodies.velocity[body_idx], desired_velocity, 2000.0f*tick_dt);
		DriftVec2 local_rot = DriftAffineDirection(m_inv, DriftVec2Normalize(desired_rotation));
		float desired_w = -(1 - expf(-10.0f*tick_dt))*atan2f(local_rot.x, local_rot.y)/tick_dt;
		state->bodies.angular_velocity[body_idx] = DriftLerpConst(state->bodies.angular_velocity[body_idx], desired_w, 50*tick_dt);
		
		navs->data[nav_idx].flow_map = DRONE_FLOW[state->drones.data[drone_idx].state];
	}
}

static void DrawDrones(DriftDraw* draw){
	DriftGameState* state = draw->state;
	
	DriftFrame frame_chasis = DRIFT_FRAMES[DRIFT_SPRITE_DRONE_CHASSIS];
	DriftFrame frame_hatch = DRIFT_FRAMES[DRIFT_SPRITE_DRONE_HATCH];
	DriftFrame frame_flood = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_FLOOD];
	DriftFrame frame_hemi = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_HEMI];
	DriftFrame frame_flame = DRIFT_FRAMES[DRIFT_SPRITE_FLAME0 + draw->frame%_DRIFT_SPRITE_FLAME_COUNT];
	const DriftRGBA8 flame_color = {0xC0, 0xC0, 0xC0, 0x80};
	
	
	uint drone_idx, transform_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&drone_idx, &state->drones.c},
		{&transform_idx, &state->transforms.c},
		{&body_idx, &state->bodies.c},
		{},
	});
	while(DriftJoinNext(&join)){
		DriftAffine m = state->transforms.matrix[transform_idx];
		if(!DriftAffineVisibility(draw->vp_matrix, DriftAffineOrigin(m), (DriftVec2){64, 64})) continue;
		
		DriftVec2 v = state->bodies.velocity[body_idx];
		float w = state->bodies.angular_velocity[body_idx]/4;
		float vn = DriftVec2Dot((DriftVec2){m.c, m.d}, v)/DRIFT_DRONE_SPEED;
		
		DriftAffine m_r = m, m_l = DriftAffineMul(m, (DriftAffine){-1, 0, 0, 1, 0, 0});
		DriftSprite* sprites = DRIFT_ARRAY_RANGE(draw->fg_sprites, 16);
		DriftLight* lights = DRIFT_ARRAY_RANGE(draw->lights, 16);
		DriftAffine flame_l = {-0.25f, 0, 0, DriftSaturate(vn - w), -3, -7};
		*sprites++ = (DriftSprite){.frame = frame_flame, .color = flame_color, .matrix = DriftAffineMul(m_r, flame_l)};
		DriftAffine flame_r = {+0.25f, 0, 0, DriftSaturate(vn + w), +3, -7};
		*sprites++ = (DriftSprite){.frame = frame_flame, .color = flame_color, .matrix = DriftAffineMul(m_r, flame_r)};
		
		*sprites++ = (DriftSprite){.frame = frame_chasis, .color = DRIFT_RGBA8_WHITE, .matrix = m_r};
		
		float hatch_open = DriftSaturate(sinf(draw->tick/8.0f));
		hatch_open *= 0*hatch_open;
		DriftAffine m_hatch = DriftAffineTRS(DriftVec2Mul((DriftVec2){4, -2}, hatch_open), 0.8f*hatch_open, DRIFT_VEC2_ONE);
		DriftAffine m_hatchr = DriftAffineMul(m_r, m_hatch);
		DriftAffine m_hatchl = DriftAffineMul(m_l, m_hatch);
		*sprites++ = (DriftSprite){.frame = frame_hatch, .color = DRIFT_RGBA8_WHITE, .matrix = m_hatchr};
		*sprites++ = (DriftSprite){.frame = frame_hatch, .color = DRIFT_RGBA8_WHITE, .matrix = m_hatchl};
		
		DriftVec4 flame_glow = DriftVec4Mul((DriftVec4){{1.14f, 0.78f, 0.11f, 0}}, 2*DriftSaturate(vn));
		*lights++ = (DriftLight){.frame = frame_hemi, .color = flame_glow, .matrix = DriftAffineMul(m_r, (DriftAffine){30, 0, 0, -20, 0, -8})};
		*lights++ = (DriftLight){.frame = frame_flood, .color = {{1, 1, 1, 0}}, .matrix = DriftAffineMul(m_hatchl, (DriftAffine){40, 0, 0, 48, +4, 8})};
		*lights++ = (DriftLight){.frame = frame_flood, .color = {{1, 1, 1, 0}}, .matrix = DriftAffineMul(m_hatchr, (DriftAffine){40, 0, 0, 48, +4, 8})};
		DriftArrayRangeCommit(draw->fg_sprites, sprites);
		DriftArrayRangeCommit(draw->lights , lights);

		if(DRIFT_DEBUG_SHOW_PATH) vis_path(state, join.entity, 1/DRIFT_TICK_HZ, DRIFT_DRONE_SPEED, 2e-2f);
	}
}

bool DriftHealthApplyDamage(DriftUpdate* update, DriftEntity entity, float amount, DriftVec2 pos){
	DriftGameState* state = update->state;
	
	uint health_idx = DriftComponentFind(&state->health.c, entity);
	if(health_idx){
		DriftHealth* health = state->health.data + health_idx;
		if(health->value > 0){
			float pan = DriftClamp(DriftAffinePoint(update->prev_vp_matrix, pos).x, -1, 1);
		
			if(update->tick - health->damage_tick0 > health->timeout*DRIFT_TICK_HZ){
				health->damage_tick0 = update->tick;
				health->value -= amount;
				
				DriftAudioPlaySample(DRIFT_BUS_SFX, health->hit_sfx, (DriftAudioParams){.gain = 1, .pan = pan});
			}
			
			// TODO Is there a better place for this?
			uint enemy_idx = DriftComponentFind(&state->enemies.c, entity);
			if(enemy_idx) state->enemies.aggro_ticks[enemy_idx] = 10*DRIFT_TICK_HZ;
			
			if(health->value <= 0){
				health->value = 0;
				
				uint body_idx = DriftComponentFind(&state->bodies.c, entity);
				DriftVec2 pos = state->bodies.position[body_idx];
				DriftVec2 vel = state->bodies.velocity[body_idx];
				if(health->drop) DriftItemMake(update->state, health->drop, pos, vel, state->enemies.tile_idx[enemy_idx]);
				
				DriftDestroyEntity(state, entity);
				DriftAudioPlaySample(DRIFT_BUS_SFX, health->die_sfx, (DriftAudioParams){.gain = 1, .pan = pan});
				DriftMakeBlast(update, pos, (DriftVec2){0, 1}, DRIFT_BLAST_EXPLODE);
			}
		}
		
		return true;
	} else {
		return false;
	}
}

typedef struct {
	uint frame_count, frame0, frame1;
	DriftRGBA8 sprite_color;
	DriftVec4 light_color;
	float light_size, light_radius;
} DriftBlastInfo;

static const DriftBlastInfo DRIFT_BLASTS[_DRIFT_BLAST_COUNT] = {
	[DRIFT_BLAST_EXPLODE] = {
		.frame_count = 11, .frame0 = DRIFT_SPRITE_EXPLOSION_SMOKE00, .frame1 = DRIFT_SPRITE_EXPLOSION_FLAME00, .sprite_color = {0xC0, 0xC0, 0xC0, 0x00},
		.light_color = {{20.00f, 17.85f, 12.24f, 0.00f}}, .light_size = 350, .light_radius = 8,
	},
	[DRIFT_BLAST_RICOCHET] = {
		.frame_count = 6, .frame0 = DRIFT_SPRITE_RICOCHET00, .sprite_color = {0xD8, 0xD8, 0xD8, 0x80},
		.light_color = (DriftVec4){{2.24f, 1.92f, 1.27f, 0.00f}}, .light_size = 100, .light_radius = 0,
	},
	[DRIFT_BLAST_VIOLET_ZAP] = {
		.frame_count = 6, .frame0 = DRIFT_SPRITE_VIOLET_ZAP00, .sprite_color = {0xD8, 0xD8, 0xD8, 0x80},
		.light_color = (DriftVec4){{2.77f, 0.52f, 6.35f, 0.00f}}, .light_size = 150, .light_radius = 0,
	},
	[DRIFT_BLAST_GREEN_FLASH] = {
		.frame_count = 15, .frame0 = DRIFT_SPRITE_GREEN_FLASH00, .sprite_color = (DriftRGBA8){0xAD, 0xAD, 0xAD, 0x00},
		.light_color = (DriftVec4){{0.37f, 2.13f, 0.01f, 0.00f}}, .light_size = 100, .light_radius = 0,
	},
};

// TODO
typedef struct {
	DriftTable t;
	uint* type;
	DriftVec2* position;
	DriftVec2* normal;
	uint* tick0;
} DriftTableBlast;
static DriftTableBlast blasts;

void DriftMakeBlast(DriftUpdate* update, DriftVec2 position, DriftVec2 normal, DriftBlastType type){
	uint idx = DriftTablePushRow(&blasts.t);
	blasts.type[idx] = type;
	blasts.position[idx] = position;
	blasts.normal[idx] = normal;
	blasts.tick0[idx] = update->tick;
}

static void draw_blasts(DriftDraw* draw){
	DriftGameState* state = draw->state;
	uint tick = draw->tick;
	
	uint i = 0;
	while(i < blasts.t.row_count){
		uint frame = (tick - blasts.tick0[i])/2;
		
		const DriftBlastInfo* blast = DRIFT_BLASTS + blasts.type[i];
		uint frame_count = blast->frame_count;
		if(frame < frame_count){
			DriftVec2 pos = blasts.position[i], n = DriftVec2Mul(blasts.normal[i], 0.5f);
			float flip = blasts.tick0[i] % 2 == 0 ? 1 : -1;
			DriftAffine transform = {flip*n.y, flip*-n.x, n.x, n.y, pos.x, pos.y};
			DRIFT_ARRAY_PUSH(draw->bullet_sprites, ((DriftSprite){
				.frame = DRIFT_FRAMES[blast->frame0 + frame], .color = blast->sprite_color, .matrix = transform,
			}));
			if(blast->frame1){
				DRIFT_ARRAY_PUSH(draw->bullet_sprites, ((DriftSprite){
					.frame = DRIFT_FRAMES[blast->frame1 + frame], .color = blast->sprite_color, .matrix = transform,
				}));
			}
			
			float intensity = fmaxf(0, 1 - (float)frame/(float)frame_count);
			DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
				.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = DriftVec4Mul(blast->light_color, intensity*intensity),
				.matrix = {blast->light_size, 0, 0, blast->light_size, pos.x, pos.y}, .radius = blast->light_radius,
			}));
			
			i++;
		} else {
			DriftTableCopyRow(&blasts.t, i, --blasts.t.row_count);
		}
	}
}

void DriftSystemsUpdate(DriftUpdate* update){
	static bool needs_init = true; // TODO static global
	if(needs_init){
		needs_init = false;
		
		DriftTableInit(&blasts.t, (DriftTableDesc){
			.mem = update->state->mem, .name = "Blasts",
			.columns.arr = {
				DRIFT_DEFINE_COLUMN(blasts.type),
				DRIFT_DEFINE_COLUMN(blasts.position),
				DRIFT_DEFINE_COLUMN(blasts.normal),
				DRIFT_DEFINE_COLUMN(blasts.tick0),
			},
		});
	}
	
	UpdatePlayer(update);
}

#define RUN_FUNC(_func_, _arg_) {TracyCZoneN(ZONE, #_func_, true); _func_(_arg_); TracyCZoneEnd(ZONE);}

void DriftSystemsTickFab(DriftGameContext* ctx, float dt){
	DriftGameState* state = ctx->state;
	if(state->fab.item){
		DriftItemType item = state->fab.item;
		
		uint duration = DriftItemBuildDuration(item);
		state->fab.progress = DriftSaturate(state->fab.progress + dt/duration);
		
		if(state->fab.progress >= 1){
			state->inventory.skiff[item] += DRIFT_ITEMS[item].makes ?: 1;
			DriftHudPushToast(ctx, DriftPlayerItemCount(state, item), "Built: %s", DriftItemName(item));
			state->fab.item = DRIFT_ITEM_NONE;
			state->fab.progress = 0;
		}
	}
}

void DriftSystemsTick(DriftUpdate* update){
	RUN_FUNC(TickNavs, update);
	RUN_FUNC(TickPlayer, update);
	RUN_FUNC(TickDrones, update);
	RUN_FUNC(TickFlows, update);
	RUN_FUNC(DriftSystemsTickWeapons, update);
	RUN_FUNC(DriftTickItemSpawns, update);
	RUN_FUNC(DriftTickEnemies, update);
	
	DriftSystemsTickFab(update->ctx, update->tick_dt);
}

static void draw_fg_decal(DriftDraw* draw, DRIFT_ARRAY(DriftSprite)* layer, DriftVec2 pos, DriftTerrainSampleInfo info, uint rnd){
	DriftTerrain* terra = draw->state->terra;
	
	pos = DriftVec2FMA(pos, info.grad, -info.dist);
	DriftVec2 g = DriftVec2Mul(info.grad, 0.75f + info.dist*0.5f/16);
	uint light[] = {
		DRIFT_SPRITE_LARGE_BUSHY_PLANTS00, DRIFT_SPRITE_LARGE_BUSHY_PLANTS01, DRIFT_SPRITE_LARGE_BUSHY_PLANTS02,
		DRIFT_SPRITE_LARGE_BUSHY_PLANTS03, DRIFT_SPRITE_LARGE_BUSHY_PLANTS04,
	};
	uint radio[] = {
		DRIFT_SPRITE_MEDIUM_CRYSTALS00, DRIFT_SPRITE_MEDIUM_CRYSTALS01, DRIFT_SPRITE_MEDIUM_CRYSTALS02,
		DRIFT_SPRITE_MEDIUM_FUNGI00, DRIFT_SPRITE_MEDIUM_FUNGI01, DRIFT_SPRITE_MEDIUM_FUNGI02,
		DRIFT_SPRITE_MEDIUM_FUNGI03, DRIFT_SPRITE_MEDIUM_FUNGI04, DRIFT_SPRITE_MEDIUM_FUNGI05,
	};
	uint cryo[] = {DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS00, DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS01, DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS02};
	uint dark[] = {
		DRIFT_SPRITE_LARGE_BURNED_PLANTS00, DRIFT_SPRITE_LARGE_BURNED_PLANTS01, DRIFT_SPRITE_LARGE_BURNED_PLANTS02,
		DRIFT_SPRITE_LARGE_BURNED_PLANTS03, DRIFT_SPRITE_LARGE_BURNED_PLANTS04
	};
	
	uint* biome_base[] = {light, radio, cryo, dark, light};
	uint biome_len[] = {sizeof(light)/sizeof(*light), sizeof(radio)/sizeof(*radio), sizeof(cryo)/sizeof(*cryo), 5, 1};
	
	uint biome = DriftTerrainSampleBiome(terra, pos).idx;
	uint frame = biome_base[biome][rnd % biome_len[biome]];
	DriftSprite sprite = {
		.matrix = {g.y, -g.x, g.x, g.y, pos.x, pos.y},
		.frame = DRIFT_FRAMES[frame], .color = DRIFT_RGBA8_WHITE,
	};
	
	if(frame == DRIFT_SPRITE_LARGE_BUSHY_PLANTS02 || frame == DRIFT_SPRITE_LARGE_BUSHY_PLANTS04){
		DriftLight light = {.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = {{0.01f, 0.18f, 0.22f, 0.00f}}, .matrix = {80, 0, 0, 80, pos.x, pos.y}};
		DRIFT_ARRAY_PUSH(draw->lights, light);
	} else if(frame == DRIFT_SPRITE_LARGE_BUSHY_PLANTS03){
		DriftLight light = {.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = {{0.12f, 0.10f, 0.01f, 0.00f}}, .matrix = {80, 0, 0, 80, pos.x, pos.y}};
		DRIFT_ARRAY_PUSH(draw->lights, light);
	} else if(DRIFT_SPRITE_MEDIUM_CRYSTALS00 <= frame && frame <= DRIFT_SPRITE_MEDIUM_CRYSTALS02){
		DriftLight light = {.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = {{0.34f, 0.40f, 0.00f, 0.00f}}, .matrix = {65, 0, 0, 65, pos.x, pos.y}};
		DRIFT_ARRAY_PUSH(draw->lights, light);
	} else if(DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS00 <= frame && frame <= DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS02){
		DriftLight light = {.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = {{0.10f, 0.14f, 0.19f, 0.00f}}, .matrix = {50, 0, 0, 50, pos.x, pos.y}};
		DRIFT_ARRAY_PUSH(draw->lights, light);
	}
	
	DRIFT_ARRAY_PUSH(*layer, sprite);
}

static void draw_bg_decal(DriftDraw* draw, DriftVec2 pos, float pdist, uint rnd){
	DriftTerrain* terra = draw->state->terra;
	
	static const uint light0[] = {
		DRIFT_SPRITE_SMALL_MOSS_PATCHES00, DRIFT_SPRITE_SMALL_MOSS_PATCHES01, DRIFT_SPRITE_SMALL_MOSS_PATCHES02, DRIFT_SPRITE_SMALL_MOSS_PATCHES03,
		DRIFT_SPRITE_SMALL_ROCKS00, DRIFT_SPRITE_SMALL_ROCKS01, DRIFT_SPRITE_SMALL_ROCKS02,
	};
	static const uint light1[] = {
		DRIFT_SPRITE_MEDIUM_MOSS_PATCHES00, DRIFT_SPRITE_MEDIUM_MOSS_PATCHES01, DRIFT_SPRITE_MEDIUM_MOSS_PATCHES02, DRIFT_SPRITE_MEDIUM_MOSS_PATCHES03,
		DRIFT_SPRITE_MEDIUM_ROCKS00, DRIFT_SPRITE_MEDIUM_ROCKS01,
	};
	static const uint light2[] = {DRIFT_SPRITE_LARGE_MOSS_PATCHES00, DRIFT_SPRITE_LARGE_MOSS_PATCHES01, DRIFT_SPRITE_LARGE_ROCKS00, DRIFT_SPRITE_LARGE_ROCKS01};
	
	static const uint* light_base[] = {light0, light1, light2};
	static const uint light_div[] = {7, 6, 4};
	
	static const uint radio0[] = {
		DRIFT_SPRITE_SMALL_CRYSTALS00, DRIFT_SPRITE_SMALL_CRYSTALS01, DRIFT_SPRITE_SMALL_CRYSTALS02,
	};
	static const uint radio1[] = {
		DRIFT_SPRITE_MEDIUM_FUNGI00, DRIFT_SPRITE_MEDIUM_FUNGI01, DRIFT_SPRITE_MEDIUM_FUNGI02,
		DRIFT_SPRITE_MEDIUM_FUNGI03, DRIFT_SPRITE_MEDIUM_FUNGI04, DRIFT_SPRITE_MEDIUM_FUNGI05,
	};
	static const uint radio2[] = {DRIFT_SPRITE_LARGE_FUNGI00, DRIFT_SPRITE_LARGE_FUNGI01, DRIFT_SPRITE_LARGE_SLIME_PATCHES00, DRIFT_SPRITE_LARGE_SLIME_PATCHES01};

	static const uint* radio_base[] = {radio0, radio1, radio2};
	static const uint radio_div[] = {3, 6, 4};
	
	static const uint cryo0[] = {
		DRIFT_SPRITE_SMALL_ICE_CHUNKS00, DRIFT_SPRITE_SMALL_ICE_CHUNKS01, DRIFT_SPRITE_SMALL_ICE_CHUNKS02,
		DRIFT_SPRITE_SMALL_ROCKS_CRYO00, DRIFT_SPRITE_SMALL_ROCKS_CRYO01,
		DRIFT_SPRITE_CRYO_SMALL_ROCK00, DRIFT_SPRITE_CRYO_SMALL_ROCK01, DRIFT_SPRITE_CRYO_SMALL_ROCK02,
		DRIFT_SPRITE_CRYO_SMALL_ROCK03, DRIFT_SPRITE_CRYO_SMALL_ROCK04, DRIFT_SPRITE_CRYO_SMALL_ROCK05,
		DRIFT_SPRITE_CRYO_SMALL_ROCK06, DRIFT_SPRITE_CRYO_SMALL_ROCK07, DRIFT_SPRITE_CRYO_SMALL_ROCK08,
		DRIFT_SPRITE_CRYO_SMALL_ROCK09, DRIFT_SPRITE_CRYO_SMALL_ROCK10, DRIFT_SPRITE_CRYO_SMALL_ROCK11,
		DRIFT_SPRITE_CRYO_SMALL_ROCK12, DRIFT_SPRITE_CRYO_SMALL_ROCK13, DRIFT_SPRITE_CRYO_SMALL_ROCK14,
	};
	static const uint cryo1[] = {DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS00, DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS01, DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS02};
	static const uint cryo2[] = {
		DRIFT_SPRITE_LARGE_FOSSILS00, DRIFT_SPRITE_LARGE_FOSSILS01, DRIFT_SPRITE_LARGE_FOSSILS02, DRIFT_SPRITE_LARGE_FOSSILS03,
	};
	
	static const uint* cryo_base[] = {cryo0, cryo1, cryo2};
	static const uint cryo_div[] = {20, 3, 4};
	
	static const uint dark0[] = {DRIFT_SPRITE_SMALL_DARK_ROCKS00, DRIFT_SPRITE_SMALL_DARK_ROCKS01};
	static const uint dark1[] = {
		DRIFT_SPRITE_MEDIUM_DARK_ROCKS00, DRIFT_SPRITE_MEDIUM_DARK_ROCKS01,
		DRIFT_SPRITE_SMALL_BURNED_PLANTS00, DRIFT_SPRITE_SMALL_BURNED_PLANTS01,
		DRIFT_SPRITE_SMALL_BURNED_PLANTS02, DRIFT_SPRITE_SMALL_BURNED_PLANTS03, DRIFT_SPRITE_SMALL_BURNED_PLANTS04
	};
	static const uint* dark_base[] = {dark0, dark1, dark1};
	static const uint dark_div[] = {2, 7, 7};
	
	// static const uint space0[] = {DRIFT_SPRITE_TMP_STAR};
	// static const uint* space_base[] = {space0, space0, space0};
	// static const uint space_div[] = {1, 1, 1};
	
	static const uint** base[] = {light_base, radio_base, cryo_base, dark_base};
	static const uint* div[] = {light_div, radio_div, cryo_div, dark_div};
	
	uint asset_size = (uint)DriftClamp((pdist - 2)/1.5f, 0, 2.99f);
	uint biome = DriftTerrainSampleBiome(terra, pos).idx;
	if(biome < 4){
		uint frame = base[biome][asset_size][rnd%div[biome][asset_size]];
		DriftVec2 rot = DriftVec2ForAngle(rnd*(float)(2*M_PI/DRIFT_RAND_MAX));
		DriftSprite sprite = {
			.matrix = {rot.x, rot.y, -rot.y, rot.x, pos.x, pos.y}, .z = 255,
			.frame = DRIFT_FRAMES[frame], .color = DRIFT_RGBA8_WHITE,
		};
		DRIFT_ARRAY_PUSH(draw->bg_sprites, sprite);
	}
}

static DriftDecalDef DRIFT_DECAL_DEFS_LIGHT[] = {
	{.label = "large rocks", .layer = 2, .poisson = 4.5f, .terrain = 64.0f, .weight = 300, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_LARGE_ROCKS00, DRIFT_SPRITE_LARGE_ROCKS01,
		0,
	}},
	{.label = "medium rocks", .layer = 1, .poisson = 2.5f, .terrain = 48.0f, .weight = 10, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_MEDIUM_ROCKS00, DRIFT_SPRITE_MEDIUM_ROCKS01,
		0,
	}},
	{.label = "medium moss", .layer = 1, .poisson = 2.5f, .terrain = 48.0f, .weight = 10, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_MEDIUM_MOSS_PATCHES00, DRIFT_SPRITE_MEDIUM_MOSS_PATCHES01,
		DRIFT_SPRITE_MEDIUM_MOSS_PATCHES02, DRIFT_SPRITE_MEDIUM_MOSS_PATCHES03,
		0,
	}},
	{.label = "small moss", .layer = 0, .poisson = 0.0f, .terrain = 8.0f, .weight = 1, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_SMALL_MOSS_PATCHES00, DRIFT_SPRITE_SMALL_MOSS_PATCHES01,
		DRIFT_SPRITE_SMALL_MOSS_PATCHES02, DRIFT_SPRITE_SMALL_MOSS_PATCHES03,
		0,
	}},
	{.label = "small rocks", .layer = 0, .poisson = 0.0f, .terrain = 8.0f, .weight = 1, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_SMALL_ROCKS00, DRIFT_SPRITE_SMALL_ROCKS01, DRIFT_SPRITE_SMALL_ROCKS02,
		0,
	}},
	{},
};

static DriftDecalDef DRIFT_DECAL_DEFS_CRYO[] = {
	{.label = "nautilus fossils L", .layer = 2, .poisson = 3.5f, .terrain = 64.0f, .weight = 300, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_LARGE_FOSSILS00, DRIFT_SPRITE_LARGE_FOSSILS01,
		0,
	}},
	{.label = "trilobyte fossils L", .layer = 2, .poisson = 3.5f, .terrain = 64.0f, .weight = 300, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_LARGE_FOSSILS02, DRIFT_SPRITE_LARGE_FOSSILS03,
		0,
	}},
	{.label = "medium ice", .layer = 1, .poisson = 1.9f, .terrain = 48.0f, .weight = 7, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS00, DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS01, DRIFT_SPRITE_MEDIUM_ICE_CRYSTALS02,
		0
	}},
	{.label = "small ice", .layer = 0, .poisson = 0.0f, .terrain = 8.0f, .weight = 2.5, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_SMALL_ICE_CHUNKS00, DRIFT_SPRITE_SMALL_ICE_CHUNKS01, DRIFT_SPRITE_SMALL_ICE_CHUNKS02,
		0,
	}},
	{.label = "cryo rocks", .layer = 0, .poisson = 0.0f, .terrain = 8.0f, .weight = 2, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_SMALL_ROCKS_CRYO00, DRIFT_SPRITE_SMALL_ROCKS_CRYO01,
		0,
	}},
	{.label = "nautilus fossils S", .layer = 0, .poisson = 1.7f, .terrain = 8.0f, .weight = 3, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_CRYO_SMALL_ROCK00, DRIFT_SPRITE_CRYO_SMALL_ROCK01,
		0,
	}},
	{.label = "trilobyte fossils S", .layer = 0, .poisson = 1.7f, .terrain = 8.0f, .weight = 3, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_CRYO_SMALL_ROCK02, DRIFT_SPRITE_CRYO_SMALL_ROCK03,
		0,
	}},
	{.label = "cryo coral", .layer = 1, .poisson = 2.1f, .terrain = 40.0f, .weight = 5, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_CRYO_SMALL_ROCK04, DRIFT_SPRITE_CRYO_SMALL_ROCK06, DRIFT_SPRITE_CRYO_SMALL_ROCK08,
		DRIFT_SPRITE_CRYO_SMALL_ROCK10, DRIFT_SPRITE_CRYO_SMALL_ROCK12, DRIFT_SPRITE_CRYO_SMALL_ROCK14,
		0,
	}},
	{.label = "cryo rocky coral", .layer = 1, .poisson = 2.1f, .terrain = 40.0f, .weight = 5, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_CRYO_SMALL_ROCK05, DRIFT_SPRITE_CRYO_SMALL_ROCK07, DRIFT_SPRITE_CRYO_SMALL_ROCK09,
		DRIFT_SPRITE_CRYO_SMALL_ROCK11, DRIFT_SPRITE_CRYO_SMALL_ROCK13,
		0,
	}},
	{},
};

static DriftDecalDef DRIFT_DECAL_DEFS_RADIO[] = {
	{.label = "small crystals", .layer = 0, .poisson = 1.2f, .terrain = 8.0f, .weight = 3.3f, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_SMALL_CRYSTALS00, DRIFT_SPRITE_SMALL_CRYSTALS01, DRIFT_SPRITE_SMALL_CRYSTALS02,
		0,
	}},
	{.label = "medium fungi", .layer = 1, .poisson = 1.5f, .terrain = 30.0f, .weight = 5, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_MEDIUM_FUNGI00, DRIFT_SPRITE_MEDIUM_FUNGI01, DRIFT_SPRITE_MEDIUM_FUNGI02,
		0,
	}},
	{.label = "medium shrooms", .layer = 1, .poisson = 2.2f, .terrain = 30.0f, .weight = 5, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_MEDIUM_FUNGI03, DRIFT_SPRITE_MEDIUM_FUNGI04, DRIFT_SPRITE_MEDIUM_FUNGI05,
		0,
	}},
	{.label = "large fungi", .layer = 2, .poisson = 4.5f, .terrain = 40.0f, .weight = 30, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_LARGE_FUNGI00,
		0,
	}},
	{.label = "large shrooms", .layer = 2, .poisson = 4.5f, .terrain = 40.0f, .weight = 30, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_LARGE_FUNGI01,
		0,
	}},
	{.label = "large slime", .layer = 2, .poisson = 4.5f, .terrain = 40.0f, .weight = 30, .sprites = (DriftSpriteEnum[]){
		DRIFT_SPRITE_LARGE_SLIME_PATCHES00, DRIFT_SPRITE_LARGE_SLIME_PATCHES01,
		0,
	}},
	{},
};

DriftDecalDef* DRIFT_DECAL_DEFS[_DRIFT_BIOME_COUNT] = {
	[DRIFT_BIOME_LIGHT] = DRIFT_DECAL_DEFS_LIGHT,
	[DRIFT_BIOME_CRYO] = DRIFT_DECAL_DEFS_CRYO,
	[DRIFT_BIOME_RADIO] = DRIFT_DECAL_DEFS_RADIO,
	[DRIFT_BIOME_DARK] = DRIFT_DECAL_DEFS_LIGHT,
	[DRIFT_BIOME_SPACE] = DRIFT_DECAL_DEFS_LIGHT,
};

static void draw_bg_decal2(DRIFT_ARRAY(DriftSprite)* layers, DriftVec2 pos, DriftBiomeType biome, float poisson_dist, float terrain_dist, uint irand){
	const DriftDecalDef* decal_def = NULL;
	DriftSpriteEnum sprite = 0;
	
	const DriftDecalDef* defs = DRIFT_DECAL_DEFS[biome];
	
	DriftReservoir rsr = {.rand = (float)irand/(float)DRIFT_RAND_MAX, .sum = 10};
	for(uint i = 0; defs[i].sprites; i++){
		const DriftDecalDef* def = defs + i;
		if(poisson_dist >= def->poisson && terrain_dist >= def->terrain){
			uint count = 0; while(def->sprites[count]) count++;
			if(DriftReservoirSample(&rsr, def->weight)){
				decal_def = def, sprite = def->sprites[irand % count];
			}
		}
	}
	
	float dist2 = DriftVec2Distance(pos, (DriftVec2){ 4449.7f, -4214.3f});
	if(poisson_dist > 1.8f && DriftReservoirSample(&rsr, 30.0f*DriftSmoothstep(1200, 0, dist2))){
		static const DriftDecalDef hive_decal = {.layer = 2};
		decal_def = &hive_decal;
		sprite = DRIFT_SPRITE_HIVE_WART;
	}
	
	if(decal_def){
		DriftVec2 rot = DriftVec2ForAngle(rsr.rand*(float)(2*M_PI));
		DRIFT_ARRAY_PUSH(*(layers + decal_def->layer), ((DriftSprite){
			.matrix = {rot.x, rot.y, -rot.y, rot.x, pos.x, pos.y},
			.frame = DRIFT_FRAMES[sprite], .color = DRIFT_RGBA8_WHITE,
			// magic numbers from the shader... should document
			.z = (uint)(255*sqrt(DriftSaturate(terrain_dist/(6*8)))),
		}));
	}
}

static void draw_decals(DriftDraw* draw){
	typedef struct {
		u8 fx, fy, fdist, idist;
	} PlacementNoise;
	static const PlacementNoise* placement;
	static uint rando[256*256];  // TODO static global
	if(placement == NULL){
		placement = DriftAssetLoad(DriftSystemMem, "bin/placement256.bin").ptr;
		DriftRandom rand[] = {{65418}};
		for(uint i = 0; i < 256*256; i++) rando[i] = DriftRand32(rand);
	}
	
	DriftGameState* state = draw->state;
	DriftAffine vp_inv = draw->vp_inverse;
	float hw = fabsf(vp_inv.a) + fabsf(vp_inv.c) + 64, hh = fabsf(vp_inv.b) + fabsf(vp_inv.d) + 64;
	DriftAABB2 bounds = {vp_inv.x - hw, vp_inv.y - hh, vp_inv.x + hw, vp_inv.y + hh};
	
	DriftAffine v_mat = draw->v_matrix;
	float v_scale = hypotf(v_mat.a, v_mat.b) + hypotf(v_mat.c, v_mat.d);
	
	DriftVec2 center = DriftAffineOrigin(vp_inv);
	
	// TODO move into draw instead.
	DRIFT_ARRAY(DriftSprite) layers[] = {
		DRIFT_ARRAY_NEW(draw->mem, 256, DriftSprite),
		DRIFT_ARRAY_NEW(draw->mem, 256, DriftSprite),
		DRIFT_ARRAY_NEW(draw->mem, 256, DriftSprite),
		DRIFT_ARRAY_NEW(draw->mem, 256, DriftSprite),
	};
	
	TracyCZoneN(ZONE_BIOME, "Biome", true);
	float q = 16;
	for(int y = (int)floorf(bounds.b/q); y*q < bounds.t; y++){
		if(draw->ctx->debug.hide_terrain_decals) break;
		if(v_scale < 1.5f) break;
		for(int x = (int)floorf(bounds.l/q); x*q < bounds.r; x++){
			uint i = (x & 255) + (y & 255)*256;
			float pdist = placement[i].idist + placement[i].fdist/255.0f;
			if(pdist <= 0) continue;
			
			DriftVec2 pos = DriftVec2FMA((DriftVec2){x*q, y*q}, (DriftVec2){placement[i].fx, placement[i].fy}, q/255);
			// DriftDebugCircle(state, pos, pdist, DRIFT_RGBA8_RED);
			
			DriftTerrainSampleInfo info = DriftTerrainSampleFine(state->terra, pos);
			if(info.dist <= 0) continue;
			
			DriftBiomeType biome = DriftTerrainSampleBiome(state->terra, pos).idx;
			if(info.dist < 16) draw_fg_decal(draw, layers + 3, pos, info, rando[i]);
			draw_bg_decal2(layers, pos, biome, pdist, info.dist, rando[i]);
		}
	}
	
	for(uint i = 0; i < 4; i++){
		size_t length = DriftArrayLength(layers[i]);
		DriftSprite* ptr = DRIFT_ARRAY_RANGE(draw->bg_sprites, length);
		memcpy(ptr, layers[i], DriftArraySize(layers[i]));
		DriftArrayRangeCommit(draw->bg_sprites, ptr + length);
	}
	TracyCZoneEnd(ZONE_BIOME);
}

void DriftSystemsDraw(DriftDraw* draw){
	draw_decals(draw);
	
	DriftGameState* state = draw->state;
	{ // TODO temp crashed ship
		DriftVec2 skiff_pos = DRIFT_SKIFF_POSITION;
		// skiff_pos = INPUT->mouse_pos_world;
		
		DriftAffine m = DriftAffineTRS(skiff_pos, -1.9300f, DRIFT_VEC2_ONE);
		DriftSprite skiff_sprite = {
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_CRASHED_SHIP], .color = DRIFT_RGBA8_WHITE,
			.matrix = m, .z = 200,
		};
		DRIFT_ARRAY_PUSH(draw->bg_sprites, skiff_sprite);

		if(state->scan_progress[DRIFT_SCAN_CONSTRUCTION_SKIFF] < 1){
			DriftRGBA8 flash = DriftHUDIndicator(draw, skiff_pos, (DriftRGBA8){0x00, 0x80, 0x80, 0x80});
			if(flash.a > 0){
				skiff_sprite.color = flash;
				DRIFT_ARRAY_PUSH(draw->flash_sprites, skiff_sprite);
			}
		}
		
		DRIFT_ARRAY_PUSH(draw->bg_sprites, ((DriftSprite){
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_LARGE_ROCKS00], .color = DRIFT_RGBA8_WHITE,
			.matrix = {1, 0, 0, 1, skiff_pos.x + 64, skiff_pos.y + 16}, .z = 120,
		}));
		
		DRIFT_ARRAY_PUSH(draw->bg_sprites, ((DriftSprite){
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_LARGE_ROCKS01], .color = DRIFT_RGBA8_WHITE,
			.matrix = {1, 0, 0, 1, skiff_pos.x + 87, skiff_pos.y - 19}, .z = 120,
		}));
		
		DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = (DriftVec4){{1.27f, 1.20f, 0.99f, 0.00f}},
			.matrix = DriftAffineMul(m, (DriftAffine){800, 0, 0, 800, 0, 50}), .radius = 24,
		}));
	}
	
	RUN_FUNC(DrawPower, draw);
	RUN_FUNC(DriftSystemsDrawWeapons, draw);
	RUN_FUNC(DriftDrawItems, draw);
	RUN_FUNC(DrawDrones, draw);
	RUN_FUNC(DriftDrawEnemies, draw);
	RUN_FUNC(DrawPlayer, draw);
	RUN_FUNC(draw_blasts, draw);
}

static void table_init(DriftGameState* state, DriftTable* table, const char* name, DriftColumnSet columns, uint capacity){
	DriftTableInit(table, (DriftTableDesc){.name = name, .mem = state->mem, .min_row_capacity = capacity, .columns = columns});
	DRIFT_ARRAY_PUSH(state->tables, table);
}

void DriftSystemsInit(DriftGameState* state){
	DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(state, &state->transforms, DriftComponentTransform, ((DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->transforms.entity),
		DRIFT_DEFINE_COLUMN(state->transforms.matrix),
	}), 1024);
	state->transforms.matrix[0] = DRIFT_AFFINE_IDENTITY;
	
	DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(state, &state->bodies, DriftComponentRigidBody, ((DriftColumnSet){
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
	}), 1024);
	state->bodies.rotation[0] = (DriftVec2){1, 0};
	
	table_init(state, &state->rtree.t, "#rtree", ((DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->rtree.node),
		DRIFT_DEFINE_COLUMN(state->rtree.pool_arr),
	}), 256);
	state->rtree.root = DriftTablePushRow(&state->rtree.t);

	DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(state, &state->players, DriftComponentPlayer, ((DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->players.entity),
		DRIFT_DEFINE_COLUMN(state->players.data),
	}), 0);

	DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(state, &state->drones, DriftComponentDrone, ((DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->drones.entity),
		DRIFT_DEFINE_COLUMN(state->drones.data),
	}), 0);

	DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(state, &state->items, DriftComponentItem, ((DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->items.entity),
		DRIFT_DEFINE_COLUMN(state->items.type),
		DRIFT_DEFINE_COLUMN(state->items.tile_idx),
	}), 1024);

	DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(state, &state->scan, DriftComponentScan, ((DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->scan.entity),
		DRIFT_DEFINE_COLUMN(state->scan.type),
	}), 1024);

	DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(state, &state->scan_ui, DriftComponentScanUI, ((DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->scan_ui.entity),
		DRIFT_DEFINE_COLUMN(state->scan_ui.type),
	}), 0);

	DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(state, &state->power_nodes, DriftComponentPowerNode, ((DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->power_nodes.entity),
		DRIFT_DEFINE_COLUMN(state->power_nodes.position),
		DRIFT_DEFINE_COLUMN(state->power_nodes.rotation),
		DRIFT_DEFINE_COLUMN(state->power_nodes.clam),
		DRIFT_DEFINE_COLUMN(state->power_nodes.active),
	}), 0);
	state->power_nodes.rotation[0] = (DriftVec2){1, 0};
	
	DriftTableInit(&state->power_edges.t, (DriftTableDesc){
		.name = "PowerNodeEdges", .mem = state->mem,
		.columns.arr = {
			DRIFT_DEFINE_COLUMN(state->power_edges.edge),
		},
	});
	DRIFT_ARRAY_PUSH(state->tables, &state->power_edges.t);
	
	static const char* FLOW_NAMES[] = {"@FlowPower", "@FlowPlayer", "@FlowWaypoint"};
	for(uint i = 0; i < _DRIFT_FLOW_MAP_COUNT; i++){
		DriftGameStateNamedComponentMake(state, &state->flow_maps[i].c, FLOW_NAMES[i], (DriftColumnSet){
			DRIFT_DEFINE_COLUMN(state->flow_maps[i].entity),
			DRIFT_DEFINE_COLUMN(state->flow_maps[i].flow),
			DRIFT_DEFINE_COLUMN(state->flow_maps[i].is_valid),
		}, 0);
		state->flow_maps[i].flow[0].dist = INFINITY;
	}	
	
	DriftComponentNav *navs = DriftAlloc(state->mem, sizeof(*navs));
	DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(state, navs, DriftComponentNav, ((DriftColumnSet){
		DRIFT_DEFINE_COLUMN(navs->entity),
		DRIFT_DEFINE_COLUMN(navs->data),
	}), 0);
	
	DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(state, &state->health, DriftComponentHealth, ((DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->health.entity),
		DRIFT_DEFINE_COLUMN(state->health.data),
	}), 0);
	
	DriftSystemsInitWeapons(state);
	DriftSystemsInitEnemies(state);
}
