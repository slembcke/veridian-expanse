/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

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
	DriftItemType* type;
	uint* tile_idx;
} DriftComponentItem;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	DriftScanType* type;
} DriftComponentScan;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	DriftScanUIType* type;
} DriftComponentScanUI;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	DriftVec2* position;
	DriftVec2* rotation;
	float* clam;
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
	uint stamp;
	float dist;
} DriftFlowNode;

typedef struct {
	uint stamp;
	
	DriftComponent c;
	DriftEntity* entity;
	DriftFlowNode* flow;
	bool* is_valid;
} DriftComponentFlowMap;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	
	DriftEnemyType* type;
	uint* tile_idx;
	u16* aggro_ticks;
} DriftComponentEnemy;

typedef struct {
	float value, maximum, timeout;
	uint damage_tick0;
	DriftItemType drop;
	DriftSFX hit_sfx, die_sfx;
} DriftHealth;

typedef struct {
	DriftComponent c;
	DriftEntity* entity;
	
	DriftHealth* data;
} DriftComponentHealth;

typedef struct DriftGunState DriftGunState;

struct DriftGunState {
	uint repeat;
	float timeout;
};

typedef struct {
	float angle[3];
	DriftVec2 current;
} DriftArmPose;

typedef struct DriftPlayerData {
	DriftVec2 desired_velocity, desired_rotation;
	float thrusters[5];
	
	float temp, energy;
	bool is_overheated, is_powered;
	u64 power_tick0, shield_tick0;
	
	bool headlight;
	float nacelle_l, nacelle_r;
	DriftArmPose arm_l, arm_r;
	
	DriftVec2 reticle;
	DriftGunState primary, secondary;
	bool is_digging;
	DriftVec2 dig_pos;
	DriftEntity grabbed_entity;
	DriftItemType grabbed_type;
	DriftEntity scanned_entity;
	DriftScanType scanned_type;
	
	struct {
		uint frame;
		DriftVec2 pos;
	} last_valid_drop;
	
	DriftToolType tool_idx, tool_select;
	float tool_anim;
} DriftPlayerData;

typedef enum {
	DRIFT_DRONE_STATE_TO_POD,
	DRIFT_DRONE_STATE_TO_SKIFF,
	_DRIFT_DRONE_STATE_COUNT,
} DriftDroneState;

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
		DriftDroneState state;
		DriftItemType item;
		uint count;
	}* data;
} DriftComponentDrone;

typedef void DriftSystemsInitFunc(DriftGameState* state);
DriftSystemsInitFunc DriftSystemsInit;
DriftSystemsInitFunc DriftSystemsInitEnemies;
DriftSystemsInitFunc DriftSystemsInitWeapons;

typedef void DriftSystemsUpdateFunc(DriftUpdate* update);
DriftSystemsUpdateFunc DriftSystemsUpdate;

typedef void DriftSystemsTickFunc(DriftUpdate* update);
DriftSystemsTickFunc DriftSystemsTick;
DriftSystemsTickFunc DriftSystemsTickWeapons;

typedef void DriftSystemsDrawFunc(DriftDraw* draw);
DriftSystemsDrawFunc DriftSystemsDraw;
DriftSystemsDrawFunc DriftSystemsDrawWeapons;

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
	uint active_count;
	uint too_close_count;
	DRIFT_ARRAY(DriftNearbyNodeInfo) nodes;
} DriftNearbyNodesInfo;

DriftNearbyNodesInfo DriftSystemPowerNodeNearby(DriftGameState* state, DriftVec2 pos, DriftMem* mem, float beam_radius);

#define DRIFT_PLASMA_N 64
float* DriftGenPlasma(DriftDraw* draw);
void DriftDrawPlasma(DriftDraw* draw, DriftVec2 start, DriftVec2 end, float* plasma_wave);

DriftEntity DriftDroneMake(DriftGameState* state, DriftVec2 pos, DriftDroneState drone_state, DriftItemType item, uint count);

bool DriftHealthApplyDamage(DriftUpdate* update, DriftEntity entity, float amount, DriftVec2 pos);

bool DriftCheckSpawn(DriftUpdate* update, DriftVec2 pos, float terrain_dist);

void FireHiveProjectile(DriftUpdate* update, DriftRay2 ray);

typedef enum {
	DRIFT_BLAST_EXPLODE,
	DRIFT_BLAST_RICOCHET,
	DRIFT_BLAST_VIOLET_ZAP,
	DRIFT_BLAST_GREEN_FLASH,
	_DRIFT_BLAST_COUNT,
} DriftBlastType;

void DriftMakeBlast(DriftUpdate* update, DriftVec2 position, DriftVec2 normal, DriftBlastType type);

DriftEntity DriftTempPlayerInit(DriftGameState* state, DriftEntity e, DriftVec2 position);

uint DriftPlayerEnergyCap(DriftGameState* state);
uint DriftPlayerNodeCap(DriftGameState* state);
uint DriftPlayerCargoCap(DriftGameState* state);
uint DriftPlayerItemCount(DriftGameState* state, DriftItemType item);
uint DriftPlayerItemCap(DriftGameState* state, DriftItemType item);
uint DriftPlayerCalculateCargo(DriftGameState* state);

void DriftPlayerUpdateGun(DriftUpdate* update, DriftPlayerData* player, DriftAffine transform);
void DriftPlayerDrawGun(DriftDraw* draw, DriftPlayerData* player, DriftAffine transform, float hud_fade);

void DriftSystemsTickFab(DriftGameContext* ctx, float dt);

typedef struct {
	const char* label;
	u8 layer;
	float poisson, terrain, weight;
	DriftSpriteEnum* sprites;
} DriftDecalDef;

extern DriftDecalDef* DRIFT_DECAL_DEFS[_DRIFT_BIOME_COUNT];
