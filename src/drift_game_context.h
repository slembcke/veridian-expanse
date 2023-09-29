/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

typedef enum {
	DRIFT_FLOW_MAP_POWER,
	DRIFT_FLOW_MAP_PLAYER,
	DRIFT_FLOW_MAP_WAYPOINT,
	_DRIFT_FLOW_MAP_COUNT,
} DriftFlowMapID;

#define TMP_SAVE_FILENAME "dump.bin"

typedef struct DriftNuklear DriftNuklear;
typedef struct DriftGameContext DriftGameContext;
typedef struct DriftGameState DriftGameState;

typedef enum {
	DRIFT_TUTORIAL_SPAWN_NORMAL,
	DRIFT_TUTORIAL_SPAWN_LIMIT,
	DRIFT_TUTORIAL_SPAWN_DRONES,
} DriftTutorialSpawnPhase;

typedef struct {
	DriftItemType type;
	uint count;
} DriftCargoSlot;

// TODO audit
struct DriftGameState {
	DriftMem* mem;
	DRIFT_ARRAY(DriftComponent*) components;
	DriftMap named_components;
	
	DRIFT_ARRAY(DriftTable*) tables;
	DRIFT_ARRAY(DriftEntity) hot_entities;
	
	DriftEntitySet entities;
	DRIFT_ARRAY(DriftEntity) dead_entities;
	
	DriftComponentTransform transforms;
	DriftComponentRigidBody bodies;
	DriftComponentPlayer players;
	DriftComponentDrone drones;
	DriftComponentItem items;
	DriftComponentScan scan;
	DriftComponentScanUI scan_ui;
	DriftComponentPowerNode power_nodes;
	DriftTablePowerNodeEdges power_edges;
	DriftComponentFlowMap flow_maps[_DRIFT_FLOW_MAP_COUNT];
	DriftComponentHealth health;
	DriftComponentEnemy enemies;
	
	DriftTerrain* terra;
	DriftRTree rtree;
	DriftPhysics* physics;
	
	DriftEntity player;
	float scan_progress[_DRIFT_SCAN_COUNT];
	
	struct {
		u16 skiff[_DRIFT_ITEM_COUNT];
		u16 transit[_DRIFT_ITEM_COUNT];
		u16 cargo[_DRIFT_ITEM_COUNT];
	} inventory;
	
	struct {
		DriftItemType item;
		float progress;
	} fab;
	
	struct {
		uint count, pod;
	} dispatch;
	
	DriftScript* tutorial;
	DriftScript* script;
	
	// TODO move into player?
	DriftAudioSampler thruster_sampler;
	
	struct {
		// NOTE: These fields do not get saved and default to 0.
		// Mostly used by the tutorial or other scripts.
		uint save_lock;
		
		bool disable_look, disable_move, disable_hud, disable_scan, disable_nodes;
		bool needs_tutorial, spawn_at_start, never_seen_map;
		DriftTutorialSpawnPhase spawn_phase;
		DriftToolType tool_restrict;
		DriftScanType scan_restrict;
		
		struct {
			bool needs_reboot, needs_scroll;
			int scroll;
		} factory;
	} status;
	
	struct {
		DRIFT_ARRAY(DriftSprite) sprites;
		DRIFT_ARRAY(DriftPrimitive) prims;
	} debug;
};

DriftGameState* DriftGameStateNew(tina_job* job);
void DriftGameStateFree(DriftGameState* state);
void DriftGameStateSetupIntro(DriftGameState* state);

void DriftGameStateRender(DriftDraw* draw);

DriftComponent* DriftGameStateNamedComponentMake(DriftGameState* state, DriftComponent* component, const char* name, DriftColumnSet columns, uint capacity);
static inline DriftComponent* DriftGetNamedComponent(DriftGameState* state, const char* name){
	uintptr_t value = DriftMapFind(&state->named_components, DriftFNV64Str(name));
	DRIFT_ASSERT_HARD(value, "Component not found '%s'", name);
	return (DriftComponent*)value;
}

#define DRIFT_GAMESTATE_TYPED_COMPONENT_MAKE(_state_, _component_, _type_, _columns_, _min_capacity_){ \
	_Static_assert(offsetof(_type_, c) == 0); /* Make sure component is well formed */ \
	_type_* do_types_match = _component_; /* Make sure types match */ \
	DriftGameStateNamedComponentMake(_state_, &(_component_)->c, DRIFT_STR(_type_), _columns_, _min_capacity_); \
}
#define DRIFT_GET_TYPED_COMPONENT(_state_, _type_) ((_type_*)DriftGetNamedComponent(_state_, DRIFT_STR(_type_)))

DriftEntity DriftMakeEntity(DriftGameState* state);
DriftEntity DriftMakeHotEntity(DriftGameState* state);
void DriftDestroyEntity(DriftGameState* state, DriftEntity entity);

typedef struct DriftToast {
	char message[64];
	u64 timestamp;
	uint count, num;
} DriftToast;

struct DriftGameContext {
	DriftGameState* state;
	DriftToast toasts[DRIFT_MAX_TOASTS];
	
	DriftDrawShared* draw_shared;
	
	struct {
		bool dynamic;
		float size, dry, wet, decay, cutoff;
	} reverb;
	
	float time_scale_log;
	// Current and init wall clock times.
	u64 clock_nanos, init_nanos;
	// Game's current update, tick, and substep times.
	u64 update_nanos, tick_nanos;
	uint current_tick, _tick_counter; // TODO should be in state instead?
	uint current_frame, _frame_counter;
	
	mu_Context* mu;
	DriftUIState ui_state;
	DriftScanType last_scan;
	bool is_docked;
	
	struct {
		DriftNuklear* ui;
		bool show_ui, reset_ui, godmode;
		
		bool reset_on_load;
		bool draw_terrain_sdf, hide_terrain_decals, disable_haze, boost_ambient;
		bool regen_terrain_on_load;
		bool pause, paint;
		u64 tick_nanos;
	} debug;
};

double DriftGameContextUpdateNanos(DriftGameContext* ctx);

typedef struct DriftUpdate {
	DriftGameContext* ctx;
	DriftGameState* state;
	DriftMem* mem;
	tina_job* job;
	uint frame, tick;
	u64 nanos;
	float dt, tick_dt;
	DriftAffine prev_vp_matrix;
} DriftUpdate;

void DriftGameStateSave(DriftGameState* state);
bool DriftGameStateLoad(DriftGameState* state);

void DriftDebugUI(DriftUpdate* _update, DriftDraw* _draw);

#if true
static inline void DriftDebugCircle(DriftGameState* state, DriftVec2 center, float radius, DriftRGBA8 color){
	DRIFT_ARRAY_PUSH(state->debug.prims, ((DriftPrimitive){.p0 = center, .p1 = center, .radii = {radius}, .color = color}));
}
static inline void DriftDebugCircle2(DriftGameState* state, DriftVec2 center, float r1, float r0, DriftRGBA8 color){
	DRIFT_ARRAY_PUSH(state->debug.prims, ((DriftPrimitive){.p0 = center, .p1 = center, .radii = {r1, r0}, .color = color}));
}
static inline void DriftDebugSegment(DriftGameState* state, DriftVec2 p0, DriftVec2 p1, float radius, DriftRGBA8 color){
	DRIFT_ARRAY_PUSH(state->debug.prims, ((DriftPrimitive){.p0 = p0, .p1 = p1, .radii = {radius}, .color = color}));
}
static inline void DriftDebugSegment2(DriftGameState* state, DriftVec2 p0, DriftVec2 p1, float r1, float r0, DriftRGBA8 color){
	DRIFT_ARRAY_PUSH(state->debug.prims, ((DriftPrimitive){.p0 = p0, .p1 = p1, .radii = {r1, r0}, .color = color}));
}
static inline void DriftDebugRay(DriftGameState* state, DriftVec2 p0, DriftVec2 dir, float mul, DriftRGBA8 color){
	DRIFT_ARRAY_PUSH(state->debug.prims, ((DriftPrimitive){.p0 = p0, .p1 = DriftVec2FMA(p0, dir, mul), .radii = {1}, .color = color}));
}
static void DriftDebugBB(DriftGameState* state, DriftAABB2 bb, float radius, DriftRGBA8 color){
	DRIFT_ARRAY_PUSH(state->debug.prims, ((DriftPrimitive){.p0 = {bb.l, bb.b}, .p1 = {bb.r, bb.b}, .radii = {radius}, .color = color}));
	DRIFT_ARRAY_PUSH(state->debug.prims, ((DriftPrimitive){.p0 = {bb.l, bb.t}, .p1 = {bb.r, bb.t}, .radii = {radius}, .color = color}));
	DRIFT_ARRAY_PUSH(state->debug.prims, ((DriftPrimitive){.p0 = {bb.l, bb.b}, .p1 = {bb.l, bb.t}, .radii = {radius}, .color = color}));
	DRIFT_ARRAY_PUSH(state->debug.prims, ((DriftPrimitive){.p0 = {bb.r, bb.b}, .p1 = {bb.r, bb.t}, .radii = {radius}, .color = color}));
}
static inline void DriftDebugTransform(DriftGameState* state, DriftAffine transform, float scale){
	DriftVec2 p0 = DriftAffinePoint(transform, (DriftVec2){0, 0});
	DriftVec2 px = DriftAffinePoint(transform, (DriftVec2){scale, 0});
	DriftVec2 py = DriftAffinePoint(transform, (DriftVec2){0, scale});
	DRIFT_ARRAY_PUSH(state->debug.prims, ((DriftPrimitive){.p0 = p0, .p1 = px, .radii = {1}, .color = DRIFT_RGBA8_RED}));
	DRIFT_ARRAY_PUSH(state->debug.prims, ((DriftPrimitive){.p0 = p0, .p1 = py, .radii = {1}, .color = DRIFT_RGBA8_GREEN}));
}
#else
#define DriftDebugCircle(...){}
#define DriftDebugSegment(...){}
#define DriftDebugSegment2(...){}
#define DriftDebugRay(...){}
#define DriftDebugTransform(...){}
#endif
