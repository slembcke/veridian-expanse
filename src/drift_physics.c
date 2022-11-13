#include <string.h>

#include "tracy/TracyC.h"

#include "drift_game.h"

enum {
	CATEGORY_TERRAIN = 0x0001,
	CATEGORY_PLAYER = 0x0002,
	CATEGORY_ITEM = 0x0004,
	CATEGORY_ENEMY = 0x0008,
	CATEGORY_PLAYER_BULLET = 0x0010,
	
	CATEGORY_GROUP_NPC = CATEGORY_TERRAIN | CATEGORY_PLAYER | CATEGORY_PLAYER_BULLET | CATEGORY_ITEM | CATEGORY_ENEMY,
};

static struct {
	u32 categories, mask;
} COLLISION_TYPES[] = {
	[DRIFT_COLLISION_TERRAIN] = {.categories = CATEGORY_TERRAIN, .mask = -1},
	[DRIFT_COLLISION_PLAYER] = {.categories = CATEGORY_PLAYER, .mask = CATEGORY_TERRAIN | CATEGORY_ENEMY},
	[DRIFT_COLLISION_ITEM] = {.categories = CATEGORY_ITEM, .mask = -1},
	[DRIFT_COLLISION_PLAYER_BULLET] = {.categories = CATEGORY_PLAYER_BULLET, .mask = CATEGORY_TERRAIN | CATEGORY_ENEMY},
	[DRIFT_COLLISION_NON_HOSTILE] = {.categories = CATEGORY_ENEMY, .mask = CATEGORY_GROUP_NPC},
	[DRIFT_COLLISION_WORKER_DRONE] = {.categories = CATEGORY_ENEMY, .mask = CATEGORY_GROUP_NPC},
};

static bool do_nothing(DriftUpdate* update, DriftPhysics* phys, DriftIndexPair pair){return true;}

static bool fatal_player_contact(DriftUpdate* update, DriftPhysics* phys, DriftIndexPair pair){
	DriftGameState* state = update->state;
	DriftHealthApplyDamage(update, state->bodies.entity[pair.idx0], 10);
	DriftHealthApplyDamage(update, state->bodies.entity[pair.idx1], INFINITY);
	return true;
}

static const struct {
	DriftCollisionType types[2];
	DriftCollisionCallback* callback;
} COLLISION_CALLBACKS[] = {
	{{}, do_nothing},
	{{DRIFT_COLLISION_PLAYER, DRIFT_COLLISION_WORKER_DRONE}, DriftWorkerDroneCollide},
	{},
};

static inline uintptr_t collision_hash(DriftCollisionType a, DriftCollisionType b){
	return (a ^ b) | ((a & b) << 16);
}


bool DriftCollisionFilter(DriftCollisionType a, DriftCollisionType b){
	// Both objects must agree to collide by having overlapping category/masks.
	return (1
		&& (COLLISION_TYPES[a].categories & COLLISION_TYPES[b].mask)
		&& (COLLISION_TYPES[b].categories & COLLISION_TYPES[a].mask)
	);
}

static DriftVec2 linearized_rotation(DriftVec2 q, float w, float dt){
	return DriftVec2Normalize((DriftVec2){q.x - q.y*w*dt, q.y + q.x*w*dt});
}

void DriftPhysicsSyncTransforms(DriftUpdate* update, float dt_diff){
	DriftAffine* transform_arr = update->state->transforms.matrix;
	DriftVec2* x_arr = update->state->bodies.position;
	DriftVec2* v_arr = update->state->bodies.velocity;
	DriftVec2* q_arr = update->state->bodies.rotation;
	float* w_arr = update->state->bodies.angular_velocity;

	uint transform_idx, body_idx;
	DriftJoin join = DriftJoinMake((DriftComponentJoin[]){
		{&body_idx, &update->state->bodies.c},
		{&transform_idx, &update->state->transforms.c},
		{},
	});
	while(DriftJoinNext(&join)){
		DriftVec2 p = DriftVec2FMA(x_arr[body_idx], v_arr[body_idx], dt_diff);
		DriftVec2 q = linearized_rotation(q_arr[body_idx], w_arr[body_idx], dt_diff);
		transform_arr[transform_idx] = (DriftAffine){q.x, q.y, -q.y, q.x, p.x, p.y};
	}
}

static inline float generalized_mass_inv(float mass_inv, float moment_inv, DriftVec2 r, DriftVec2 n){
	float rcn = DriftVec2Cross(r, n);
	return mass_inv + rcn*rcn*moment_inv;
}

static inline DriftVec2 relative_velocity_at(DriftIndexPair pair, DriftVec2* v, float* w, DriftVec2 r0, DriftVec2 r1){
	DriftVec2 v0 = DriftVec2FMA(v[pair.idx0], DriftVec2Perp(r0), w[pair.idx0]);
	DriftVec2 v1 = DriftVec2FMA(v[pair.idx1], DriftVec2Perp(r1), w[pair.idx1]);
	return DriftVec2Sub(v0, v1);
}

static void PushContact(DriftPhysics* phys, DriftIndexPair pair, float overlap, DriftVec2 n, DriftVec2 r0, DriftVec2 r1){
	DriftVec2 t = DriftVec2Perp(n);
	float mass_sum = phys->m_inv[pair.idx0] + phys->m_inv[pair.idx1];
	float i0 = phys->i_inv[pair.idx0], i1 = phys->i_inv[pair.idx1];
	float rcn0 = DriftVec2Cross(r0, n), rcn1 = DriftVec2Cross(r1, n);
	float rct0 = DriftVec2Cross(r0, t), rct1 = DriftVec2Cross(r1, t);
	
	float elasticity = 0.0f;
	float vn_rel = DriftVec2Dot(n, relative_velocity_at(pair, phys->v, phys->w, r0, r1));
	
	DRIFT_ARRAY_PUSH(phys->contact, ((DriftContact){
		.pair = pair, .n = n, .r0 = r0, .r1 = r1,
		.friction = 0.3f, .bounce = elasticity*vn_rel, .bias = -0.1f*fminf(0.0f, overlap + 1.0f), // TODO hard-coded friction and bias
		.mass_n = 1.0f/(mass_sum + rcn0*rcn0*i0 + rcn1*rcn1*i1),
		.mass_t = 1.0f/(mass_sum + rct0*rct0*i0 + rct1*rct1*i1),
	}));
}

static DriftContactFunc ContactCircleCircle;
static void ContactCircleCircle(DriftPhysics* phys, DriftIndexPair pair){
	DriftVec2 c0 = phys->x[pair.idx0], c1 = phys->x[pair.idx1];
	DriftVec2 dx = DriftVec2Sub(c0, c1);
	float overlap = DriftVec2Length(dx) - (phys->r[pair.idx0] + phys->r[pair.idx1]);
	
	if(overlap < 0){
		DriftVec2 n = DriftVec2Normalize(dx);
		DriftVec2 r0 = DriftVec2Mul(n, -phys->r[pair.idx0]);
		DriftVec2 r1 = DriftVec2Mul(n,  phys->r[pair.idx1]);
		PushContact(phys, pair, overlap, n, r0, r1);
	}
}

static inline DriftVec2 ClosestPoint(DriftVec2 p, DriftSegment seg){
	DriftVec2 delta = DriftVec2Sub(seg.a, seg.b);
	float t = DriftClamp(DriftVec2Dot(delta, DriftVec2Sub(p, seg.b))/DriftVec2LengthSq(delta), 0, 1);
	return DriftVec2Add(seg.b, DriftVec2Mul(delta, t));
}

static void ApplyImpulse(const DriftPhysics* phys, DriftIndexPair pair, DriftVec2 r0, DriftVec2 r1, DriftVec2 j){
	phys->v[pair.idx0].x += j.x*phys->m_inv[pair.idx0];
	phys->v[pair.idx0].y += j.y*phys->m_inv[pair.idx0];
	phys->v[pair.idx1].x -= j.x*phys->m_inv[pair.idx1];
	phys->v[pair.idx1].y -= j.y*phys->m_inv[pair.idx1];
	phys->w[pair.idx0] += DriftVec2Cross(r0, j)*phys->i_inv[pair.idx0];
	phys->w[pair.idx1] -= DriftVec2Cross(r1, j)*phys->i_inv[pair.idx1];
}

static void ApplyBiasImpulse(const DriftPhysics* phys, DriftIndexPair pair, DriftVec2 r0, DriftVec2 r1, DriftVec2 j){
	phys->x_bias[pair.idx0].x += j.x*phys->m_inv[pair.idx0];
	phys->x_bias[pair.idx0].y += j.y*phys->m_inv[pair.idx0];
	phys->x_bias[pair.idx1].x -= j.x*phys->m_inv[pair.idx1];
	phys->x_bias[pair.idx1].y -= j.y*phys->m_inv[pair.idx1];
	phys->q_bias[pair.idx0] += DriftVec2Cross(r0, j)*phys->i_inv[pair.idx0];
	phys->q_bias[pair.idx1] -= DriftVec2Cross(r1, j)*phys->i_inv[pair.idx1];
}

typedef void physics_job_func(const DriftPhysics* phys, uint i0, uint i1);
typedef struct {
	const DriftPhysics* phys;
	uint min_idx, max_idx, size;
	physics_job_func* func;
	tina_group group;
} PhysicsJobContext;

static void physics_job(tina_job* job){
	PhysicsJobContext* ctx = tina_job_get_description(job)->user_data;
	uint idx = tina_job_get_description(job)->user_idx;
	ctx->func(ctx->phys, 
		DRIFT_MAX((idx + 0)*ctx->size, ctx->min_idx),
		DRIFT_MIN((idx + 1)*ctx->size, ctx->max_idx)
	);
}

static PhysicsJobContext* phys_job_enqueue(DriftUpdate* update, DriftPhysics* phys, uint min_idx, uint max_idx, uint size, physics_job_func* func){
	TracyCZoneN(ZONE, "Enqueue", true);
	PhysicsJobContext* ctx = DRIFT_COPY(update->mem, ((PhysicsJobContext){
		.phys = phys, .min_idx = min_idx, .max_idx = max_idx, .size = size, .func = func
	}));
	
	tina_job_description descs[1024];
	uint desc_count = 0;
	
	for(uint i = 0; i*ctx->size < ctx->max_idx; i++){
		DRIFT_ASSERT_HARD(desc_count < sizeof(descs)/sizeof(*descs), "Too many jobs!");
		descs[desc_count++] = (tina_job_description){
			.name = "JobPhysics", .func = physics_job, .user_data = ctx, .user_idx = i, .queue_idx = DRIFT_JOB_QUEUE_WORK
		};
	}
	
	tina_scheduler_enqueue_batch(update->scheduler, descs, desc_count, &ctx->group, 0);
	TracyCZoneEnd(ZONE);
	return ctx;
}

static void bounds_job(const DriftPhysics* phys, uint i0, uint i1){
	TracyCZoneN(ZONE_BOUNDS, "Bounds", true);
	for(uint i = i0; i < i1; i++){
		DriftVec2 center = phys->x[i];
		float radius = phys->r[i];
		DriftVec2 displacement = DriftVec2Mul(phys->v[i], phys->dt);
		
		DriftAABB2 bb = phys->bounds[i] = (DriftAABB2){
			.l = center.x - radius + fmaxf(0.0f, -displacement.x),
			.b = center.y - radius + fmaxf(0.0f, -displacement.y),
			.r = center.x + radius + fmaxf(0.0f, +displacement.x),
			.t = center.y + radius + fmaxf(0.0f, +displacement.y),
		};
		
		DriftVec2 dialate = DriftVec2Mul((DriftVec2){bb.r - bb.l, bb.t - bb.b}, 0.25f);
		DriftVec2 delta = DriftVec2Mul(phys->v[i], 4*phys->dt);
		phys->loose_bounds[i] = (DriftAABB2){
			.l = bb.l + fminf(-dialate.x, delta.x),
			.b = bb.b + fminf(-dialate.y, delta.y),
			.r = bb.r + fmaxf(+dialate.x, delta.x),
			.t = bb.t + fmaxf(+dialate.y, delta.y),
		};
	}
	TracyCZoneEnd(ZONE_BOUNDS);
}

static void terrain_job(const DriftPhysics* phys, uint i0, uint i1){
	TracyCZoneN(ZONE_TERRAIN, "Terrain", true);
	for(uint i = i0; i < i1; i++){
		// TODO collision filtering.
		DriftTerrainSampleInfo info = DriftTerrainSampleFine(phys->terra, phys->x[i]);
		phys->ground_plane[i] = (DriftVec3){{.x = info.grad.x, .y = info.grad.y, .z = DriftVec2Dot(info.grad, phys->x[i]) - info.dist}};
	}
	TracyCZoneEnd(ZONE_TERRAIN);
}

void DriftPhysicsTick(DriftUpdate* update){
	DriftGameState* state = update->state;
	
	if(state->physics) DriftZoneMemRelease(state->physics->mem);
	DriftMem* mem = DriftZoneMemAquire(update->ctx->app->zone_heap, "Physics");
	
	uint body_count = update->state->bodies.c.table.row_count;
	uint substeps = 4, iterations = 2;
	DriftPhysics* phys = state->physics = DRIFT_COPY(mem, ((DriftPhysics){
		.mem = mem,
		.dt = update->dt,
		.dt_sub = update->tick_dt/substeps,
		.dt_sub_inv = substeps/update->tick_dt,
		
		.body_count = body_count,
		.terra = state->terra,
		.bias_coef = 0.25f,
		
		.x = state->bodies.position, .q = state->bodies.rotation,
		.v = state->bodies.velocity, .w = state->bodies.angular_velocity,
		
		.m_inv = state->bodies.mass_inv, .i_inv = state->bodies.moment_inv,
		.r = state->bodies.radius,
		.ctype = state->bodies.collision_type,
		
		.x_bias = DriftAlloc(mem, body_count*sizeof(*phys->x_bias)),
		.q_bias = DriftAlloc(mem, body_count*sizeof(*phys->q_bias)),
		.bounds = DriftAlloc(mem, body_count*sizeof(*phys->bounds)),
		.loose_bounds = DriftAlloc(mem, body_count*sizeof(*phys->loose_bounds)),
		// TODO should come up with real hueristics for these eventually.
		.ground_plane = DriftAlloc(mem, body_count*sizeof(*phys->ground_plane)),
		.cpair = DRIFT_ARRAY_NEW(mem, 2*body_count, DriftCollisionPair),
		.contact = DRIFT_ARRAY_NEW(mem, 2*body_count, DriftContact),
	}));
	
	TracyCZoneN(ZONE_COLLISION_PAIRS, "Collision Pairs", true);
	PhysicsJobContext* bounds_job_ctx = phys_job_enqueue(update, phys, 0, body_count, 1024, bounds_job);
	PhysicsJobContext* terrain_job_ctx = phys_job_enqueue(update, phys, 1, body_count, 256, terrain_job);
	
	TracyCZoneN(ZONE_WAIT_JOBS, "Wait For Bounds", true);
	tina_job_wait(update->job, &bounds_job_ctx->group, 0);
	TracyCZoneEnd(ZONE_WAIT_JOBS);
	
	TracyCZoneN(ZONE_BROADPHASE, "Broadphase", true);
	// TODO A bit awkward, the RTree indexes start at 0, but the component arrays start at 1.
	DriftRTreeUpdate(&state->rtree, phys->bounds + 1, phys->loose_bounds + 1, body_count - 1, update->mem);
	DRIFT_ARRAY(DriftIndexPair) overlap_pairs = DriftRTreePairs(&state->rtree, phys->bounds + 1, update->job, update->mem);
	
	uint overlap_pair_count = DriftArrayLength(overlap_pairs);
	for(uint i = 0; i < overlap_pair_count; i++){
		DriftIndexPair pair = overlap_pairs[i];
		pair.idx0++, pair.idx1++;
		// Need to fix RTree indexes.
		if(DriftCollisionFilter(phys->ctype[pair.idx0], phys->ctype[pair.idx1])){
			DRIFT_ARRAY_PUSH(phys->cpair, ((DriftCollisionPair){.ipair = pair, .make_contacts = ContactCircleCircle}));
		}
	}
	TracyCZoneEnd(ZONE_BROADPHASE);
	TracyCZoneEnd(ZONE_COLLISION_PAIRS);
	
	tina_job_wait(update->job, &terrain_job_ctx->group, 0);
	
	TracyCZoneN(ZONE_CALLBACKS, "Callbacks", true);
	static DriftMap COLLISION_MAP;
	// Initialize collision calback map if needed.
	if(COLLISION_MAP.table.buffer == NULL){
		DriftMapInit(&COLLISION_MAP, DriftSystemMem, "CollisionCallbacks", 0);
		for(uint i = 1; COLLISION_CALLBACKS[i].callback; i++){
			uintptr_t key = collision_hash(COLLISION_CALLBACKS[i].types[0], COLLISION_CALLBACKS[i].types[1]);
			DriftMapInsert(&COLLISION_MAP, key, i);
		}
	}
	
	// TODO should this happen in substep to access contacts?
	uint collision_pair_count = DriftArrayLength(phys->cpair);
	for(uint i = 0; i < collision_pair_count; i++){
		DriftIndexPair pair = phys->cpair[i].ipair;
		DriftCollisionType t0 = phys->ctype[pair.idx0], t1 = phys->ctype[pair.idx1];
		uint idx = DriftMapFind(&COLLISION_MAP, collision_hash(t0, t1));
		// Swap the order if the it doesn't match the definition.
		if(COLLISION_CALLBACKS[idx].types[0] != t0) pair = (DriftIndexPair){pair.idx1, pair.idx0};
		// TODO Need to pass contact info or fixup swapped normals?
		COLLISION_CALLBACKS[idx].callback(update, phys, pair);
	}
	TracyCZoneEnd(ZONE_CALLBACKS);
}

void DriftPhysicsSubstep(DriftUpdate* update){
	DriftPhysics* phys = update->state->physics;
	uint body_count = phys->body_count;
	
	TracyCZoneN(ZONE_SUBSTEP, "Physics Substep", true);
	TracyCZoneN(ZONE_INTPOS, "IntPos", true);
	// Integrate position.
	for(uint i = 0; i < body_count; i++){
		phys->x[i].x += phys->v[i].x*phys->dt_sub;
		phys->x[i].y += phys->v[i].y*phys->dt_sub;
		phys->q[i] = linearized_rotation(phys->q[i], phys->w[i], phys->dt_sub);
		
		// TODO Should this validate bounding boxes?
		// Is it realistically possible to add to the cpairs here anyway?
	}
	TracyCZoneEnd(ZONE_INTPOS);
	
	// Generate contacts
	TracyCZoneN(ZONE_CONTACTS, "Contacts", true);
	DriftArrayHeader(phys->contact)->count = 0;
	
	uint collision_pair_count = DriftArrayLength(phys->cpair);
	for(uint i = 0; i < collision_pair_count; i++) phys->cpair[i].make_contacts(phys, phys->cpair[i].ipair);
	TracyCZoneEnd(ZONE_CONTACTS);
	
	TracyCZoneN(ZONE_TERRAIN, "Terrain", true);
	for(uint i = 1; i < body_count; i++){
		DriftVec3 plane = phys->ground_plane[i];
		DriftVec2 n = {plane.x, plane.y};
		float overlap = DriftVec2Dot(phys->x[i], n) - phys->r[i] - plane.z;
		if(overlap < 0) PushContact(phys, (DriftIndexPair){i, 0}, overlap, n, DriftVec2Mul(n, -phys->r[i]), DRIFT_VEC2_ZERO);
	}
	TracyCZoneEnd(ZONE_TERRAIN);
	
	uint contact_count = DriftArrayLength(phys->contact);
	
	// Integrate velocity here... in the future if needed I guess?
	{}
	
	// Apply cached impulses.
	TracyCZoneN(ZONE_PRESTEP, "Pre-step", true);
	for(uint i = 0; i < contact_count; i++){
		DriftContact* con = phys->contact + i;
		ApplyImpulse(phys, con->pair, con->r0, con->r1, DriftVec2Rotate(con->n, (DriftVec2){con->jn, con->jt}));
	}
	TracyCZoneEnd(ZONE_PRESTEP);
	
	TracyCZoneN(ZONE_SOLVE, "Solve", true);
	for(uint iter = 0; iter < DRIFT_PHYSICS_ITERATIONS; iter++){
		// Reset bias velocities.
		memset(phys->x_bias, 0, body_count*sizeof(*phys->x_bias));
		memset(phys->q_bias, 0, body_count*sizeof(*phys->q_bias));
		
		// Solve contacts.
		for(uint i = 0; i < contact_count; i++){
			DriftContact* con = phys->contact + i;
			DriftIndexPair pair = con->pair;
			
			DriftVec2 n = con->n, r0 = con->r0, r1 = con->r1;
			DriftVec2 v_rel = relative_velocity_at(pair, phys->v, phys->w, r0, r1);
			
			// Normal + restitution impulse.
			float vn_rel = DriftVec2Dot(v_rel, n);
			float jn0 = con->jn, jn = -(con->bounce + vn_rel)*con->mass_n;
			jn = con->jn = fmaxf(jn + jn0, 0.0f);
			
			// Friction impulse.
			float vt_rel = DriftVec2Dot(v_rel, DriftVec2Perp(n));
			float jt_max = con->friction*con->jn;
			float jt0 = con->jt, jt = -vt_rel*con->mass_t;
			jt = con->jt = DriftClamp(jt + jt0, -jt_max, jt_max);
			
			ApplyImpulse(phys, con->pair, r0, r1, DriftVec2Rotate(n, (DriftVec2){jn - jn0, jt - jt0}));
			
			// Bias impulse.
			float vn_rel_bias = DriftVec2Dot(n, relative_velocity_at(pair, phys->x_bias, phys->q_bias, r0, r1));
			float jbn = (con->bias - vn_rel_bias)*con->mass_n;
			con->jbn = fmaxf(jbn + con->jbn, 0.0f);
			
			ApplyBiasImpulse(phys, pair, r0, r1, DriftVec2Mul(n, con->jbn));
		}
	}
	TracyCZoneEnd(ZONE_SOLVE);
	
	TracyCZoneN(ZONE_RESOLVE, "Resolve", true);
	for(uint i = 0; i < body_count; i++){
		phys->x[i].x += phys->x_bias[i].x;
		phys->x[i].y += phys->x_bias[i].y;
		phys->w[i] += phys->q_bias[i];
	}
	TracyCZoneEnd(ZONE_RESOLVE);
	TracyCZoneEnd(ZONE_SUBSTEP);
}
