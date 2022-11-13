typedef struct mu_Context mu_Context;
typedef union SDL_Event SDL_Event;

mu_Context* DriftUIInit(void);
void DriftUIHotload(mu_Context* mu);

void DriftUIHandleEvent(mu_Context* mu, SDL_Event* event, float scale);
void DriftUIBegin(DriftDraw* draw);
void DriftUIPresent(DriftDraw* draw);

void DriftUIOpen(DriftGameContext* ctx, const char* ui);

typedef enum {
	DRIFT_PAUSE_STATE_NONE,
	DRIFT_PAUSE_STATE_MENU,
	DRIFT_PAUSE_STATE_SETTINGS,
} DriftPauseState;

void DriftPauseLoop(DriftGameContext* ctx, tina_job* job, DriftAffine vp_matrix);

typedef enum {
	DRIFT_UI_STATE_NONE,
	DRIFT_UI_STATE_SCAN,
	DRIFT_UI_STATE_CRAFT,
	_DRIFT_UI_STATE_MAX,
} DriftUIState;

void DriftGameContextMapLoop(DriftGameContext* ctx, tina_job* job, DriftAffine game_vp_matrix, DriftUIState ui_state, uintptr_t data);
void DriftScanUI(DriftDraw* draw, DriftUIState* ui_state, DriftScanType select);
void DriftCraftUI(DriftDraw* draw, DriftUIState* ui_state);
