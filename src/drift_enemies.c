#include <stdlib.h>
#include "drift_game.h"

static DriftEntity SpawnGlowBug(DriftGameState* state, DriftVec2 pos, DriftVec2 rot){
	DriftEntity e = DriftMakeEntity(state);
	DriftComponentAdd(&state->transforms.c, e);

	uint enemy_idx = DriftComponentAdd(&state->enemies.c, e);
	state->enemies.type[enemy_idx] = DRIFT_ENEMY_GLOW_BUG;
	
	uint scan_idx = DriftComponentAdd(&state->scan.c, e);
	state->scan.type[scan_idx] = DRIFT_SCAN_GLOW_BUG;
	
	uint nav_idx = DriftComponentAdd(&state->bug_nav.c, e);
	state->bug_nav.speed[nav_idx] = 50;
	state->bug_nav.accel[nav_idx] = 150;

	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	float mass = 2, radius = 8;
	state->bodies.position[body_idx] = pos;
	state->bodies.rotation[body_idx] = rot;
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 2/(mass*radius*radius);
	state->bodies.radius[body_idx] = radius;
	state->bodies.collision_type[body_idx] = DRIFT_COLLISION_NON_HOSTILE;
	
	uint health_idx = DriftComponentAdd(&state->health.c, e);
	state->health.data [health_idx] = (DriftHealth){.value = 25, .maximum = 25, .drop = DRIFT_ITEM_LUMIUM};
	
	return e;
}

static DriftEntity SpawnWorkerDrone(DriftGameState* state, DriftVec2 pos, DriftVec2 rot){
	DriftEntity e = DriftMakeEntity(state);
	DriftComponentAdd(&state->transforms.c, e);

	uint enemy_idx = DriftComponentAdd(&state->enemies.c, e);
	state->enemies.type[enemy_idx] = DRIFT_ENEMY_WORKER_BUG;
	
	uint scan_idx = DriftComponentAdd(&state->scan.c, e);
	state->scan.type[scan_idx] = DRIFT_SCAN_HIVE_WORKER;
	
	uint nav_idx = DriftComponentAdd(&state->bug_nav.c, e);
	state->bug_nav.speed[nav_idx] = 100;
	state->bug_nav.accel[nav_idx] = 300;

	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	float mass = 8, radius = 12;
	state->bodies.position[body_idx] = pos;
	state->bodies.rotation[body_idx] = rot;
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 2/(mass*radius*radius);
	state->bodies.radius[body_idx] = radius;
	state->bodies.collision_type[body_idx] = DRIFT_COLLISION_WORKER_DRONE;

	uint health_idx = DriftComponentAdd(&state->health.c, e);
	state->health.data [health_idx] = (DriftHealth){.value = 100, .maximum = 100, .drop = DRIFT_ITEM_SCRAP};
	
	return e;
}

bool DriftWorkerDroneCollide(DriftUpdate* update, DriftPhysics* phys, DriftIndexPair pair){
	DriftGameState* state = update->state;
	
	uint enemy_idx = DriftComponentFind(&state->enemies.c, state->bodies.entity[pair.idx1]);
	if(state->enemies.aggro_ticks[enemy_idx]){
		DriftVec2 p0 = phys->x[pair.idx0], p1 = phys->x[pair.idx1];
		
		DriftVec2 delta = DriftVec2Sub(phys->x[pair.idx0], phys->x[pair.idx1]);
		DriftVec2 dir = DriftVec2Normalize(delta);
		phys->v[pair.idx1] = DriftVec2Mul(dir, -100);
		phys->q[pair.idx1] = DriftVec2Perp(dir);
		
		DriftHealthApplyDamage(update, state->bodies.entity[pair.idx0], 20);
	}
	
	return true;
}

static void tick_spawns(DriftUpdate* update, DriftVec2 player_pos){
	DriftGameState* state = update->state;
	float dt = update->tick_dt;
	
	uint enemy_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &state->enemies.c, .variable = &enemy_idx},
		{.component = &state->bodies.c, .variable = &body_idx},
		{},
	});
	
	// First despawn enemies that are far away.
	while(DriftJoinNext(&join)){
		DriftVec2 pos = state->bodies.position[body_idx];
		
		// TODO push these checks into a list.
		DriftVec2 delta = DriftVec2Sub(player_pos, pos);
		if(DriftVec2Length(delta) > 1024){
			DriftDestroyEntity(update, join.entity);
		}
	}
	
	// Then spawn new enemies to fill in.
	static uint spawn_counter = 0;
	if(update->tick % (uint)(1*DRIFT_TICK_HZ) == 0){
		int count = 50 - state->enemies.c.count;
		spawn_counter = count > 0 ? count : 0;
	}
	
	for(uint retries = 0; retries < 10 && spawn_counter; retries++){
		DriftVec2 offset = DriftRandomInUnitCircle();
		DriftVec2 pos = DriftVec2FMA(player_pos, offset, DRIFT_SPAWN_RADIUS);
		if(DriftCheckSpawn(update, pos, 8)){
			DriftSelectionContext ctx = {.rand = rand()};
			DriftEntity (*spawn_func)(DriftGameState*, DriftVec2, DriftVec2) = NULL;
			
			switch(DriftTerrainSampleBiome(state->terra, pos).idx){
				default:
				case DRIFT_BIOME_LIGHT:{
					if(DriftSelectWeight(&ctx, 100)) spawn_func = SpawnGlowBug;
					if(DriftSelectWeight(&ctx, 20)) spawn_func = SpawnWorkerDrone;
				} break;
			}
			
			spawn_func(state, pos, DriftVec2Normalize(offset));
			spawn_counter--;
		}
	}
}

static void tick_enemies(DriftUpdate* update, DriftVec2 player_pos){
	DriftGameState* state = update->state;
	bool player_alive = DriftEntitySetCheck(&update->state->entities, update->ctx->player);
	
	// TODO track a nearest enemy for each and bias away from it as well.
	
	DRIFT_COMPONENT_FOREACH(&state->enemies.c, enemy_idx){
		// Decrement aggro ticks.
		u16* ticks = state->enemies.aggro_ticks + enemy_idx;
		if(*ticks) (*ticks)--;
	}
	
	uint enemy_idx, nav_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &state->enemies.c, .variable = &enemy_idx},
		{.component = &state->bug_nav.c, .variable = &nav_idx},
		{.component = &state->bodies.c, .variable = &body_idx},
		{},
	});
	
	while(DriftJoinNext(&join)){
		DriftVec2 pos = state->bodies.position[body_idx];
		DriftVec2 player_delta = DriftVec2Sub(pos, player_pos);
		float player_dist = DriftVec2Length(player_delta);
		
		u16* aggro_ticks = state->enemies.aggro_ticks + enemy_idx;
		// disable aggro if the player is dead.
		if(!player_alive) *aggro_ticks = 0;
		bool aggro = (*aggro_ticks != 0);
		
		// Deflect from the player.
		float bias = DriftSaturate((48 - player_dist)/48);
		state->bug_nav.forward_bias[nav_idx] = DriftVec2Mul(player_delta, bias*bias);
		
		switch(state->enemies.type[enemy_idx]){
			case DRIFT_ENEMY_GLOW_BUG: {
				if(aggro){
					state->bug_nav.forward_bias[nav_idx] = DriftVec2Mul(player_delta, 0.2f/DriftVec2Length(player_delta));
					state->bug_nav.speed[nav_idx] = 100;
					state->bug_nav.accel[nav_idx] = 300;
				} else {
					state->bug_nav.speed[nav_idx] = 50;
					state->bug_nav.accel[nav_idx] = 150;
				}
			} break;
			
			case DRIFT_ENEMY_WORKER_BUG: {
				if(aggro){
					state->bug_nav.forward_bias[nav_idx] = DriftVec2Mul(player_delta, -0.3f/DriftVec2Length(player_delta));
					state->bug_nav.speed[nav_idx] = 150;
					state->bug_nav.accel[nav_idx] = 900;
				} else {
					state->bug_nav.speed[nav_idx] = 100;
					state->bug_nav.accel[nav_idx] = 300;
				}
			} break;
			
			default: break;
		}
		
		// if(aggro) DriftDebugCircle(state, pos, 5, DRIFT_RGBA8_RED);
	}
}

static void tick_navs(DriftUpdate* update){
	DriftGameState* state = update->state;
	
	uint nav_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &state->bug_nav.c, .variable = &nav_idx},
		{.component = &state->bodies.c, .variable = &body_idx},
		{},
	});
	
	float period = 2e9;
	float phase = 2*(float)M_PI/period*(update->ctx->tick_nanos % (u64)period), inc = 0.2f;
	DriftVec2 rot = {cosf(phase), sinf(phase)}, rinc = {cosf(inc), sinf(inc)};

	while(DriftJoinNext(&join)){
		DriftVec2 pos = state->bodies.position[body_idx];
		DriftVec2 forward = DriftVec2Perp(state->bodies.rotation[body_idx]);
		DriftVec2 forward_bias = DriftVec2Add(forward, state->bug_nav.forward_bias[nav_idx]);
		
		// Push the forward vector away from terrain.
		DriftTerrainSampleInfo info = DriftTerrainSampleFine(state->terra, pos);
		float terrain_bias = DriftSaturate((45 - info.dist)/35);
		forward_bias = DriftVec2FMA(forward_bias, info.grad, terrain_bias*terrain_bias);
		// Push it towards a random rotating direction.
		forward_bias = DriftVec2FMA(forward_bias, rot, 0.3f);
		
		forward_bias = DriftVec2Normalize(forward_bias);
		// DriftDebugSegment(state, pos, DriftVec2FMA(pos, forward_bias, 20), 1, DRIFT_RGBA8_RED);
		
		DriftVec2* v = state->bodies.velocity + body_idx;
		float* w = state->bodies.angular_velocity + body_idx;
		// Accelerate towards forward bias.
		const float speed = state->bug_nav.speed[nav_idx], accel = state->bug_nav.accel[nav_idx];
		*v = DriftVec2LerpConst(*v, DriftVec2Mul(forward_bias, speed), accel*update->tick_dt);
		// Rotate to align with motion.
		*w = (*w)*0.7f + DriftVec2Cross(forward, *v)/50;
		
		// Increment the rotation to put the next bug out of phase with this one.
		rot = DriftVec2Rotate(rot, rinc);
	}
}

void DriftTickEnemies(DriftUpdate* update){
	DriftGameState* state = update->state;
	uint player_body_idx = DriftComponentFind(&state->bodies.c, update->ctx->player);
	DriftVec2 player_pos = state->bodies.position[player_body_idx];
	
	tick_spawns(update, player_pos);
	tick_enemies(update, player_pos);
	tick_navs(update);
}

static uint anim_loop(uint tick, uint div, uint f0, uint f1){
	return f0 + (tick/div)%(f1 - f0 + 1);
}

static void DrawGlowBug(DriftDraw* draw, DriftEntity e, DriftAffine transform){
	DriftGameState* state = draw->state;
	uint tick = e.id + draw->tick;
	uint frame = anim_loop(tick++, 8, DRIFT_SPRITE_GLOW_BUG1, DRIFT_SPRITE_GLOW_BUG6);
	DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteMake(frame, DRIFT_RGBA8_WHITE, transform));
	
	DriftAffine m = DriftAffineMul(transform, (DriftAffine){90, 0, 0, 90, 0, -8});
	DRIFT_ARRAY_PUSH(draw->lights, DriftLightMake(false, DRIFT_SPRITE_LIGHT_RADIAL, (DriftVec4){{0.12f, 0.08f, 0.02f, 2.00f}}, m, 0));
}

static void DrawWorkerBug(DriftDraw* draw, DriftEntity e, DriftAffine transform){
	DriftGameState* state = draw->state;
	uint tick = e.id + draw->tick;
	uint frame = anim_loop(tick++, 8, DRIFT_SPRITE_WORKER_DRONE1, DRIFT_SPRITE_WORKER_DRONE4);//DRIFT_SPRITE_WORKER_DRONE1 + (tick++)/8%4;
	DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteMake(frame, DRIFT_RGBA8_WHITE, transform));
	
	DriftAffine m = DriftAffineMul(transform, (DriftAffine){30, 0, 0, 30, 0, -8});
	DRIFT_ARRAY_PUSH(draw->lights, DriftLightMake(false, DRIFT_SPRITE_LIGHT_RADIAL, (DriftVec4){{0.26f, 0.43f, 0.51f, 1.00f}}, m, 0));
}

typedef void DrawFunc(DriftDraw* draw, DriftEntity e, DriftAffine transform);
static DrawFunc* const DRAW_FUNCS[] = {
	[DRIFT_ENEMY_GLOW_BUG] = DrawGlowBug,
	[DRIFT_ENEMY_WORKER_BUG] = DrawWorkerBug,
};

void DriftDrawEnemies(DriftDraw* draw){
	DriftGameState* state = draw->state;
	
	uint enemy_idx, transform_idx, health_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &state->enemies.c, .variable = &enemy_idx},
		{.component = &state->transforms.c, .variable = &transform_idx},
		{.component = &state->health.c, .variable = &health_idx},
		{},
	});
	
	const uint health_div = 8;
	
	while(DriftJoinNext(&join)){
		DriftEnemyType type = state->enemies.type[enemy_idx];
		DriftAffine transform = state->transforms.matrix[transform_idx];
		DRAW_FUNCS[type](draw, join.entity, transform);
		
		DriftHealth* health = state->health.data + health_idx;
		uint frame = (uint)roundf(health_div*health->value/health->maximum);
		
		if(frame % health_div){
			DriftAffine t = {1, 0, 0, 1, transform.x, transform.y - 14};
			DRIFT_ARRAY_PUSH(draw->overlay_sprites, ((DriftSprite){
				.frame = DRIFT_SPRITE_FRAMES[DRIFT_SPRITE_HEALTH1 - 1 + frame], .color = DRIFT_RGBA8_WHITE, .matrix = t,
			}));
		}
	}
}
