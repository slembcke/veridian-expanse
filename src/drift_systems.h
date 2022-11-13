typedef struct DriftGameContext DriftGameContext;
typedef struct DriftUpdate DriftUpdate;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
} DriftComponentTag;

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
} DriftComponentItem;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	DriftScanType* type;
} DriftComponentScan;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	DriftVec2* position;
	bool* active;
} DriftComponentPowerNode;

typedef struct {
	DriftEntity e0, e1;
	DriftVec2 p0, p1;
} DriftPowerNodeEdge;

typedef struct {
	DriftTable t;
	uint update_cursor;
	DriftPowerNodeEdge* edge;
} DriftTablePowerNodeEdges;

typedef struct {
	DriftEntity next;
	uint mark;
	float dist;
} DriftFlowNode;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	DriftFlowNode* flow;
	bool* current;
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
	
	DriftEnemyType* type;
	u16* aggro_ticks;
} DriftComponentEnemy;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;

	float* speed;
	float* accel;
	DriftVec2* forward_bias;
} DriftComponentBugNav;

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
	uint damage_timestamp, damage_timeout;
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

typedef struct {
	float angle[3];
	DriftVec2 current;
} DriftArmPose;

typedef struct DriftPlayerData {
	DriftVec2 desired_velocity, desired_rotation;
	float thrusters[5];
	
	float temp, energy, energy_cap;
	bool is_overheated, is_powered;
	// TODO should store connected pnode instead of bool
	uint power_timestamp, shield_timestamp;
	
	bool headlight;
	float nacelle_l, nacelle_r;
	DriftPlayerAnimState anim_state;
	DriftArmPose arm_l, arm_r;
	
	DriftVec2 reticle;
	bool is_digging;
	DriftVec2 dig_pos;
	DriftEntity grabbed_entity;
	DriftItemType grabbed_type;
	DriftEntity scanned_entity;
	
	struct {
		uint frame;
		DriftVec2 pos;
	} last_valid_drop;
	
	DriftCargoSlot cargo_slots[DRIFT_PLAYER_CARGO_SLOT_COUNT];
	DriftToolType tool_idx;
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

void DriftDrawPowerMap(DriftDraw* draw, float scale);

typedef struct {
	DriftEntity e;
	DriftVec2 pos;
	// If this node can be connected to by another node.
	bool node_can_connect;
	// If this node can be connected to by a player.
	bool player_can_connect;
	// Node is too close to allow connection.
	bool is_too_close;
	// Node raycast blocked at t=.
	float blocked_at;
} DriftNearbyNodeInfo;

typedef struct {
	DriftVec2 pos;
	bool node_can_connect;
	bool node_can_reach;
	bool player_can_connect;
	uint too_close_count;
	DRIFT_ARRAY(DriftNearbyNodeInfo) nodes;
} DriftNearbyNodesInfo;

DriftNearbyNodesInfo DriftSystemPowerNodeNearby(DriftGameState* state, DriftVec2 pos, DriftMem* mem, float beam_radius);

DriftEntity DriftDroneMake(DriftGameState* state, DriftVec2 pos);

void DriftHealthApplyDamage(DriftUpdate* update, DriftEntity entity, float amount);

bool DriftCheckSpawn(DriftUpdate* update, DriftVec2 pos, float terrain_dist);

void FireProjectile(DriftUpdate* update, DriftVec2 pos, DriftVec2 vel);
void MakeBlast(DriftUpdate* update, DriftVec2 position);

DriftEntity DriftTempPlayerInit(DriftGameState* state, DriftEntity e, DriftVec2 position);
