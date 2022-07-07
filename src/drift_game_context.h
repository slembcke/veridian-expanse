#define DRIFT_FLOW_MAP_COUNT 2

#define TMP_SAVE_FILENAME "dump.bin"
#define TMP_PREFS_FILENAME "prefs.bin"

typedef struct DriftNuklear DriftNuklear;
typedef struct DriftGameContext DriftGameContext;
typedef struct DriftGameState DriftGameState;

typedef struct {
	float master_volume, music_volume;
} DriftPreferences;

struct DriftGameState {
	DRIFT_ARRAY(DriftComponent*) components;
	DRIFT_ARRAY(DriftTable*) tables;
	
	mtx_t entities_mtx;
	DriftEntitySet entities;
	DriftComponentTransform transforms;
	DriftComponentSprite sprites;
	DriftComponentRigidBody bodies;
	DriftComponentPlayer players;
	DriftComponentDrone drones;
	DriftComponentOreDeposit ore_deposits;
	DriftComponentPickup pickups;
	DriftComponentPowerNode power_nodes;
	DriftTablePowerNodeEdges power_edges;
	DriftComponentFlowMap flow_maps[DRIFT_FLOW_MAP_COUNT];
	DriftNavComponent navs;
	DriftComponentProjectiles projectiles;
	DriftComponentHealth health;
	
	DriftTerrain* terra;
	DriftRTree rtree;
	DriftPhysics* physics;
	
	DRIFT_ARRAY(DriftPrimitive) debug_prims;
};

typedef struct {
	DriftGameContext* ctx;
	u8 buffer[64*1024];
	tina* coro;
	char message[256];
	bool debug_skip;
} DriftScript;

struct DriftGameContext {
	DriftApp* app;
	
	DriftGameState state;
	DriftEntity player;
	DriftScript script;
	const char* message;
	
	DriftInput input;
	DriftPreferences prefs;
	DriftAudioContext* audio;
	bool audio_ready;
	
	// TODO Move to player data?
	u16 inventory[_DRIFT_ITEM_TYPE_COUNT];
	
	DriftDrawShared* draw_shared;
	
	float time_scale_log;
	// Current and init wall clock times.
	u64 clock_nanos, init_nanos;
	// Game's current update, tick, and substep times.
	u64 update_nanos, tick_nanos;
	uint current_frame, current_tick;
	
	struct {
		DriftNuklear* ui;
		bool show_ui;
		
		bool reset_on_load;
		bool draw_terrain_sdf;
		bool regen_terrain_on_load;
		bool pause, paint;
		u64 tick_nanos;
	} debug;
};

typedef struct DriftUpdate {
	DriftGameContext* ctx;
	DriftGameState* state;
	DriftAudioContext* audio;
	DriftZoneMem *zone;
	DriftMem* mem;
	tina_job* job;
	tina_scheduler* scheduler;
	float dt, tick_dt;
	DriftAffine prev_vp_matrix;
	DriftEntity* _dead_entities;
} DriftUpdate;

void DriftContextGameStart(tina_job* job);
void DriftGameContextLoop(tina_job* job);

DriftEntity DriftMakeEntity(DriftGameState* state);
void DriftDestroyEntity(DriftUpdate* update, DriftEntity entity);

#if true
static inline void DriftDebugCircle(DriftGameState* state, DriftVec2 center, float radius, DriftRGBA8 color){
	DRIFT_ARRAY_PUSH(state->debug_prims, ((DriftPrimitive){.p0 = center, .p1 = center, .radii = {radius}, .color = color}));
}
static inline void DriftDebugCircle2(DriftGameState* state, DriftVec2 center, float r1, float r0, DriftRGBA8 color){
	DRIFT_ARRAY_PUSH(state->debug_prims, ((DriftPrimitive){.p0 = center, .p1 = center, .radii = {r1, r0}, .color = color}));
}
static inline void DriftDebugSegment(DriftGameState* state, DriftVec2 p0, DriftVec2 p1, float radius, DriftRGBA8 color){
	DRIFT_ARRAY_PUSH(state->debug_prims, ((DriftPrimitive){.p0 = p0, .p1 = p1, .radii = {radius}, .color = color}));
}
static inline void DriftDebugSegment2(DriftGameState* state, DriftVec2 p0, DriftVec2 p1, float r1, float r0, DriftRGBA8 color){
	DRIFT_ARRAY_PUSH(state->debug_prims, ((DriftPrimitive){.p0 = p0, .p1 = p1, .radii = {r1, r0}, .color = color}));
}
#else
#define DriftDebugCircle(...){}
#define DriftDebugSegment(...){}
#define DriftDebugSegment2(...){}
#endif
