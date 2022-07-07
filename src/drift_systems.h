typedef struct DriftGameContext DriftGameContext;
typedef struct DriftUpdate DriftUpdate;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	DriftAffine* matrix;
} DriftComponentTransform;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	struct {
		uint frame;
		DriftRGBA8 color;

		DriftLight light;
	}* data;
} DriftComponentSprite;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	DriftVec2* position;
	DriftVec2* velocity;
	DriftVec2* rotation;
	float* angular_velocity;
	
	float* mass_inv;
	float* moment_inv;
	
	DriftVec2* offset;
	float* radius;
	DriftCollisionType* collision_type;
} DriftComponentRigidBody;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
} DriftComponentOreDeposit;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	DriftItemType* type;
} DriftComponentPickup;

typedef struct {
	s16 x, y;
} DriftPowerNode;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	DriftPowerNode* node;
} DriftComponentPowerNode;

typedef struct {
	DriftEntity e0, e1;
	s16 x0, y0, x1, y1;
} DriftPowerNodeEdge;

typedef struct {
	DriftTable t;
	uint update_cursor;
	DriftPowerNodeEdge* edge;
} DriftTablePowerNodeEdges;

typedef struct {
	DriftEntity next;
	float root_dist, next_dist;
} DriftFlowMapNode;

typedef struct {
	DriftComponent c;
	DriftName name;
	DriftEntity* entity;
	DriftFlowMapNode* node;
} DriftComponentFlowMap;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	struct {
		u8 flow_map;
		float radius;
		DriftEntity next_node;
		DriftVec2 target_pos;
	}* data;
} DriftNavComponent;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	DriftVec2* origin;
	DriftVec2* velocity;
	u32* tick0;
	float* timeout;
} DriftComponentProjectiles;

typedef struct {
	float value, maximum;
	DriftItemType drop;
} DriftHealth;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	
	DriftHealth* data;
} DriftComponentHealth;

typedef struct {
	DriftItemType type;
	DriftItemType request;
	uint count;
} DriftCargoSlot;

typedef struct {
	float value, target, rate;
} DriftAnimState;

typedef struct {
	DriftAnimState hatch_l, hatch_r;
	DriftAnimState laser, cannons;
} DriftPlayerAnimState;

typedef struct {
	DriftAffine arr[4];
} PlayerCannonTransforms;

PlayerCannonTransforms CalculatePlayerCannonTransforms(float cannon_anim);

typedef struct DriftPlayerData {
	bool local;

	DriftVec2 desired_velocity, desired_rotation;
	float thrusters[5];
	DriftAudioSampler thruster_sampler;
	
	float power_reserve, power_capacity;
	
	DriftToolType quickslots[DRIFT_QUICKSLOT_COUNT];
	uint quickslot_idx;
	
	bool headlight;
	float nacelle_l, nacelle_r;
	DriftPlayerAnimState anim_state;
	
	DriftVec2 reticle;
	bool is_digging;
	DriftVec2 dig_pos;
	DriftEntity grabbed_entity;
	DriftItemType grabbed_type;
	bool cargo_hatch_open;
	
	DriftCargoSlot cargo_slots[DRIFT_PLAYER_CARGO_SLOT_COUNT];
	uint cargo_idx;
} DriftPlayerData;

DriftCargoSlot* DriftPlayerGetCargoSlot(DriftPlayerData* player, DriftItemType type);

typedef struct {
	DriftComponent c;
	uint count;
	
	DriftEntity* entity;
	DriftPlayerData* data;
} DriftComponentPlayer;

typedef struct {
	DriftComponent c;
	uint count;
	
	DriftEntity* entity;
	struct {
		
	}* data;
} DriftComponentDrone;

void DriftSystemsInit(DriftGameState* state);
void DriftSystemsUpdate(DriftUpdate* update);
void DriftSystemsTick(DriftUpdate* update);
void DriftSystemsDraw(DriftDraw* draw);

typedef struct DriftPhysics DriftPhysics;
bool DriftCollisionFilter(DriftCollisionType a, DriftCollisionType b);
void DriftPhysicsSyncTransforms(DriftUpdate* update, float dt_diff);
void DriftPhysicsTick(DriftUpdate* update);
void DriftPhysicsSubstep(DriftUpdate* update);

typedef struct {
	DriftEntity e;
	DriftVec2 pos;
	bool valid;
	bool is_too_close;
	float blocked_at;
} DriftNearbyNodeInfo;

typedef struct {
	DriftVec2 pos;
	bool valid_node;
	bool valid_power;
	DRIFT_ARRAY(DriftNearbyNodeInfo) nodes;
} DriftNearbyNodesInfo;

DriftNearbyNodesInfo DriftSystemPowerNodeNearby(DriftGameState* state, DriftVec2 pos, DriftMem* mem);

DriftEntity DriftDroneMake(DriftGameState* state, DriftVec2 pos);

void DriftHealthApplyDamage(DriftUpdate* update, DriftEntity entity, float amount);
