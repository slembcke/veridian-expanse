#pragma once

#include "tinycthread/tinycthread.h"
#include "tina/tina_jobs.h"

tina_scheduler* tina_job_get_scheduler(tina_job* job);

void DriftThrottledParallelFor(tina_job* job, const char* name, tina_job_func func, void* user_data, uint count);

#define DRIFT_APP_DEFAULT_SCREEN_W 1280
#define DRIFT_APP_DEFAULT_SCREEN_H 720

enum {
	DRIFT_JOB_QUEUE_WORK,
	DRIFT_JOB_QUEUE_GFX,
	DRIFT_JOB_QUEUE_MAIN,
	_DRIFT_JOB_QUEUE_COUNT,
};

enum {
	DRIFT_THREAD_ID_MAIN,
	DRIFT_THREAD_ID_GFX,
	DRIFT_THREAD_ID_WORKER0,
};

uint DriftAppGetThreadID(void);

typedef struct DriftApp DriftApp;

typedef struct {
	DriftApp* app;
	uint id, queue;
	thrd_t thread;
	const char* name;
} DriftThread;

typedef enum {
	DRIFT_SHELL_START,
	DRIFT_SHELL_SHOW_WINDOW,
	DRIFT_SHELL_STOP,
	DRIFT_SHELL_BEGIN_FRAME,
	DRIFT_SHELL_PRESENT_FRAME,
	DRIFT_SHELL_TOGGLE_FULLSCREEN,
} DriftShellEvent;

typedef void* DriftShellFunc(DriftApp* app, DriftShellEvent event, void* ctx);

#define DRIFT_APP_MAX_THREADS 16u

typedef struct DriftGfxDriver DriftGfxDriver;
typedef struct DriftGfxRenderer DriftGfxRenderer;

struct DriftApp {
	int argc;
	const char** argv;
	
#if DRIFT_MODULES
	void* module;
	const char* module_libname;
	const char* module_entrypoint;
	const char* module_build_command;
	
	tina_group module_entrypoint_jobs, module_rebuild_jobs;
	
	enum {
		DRIFT_APP_MODULE_IDLE,
		DRIFT_APP_MODULE_BUILDING,
		DRIFT_APP_MODULE_ERROR,
		DRIFT_APP_MODULE_READY,
	} module_status;
#endif
	
	tina_job_func* init_func;
	DriftShellFunc* shell_func;
	DriftShellFunc* shell_restart;
	int window_x, window_y, window_w, window_h;
	void* shell_window;
	void* shell_context;
	bool fullscreen;
	
	// Jobs.
	tina_scheduler* scheduler;
	uint thread_count;
	DriftThread threads[DRIFT_APP_MAX_THREADS];
	
	const DriftGfxDriver* gfx_driver;
	
	DriftZoneMemHeap* zone_heap;
	void* app_context;
};

void* DriftShellConsole(DriftApp* app, DriftShellEvent event, void* shell_value);
void* DriftShellSDLGL(DriftApp* app, DriftShellEvent event, void* shell_value);
void* DriftShellSDLVk(DriftApp* app, DriftShellEvent event, void* shell_value);

#if DRIFT_MODULES
extern tina_job_func* DriftAppModuleStart;
bool DriftAppModuleRequestReload(DriftApp* app, tina_job* job);
#endif

int DriftMain(DriftApp* app);
void DriftAppShowWindow(DriftApp* app);
DriftGfxRenderer* DriftAppBeginFrame(DriftApp* app, DriftZoneMem* zone);
void DriftAppPresentFrame(DriftApp* app, DriftGfxRenderer* renderer);
void DriftAppHaltScheduler(DriftApp* app);
void DriftAppToggleFullscreen(DriftApp* app);

void DriftAppAssertMainThread();
void DriftAppAssertGfxThread();
