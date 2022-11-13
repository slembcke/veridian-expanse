#pragma once

#include "tinycthread/tinycthread.h"
#include "tina/tina_jobs.h"

tina_scheduler* tina_job_get_scheduler(tina_job* job);

void DriftThrottledParallelFor(tina_job* job, tina_job_func func, void* user_data, uint count);
void tina_scheduler_enqueue_n(tina_scheduler* sched, tina_job_func* func, void* user_data, uint count, unsigned queue_idx, tina_group* group);
void DriftParallelFor(tina_job* job, tina_job_func func, void* user_data, uint count);

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

#define TMP_PREFS_FILENAME "prefs.bin"
typedef struct {
	float master_volume, music_volume;
} DriftPreferences;

void DriftPrefsIO(DriftIO* io);

typedef struct DriftGfxDriver DriftGfxDriver;
typedef struct DriftGfxRenderer DriftGfxRenderer;
typedef struct DriftAudioContext DriftAudioContext;

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
		DRIFT_MODULE_RUNNING, // Module is running normally.
		DRIFT_MODULE_BUILDING, // Module is building.
		DRIFT_MODULE_ERROR, // Module failed to build or load.
		DRIFT_MODULE_READY, // Module is built and ready to reload.
	} module_status;
#endif
	
	DriftPreferences prefs;
	
	tina_job_func* entry_func;
	DriftShellFunc* shell_func;
	DriftShellFunc* shell_restart;
	int window_x, window_y, window_w, window_h;
	float scaling_factor;
	void* shell_window;
	void* shell_context;
	bool fullscreen;
	
	DriftAudioContext* audio;
	
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
void DriftModuleRun(tina_job* job);
void DriftModuleRequestReload(DriftApp* app, tina_job* job);
#endif

int DriftMain(DriftApp* app);
void DriftAppShowWindow(DriftApp* app);
DriftGfxRenderer* DriftAppBeginFrame(DriftApp* app, DriftMem* mem);
void DriftAppPresentFrame(DriftApp* app, DriftGfxRenderer* renderer);
void DriftAppHaltScheduler(DriftApp* app);
void DriftAppToggleFullscreen(DriftApp* app);

void DriftAppAssertMainThread(void);
void DriftAppAssertGfxThread(void);
