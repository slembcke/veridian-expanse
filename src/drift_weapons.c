#include "drift_game.h"

typedef enum {
	DRIFT_PROJECTILE_NONE,
	DRIFT_PROJECTILE_PLAYER_SLUG,
	DRIFT_PROJECTILE_PLAYER_PLASMA,
	DRIFT_PROJECTILE_PLAYER_PROTON,
	DRIFT_PROJECTILE_HIVE,
	_DRIFT_PROJECTILE_COUNT,
} DriftProjectileType;

typedef struct {
	float speed, damage, timeout;
	DriftCollisionType collision;
	uint frame, frame_count;
	DriftRGBA8 sprite_color;
	DriftVec2 sprite_size;
	float light_size, collision_radius;
	DriftVec4 light_color;
	DriftBlastType ricochet;
	DriftSFX sfx;
	float gain;
} DriftProjectileInfo;

static const DriftProjectileInfo DRIFT_PROJECTILES[_DRIFT_PROJECTILE_COUNT] = {
	[DRIFT_PROJECTILE_PLAYER_SLUG] = {
		.speed = 2000, .damage = 10, .timeout = 0.2f,
		.collision_radius = 4, .collision = DRIFT_COLLISION_PLAYER_BULLET,
		.frame = DRIFT_SPRITE_BULLET00, .frame_count = 8,
		.sprite_size = {0.5f, 0.15f}, .sprite_color = {0xC0, 0xC0, 0xC0, 0xC0},
		.light_size = 64, .light_color = (DriftVec4){{0.46f, 0.27f, 0.12f, 0.00f}},
		.ricochet = DRIFT_BLAST_RICOCHET,
		.sfx = DRIFT_SFX_SERVO_SHOT2, .gain = 0.3f
	},
	[DRIFT_PROJECTILE_PLAYER_PLASMA] = {
		.speed = 700, .damage = 8, .timeout = 1.0f,
		.collision_radius = 4, .collision = DRIFT_COLLISION_PLAYER_BULLET,
		.frame = DRIFT_SPRITE_GREEN_PLASMA00, .frame_count = 8,
		.sprite_size = {0.5f, 0.5f}, .sprite_color = {0xC0, 0xC0, 0xC0, 0xC0},
		.light_size = 48, .light_color = (DriftVec4){{0, 1, 0, 0.00f}},
		.ricochet = DRIFT_BLAST_GREEN_FLASH,
		.sfx = DRIFT_SFX_PLASMA_BLAST, .gain = 0.5f
	},
	[DRIFT_PROJECTILE_PLAYER_PROTON] = {
		.speed = 300, .damage = 100, .timeout = 2.5f,
		.collision_radius = 16, .collision = DRIFT_COLLISION_PLAYER_BULLET,
		.frame = DRIFT_SPRITE_PROTON_BURST00, .frame_count = 8,
		.sprite_size = {1.0f, 1.0f}, .sprite_color = {0xC0, 0xC0, 0xC0, 0x40},
		.light_size = 150, .light_color = (DriftVec4){{2.47f, 0.00f, 3.80f, 0.00f}},
		.ricochet = DRIFT_BLAST_VIOLET_ZAP,
		.sfx = DRIFT_SFX_BULLET_FIRE1, .gain = 0.5f
	},
	[DRIFT_PROJECTILE_HIVE] = {
		.speed = 500, .damage = 1, .timeout = 1.5f,
		.collision_radius = 8, .collision = DRIFT_COLLISION_ENEMY_BULLET,
		.frame = DRIFT_SPRITE_HIVE_BULLET00, .frame_count = 30,
		.sprite_size = {1.0f, 1.0f}, .sprite_color = {0xC0, 0xC0, 0xC0, 0x40},
		.light_size = 100, .light_color = (DriftVec4){{0.35f, 0.03f, 0.37f, 0.00f}},
		.ricochet = DRIFT_BLAST_VIOLET_ZAP,
		.sfx = DRIFT_SFX_BULLET_FIRE1, .gain = 0.5f
	},
};

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	DriftProjectileType* type;
	DriftRay2* ray;
	DriftSegment* path;
	float* timeout;
	float* size;
} DriftComponentProjectiles;

typedef struct {
	float alpha;
	DriftVec2 point, normal;
} RayHit;

static inline RayHit circle_to_segment_query(DriftVec2 center, float r1, DriftVec2 a, DriftVec2 b, float r2){
	float rsum = r1 + r2;
	DriftVec2 da = DriftVec2Sub(a, center);
	float da_da = DriftVec2Dot(da, da);
	
	// Segment started inside.
	if(da_da < rsum*rsum){
		DriftVec2 n = DriftVec2Normalize(da);
		return (RayHit){.alpha = 0, .point = DriftVec2Sub(a, DriftVec2Mul(n, r2)), .normal = n};
	}
	
	DriftVec2 db = DriftVec2Sub(b, center);
	float da_db = DriftVec2Dot(da, db);
	
	float qa = da_da - 2*da_db + DriftVec2Dot(db, db);
	float qb = da_db - da_da;
	float det = qb*qb - qa*(da_da - rsum*rsum);
	
	if(det >= 0.0f){
		float t = -(qb + sqrtf(det))/qa;
		if(0 <= t && t <= 1){
			DriftVec2 n = DriftVec2Normalize(DriftVec2Lerp(da, db, t));
			return (RayHit){.alpha = t, .point = DriftVec2Sub(DriftVec2Lerp(a, b, t), DriftVec2Mul(n, r2)), .normal = n};
		}
	}
	
	return (RayHit){.alpha = 1};
}

static void tick_bullets(DriftUpdate* update){
	DriftGameState* state = update->state;
	DriftComponentProjectiles* projectiles = DRIFT_GET_TYPED_COMPONENT(state, DriftComponentProjectiles);
	size_t row_count = projectiles->c.table.row_count;
	
	DRIFT_COMPONENT_FOREACH(&projectiles->c, i){
		if(projectiles->timeout[i] <= 0) DriftDestroyEntity(state, projectiles->entity[i]);
		projectiles->timeout[i] -= update->tick_dt;
		
		DriftRay2 ray = projectiles->ray[i];
		DriftSegment seg = {
			.a = ray.origin,
			.b = DriftVec2FMA(ray.origin, ray.dir, update->tick_dt),
		};
		
		projectiles->ray[i].origin = seg.b;
		projectiles->path[i] = seg;
		// DriftDebugSegment(state, p0, p1, 4, DRIFT_RGBA8_GREEN);
	}
	
	// Check terrain collisions first.
	DRIFT_ARRAY(RayHit) hits = DRIFT_ARRAY_NEW(update->mem, row_count, RayHit);
	DRIFT_COMPONENT_FOREACH(&projectiles->c, i){
		DriftSegment seg = projectiles->path[i];
		float t = DriftTerrainRaymarch(state->terra, seg.a, seg.b, 0, 1);
		DriftVec2 p = DriftVec2Lerp(seg.a, seg.b, t);
		DriftVec2 n = DriftTerrainSampleFine(state->terra, p).grad;
		hits[i] = (RayHit){.alpha = t, .point = p, .normal = n};
	}
	
	// Check object collisions second.
	DRIFT_ARRAY(DriftEntity) entities = DRIFT_ARRAY_NEW(update->mem, row_count, DriftEntity);
	DRIFT_COMPONENT_FOREACH(&projectiles->c, i){
		const DriftProjectileInfo* info = DRIFT_PROJECTILES + projectiles->type[i];
		DRIFT_COMPONENT_FOREACH(&state->bodies.c, body_idx){
			if(DriftCollisionFilter(info->collision, state->bodies.collision_type[body_idx])){
				DriftEntity entity = state->bodies.entity[body_idx];
				
				float radius = info->collision_radius;
				DriftSegment path = projectiles->path[i];
				RayHit query = circle_to_segment_query(state->bodies.position[body_idx], state->bodies.radius[body_idx], path.a, path.b, radius);
				if(query.alpha < hits[i].alpha){
					hits[i].alpha = query.alpha;
					hits[i].normal = query.normal;
					entities[i] = entity;
				}
			}
		}
	}
	
	// Apply damage to hit objects.
	DRIFT_COMPONENT_FOREACH(&projectiles->c, i){
		if(hits[i].alpha < 1 && projectiles->timeout[i] > 0){
			DriftProjectileType type = projectiles->type[i];
			DriftMakeBlast(update, hits[i].point, hits[i].normal, DRIFT_PROJECTILES[type].ricochet);
			
			float damage = projectiles->size[i]*DRIFT_PROJECTILES[type].damage;
			bool hit_obj = DriftHealthApplyDamage(update, entities[i], damage, hits[i].point);
			if(!hit_obj){
				float pan = DriftClamp(DriftAffinePoint(update->prev_vp_matrix, hits[i].point).x, -1, 1);
				DriftAudioPlaySample(DRIFT_BUS_SFX, DRIFT_SFX_RICHOCHET_DIRT, (DriftAudioParams){.gain = 1.0f, .pan = pan});
			}
			
			projectiles->timeout[i] = 0; // TODO is this better?
			// DriftDestroyEntity(state, projectiles->entity[i]);
		}
	}
}

static void draw_projectiles(DriftDraw* draw){
	DriftGameState* state = draw->state;
	DriftComponentProjectiles* projectiles = DRIFT_GET_TYPED_COMPONENT(state, DriftComponentProjectiles);
	
	// Amount to interpolate bullet positions.
	float alpha = draw->dt_before_tick*DRIFT_TICK_HZ;
	
	DRIFT_COMPONENT_FOREACH(&projectiles->c, i){
		const DriftProjectileInfo* info = DRIFT_PROJECTILES + projectiles->type[i];
		DriftRay2 ray = projectiles->ray[i];
		DriftSegment path = projectiles->path[i];
		DriftVec2 delta = DriftVec2Sub(path.b, path.a);
		DriftVec2 pos = DriftVec2FMA(path.b, delta, alpha);
		
		DriftDebugSegment2(state, path.a, path.b, 5, 4, DRIFT_RGBA8_RED);
		DriftDebugCircle(state, pos, 5, DRIFT_RGBA8_RED);
		
		float size = projectiles->size[i];
		float fade = DriftSmoothstep(0.0f, 0.25f, projectiles->timeout[i]/info->timeout);
		DriftFrame frame = DRIFT_FRAMES[info->frame + (draw->tick/2 % info->frame_count)];
		DriftVec2 s = DriftVec2Mul(info->sprite_size, size/DriftVec2Length(delta));
		DRIFT_ARRAY_PUSH(draw->bullet_sprites, ((DriftSprite){
			.frame = frame, .color = DriftRGBA8Fade(info->sprite_color, fade),
			.matrix = {s.x*delta.x, s.x*delta.y, -s.y*delta.y, s.y*delta.x, pos.x, pos.y},
		}));
		
		float light_size = size*info->light_size;
		DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
			.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = DriftVec4Mul(info->light_color, fade),
			.matrix = {light_size, 0, 0, light_size, pos.x, pos.y},
		}));
	}
}

static void pew_pew(DriftUpdate* update, DriftVec2 pos, DriftProjectileType type){
	static DriftRandom rand[1]; // TODO static global
	float pan = DriftClamp(DriftAffinePoint(update->prev_vp_matrix, pos).x, -1, 1);
	float pitch = expf(0.05f*DriftRandomSNorm(rand));
	DriftAudioPlaySample(DRIFT_BUS_SFX, DRIFT_PROJECTILES[type].sfx, (DriftAudioParams){.gain = DRIFT_PROJECTILES[type].gain, .pan = pan, .pitch = pitch});
}

static DriftEntity fire_projectile(DriftUpdate* update, DriftProjectileType type, DriftVec2 vel, DriftRay2 ray, float size){
	DriftComponentProjectiles* projectiles = DRIFT_GET_TYPED_COMPONENT(update->state, DriftComponentProjectiles);
	
	DriftEntity e = DriftMakeEntity(update->state);
	uint bullet_idx = DriftComponentAdd(&projectiles->c, e);
	projectiles->type[bullet_idx] = type;
	projectiles->ray[bullet_idx] = (DriftRay2){.origin = ray.origin, .dir = DriftVec2FMA(vel, ray.dir, DRIFT_PROJECTILES[type].speed)};
	projectiles->timeout[bullet_idx] = DRIFT_PROJECTILES[type].timeout;
	projectiles->size[bullet_idx] = size;
	
	return e;
}

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

typedef struct {
	uint repeat;
	float inc, cooldown;
} DriftGunDef;

static void fire_common(DriftUpdate* update, DriftPlayerData* player, DriftGunState* gun, const DriftGunDef* gun_def, DriftInputValue input_value){
	gun->timeout -= update->dt;
	
	DriftPlayerInput* input = &INPUT->player;
	if(DriftInputButtonPress(input_value)){
		if(player->energy == 0){
			DriftAudioPlaySample(DRIFT_BUS_HUD, DRIFT_SFX_DENY, (DriftAudioParams){.gain = 1});
			DriftHudPushToast(update->ctx, 0, DRIFT_TEXT_RED"Weapon has no power");
		} else if(player->is_overheated){
			DriftAudioPlaySample(DRIFT_BUS_HUD, DRIFT_SFX_DENY, (DriftAudioParams){.gain = 1});
		} else if(gun->repeat == 0){
			*gun = (DriftGunState){.repeat = gun_def->repeat, .timeout = fmaxf(0, gun->timeout)};
		}
	}
}

static bool fire_repeating(DriftUpdate* update, DriftPlayerData* player, DriftGunState* gun, const DriftGunDef* gun_def, DriftInputValue input_value){
	fire_common(update, player, gun, gun_def, input_value);
	
	// Reset repeat when released.
	// if(!DriftInputButtonState(DRIFT_INPUT_FIRE) && gun->repeat){
	// 	*gun = (DriftGunState){.timeout = gun_def->cooldown};
	// }
	
	if(gun->repeat && gun->timeout <= 0){
		gun->repeat--;
		gun->timeout += gun->repeat ? gun_def->inc : gun_def->cooldown;
		
		// TODO move into gundef
		player->energy = fmaxf(player->energy - 2, 0);
		player->temp += 4e-2f;
		
		return true;
	} else {
		return false;
	}
}

static bool fire_charged(DriftUpdate* update, DriftPlayerData* player, DriftGunState* gun, const DriftGunDef* gun_def, DriftInputValue input_value){
	fire_common(update, player, gun, gun_def, input_value);
	if(!DriftInputButtonState(DRIFT_INPUT_FIRE) && gun->repeat){
		gun->repeat = 0;
		return true;
	} else {
		return false;
	}
}

DriftRGBA8 RETICLE_GLOW = {0xFF, 0x00, 0x00, 0x80};
DriftRGBA8 RETICLE_FAINT = {0x80, 0x00, 0x00, 0x40};

static void fire_slug_gun(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform, DriftVec2 vel, uint level){
	static const DriftGunDef slug_gun[] = {
		{.repeat =  2, .inc = 0.10f, .cooldown = 0.20f},
		{.repeat =  6, .inc = 0.07f, .cooldown = 0.20f},
		{.repeat = ~0, .inc = 0.05f, .cooldown = 0.05f},
	};

	DriftGunState* gun = &player->primary;
	if(fire_repeating(update, player, gun, slug_gun + level, DRIFT_INPUT_FIRE)){
		PlayerCannonTransforms cannons = CalculatePlayerCannonTransforms(1);
		DriftAffine cannon = cannons.arr[gun->repeat%4];
		cannon.c -= cannon.x/300;
		cannon = DriftAffineMul(transform, cannon);
		DriftVec2 dir = DriftVec2Normalize(DriftAffineDirection(cannon, (DriftVec2){0, 1}));
		
		DriftRay2 ray = {.origin = DriftAffineOrigin(cannon), .dir = dir};
		fire_projectile(update, DRIFT_PROJECTILE_PLAYER_SLUG, vel, ray, 1);
		pew_pew(update, DriftAffineOrigin(transform), DRIFT_PROJECTILE_PLAYER_SLUG);
		DriftRumble();
	}
}

static void draw_slug_gun(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform, float hud_fade){
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
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[2], .p1 = p[3], .radii = {1.2f}, .color = DriftRGBA8Fade(RETICLE_GLOW, hud_fade)}));
		DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){.p0 = p[0], .p1 = p[1], .radii = {1.0f}, .color = DriftRGBA8Fade(RETICLE_FAINT, hud_fade)}));
	}
}

static void fire_plasma_gun(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform, DriftVec2 vel, uint level){
	static const DriftGunDef plasma_gun[] = {
		{.repeat =  1, .inc = 0.00f, .cooldown = 0.20f},
		{.repeat =  2, .inc = 0.10f, .cooldown = 0.20f},
		{.repeat = ~0, .inc = 0.15f, .cooldown = 0.15f},
	};

	DriftGunState* gun = &player->primary;
	if(fire_repeating(update, player, gun, plasma_gun + level, DRIFT_INPUT_FIRE)){
		static DriftRandom rand = {};
		PlayerCannonTransforms cannons = CalculatePlayerCannonTransforms(1);
		
		for(uint i = 0; i < 4; i++){
			DriftAffine cannon = cannons.arr[i];
			cannon.c -= cannon.x/200;
			cannon = DriftAffineMul(transform, cannon);
			DriftVec2 dir = DriftVec2Normalize(DriftAffineDirection(cannon, (DriftVec2){0, 1}));
			
			fire_projectile(update, DRIFT_PROJECTILE_PLAYER_PLASMA, vel, (DriftRay2){
				.origin = DriftAffineOrigin(cannon),
				.dir = DriftVec2FMA(dir, DriftRandomInUnitCircle(&rand), 0.3f),
			}, DriftLerp(0.5f, 1.0f, DriftRandomUNorm(&rand)));
		}
		
		pew_pew(update, DriftAffineOrigin(transform), DRIFT_PROJECTILE_PLAYER_PLASMA);
		DriftRumble();
	}
}

static void draw_plasma_gun(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform, float hud_fade){
	PlayerCannonTransforms cannons = CalculatePlayerCannonTransforms(player->tool_anim);
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = DRIFT_FRAMES[DRIFT_SPRITE_GUN], .color = DRIFT_RGBA8_WHITE, .matrix = DriftAffineMul(transform, cannons.arr[0])}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = DRIFT_FRAMES[DRIFT_SPRITE_GUN], .color = DRIFT_RGBA8_WHITE, .matrix = DriftAffineMul(transform, cannons.arr[1])}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = DRIFT_FRAMES[DRIFT_SPRITE_GUN], .color = DRIFT_RGBA8_WHITE, .matrix = DriftAffineMul(transform, cannons.arr[2])}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = DRIFT_FRAMES[DRIFT_SPRITE_GUN], .color = DRIFT_RGBA8_WHITE, .matrix = DriftAffineMul(transform, cannons.arr[3])}));
	
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){
		.p0 = DriftAffinePoint(transform, (DriftVec2){ 20,  40}),
		.p1 = DriftAffinePoint(transform, (DriftVec2){ 30, 100}),
		.radii = {1.2f}, .color = DriftRGBA8Fade(RETICLE_GLOW, hud_fade),
	}));
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){
		.p0 = DriftAffinePoint(transform, (DriftVec2){  0,  40}),
		.p1 = DriftAffinePoint(transform, (DriftVec2){  0, 100}),
		.radii = {1.2f}, .color = DriftRGBA8Fade(RETICLE_FAINT, hud_fade),
	}));
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){
		.p0 = DriftAffinePoint(transform, (DriftVec2){-20,  40}),
		.p1 = DriftAffinePoint(transform, (DriftVec2){-30, 100}),
		.radii = {1.2f}, .color = DriftRGBA8Fade(RETICLE_GLOW, hud_fade),
	}));
}

static void fire_proton_gun(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform, DriftVec2 vel, uint level){
	DriftGunState* gun = &player->primary;
	if(fire_charged(update, player, gun, &(DriftGunDef){.repeat = 1}, DRIFT_INPUT_FIRE)){
		DriftRay2 ray = {.origin = {transform.x, transform.y}, .dir = {transform.c, transform.d}};
		float value = DriftClamp(-gun->timeout/1, 0.1f, 1.0f);
		fire_projectile(update, DRIFT_PROJECTILE_PLAYER_PROTON, vel, ray, value);
		pew_pew(update, DriftAffineOrigin(transform), DRIFT_PROJECTILE_PLAYER_PROTON);
		DriftRumble();
	}
}

static void draw_proton_gun(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform, float hud_fade){
	PlayerCannonTransforms cannons = CalculatePlayerCannonTransforms(player->tool_anim);
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = DRIFT_FRAMES[DRIFT_SPRITE_GUN], .color = DRIFT_RGBA8_WHITE, .matrix = DriftAffineMul(transform, cannons.arr[0])}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = DRIFT_FRAMES[DRIFT_SPRITE_GUN], .color = DRIFT_RGBA8_WHITE, .matrix = DriftAffineMul(transform, cannons.arr[1])}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = DRIFT_FRAMES[DRIFT_SPRITE_GUN], .color = DRIFT_RGBA8_WHITE, .matrix = DriftAffineMul(transform, cannons.arr[2])}));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){.frame = DRIFT_FRAMES[DRIFT_SPRITE_GUN], .color = DRIFT_RGBA8_WHITE, .matrix = DriftAffineMul(transform, cannons.arr[3])}));
	
	DriftGunState* gun = &player->primary;
	const DriftProjectileInfo* info = DRIFT_PROJECTILES + DRIFT_PROJECTILE_PLAYER_PROTON;
	float value = fmaxf(0.1f, DriftHermite3(DriftSaturate(-gun->timeout*gun->repeat/1)));
	DRIFT_ARRAY_PUSH(draw->fg_sprites, ((DriftSprite){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_PROTON_BURST00 + draw->tick/2 % 8], .color = info->sprite_color,
		.matrix = DriftAffineMul(transform, (DriftAffine){0, value, value, 0, 0, 32}),
	}));
	
	float light_size = value*info->light_size;
	DRIFT_ARRAY_PUSH(draw->lights, ((DriftLight){
		.frame = DRIFT_FRAMES[DRIFT_SPRITE_LIGHT_RADIAL], .color = info->light_color,
		.matrix = DriftAffineMul(transform, (DriftAffine){light_size, 0, 0, light_size, 0, 32}),
	}));
	
	float size = value*info->collision_radius;
	
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){
		.p0 = DriftAffinePoint(transform, (DriftVec2){ size, 100}),
		.p1 = DriftAffinePoint(transform, (DriftVec2){ size, 120}),
		.radii = {1.2f}, .color = DriftRGBA8Fade(RETICLE_GLOW, hud_fade),
	}));
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){
		.p0 = DriftAffinePoint(transform, (DriftVec2){0, 95}),
		.p1 = DriftAffinePoint(transform, (DriftVec2){0, 125}),
		.radii = {1.2f}, .color = DriftRGBA8Fade(RETICLE_FAINT, hud_fade),
	}));
	DRIFT_ARRAY_PUSH(draw->overlay_prims, ((DriftPrimitive){
		.p0 = DriftAffinePoint(transform, (DriftVec2){-size, 100}),
		.p1 = DriftAffinePoint(transform, (DriftVec2){-size, 120}),
		.radii = {1.2f}, .color = DriftRGBA8Fade(RETICLE_GLOW, hud_fade),
	}));
}

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	DriftEntity* target;
} DriftComponentMissiles;

static void fire_missiles(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform, DriftVec2 vel, uint level){
	DriftGunState* gun = &player->secondary;
	DriftGunDef gun_def = {.repeat = 1, .inc = 0.0f, .cooldown = 0.5f};
	if(fire_repeating(update, player, gun, &gun_def, DRIFT_INPUT_ALT)){
		DriftRay2 ray = {.origin = {transform.x, transform.y}, .dir = {transform.c, transform.d}};
		float value = DriftClamp(-gun->timeout/1, 0.1f, 1.0f);
		DriftEntity e = fire_projectile(update, DRIFT_PROJECTILE_PLAYER_PROTON, vel, ray, 0.3f);
		
		DriftComponentMissiles* missiles = DRIFT_GET_TYPED_COMPONENT(update->state, DriftComponentMissiles);
		DriftComponentAdd(&missiles->c, e);
	}
}

static void tick_missiles(DriftUpdate* update){
	DriftComponentMissiles* missiles = DRIFT_GET_TYPED_COMPONENT(update->state, DriftComponentMissiles);
	DriftComponentProjectiles* projectiles = DRIFT_GET_TYPED_COMPONENT(update->state, DriftComponentProjectiles);
	
	uint projectile_idx, missile_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{.component = &projectiles->c, .variable = &projectile_idx},
		{.component = &missiles->c, .variable = &missile_idx},
		{}
	});
	while(DriftJoinNext(&join)){
		projectiles->ray[projectile_idx].dir.y += 100.0f*update->tick_dt;
	}
}

typedef enum {
	DRIFT_GUN_TYPE_SLUG,
	DRIFT_GUN_TYPE_PLASMA,
	DRIFT_GUN_TYPE_PROTON,
} DriftGunType;

DriftGunType GUN_TYPE = DRIFT_GUN_TYPE_SLUG;
uint GUN_LEVEL = 0;

void DriftPlayerUpdateGun(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform){
	DriftGameState* state = update->state;
	DriftVec2 player_velocity = state->bodies.velocity[DriftComponentFind(&state->bodies.c, state->player)];
	
	typedef void PrimaryFuncs(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform, DriftVec2 vel, uint level);
	static PrimaryFuncs* const PRIMARY_FUNCS[] = {
		[DRIFT_GUN_TYPE_SLUG] = fire_slug_gun,
		[DRIFT_GUN_TYPE_PLASMA] = fire_plasma_gun,
		[DRIFT_GUN_TYPE_PROTON] = fire_proton_gun,
	};
	PRIMARY_FUNCS[GUN_TYPE](update, player, transform, player_velocity, GUN_LEVEL);
	
	fire_missiles(update, player, transform, player_velocity, 0);
}

void DriftPlayerDrawGun(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform, float hud_fade){
 typedef void DrawFunc(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform, float hud_fade);
 static DrawFunc* const DRAW_FUNCS[] = {
		[DRIFT_GUN_TYPE_SLUG] = draw_slug_gun,
		[DRIFT_GUN_TYPE_PLASMA] = draw_plasma_gun,
		[DRIFT_GUN_TYPE_PROTON] = draw_proton_gun,
 };
 
 DRAW_FUNCS[GUN_TYPE](draw, player, transform, hud_fade);
}

void FireHiveProjectile(DriftUpdate* update, DriftRay2 ray){
	fire_projectile(update, DRIFT_PROJECTILE_HIVE, DRIFT_VEC2_ZERO, ray, 1);
}

void DriftSystemsInitWeapons(DriftGameState* state){
	DriftComponentProjectiles* projectiles = DriftAlloc(state->mem, sizeof(*projectiles));
	DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(state, projectiles, DriftComponentProjectiles, ((DriftColumnSet){
		DRIFT_DEFINE_COLUMN(projectiles->entity),
		DRIFT_DEFINE_COLUMN(projectiles->type),
		DRIFT_DEFINE_COLUMN(projectiles->ray),
		DRIFT_DEFINE_COLUMN(projectiles->path),
		DRIFT_DEFINE_COLUMN(projectiles->timeout),
		DRIFT_DEFINE_COLUMN(projectiles->size),
	}), 0);
	projectiles->size[0] = 1;
	
	DriftComponentMissiles* missiles = DriftAlloc(state->mem, sizeof(*missiles));
	DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(state, missiles, DriftComponentMissiles, ((DriftColumnSet){
		DRIFT_DEFINE_COLUMN(missiles->entity),
		DRIFT_DEFINE_COLUMN(missiles->target),
	}), 0);
}

void DriftSystemsTickWeapons(DriftUpdate* update){
	tick_missiles(update);
	tick_bullets(update);
}

void DriftSystemsDrawWeapons(DriftDraw* draw){
	draw_projectiles(draw);
}
