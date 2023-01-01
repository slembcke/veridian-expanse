#define DRIFT_FLOW_MAP_COUNT 2

#define TMP_SAVE_FILENAME "dump.bin"

typedef struct DriftNuklear DriftNuklear;
typedef struct DriftGameContext DriftGameContext;
typedef struct DriftGameState DriftGameState;

// TODO audit
struct DriftGameState {
	DRIFT_ARRAY(DriftComponent*) components;
	DRIFT_ARRAY(DriftTable*) tables;
	
	mtx_t entities_mtx;
	DriftEntitySet entities;
	DriftComponentTransform transforms;
	DriftComponentRigidBody bodies;
	DriftComponentPlayer players;
	DriftComponentDrone drones;
	// DriftComponentOreDeposit ore_deposits;
	DriftComponentItem items;
	DriftComponentScan scan;
	DriftComponentPowerNode power_nodes;
	DriftTablePowerNodeEdges power_edges;
	DriftComponentFlowMap flow_maps[DRIFT_FLOW_MAP_COUNT];
	DriftNavComponent navs;
	DriftComponentProjectiles projectiles;
	DriftComponentHealth health;
	DriftComponentBugNav bug_nav;
	DriftComponentEnemy enemies;
	
	DriftTerrain* terra;
	DriftRTree rtree;
	DriftPhysics* physics;
	
	DriftAudioSampler thruster_sampler;
	
	u16 inventory[_DRIFT_ITEM_COUNT];
	float scan_progress[_DRIFT_SCAN_COUNT];
	
	struct {
		bool enable_controls;
		bool show_hud;
		DriftScanType scan_restrict;
		bool factory_rebooted;
		
		DriftEntity factory_node;
	} status;
	
	struct {
		DRIFT_ARRAY(DriftSprite) sprites;
		DRIFT_ARRAY(DriftPrimitive) prims;
	} debug;
};

void DriftGameStateInit(DriftGameState* state, bool reset_game);
void DriftGameStateRender(DriftDraw* draw);

DriftEntity DriftMakeEntity(DriftGameState* state);
void DriftDestroyEntity(DriftUpdate* update, DriftEntity entity);

typedef struct DriftScript {
	DriftUpdate* update;
	
	u8 buffer[64*1024];
	tina* coro;
	bool debug_skip;
	
	void* script_ctx;
	void (*draw)(DriftDraw* draw, struct DriftScript* script);
} DriftScript;

void* DriftTutorialScript(tina* coro, void* value);

typedef struct DriftToast {
	char message[64];
	u64 timestamp;
	uint count;
} DriftToast;

struct DriftGameContext {
	DriftApp* app;
	
	DriftGameState state;
	DriftEntity player;
	DriftScript script;
	DriftToast toasts[DRIFT_MAX_TOASTS];
	
	DriftInput input;
	
	DriftDrawShared* draw_shared;
	
	float time_scale_log;
	// Current and init wall clock times.
	u64 clock_nanos, init_nanos;
	// Game's current update, tick, and substep times.
	u64 update_nanos, tick_nanos;
	uint current_tick, _tick_counter;
	uint current_frame, _frame_counter;
	
	mu_Context* mu;
	DriftUIState ui_state;
	DriftScanType last_scan;
	
	struct {
		DriftNuklear* ui;
		bool show_ui, hide_hud, godmode;
		
		bool reset_on_load;
		bool draw_terrain_sdf, hide_terrain_decals, disable_haze, boost_ambient;
		bool regen_terrain_on_load;
		bool pause, paint;
		u64 tick_nanos;
	} debug;
};

double DriftGameContextUpdateNanos(DriftGameContext* ctx);

void DriftContextGameStart(tina_job* job);
void DriftGameContextLoop(tina_job* job);

void DriftContextPushToast(DriftGameContext* ctx, const char* format, ...);

typedef struct DriftUpdate {
	DriftGameContext* ctx;
	DriftGameState* state;
	DriftAudioContext* audio;
	DriftMem* mem;
	tina_job* job;
	tina_scheduler* scheduler;
	uint frame, tick;
	u64 nanos;
	float dt, tick_dt;
	DriftAffine prev_vp_matrix;
	DriftEntity* _dead_entities;
} DriftUpdate;

void DriftGameStateIO(DriftIO* io);
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
