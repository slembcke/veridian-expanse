/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include "drift_game.h"

typedef struct {
	DriftComponent c;
	DriftEntity* entity;

	float* speed;
	float* accel;
	DriftVec2* forward_bias;
} DriftComponentBugNav;

typedef struct {
	float health;
	float pod_progress;
	bool chunk_taken;
} DriftHiveData;

typedef struct {
	DriftComponent c;
	
	DriftEntity* entity;
	DriftHiveData* data;
} DriftComponentHives;

void DriftSystemsInitEnemies(DriftGameState* state){
	DriftComponentBugNav* bug_nav = DriftAlloc(state->mem, sizeof(*bug_nav));
	DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(state, bug_nav, DriftComponentBugNav, ((DriftColumnSet){
		DRIFT_DEFINE_COLUMN(bug_nav->entity),
		DRIFT_DEFINE_COLUMN(bug_nav->speed),
		DRIFT_DEFINE_COLUMN(bug_nav->accel),
		DRIFT_DEFINE_COLUMN(bug_nav->forward_bias),
	}), 0);
	
	DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(state, &state->enemies, DriftComponentEnemy, ((DriftColumnSet){
		DRIFT_DEFINE_COLUMN(state->enemies.entity),
		DRIFT_DEFINE_COLUMN(state->enemies.type),
		DRIFT_DEFINE_COLUMN(state->enemies.tile_idx),
		DRIFT_DEFINE_COLUMN(state->enemies.aggro_ticks),
	}), 0);
	
	DriftComponentHives* hives = DriftAlloc(state->mem, sizeof(*hives));
	DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(state, hives, DriftComponentHives, ((DriftColumnSet){
		DRIFT_DEFINE_COLUMN(hives->entity),
		DRIFT_DEFINE_COLUMN(hives->data),
	}), 0);
	hives->data[0] = (DriftHiveData){.health = 1000};
}

DriftEntity DriftSpawnEnemy(DriftGameState* state, DriftEnemyType type, DriftVec2 pos, DriftVec2 rot);

static DriftRGBA8 health_flash(uint tick, DriftHealth* health){
	return DriftRGBA8Fade(DRIFT_RGBA8_RED, 0.8f*DriftSaturate(1 - (tick - health->damage_tick0)/15.0f));
}

#define HIVE_POD_SIZE 15

bool AVOID_PLAYER = false;

typedef struct {
	DriftEntity entity;
	DriftEntity pod_entity;
	DriftVec2 pod_dest;
	
	DriftRandom rand[1];
} HiveContext;

typedef struct {
	DriftEntity key;
	DriftVec2 pos;
} HiveInfo;

static const HiveInfo HIVE_INFO[] = {
	{.key = {DRIFT_ENTITY_TAG_STATIC | 0}, .pos = { 4449.7f, -4214.3f}},
	{.key = {DRIFT_ENTITY_TAG_STATIC | 1}, .pos = { 2429.8f, -7057.4f}},
	{.key = {DRIFT_ENTITY_TAG_STATIC | 2}, .pos = { -737.8f, -5462.0f}},
	{.key = {DRIFT_ENTITY_TAG_STATIC | 3}, .pos = { -288.2f,   604.2f}},
	{.key = {DRIFT_ENTITY_TAG_STATIC | 4}, .pos = {-4130.9f,   817.5f}},
	{.key = {DRIFT_ENTITY_TAG_STATIC | 5}, .pos = {-4952.8f,  2214.7f}},
	{.key = {DRIFT_ENTITY_TAG_STATIC | 6}, .pos = {-2984.9f,  3390.6f}},
	{.key = {DRIFT_ENTITY_TAG_STATIC | 7}, .pos = {-1054.8f,  2325.7f}},
};
static const uint HIVE_COUNT = sizeof(HIVE_INFO)/sizeof(*HIVE_INFO);

static DriftHealth* get_health(DriftGameState* state, DriftEntity entity){
	return state->health.data + DriftComponentFind(&state->health.c, entity);
}

static DriftAffine get_transform(DriftGameState* state, DriftEntity entity){
	return state->transforms.matrix[DriftComponentFind(&state->transforms.c, entity)];
}

static void draw_hive(DriftDraw* draw, DriftScript* script, DriftHiveData* data){
	DriftGameState* state = draw->state;
	HiveContext* hive = script->draw_data;
	DriftAffine transform = get_transform(state, hive->entity);
	
	DriftVec2 pos = DriftAffineOrigin(transform);
	DriftVec2 pod = DriftVec2Lerp(pos, hive->pod_dest, data->pod_progress);
	DriftVec2 player_pos = state->bodies.position[DriftComponentFind(&state->bodies.c, draw->state->player)];
	
	if(data->pod_progress == 1){
		DriftVec2 p0 = DriftVec2LerpConst(pod, pos, HIVE_POD_SIZE);
		DriftVec2 p1 = DriftVec2LerpConst(pos, pod, 50);
		DriftDrawPlasma(draw, p0, p1, DriftGenPlasma(draw));
		DriftSprite sprite = {
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_HIVE_POD], .color = DRIFT_RGBA8_WHITE,
			.matrix = {1, 0, 0, 1, pod.x, pod.y}, .z = 200,
		};
		DRIFT_ARRAY_PUSH(draw->bg_sprites, sprite);
		
		DriftHealth* pod_health = state->health.data + DriftComponentFind(&state->health.c, hive->pod_entity);
		sprite.color = health_flash(draw->tick, pod_health);
		if(sprite.color.a) DRIFT_ARRAY_PUSH(draw->flash_sprites, sprite);
		
		if(state->scan_progress[DRIFT_SCAN_HIVE_POD] < 1){
			sprite.color = DriftHUDIndicator(draw, pod, (DriftRGBA8){0x00, 0x80, 0x80, 0x80});
			if(sprite.color.a) DRIFT_ARRAY_PUSH(draw->flash_sprites, sprite);
		}
		
		DriftVec2 dir = DriftVec2Normalize(DriftVec2Sub(player_pos, pod));
		DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_FLOOD], .color = (DriftVec4){{4.17f, 0.03f, 2.93f, 0.00f}},
			.matrix = {-50*dir.y, 50*dir.x, 200*dir.x, 200*dir.y, pod.x, pod.y}, .radius = 8,
		}));
		
		const char* bar = "########--------" + (uint)(8*pod_health->value/pod_health->maximum);
		DriftDrawTextF(draw, &draw->overlay_sprites, (DriftVec2){pod.x - 30, pod.y - 25}, "|%.8s|", bar);
	} else if(data->pod_progress > 0){
		const uint particle_count = 10;
		const u64 duration_nanos = 0.4e9;
		for(uint i = 0; i < particle_count; i++){
			u64 nanos = (draw->update_nanos + i*duration_nanos/particle_count);
			uint seed = nanos/duration_nanos + i;
			float alpha = (float)(nanos % duration_nanos)/(float)duration_nanos;
			
			DriftRandom rand[] = {{seed}};
			DriftRand32(rand);
			DriftRand32(rand);
			
			DriftVec2 delta = DriftRandomInUnitCircle(rand);
			DriftVec2 p = DriftVec2FMA(pod, (DriftVec2){30*delta.x, 30*delta.y}, DriftLerp(0.25f, 1.0f, alpha));
			DriftVec2 q = DriftVec2Normalize(delta);
			
			DRIFT_ARRAY_PUSH(draw->bg_sprites, ((DriftSprite){
				.frame = DRIFT_FRAMES[DRIFT_SPRITE_SMALL_ROCKS00 + DriftRand32(rand)%3],
				.color = DriftRGBA8Fade(DRIFT_RGBA8_WHITE, DriftSmoothstep(1.0f, 0.8f, alpha)),
				.matrix = {q.x, q.y, -q.y, q.x, p.x, p.y}, .z = 200 - (int)(400*alpha*(1 - alpha)),
			}));
		}
	}
	
	// Draw hive
	DriftSprite sprite = {
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_HIVE00 + draw->tick/4 % 28], .color = DRIFT_RGBA8_WHITE,
		.matrix = transform, .z = 200
	};
	DRIFT_ARRAY_PUSH(draw->bg_sprites, sprite);
	
	// Draw flash
	DriftHealth* hive_health = state->health.data + DriftComponentFind(&state->health.c, hive->entity);
	sprite.color = health_flash(draw->tick, hive_health);
	if(state->scan_progress[DRIFT_SCAN_HIVE] < 1){
		sprite.color = DriftRGBA8Composite(sprite.color, DriftHUDIndicator(draw, pos, (DriftRGBA8){0x00, 0x80, 0x80, 0x80}));
	}
	if(sprite.color.a) DRIFT_ARRAY_PUSH(draw->flash_sprites, sprite);
	
	DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = (DriftVec4){{0.02f, 0.00f, 0.09f, 0.00f}},
		.matrix = {250, 0, 0, 250, pos.x, pos.y},
	}));
	
	// Draw shield if invulnerable
	if(hive_health->timeout){
		DriftVec2 rot = DriftVec2Mul(DriftRandomOnUnitCircle(hive->rand), 0.5f);
		rot = DriftVec2Mul(rot, 2.5f);
		DRIFT_ARRAY_PUSH(draw->bg_sprites, ((DriftSprite){
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_SHIELD], .color = (DriftRGBA8){0x16, 0x4A, 0xCB, 0x00},
			.matrix = {rot.x, rot.y, rot.y, -rot.x, pos.x, pos.y}, .z = 150
		}));
		DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = DRIFT_VEC4_BLUE,
			.matrix = {250, 0, 0, 250, pos.x, pos.y},
		}));
	} else {
		const char* bar = "############------------" + (uint)(12*hive_health->value/hive_health->maximum);
		DriftDrawTextF(draw, &draw->overlay_sprites, (DriftVec2){pos.x - 50, pos.y - 50}, "|%.12s|", bar);
	}
}

static void hive_boss_draw(DriftDraw* draw, DriftScript* script){
	DriftGameState* state = script->state;
	HiveInfo* info = script->user_data;
	DriftComponentHives* hives = DRIFT_GET_TYPED_COMPONENT(state, DriftComponentHives);
	
	DriftHiveData* data = hives->data + DriftComponentFind(&hives->c, info->key);
	if(data->health > 0){
		draw_hive(draw, script, data);
	} else {
		DriftVec2 pos = DriftAffineOrigin(get_transform(state, info->key));
		DRIFT_ARRAY_PUSH(draw->bg_sprites, ((DriftSprite){
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_HIVE00], .color = DRIFT_RGBA8_WHITE, .matrix = {1, 0, 0, 1, pos.x, pos.y}, .z = 200
		}));
		
		uint frame0 = DRIFT_SPRITE_COPPER_DEPOSIT00, frame_n = DRIFT_SPRITE_COPPER_DEPOSIT11;
		DriftSprite sprite = {
			.frame = DRIFT_FRAMES[frame0 + (draw->tick/5)%(frame_n - frame0 - 1)], .color = DRIFT_RGBA8_WHITE, .matrix = {1, 0, 0, 1, pos.x, pos.y}, .z = 150
		};
		
		DRIFT_ARRAY_PUSH(draw->bg_sprites, sprite);
		if(state->scan_progress[DRIFT_SCAN_COPPER_DEPOSIT] < 1){
			sprite.color = DriftHUDIndicator(draw, pos, (DriftRGBA8){0x00, 0x80, 0x80, 0x80});
			if(sprite.color.a) DRIFT_ARRAY_PUSH(draw->flash_sprites, sprite);
		}
	}
}

static void hive_state_shield(DriftScript* script, HiveContext* hive, DriftHiveData* data){
	DriftGameState* state = script->state;
	DriftHealth* health = get_health(state, hive->entity);
	health->timeout = INFINITY; // TODO this suppresses sounds
	health->hit_sfx = DRIFT_SFX_SHIELD_SPARK;
	data->pod_progress = 1;
	
	// Make a shield pod and wait for the player to blow that up.
	hive->pod_entity = DriftMakeHotEntity(state);
	uint transform_idx = DriftComponentAdd(&state->transforms.c, hive->pod_entity);
	uint scan_idx = DriftComponentAdd(&state->scan.c, hive->pod_entity);
	state->scan.type[scan_idx] = DRIFT_SCAN_HIVE_POD;
	
	uint body_idx = DriftComponentAdd(&state->bodies.c, hive->pod_entity);
	state->bodies.position[body_idx] = hive->pod_dest;
	state->bodies.rotation[body_idx] = DriftRandomOnUnitCircle(hive->rand);
	state->bodies.radius[body_idx] = 15;
	state->bodies.collision_type[body_idx] = DRIFT_COLLISION_HIVE;
	
	uint health_idx = DriftComponentAdd(&state->health.c, hive->pod_entity);
	state->health.data[health_idx] = (DriftHealth){
		.value = 100, .maximum = 100, .timeout = 0, .drop = DRIFT_ITEM_VIRIDIUM,
		.hit_sfx = DRIFT_SFX_BULLET_HIT, .die_sfx = DRIFT_SFX_EXPLODE,
	};
	
	while(DriftScriptYield(script)
		&& DriftEntitySetCheck(&state->entities, hive->entity)
		&& DriftEntitySetCheck(&state->entities, hive->pod_entity)
	){
		DriftUpdate* update = script->update;
		DriftVec2 player_pos = state->bodies.position[DriftComponentFind(&state->bodies.c, update->state->player)];
		DriftVec2 delta = DriftVec2Sub(player_pos, hive->pod_dest);
		if(update->tick % 60 == 0 && DriftVec2Length(delta) < 500){
			FireHiveProjectile(update, (DriftRay2){hive->pod_dest, DriftVec2Normalize(delta)});
		}
	}
	
	if(script->run) data->pod_progress = 0;
	get_health(state, hive->entity)->timeout = 0;
	DriftDestroyEntity(state, hive->pod_entity);
}

static void hive_state_aggro(DriftScript* script, HiveContext* hive, DriftHiveData* data){
	DriftGameState* state = script->state;
	
	static const DriftEnemyType SPAWNS[][4] = {
		{DRIFT_ENEMY_FIGHTER_BUG, DRIFT_ENEMY_FIGHTER_BUG, DRIFT_ENEMY_NONE},
		{DRIFT_ENEMY_WORKER_BUG, DRIFT_ENEMY_FIGHTER_BUG, DRIFT_ENEMY_NONE},
		{DRIFT_ENEMY_WORKER_BUG, DRIFT_ENEMY_WORKER_BUG, DRIFT_ENEMY_NONE},
		{DRIFT_ENEMY_WORKER_BUG, DRIFT_ENEMY_NONE},
	};
	
	DriftHealth* health = get_health(state, hive->entity);
	health->hit_sfx = DRIFT_SFX_THUD2;
	float health_per_stage = health->maximum/4;
	float health_min = health->value - health_per_stage;
	float health_prev = health->value;
	const uint* spawns = SPAWNS[(uint)(4*health->value/health->maximum)];
	
	DriftAffine transform = get_transform(state, hive->entity);
	DriftVec2 pos = DriftAffineOrigin(transform);
	DriftVec2 dir = DriftVec2Mul(DriftRandomOnUnitCircle(hive->rand), 200);
	DriftVec2 dest = DriftAffinePoint(transform, dir);
	float t = DriftTerrainRaymarch(state->terra, pos, dest, 2*HIVE_POD_SIZE, 1);
	hive->pod_dest = DriftVec2Lerp(pos, dest, t);
	
	AVOID_PLAYER = true;
	
	float aggro_duration = 15;
	uint ticks = 0, tick0 = script->update->tick;
	while(DriftScriptYield(script) && DriftEntitySetCheck(&state->entities, hive->entity)){
		if(ticks % 30 == 0 && *spawns){
			// TODO spawn from tunnel
			DriftEntity enemy = DriftSpawnEnemy(state, *spawns, pos, (DriftVec2){1, 0});
			// TODO move this as a spawning param?
			state->enemies.aggro_ticks[DriftComponentFind(&state->enemies.c, enemy)] = (uint)(600*DRIFT_TICK_HZ);
			state->health.data[DriftComponentFind(&state->health.c, enemy)].drop = DRIFT_ITEM_NONE;
			spawns++;
		}
		
		health = get_health(state, hive->entity);
		data->health = health->value;
		
		data->pod_progress += 1/(aggro_duration*DRIFT_TICK_HZ) + (health_prev - health->value)/health_per_stage;
		health_prev = health->value;
		
		if(data->pod_progress >= 1){
			health->value = fmaxf(health->value, health_min);
			hive_state_shield(script, hive, data);
			break;
		}
		
		ticks++;
	}
	
	// copy final health value
	data->health = get_health(state, hive->entity)->value;
	AVOID_PLAYER = false;
}

static void hive_normal_state(DriftScript* script, HiveContext* hive, DriftHiveData* data){
	DriftGameState* state = script->state;
	HiveInfo* info = script->user_data;
	DriftComponentHives* hives = DRIFT_GET_TYPED_COMPONENT(state, DriftComponentHives);
	
	DriftEntity entity = hive->entity = DriftMakeHotEntity(state);
	uint transform_idx = DriftComponentAdd(&state->transforms.c, entity);

	uint scan_idx = DriftComponentAdd(&state->scan.c, hive->entity);
	state->scan.type[scan_idx] = DRIFT_SCAN_HIVE;
	
	uint body_idx = DriftComponentAdd(&state->bodies.c, hive->entity);
	state->bodies.position[body_idx] = info->pos;
	state->bodies.rotation[body_idx] = DriftRandomOnUnitCircle(hive->rand);
	state->bodies.radius[body_idx] = 37;
	state->bodies.collision_type[body_idx] = DRIFT_COLLISION_HIVE;
	
	uint health_idx = DriftComponentAdd(&state->health.c, hive->entity);
	state->health.data[health_idx] = (DriftHealth){
		.value = data->health, .maximum = hives->data[0].health,
		.hit_sfx = DRIFT_SFX_THUD1, .die_sfx = DRIFT_SFX_HIVE_DEATH,
	};
	
	DRIFT_LOG("spawned hive, health: %.1f, prog: %.1f", data->health, data->pod_progress);
	while(DriftScriptYield(script) && DriftEntitySetCheck(&state->entities, hive->entity)){
		DriftHealth* health = get_health(state, hive->entity);
		if(health->value < health->maximum) hive_state_aggro(script, hive, data);
	}
	DriftDestroyEntity(state, hive->entity);
	DRIFT_LOG("destroyed hive, health: %.1f, prog: %.1f", data->health, data->pod_progress);
}

static void hive_destroyed_state(DriftScript* script, HiveContext* hive, DriftHiveData* data){
	DriftGameState* state = script->state;
	
	HiveInfo* info = script->user_data;
	DriftVec2 pos = info->pos;
	uint transform_idx = DriftComponentAdd2(&state->transforms.c, info->key, false);
	state->transforms.matrix[transform_idx] = (DriftAffine){1, 0, 0, 1, pos.x, pos.y};

	uint scan_idx = DriftComponentAdd2(&state->scan.c, info->key, false);
	state->scan.type[scan_idx] = DRIFT_SCAN_COPPER_DEPOSIT;
	
	if(!data->chunk_taken){
		DriftEntity chunk = DriftItemMake(state, DRIFT_ITEM_COPPER, pos, DRIFT_VEC2_ZERO, 0);
		while(DriftScriptYield(script) && DriftEntitySetCheck(&state->entities, chunk)){
			DriftUpdate* update = script->update;
			DriftVec2 orbit = DriftWaveComplex(update->nanos, 0.032f);
			orbit = DriftVec2FMA(orbit, DriftWaveComplex(update->nanos, 0.1f), 3.0f);
			orbit = DriftVec2FMA(pos, orbit, 16);
			// DriftDebugCircle(state, orbit, 5, DRIFT_RGBA8_RED);
			
			uint body_idx = DriftComponentFind(&state->bodies.c, chunk);
			DriftVec2 delta = DriftVec2Sub(orbit, state->bodies.position[body_idx]);
			state->bodies.velocity[body_idx] = DriftVec2Mul(delta, 1e-2f/update->tick_dt);
			
			if(DriftVec2Length(delta) > 200){
				DriftDestroyEntity(state, chunk);
				chunk = DriftItemMake(state, DRIFT_ITEM_COPPER, pos, DRIFT_VEC2_ZERO, 0);
			}
			
			float* w = state->bodies.angular_velocity + body_idx;
			*w = DriftLerp(-3, *w, expf(-update->tick_dt));
		}
		
		if(DriftEntitySetCheck(&state->entities, chunk)){
			DriftDestroyEntity(state, chunk);
		} else {
			DRIFT_LOG("hive chunk taken");
			data->chunk_taken = true;
		}
	} else {
		DRIFT_LOG("hive no chunk");
	}
}

static bool hive_boss_check(DriftScript* script){
	DriftGameState* state = script->state;
	uint player_body_idx = DriftComponentFind(&state->bodies.c, state->player);
	DriftVec2 player_pos = state->bodies.position[player_body_idx];
	
	HiveInfo* info = script->user_data;
	return DriftVec2Near(player_pos, info->pos, 1000);
}

static void hive_boss_body(DriftScript* script){
	HiveContext hive = {};
	
	script->draw = hive_boss_draw;
	script->draw_data = &hive;
	
	HiveInfo* info = script->user_data;
	DriftGameState* state = script->state;
	DriftComponentHives* hives = DRIFT_GET_TYPED_COMPONENT(state, DriftComponentHives);
	
	uint data_idx = DriftComponentFind(&hives->c, info->key);
	if(data_idx == 0) data_idx = DriftComponentAdd(&hives->c, info->key);
	DriftHiveData* data = hives->data + data_idx;
	
	if(data->health > 0) hive_normal_state(script, &hive, data);
	if(script->run) hive_destroyed_state(script, &hive, data);
}

static void hive_boss_script(DriftScript* script){
	script->check = hive_boss_check;
	script->body = hive_boss_body;
}

static void tick_hive_boss(DriftUpdate* update, DriftVec2 player_pos){
	DriftGameState* state = update->state;
	float radius = 1000;
	
	if(state->script == NULL){
		// Look for a nearby hive.
		for(uint i = 0; i < HIVE_COUNT; i++){
			if(DriftVec2Near(player_pos, HIVE_INFO[i].pos, radius)){
				state->script = DriftScriptNew(hive_boss_script, (void*)(HIVE_INFO + i), update->ctx);
				break;
			}
		}
	}
}

typedef struct {
	DriftEnemyType type;
	DriftScanType scan;
	float mass, radius;
	DriftCollisionType collision;
	DriftHealth health;
} DriftEnemyInfo;

static DriftEntity spawn_enemy(DriftGameState* state, DriftVec2 pos, DriftVec2 rot, DriftEnemyInfo info){
	DriftEntity e = DriftMakeEntity(state);
	DriftComponentAdd(&state->transforms.c, e);

	uint enemy_idx = DriftComponentAdd(&state->enemies.c, e);
	state->enemies.type[enemy_idx] = info.type;
	
	uint scan_idx = DriftComponentAdd(&state->scan.c, e);
	state->scan.type[scan_idx] = info.scan;
	
	uint body_idx = DriftComponentAdd(&state->bodies.c, e);
	float mass = info.mass, radius = info.radius;
	state->bodies.position[body_idx] = pos;
	state->bodies.rotation[body_idx] = rot;
	state->bodies.mass_inv[body_idx] = 1/mass;
	state->bodies.moment_inv[body_idx] = 2/(mass*radius*radius);
	state->bodies.radius[body_idx] = radius;
	state->bodies.collision_type[body_idx] = info.collision;
	
	uint health_idx = DriftComponentAdd(&state->health.c, e);
	state->health.data[health_idx] = info.health;
	
	return e;
}

static DriftEntity spawn_glow_bug(DriftGameState* state, DriftVec2 pos, DriftVec2 rot){
	DriftEntity e = spawn_enemy(state, pos, rot, (DriftEnemyInfo){
		.type = DRIFT_ENEMY_GLOW_BUG, .scan = DRIFT_SCAN_GLOW_BUG,
		.mass = 2, .radius = 8, .collision = DRIFT_COLLISION_NON_HOSTILE,
		.health = {.value = 20, .maximum = 20, .drop = DRIFT_ITEM_LUMIUM, .hit_sfx = DRIFT_SFX_THUD1, .die_sfx = DRIFT_SFX_EXPLODE},
	});
	
	DriftComponentAdd(DriftGetNamedComponent(state, DRIFT_STR(DriftComponentBugNav)), e);
	return e;
}

static DriftEntity spawn_worker_bug(DriftGameState* state, DriftVec2 pos, DriftVec2 rot){
	DriftEntity e = spawn_enemy(state, pos, rot, (DriftEnemyInfo){
		.type = DRIFT_ENEMY_WORKER_BUG, .scan = DRIFT_SCAN_HIVE_WORKER,
		.mass = 8, .radius = 12, .collision = DRIFT_COLLISION_HIVE_DRONE,
		.health = {.value = 50, .maximum = 50, .drop = DRIFT_ITEM_SCRAP, .hit_sfx = DRIFT_SFX_THUD1, .die_sfx = DRIFT_SFX_EXPLODE},
	});
	
	DriftComponentAdd(DriftGetNamedComponent(state, DRIFT_STR(DriftComponentBugNav)), e);
	return e;
}

static DriftEntity spawn_fighter_bug(DriftGameState* state, DriftVec2 pos, DriftVec2 rot){
	DriftEntity e = spawn_enemy(state, pos, rot, (DriftEnemyInfo){
		.type = DRIFT_ENEMY_FIGHTER_BUG, .scan = DRIFT_SCAN_HIVE_FIGHTER,
		.mass = 8, .radius = 12, .collision = DRIFT_COLLISION_HIVE_DRONE,
		.health = {.value = 100, .maximum = 100, .drop = DRIFT_ITEM_SCRAP, .hit_sfx = DRIFT_SFX_THUD1, .die_sfx = DRIFT_SFX_EXPLODE},
	});
	
	DriftComponentAdd(DriftGetNamedComponent(state, DRIFT_STR(DriftComponentBugNav)), e);
	return e;
}

static DriftEntity spawn_trilobyte(DriftGameState* state, DriftVec2 pos, DriftVec2 rot){
	DriftEntity e = spawn_enemy(state, pos, rot, (DriftEnemyInfo){
		.type = DRIFT_ENEMY_TRILOBYTE_LARGE, .scan = DRIFT_SCAN_TRILOBYTE_LARGE,
		.mass = 8, .radius = 14, .collision = DRIFT_COLLISION_HIVE_DRONE,
		.health = {.value = 400, .maximum = 400, .drop = DRIFT_ITEM_SCRAP, .hit_sfx = DRIFT_SFX_THUD1, .die_sfx = DRIFT_SFX_EXPLODE},
	});
	
	DriftComponentAdd(DriftGetNamedComponent(state, DRIFT_STR(DriftComponentBugNav)), e);
	return e;
}

static DriftEntity spawn_nautilus(DriftGameState* state, DriftVec2 pos, DriftVec2 rot){
	DriftEntity e = spawn_enemy(state, pos, rot, (DriftEnemyInfo){
		.type = DRIFT_ENEMY_NAUTILUS_HEAVY, .scan = DRIFT_SCAN_NAUTILUS_HEAVY,
		.mass = 8, .radius = 20, .collision = DRIFT_COLLISION_HIVE_DRONE,
		.health = {.value = 250, .maximum = 250, .drop = DRIFT_ITEM_SCRAP, .hit_sfx = DRIFT_SFX_THUD1, .die_sfx = DRIFT_SFX_EXPLODE},
	});
	
	DriftComponentAdd(DriftGetNamedComponent(state, DRIFT_STR(DriftComponentBugNav)), e);
	return e;
}

// TODO hrm, need more enemy types before deciding what to do here.
typedef DriftEntity (*DriftEnemySpawnFunc)(DriftGameState* state, DriftVec2 pos, DriftVec2 rot);
DriftEnemySpawnFunc DRIFT_ENEMY_SPAWNS[_DRIFT_ENEMY_COUNT] = {
	[DRIFT_ENEMY_WORKER_BUG] = spawn_worker_bug,
	[DRIFT_ENEMY_FIGHTER_BUG] = spawn_fighter_bug,
};

DriftEntity DriftSpawnEnemy(DriftGameState* state, DriftEnemyType type, DriftVec2 pos, DriftVec2 rot){
	return (DRIFT_ENEMY_SPAWNS[type])(state, pos, rot);
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
		
		DriftHealthApplyDamage(update, state->bodies.entity[pair.idx0], 20, p1);
	}
	
	return true;
}

static void tick_spawns(DriftUpdate* update, DriftVec2 player_pos){
	static DriftRandom rand[1]; // TODO static global
	DriftGameState* state = update->state;
	DriftTerrain* terra = state->terra;
	DriftTutorialSpawnPhase spawn_phase = state->status.spawn_phase;
	
	uint indexes[200];
	uint tile_count = DriftTerrainSpawnTileIndexes(terra, indexes, 200, player_pos, DRIFT_SPAWN_RADIUS);
	
	for(uint i = 0; i < tile_count; i++){
		uint tile_idx = indexes[i];
		if(spawn_phase == DRIFT_TUTORIAL_SPAWN_LIMIT && (tile_idx & 1) == 0) continue;
		
		uint spawn_count = DriftTerrainTileBiomass(terra, tile_idx);
		if(spawn_count == 0) continue;
		DRIFT_ASSERT_WARN(spawn_count <=1, "too many spawns %d", spawn_count);
		
		DRIFT_ASSERT(spawn_count <= 6, "spawn count overflow on tile %d of %d.", tile_idx, spawn_count);
		DriftVec2 locations[6];
		uint location_count = DriftTerrainSpawnLocations(terra, locations, 6, tile_idx, tile_idx, 4);
		if(location_count < spawn_count) spawn_count = location_count;
		
		for(uint i = 0; i < spawn_count; i++){
			DriftVec2 pos = locations[i];
			DriftReservoir res = DriftReservoirMake(rand);
			DriftEntity (*spawn_func)(DriftGameState* state, DriftVec2 pos, DriftVec2 rot) = NULL;
			
			switch(DriftTerrainSampleBiome(state->terra, pos).idx){
				default:
				case DRIFT_BIOME_LIGHT:{
					static const float WORKER_PHASE[] = {[DRIFT_TUTORIAL_SPAWN_LIMIT] = 0, [DRIFT_TUTORIAL_SPAWN_DRONES] = 100, [DRIFT_TUTORIAL_SPAWN_NORMAL] = 20};
					static const float FIGHTER_PHASE[] = {[DRIFT_TUTORIAL_SPAWN_LIMIT] = 0, [DRIFT_TUTORIAL_SPAWN_DRONES] = 0, [DRIFT_TUTORIAL_SPAWN_NORMAL] = 1};
					if(DriftReservoirSample(&res, 100)) spawn_func = spawn_glow_bug;
					if(DriftReservoirSample(&res, WORKER_PHASE[spawn_phase])) spawn_func = spawn_worker_bug;
					if(DriftReservoirSample(&res, FIGHTER_PHASE[spawn_phase])) spawn_func = spawn_fighter_bug;
				} break;
				case DRIFT_BIOME_CRYO:{
					if(DriftReservoirSample(&res, 10)) spawn_func = spawn_trilobyte;
					if(DriftReservoirSample(&res, 30)) spawn_func = spawn_nautilus;
				} break;
			}
			
			DriftEntity e = spawn_func(state, pos, DriftVec2Normalize(DriftVec2Sub(player_pos, pos)));
			state->enemies.tile_idx[DriftComponentFind(&state->enemies.c, e)] = tile_idx;
		}
	}
	
	uint enemy_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &state->enemies.c, .variable = &enemy_idx},
		{.component = &state->bodies.c, .variable = &body_idx},
		{},
	});
	
	// First despawn enemies that are far away.
	while(DriftJoinNext(&join)){
		DriftVec2 pos = state->bodies.position[body_idx];
		
		DriftVec2 delta = DriftVec2Sub(player_pos, pos);
		if(DriftVec2Length(delta) > DRIFT_SPAWN_RADIUS){
			uint tile_idx = state->enemies.tile_idx[DriftComponentFind(&state->enemies.c, join.entity)];
			DriftTerrainTileBiomassInc(terra, tile_idx);
			DriftDestroyEntity(update->state, join.entity);
		}
	}
}

static void tick_enemies(DriftUpdate* update, DriftVec2 player_pos){
	DriftGameState* state = update->state;
	DriftComponentBugNav* bug_nav = DRIFT_GET_TYPED_COMPONENT(state, DriftComponentBugNav);
	bool player_alive = DriftEntitySetCheck(&update->state->entities, update->state->player);
	
	// TODO track a nearest enemy for each and bias away from it as well.
	
	DRIFT_COMPONENT_FOREACH(&state->enemies.c, enemy_idx){
		// Decrement aggro ticks.
		u16* ticks = state->enemies.aggro_ticks + enemy_idx;
		if(*ticks) (*ticks)--;
	}
	
	uint enemy_idx, nav_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &state->enemies.c, .variable = &enemy_idx},
		{.component = &bug_nav->c, .variable = &nav_idx},
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
		bug_nav->forward_bias[nav_idx] = DriftVec2Mul(player_delta, bias*bias);
		
		switch(state->enemies.type[enemy_idx]){
			case DRIFT_ENEMY_GLOW_BUG: {
				if(aggro){
					bug_nav->forward_bias[nav_idx] = DriftVec2Mul(player_delta, 0.2f/DriftVec2Length(player_delta));
					bug_nav->speed[nav_idx] = 100;
					bug_nav->accel[nav_idx] = 300;
				} else {
					bug_nav->speed[nav_idx] = 50;
					bug_nav->accel[nav_idx] = 150;
				}
			} break;
			
			case DRIFT_ENEMY_WORKER_BUG: {
				if(aggro){
					bug_nav->forward_bias[nav_idx] = DriftVec2Mul(player_delta, -0.3f/DriftVec2Length(player_delta));
					bug_nav->speed[nav_idx] = 150;
					bug_nav->accel[nav_idx] = 900;
				} else {
					if(AVOID_PLAYER) bug_nav->forward_bias[nav_idx] = DriftVec2Mul(player_delta, 0.2f/DriftVec2Length(player_delta));
					bug_nav->speed[nav_idx] = 100;
					bug_nav->accel[nav_idx] = 300;
				}
			} break;
			
			case DRIFT_ENEMY_FIGHTER_BUG: {
				if(aggro){
					bug_nav->forward_bias[nav_idx] = DriftVec2Mul(player_delta, -0.3f/DriftVec2Length(player_delta));
					bug_nav->speed[nav_idx] = 250;
					bug_nav->accel[nav_idx] = 1200;
				} else {
					if(AVOID_PLAYER) bug_nav->forward_bias[nav_idx] = DriftVec2Mul(player_delta, 0.2f/DriftVec2Length(player_delta));
					bug_nav->speed[nav_idx] = 150;
					bug_nav->accel[nav_idx] = 450;
				}
			} break;
			
			case DRIFT_ENEMY_TRILOBYTE_LARGE: {
				if(aggro){
					bug_nav->forward_bias[nav_idx] = DriftVec2Mul(player_delta, -0.3f/DriftVec2Length(player_delta));
					bug_nav->speed[nav_idx] = 150;
					bug_nav->accel[nav_idx] = 900;
				} else {
					bug_nav->speed[nav_idx] = 100;
					bug_nav->accel[nav_idx] = 300;
				}
			} break;
			
			case DRIFT_ENEMY_NAUTILUS_HEAVY: {
				if(aggro){
					bug_nav->forward_bias[nav_idx] = DriftVec2Mul(player_delta, -0.3f/DriftVec2Length(player_delta));
					bug_nav->speed[nav_idx] = 150;
					bug_nav->accel[nav_idx] = 900;
				} else {
					bug_nav->speed[nav_idx] = 50;
					bug_nav->accel[nav_idx] = 100;
				}
			} break;
			
			default: break;
		}
		
		// if(aggro) DriftDebugCircle(state, pos, 5, DRIFT_RGBA8_RED);
	}
	
	tick_hive_boss(update, player_pos);
}

static void tick_bug_navs(DriftUpdate* update){
	DriftGameState* state = update->state;
	DriftComponentBugNav* bug_nav = DRIFT_GET_TYPED_COMPONENT(state, DriftComponentBugNav);
	
	uint nav_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &bug_nav->c, .variable = &nav_idx},
		{.component = &state->bodies.c, .variable = &body_idx},
		{},
	});
	
	DriftVec2 rot = DriftWaveComplex(update->nanos, 0.5f);
	DriftVec2 inc = DriftVec2ForAngle(0.2f);

	while(DriftJoinNext(&join)){
		DriftVec2 pos = state->bodies.position[body_idx];
		DriftVec2 forward = DriftVec2Perp(state->bodies.rotation[body_idx]);
		DriftVec2 forward_bias = DriftVec2Add(forward, bug_nav->forward_bias[nav_idx]);
		
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
		const float speed = bug_nav->speed[nav_idx], accel = bug_nav->accel[nav_idx];
		*v = DriftVec2LerpConst(*v, DriftVec2Mul(forward_bias, speed), accel*update->tick_dt);
		// Rotate to align with motion.
		*w = (*w)*0.7f + DriftVec2Cross(forward, *v)/50;
		
		// Increment the rotation to put the next bug out of phase with this one.
		rot = DriftVec2Rotate(rot, inc);
	}
}

void DriftTickEnemies(DriftUpdate* update){
	DriftGameState* state = update->state;
	uint player_body_idx = DriftComponentFind(&state->bodies.c, state->player);
	DriftVec2 player_pos = state->bodies.position[player_body_idx];
	
	tick_spawns(update, player_pos);
	tick_enemies(update, player_pos);
	tick_bug_navs(update);
}

static uint anim_loop(uint tick, uint div, uint f0, uint f1){
	return f0 + (tick/div)%(f1 - f0 + 1);
}

static void draw_flash(DriftDraw* draw, DriftAffine transform, DriftRGBA8 color, DriftFrame frame){
	DRIFT_ARRAY_PUSH(draw->flash_sprites, ((DriftSprite){.frame = frame, .color = color, .matrix = transform}));
}

static void draw_agro(DriftDraw* draw, DriftAffine transform, DriftRGBA8 color, float size){
	DriftFrame frame = DRIFT_FRAMES[DRIFT_SPRITE_RADIAL_GRAD];
	size /= frame.bounds.r - frame.bounds.l;
	
	DRIFT_ARRAY_PUSH(draw->bg_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_RADIAL_GRAD], .color = color,
		.matrix = DriftAffineMul(transform, (DriftAffine){size, 0, 0, size, 0, 0}),
	}));
}

static void DrawGlowBug(DriftDraw* draw, DriftEntity e, DriftAffine transform, DriftRGBA8 flash, DriftRGBA8 aggro){
	DriftGameState* state = draw->state;
	uint tick = e.id + draw->tick;
	DriftFrame frame = DRIFT_FRAMES[anim_loop(tick, 8, DRIFT_SPRITE_GLOW_BUG00, DRIFT_SPRITE_GLOW_BUG05)];
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = frame, .color = DRIFT_RGBA8_WHITE, .matrix = transform}));
	if(flash.a) DRIFT_ARRAY_PUSH(draw->flash_sprites, ((DriftSprite){.frame = frame, .color = flash, .matrix = transform}));
	
	DriftAffine m = DriftAffineMul(transform, (DriftAffine){120, 0, 0, 120, 0, -8});
	DRIFT_ARRAY_PUSH(draw->lights, DriftLightMake(DRIFT_SPRITE_LIGHT_RADIAL, (DriftVec4){{0.12f, 0.08f, 0.02f, 2.00f}}, m, 0));
	
	// DriftVec2 p = DriftAffineOrigin(m);
	// DRIFT_ARRAY_PUSH(draw->shadow_masks, ((DriftSegment){.a = {p.x, p.y - 5}, .b = {p.x, p.y + 5}}));
}

static void DrawWorkerBug(DriftDraw* draw, DriftEntity e, DriftAffine transform, DriftRGBA8 flash, DriftRGBA8 aggro){
	uint tick = e.id + draw->tick;
	DriftFrame frame = DRIFT_FRAMES[anim_loop(tick, 8, DRIFT_SPRITE_WORKER_DRONE00, DRIFT_SPRITE_WORKER_DRONE03)];
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = frame, .color = DRIFT_RGBA8_WHITE, .matrix = transform}));
	if(flash.a) draw_flash(draw, transform, flash, frame);
	if(aggro.a) draw_agro(draw, transform, aggro, 40);
	
	DriftAffine m = DriftAffineMul(transform, (DriftAffine){40, 0, 0, 40, 0, -8});
	DRIFT_ARRAY_PUSH(draw->lights, DriftLightMake(DRIFT_SPRITE_LIGHT_RADIAL, (DriftVec4){{0.26f, 0.43f, 0.51f, 1.00f}}, m, 0));
}

static void DrawFighterBug(DriftDraw* draw, DriftEntity e, DriftAffine transform, DriftRGBA8 flash, DriftRGBA8 aggro){
	uint tick = e.id + draw->tick;
	DriftFrame frame = DRIFT_FRAMES[anim_loop(tick, 8, DRIFT_SPRITE_FIGHTER_DRONE00, DRIFT_SPRITE_FIGHTER_DRONE03)];
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = frame, .color = DRIFT_RGBA8_WHITE, .matrix = transform}));
	if(flash.a) draw_flash(draw, transform, flash, frame);
	if(aggro.a) draw_agro(draw, transform, aggro, 50);
	
	DriftAffine m = DriftAffineMul(transform, (DriftAffine){55, 0, 0, 55, 0, -8});
	DRIFT_ARRAY_PUSH(draw->lights, DriftLightMake(DRIFT_SPRITE_LIGHT_RADIAL, (DriftVec4){{0.89f, 0.11f, 0.99f, 0.00f}}, m, 0));
}

static void DrawTrilobyte(DriftDraw* draw, DriftEntity e, DriftAffine transform, DriftRGBA8 flash, DriftRGBA8 aggro){
	DriftGameState* state = draw->state;
	uint tick = e.id + draw->tick;
	
	uint body_idx = DriftComponentFind(&state->bodies.c, e);
	float turn = -0.1f*state->bodies.angular_velocity[body_idx];
	DriftVec2 q = DriftVec2ForAngle(turn);
	
	DriftAffine t = transform, foo[8];
	for(uint i = 0; i < 8; i++){
		foo[i] = t = DriftAffineMul(t, (DriftAffine){q.x, q.y, -q.y, q.x, 0, -8});
	}
	
	DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteMake(DRIFT_SPRITE_TRILOBYTE_TAIL_BLUE00, DRIFT_RGBA8_WHITE, foo[7]));
	for(uint i = 6; i < 8; i--){
		uint frame = anim_loop(tick + 5*i, 4, DRIFT_SPRITE_TRILOBYTE_BODY_BLUE00, DRIFT_SPRITE_TRILOBYTE_BODY_BLUE05);
		DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteMake(frame, DRIFT_RGBA8_WHITE, foo[i]));
	}
	DRIFT_ARRAY_PUSH(draw->fg_sprites, DriftSpriteMake(DRIFT_SPRITE_TRILOBYTE_HEAD_BLUE00, DRIFT_RGBA8_WHITE, transform));
}

static void DrawNautilus(DriftDraw* draw, DriftEntity e, DriftAffine transform, DriftRGBA8 flash, DriftRGBA8 aggro){
	// TODO should just orient the nautilus movement code instead of this.
	transform = DriftAffineMul(transform, (DriftAffine){0, 1, -1, 0, 0, 0});
	if(e.id % 2 == 0) transform = DriftAffineMul(transform, (DriftAffine){1, 0, 0, -1, 0, 0});
	
	uint tick = e.id + draw->tick;
	DriftFrame frame = DRIFT_FRAMES[anim_loop(tick, 8, DRIFT_SPRITE_NAUTILUS_HEAVY_IDLE00, DRIFT_SPRITE_NAUTILUS_HEAVY_IDLE07)];
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = frame, .color = DRIFT_RGBA8_WHITE, .matrix = transform}));
	if(flash.a) DRIFT_ARRAY_PUSH(draw->flash_sprites, ((DriftSprite){.frame = frame, .color = flash, .matrix = transform}));
}

typedef void DrawFunc(DriftDraw* draw, DriftEntity e, DriftAffine transform, DriftRGBA8 flash, DriftRGBA8 aggro);
static DrawFunc* const DRAW_FUNCS[_DRIFT_ENEMY_COUNT] = {
	[DRIFT_ENEMY_GLOW_BUG] = DrawGlowBug,
	[DRIFT_ENEMY_WORKER_BUG] = DrawWorkerBug,
	[DRIFT_ENEMY_FIGHTER_BUG] = DrawFighterBug,
	[DRIFT_ENEMY_TRILOBYTE_LARGE] = DrawTrilobyte,
	[DRIFT_ENEMY_NAUTILUS_HEAVY] = DrawNautilus,
};

void DriftDrawEnemies(DriftDraw* draw){
	DriftGameState* state = draw->state;
	DriftRGBA8 scan_color = {0x00, 0x80, 0x80, 0x80};
	if(state->status.disable_scan) scan_color = DRIFT_RGBA8_CLEAR;
	
	uint enemy_idx, transform_idx, health_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &state->enemies.c, .variable = &enemy_idx},
		{.component = &state->transforms.c, .variable = &transform_idx},
		{.component = &state->health.c, .variable = &health_idx},
		{},
	});
	
	DriftAffine vp_matrix = draw->vp_matrix;
	while(DriftJoinNext(&join)){
		DriftAffine transform = state->transforms.matrix[transform_idx];
		DriftVec2 pos = DriftAffineOrigin(transform);
		if(!DriftAffineVisibility(vp_matrix, pos, (DriftVec2){128, 128})) continue;
		
		DriftHealth* health = state->health.data + health_idx;
		DriftRGBA8 flash = health_flash(draw->tick, health);
		DriftRGBA8 aggro = DRIFT_RGBA8_CLEAR;
		if(state->enemies.aggro_ticks[enemy_idx] > 0){
			aggro = DriftRGBA8Fade(DRIFT_RGBA8_RED, 0.5f + 0.5f*DriftWaveComplex(draw->update_nanos, 4.0f).x);
		}
		
		uint scan_idx = DriftComponentFind(&state->scan.c, join.entity);
		if(state->scan_progress[state->scan.type[scan_idx]] < 1){
			flash = DriftRGBA8Composite(flash, DriftHUDIndicator(draw, pos, scan_color));
		}
		
		DriftEnemyType type = state->enemies.type[enemy_idx];
		DRAW_FUNCS[type](draw, join.entity, transform, flash, aggro);
		
		if(health->value < health->maximum){
			const uint health_div = DRIFT_SPRITE_HEALTH7 - DRIFT_SPRITE_HEALTH1;
			uint frame = (uint)floorf(health_div*health->value/health->maximum);
			DRIFT_ARRAY_PUSH(draw->overlay_sprites, ((DriftSprite){
				.frame = DRIFT_FRAMES[DRIFT_SPRITE_HEALTH1 + frame], .color = DRIFT_RGBA8_WHITE,
				.matrix = {1, 0, 0, 1, pos.x, pos.y - 14},
			}));
		}
	}
}

void DriftDrawHivesMap(DriftDraw* draw, float scale){
	DriftGameState* state = draw->state;
	DriftComponentHives* hives = DRIFT_GET_TYPED_COMPONENT(state, DriftComponentHives);
	
	for(uint i = 0; i < HIVE_COUNT; i++){
		DriftVec2 pos = HIVE_INFO[i].pos;
		DriftHiveData* data = hives->data + DriftComponentFind(&hives->c, HIVE_INFO[i].key);
		DRIFT_ARRAY_PUSH(draw->bg_prims, ((DriftPrimitive){
			.p0 = pos, .p1 = pos, .radii = {10*scale, 8*scale}, .color = (data->health > 0 ? DRIFT_RGBA8_MAGENTA : DRIFT_RGBA8_ORANGE),
		}));
	}
}
