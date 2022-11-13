typedef struct DriftPhysics DriftPhysics;

typedef void DriftContactFunc(DriftPhysics* ctx, DriftIndexPair pair);

typedef struct {
	DriftIndexPair ipair;
	DriftContactFunc* make_contacts;
} DriftCollisionPair;

typedef struct {
	DriftIndexPair pair;
	
	// Collision properties.
	DriftVec2 n, r0, r1;
	
	// Contact properties.
	float friction, bounce, bias;
	// Generalized mass.
	float mass_n, mass_t;
	
	// Cached impulses.
	float jn, jt, jbn;
} DriftContact;

struct DriftPhysics {
	DriftMem* mem;
	
	float dt, dt_sub, dt_sub_inv;
	uint body_count;
	DriftTerrain* terra;
	
	float bias_coef;
	
	DriftVec2 *x; DriftVec2 *q;
	DriftVec2 *v; float *w;
	float *m_inv, *i_inv;
	float* r;
	DriftCollisionType* ctype;
	
	DriftVec2* x_bias; float* q_bias;
	DriftAABB2* bounds; DriftAABB2* loose_bounds;
	DriftVec3* ground_plane;
	DRIFT_ARRAY(DriftCollisionPair) cpair;
	DRIFT_ARRAY(DriftContact) contact;
};

typedef bool DriftCollisionCallback(DriftUpdate* update, DriftPhysics* phys, DriftIndexPair pair);
bool DriftCollisionFilter(DriftCollisionType a, DriftCollisionType b);
void DriftPhysicsSyncTransforms(DriftUpdate* update, float dt_diff);
void DriftPhysicsTick(DriftUpdate* update);
void DriftPhysicsSubstep(DriftUpdate* update);
