/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "base/drift_base.h"

#define DRIFT_SUBSTEPS 4
#define DRIFT_PHYSICS_ITERATIONS 2

#define DRIFT_TICK_HZ 60.0f
#define DRIFT_SUBSTEP_HZ (DRIFT_TICK_HZ*DRIFT_SUBSTEPS)

#define DRIFT_PLAYER_COUNT 1
#define DRIFT_MAX_TOASTS 4

#define DRIFT_PLAYER_SPEED 300
#define DRIFT_PLAYER_AUTOPILOT_SPEED (2*DRIFT_PLAYER_SPEED)
#define DRIFT_PLAYER_SIZE 14

#define DRIFT_SCAN_DURATION 3

#define DRIFT_POWER_BEAM_RADIUS (DRIFT_PLAYER_SIZE + 2)
#define DRIFT_POWER_EDGE_MIN_LENGTH 64
#define DRIFT_POWER_EDGE_MAX_LENGTH 192

#define DRIFT_MAX_BIOME 4
#define DRIFT_SPAWN_RADIUS 2048

#define DRIFT_START_POSITION ((DriftVec2){1304.3f, -2534.8f})
#define DRIFT_SKIFF_POSITION ((DriftVec2){3285.1f, -2731.1f})

#define DRIFT_SCRIPT_BUFFER_SIZE (128*1024)

#define DRIFT_PANEL_COLOR ((DriftRGBA8){0x01, 0x18, 0x24, 0x9C})

typedef struct DriftGameContext DriftGameContext;
typedef struct DriftGameState DriftGameState;
typedef struct DriftUpdate DriftUpdate;
typedef struct DriftDraw DriftDraw;
typedef struct DriftScript DriftScript;

typedef struct mu_Context mu_Context;
typedef struct mu_Container mu_Container;
typedef union SDL_Event SDL_Event;

typedef struct DriftPhysics DriftPhysics;
typedef bool DriftCollisionCallback(DriftUpdate* update, DriftPhysics* phys, DriftIndexPair pair);

typedef enum {
	DRIFT_BIOME_LIGHT,
	DRIFT_BIOME_RADIO,
	DRIFT_BIOME_CRYO,
	DRIFT_BIOME_DARK,
	DRIFT_BIOME_SPACE,
	_DRIFT_BIOME_COUNT,
} DriftBiomeType;

typedef enum {
	DRIFT_UI_STATE_NONE,
	DRIFT_UI_STATE_SPLASH,
	DRIFT_UI_STATE_PAUSE,
	DRIFT_UI_STATE_SETTINGS,
	DRIFT_UI_STATE_NYI,
	DRIFT_UI_STATE_MAP,
	DRIFT_UI_STATE_SCAN,
	DRIFT_UI_STATE_CRAFT,
	DRIFT_UI_STATE_LOGS,
	_DRIFT_UI_STATE_MAX,
} DriftUIState;

typedef enum {
	DRIFT_COLLISION_NONE,
	DRIFT_COLLISION_TERRAIN,
	DRIFT_COLLISION_PLAYER,
	DRIFT_COLLISION_ITEM,
	DRIFT_COLLISION_NON_HOSTILE,
	DRIFT_COLLISION_HIVE_DRONE,
	DRIFT_COLLISION_PLAYER_BULLET,
	DRIFT_COLLISION_ENEMY_BULLET,
	DRIFT_COLLISION_HIVE,
	DRIFT_COLLISION_PLAYER_DRONE,
} DriftCollisionType;

typedef enum {
	DRIFT_ITEM_NONE,
	
	// Scrap materials
	DRIFT_ITEM_SCRAP,
	DRIFT_ITEM_ADVANCED_SCRAP,
	
	// Ore materials
	DRIFT_ITEM_VIRIDIUM,
	DRIFT_ITEM_BORONITE,
	DRIFT_ITEM_RADONITE,
	DRIFT_ITEM_METRIUM,
	
	// Rare materials
	DRIFT_ITEM_COPPER,
	DRIFT_ITEM_SILVER,
	DRIFT_ITEM_GOLD,
	DRIFT_ITEM_GRAPHENE,
	
	// Biomech materials
	DRIFT_ITEM_LUMIUM,
	DRIFT_ITEM_FLOURON,
	DRIFT_ITEM_FUNGICITE,
	DRIFT_ITEM_MORPHITE,
	
	// Intermediate materials
	DRIFT_ITEM_POWER_SUPPLY,
	DRIFT_ITEM_OPTICS,
	
	// Tools
	DRIFT_ITEM_HEADLIGHT,
	DRIFT_ITEM_MINING_LASER,
	DRIFT_ITEM_FAB_RADIO,
	
	// Upgrades
	DRIFT_ITEM_AUTOCANNON,
	DRIFT_ITEM_ZIP_CANNON,
	DRIFT_ITEM_SHIELD_L2,
	DRIFT_ITEM_SHIELD_L3,
	DRIFT_ITEM_CARGO_L2,
	DRIFT_ITEM_CARGO_L3,
	DRIFT_ITEM_NODES_L2,
	DRIFT_ITEM_NODES_L3,
	DRIFT_ITEM_STORAGE_L2,
	DRIFT_ITEM_STORAGE_L3,
	// DRIFT_ITEM_HEAT_EXCHANGER,
	// DRIFT_ITEM_THERMAL_CONDENSER,
	// DRIFT_ITEM_RADIO_PLATING,
	// DRIFT_ITEM_MIRROR_PLATING,
	// DRIFT_ITEM_SPECTROMETER,
	// DRIFT_ITEM_SMELTING_MODULE,
	
	// Consumables
	DRIFT_ITEM_POWER_NODE,
	// DRIFT_ITEM_FUNGAL_NODE,
	// DRIFT_ITEM_METRIUM_NODE,
	// DRIFT_ITEM_VIRIDIUM_SLUG,
	// DRIFT_ITEM_MISSILES,
	DRIFT_ITEM_DRONE,
	
	_DRIFT_ITEM_COUNT,
} DriftItemType;

typedef enum {
	DRIFT_SCAN_NONE,
	
	DRIFT_SCAN_VIRIDIUM,
	DRIFT_SCAN_BORONITE,
	DRIFT_SCAN_RADONITE,
	DRIFT_SCAN_METRIUM,
	
	DRIFT_SCAN_LUMIUM,
	DRIFT_SCAN_FLOURON,
	DRIFT_SCAN_FUNGICITE,
	DRIFT_SCAN_MORPHITE,
	
	DRIFT_SCAN_SCRAP,
	DRIFT_SCAN_GLOW_BUG,
	DRIFT_SCAN_HIVE_WORKER,
	DRIFT_SCAN_HIVE_FIGHTER,
	DRIFT_SCAN_TRILOBYTE_LARGE,
	DRIFT_SCAN_NAUTILUS_HEAVY,
	DRIFT_SCAN_CONSTRUCTION_SKIFF,
	DRIFT_SCAN_POWER_NODE,
	DRIFT_SCAN_OPTICS,
	DRIFT_SCAN_HEADLIGHT,
	DRIFT_SCAN_AUTOCANNON,
	DRIFT_SCAN_ZIP_CANNON,
	DRIFT_SCAN_LASER,
	DRIFT_SCAN_FAB_RADIO,
	DRIFT_SCAN_SHIELD_L2,
	DRIFT_SCAN_SHIELD_L3,
	DRIFT_SCAN_CARGO_L2,
	DRIFT_SCAN_CARGO_L3,
	DRIFT_SCAN_NODES_L2,
	DRIFT_SCAN_NODES_L3,
	DRIFT_SCAN_STORAGE_L2,
	DRIFT_SCAN_STORAGE_L3,
	DRIFT_SCAN_HIVE,
	DRIFT_SCAN_HIVE_POD,
	DRIFT_SCAN_COPPER,
	DRIFT_SCAN_COPPER_DEPOSIT,
	DRIFT_SCAN_SILVER,
	DRIFT_SCAN_SILVER_DEPOSIT,
	DRIFT_SCAN_GOLD,
	DRIFT_SCAN_GOLD_DEPOSIT,
	DRIFT_SCAN_GRAPHENE,
	DRIFT_SCAN_POWER_SUPPLY,
	DRIFT_SCAN_DRONE,
	_DRIFT_SCAN_COUNT,
} DriftScanType;

typedef enum {
	DRIFT_SCAN_UI_NONE,
	DRIFT_SCAN_UI_FABRICATOR,
	_DRIFT_SCAN_UI_COUNT,
} DriftScanUIType;

typedef enum {
	DRIFT_ENEMY_NONE,
	DRIFT_ENEMY_GLOW_BUG,
	DRIFT_ENEMY_WORKER_BUG,
	DRIFT_ENEMY_FIGHTER_BUG,
	DRIFT_ENEMY_TRILOBYTE_LARGE,
	DRIFT_ENEMY_NAUTILUS_HEAVY,
	_DRIFT_ENEMY_COUNT,
} DriftEnemyType;

enum {
	DRIFT_SFX_NONE,
#include "sound_enums.inc"
	_DRIFT_SFX_COUNT,
};

#include "drift_input.h"
#include "drift_sprite.h"
#include "drift_draw.h"
#include "drift_terrain.h"
#include "drift_tools.h"
#include "drift_systems.h"
#include "drift_game_context.h"

// Loops

typedef enum {
	// Normal gameloop exit.
	DRIFT_LOOP_YIELD_DONE,
	// A game reload has been requested. (switching gfx, etc)
	DRIFT_LOOP_YIELD_RELOAD,
	// Hotload requested.
	DRIFT_LOOP_YIELD_HOTLOAD,
} DriftLoopYield;

DriftLoopYield DriftMenuLoop(tina_job* job, DriftGameContext* ctx);
DriftLoopYield DriftGameContextLoop(tina_job* job);

void DriftGameStart(tina_job* job);

// Physics

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
	DriftVec3* ground_plane;
	DRIFT_ARRAY(DriftCollisionPair) cpair;
	DRIFT_ARRAY(DriftContact) contact;
};

bool DriftCollisionFilter(DriftCollisionType a, DriftCollisionType b);

void DriftPhysicsTick(DriftUpdate* update, DriftMem* mem);
void DriftPhysicsSubstep(DriftUpdate* update);
void DriftPhysicsSyncTransforms(DriftUpdate* update, float dt_diff);

// UI

mu_Context* DriftUIInit(void);
void DriftUIHotload(mu_Context* mu);

void DriftUIHandleEvent(mu_Context* mu, SDL_Event* event, float scale);
void DriftUIBegin(mu_Context* mu, DriftDraw* draw);
void DriftUIPresent(mu_Context* mu, DriftDraw* draw);

DriftLoopYield DriftPauseLoop(DriftGameContext* ctx, tina_job* job, DriftAffine vp_matrix, bool* exit_to_menu);
DriftLoopYield DriftGameContextMapLoop(DriftGameContext* ctx, tina_job* job, DriftAffine game_vp_matrix, uintptr_t data);

void DriftUIOpen(DriftGameContext* ctx, const char* ui);
void DriftUICloseIndicator(mu_Context* mu, mu_Container* win);

void DriftScanUI(mu_Context* mu, DriftDraw* draw, DriftScanType* select, DriftUIState* ui_state);
void DriftCraftUI(mu_Context* mu, DriftDraw* draw, DriftUIState* ui_state);

// HUD

DriftRGBA8 DriftHUDIndicator(DriftDraw* draw, DriftVec2 pos, DriftRGBA8 color);
DriftAffine DriftHudDrawOffsetLabel(DriftDraw* draw, DriftVec2 origin, DriftVec2 dir, DriftVec4 color, const char* label);
void DriftHUDPushSwoop(DriftGameContext* ctx, DriftVec2 origin, DriftVec2 dir, DriftVec4 color, DriftItemType type);
void DriftHudPushToast(DriftGameContext* ctx, uint num, const char* format, ...);
void DriftDrawHud(DriftDraw* draw);
DriftVec2 DriftHUDDrawPanel(DriftDraw* draw, DriftVec2 panel_origin, DriftVec2 panel_size, float alpha);
void DriftDrawControls(DriftDraw* draw);

// Scans

typedef struct {
	const char* name;
	const char* description;
	const char* usage;
	float radius;
	DriftVec2 offset;
	DriftUIState ui_state;
} DriftScan;
extern const DriftScan DRIFT_SCANS[_DRIFT_SCAN_COUNT];

// Items

#define DRIFT_ITEM_MAX_INGREDIENTS 4
typedef struct {
	uint type, count;
} DriftIngredient;

typedef struct {
	DriftScanType scan;
	bool is_cargo, from_biomass, is_part;
	uint mass, limit, duration, makes;
	DriftIngredient ingredients[DRIFT_ITEM_MAX_INGREDIENTS];
} DriftItem;
extern const DriftItem DRIFT_ITEMS[_DRIFT_ITEM_COUNT];

static inline const char* DriftItemName(DriftItemType type){return DRIFT_SCANS[DRIFT_ITEMS[type].scan].name;}
static inline uint DriftItemBuildDuration(DriftItemType type){return DRIFT_ITEMS[type].duration ?: 1000;}

DriftEntity DriftItemMake(DriftGameState* state, DriftItemType type, DriftVec2 pos, DriftVec2 vel, uint tile_idx);
void DriftItemDraw(DriftDraw* draw, DriftItemType type, DriftVec2 pos, uint id, uint tick);
void DriftItemGrab(DriftUpdate* update, DriftEntity entity, DriftItemType type);
void DriftItemDrop(DriftUpdate* update, DriftEntity entity, DriftItemType type);

void DriftTickItemSpawns(DriftUpdate* update);
void DriftDrawItems(DriftDraw* draw);
DriftSprite DriftSpriteForItem(DriftItemType type, DriftAffine matrix, uint id, uint tick);

void DriftPowerNodeActivate(DriftGameState* state, DriftEntity e, DriftMem* mem);

// Enemies

extern DriftCollisionCallback DriftWorkerDroneCollide;

void DriftTickEnemies(DriftUpdate* update);
void DriftDrawEnemies(DriftDraw* draw);

void DriftDrawHivesMap(DriftDraw* draw, float scale);

// Scripts

struct DriftScript {
	void (*body)(DriftScript* script);
	bool (*check)(DriftScript* script);
	void (*draw)(DriftDraw* draw, DriftScript* script);
	void *user_data, *draw_data;
	bool run;
	
	DriftGameContext* game_ctx;
	DriftGameState* state;
	DriftUpdate* update;
	bool debug_skip;
	
	tina* _coro;
	u8 _stack_buffer[];
};

typedef void DriftScriptInitFunc(DriftScript* script);
DriftScript* DriftScriptNew(DriftScriptInitFunc* init_func, void* script_data, DriftGameContext* game_ctx);
bool DriftScriptTick(DriftScript* script, DriftUpdate* update);

void DriftScriptDraw(DriftScript* script, DriftDraw* draw);
void DriftScriptFree(DriftScript* script);

static inline bool DriftScriptYield(DriftScript* script){
	if(script->run) tina_yield(script->_coro, 0);
	return script->run;
}

extern DriftScriptInitFunc DriftTutorialScript;

// SDFs

#define DRIFT_SDF_MAX_DIST (DRIFT_TERRAIN_TILE_SIZE/4)

static inline u8 DriftSDFEncode(float x){
	const float quantization_fix = 0.5f/256.0f;
	return (u8)DriftClamp(255*(0.5f + 0.5f*x/DRIFT_SDF_MAX_DIST + quantization_fix), 0, 255);
}

static inline float DriftSDFDecode(u8 x){return (2*x/255.0f - 1)*DRIFT_SDF_MAX_DIST;}
static inline float DriftSDFValue(DriftVec3 c){return hypotf(c.x, c.y) + c.z;}
void DriftSDFFloodRow(DriftVec3* dst, uint dst_stride, DriftVec3* src, uint len, int r);

// Debug goodies

#if DRIFT_DEBUG
extern DriftVec4 TMP_COLOR[4];
extern float TMP_VALUE[4];
#endif
