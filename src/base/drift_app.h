/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include "tina/tina_jobs.h"

tina_scheduler* tina_job_get_scheduler(tina_job* job);

void DriftThrottledParallelFor(tina_job* job, tina_job_func func, void* user_data, uint count);
void tina_scheduler_enqueue_n(tina_scheduler* sched, tina_job_func* func, void* user_data, uint count, unsigned queue_idx, tina_group* group);
void DriftParallelFor(tina_job* job, tina_job_func func, void* user_data, uint count);

#define DRIFT_APP_DEFAULT_SCREEN_W 1280
#define DRIFT_APP_DEFAULT_SCREEN_H 720

typedef struct DriftApp DriftApp;
extern DriftApp* APP;

typedef enum {
	DRIFT_SHELL_START,
	DRIFT_SHELL_SHOW_WINDOW,
	DRIFT_SHELL_STOP,
	DRIFT_SHELL_BEGIN_FRAME,
	DRIFT_SHELL_PRESENT_FRAME,
	DRIFT_SHELL_TOGGLE_FULLSCREEN,
} DriftShellEvent;

typedef void* DriftShellFunc(DriftShellEvent event, void* ctx);

#define DRIFT_APP_MAX_THREADS 16u

#define TMP_PREFS_FILENAME "prefs.bin"
typedef struct {
	float master_volume, music_volume, effects_volume;
	float sharpening, lightfield_scale;
	float mouse_sensitivity, joy_deadzone;
	bool hires;
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
	bool request_quit;
	int window_x, window_y, window_w, window_h;
	float scaling_factor;
	void* shell_window;
	void* shell_context;
	bool fullscreen, no_splash;
	
	DriftAudioContext* audio;
	
	// Jobs.
	tina_scheduler* scheduler;
	
	const DriftGfxDriver* gfx_driver;
	
	DriftZoneMemHeap* zone_heap;
	void* app_context;
	void* input_context;
};

void* DriftShellConsole(DriftShellEvent event, void* shell_value);
void* DriftShellSDLGL(DriftShellEvent event, void* shell_value);
void* DriftShellSDLVk(DriftShellEvent event, void* shell_value);

#if DRIFT_MODULES
void DriftModuleRun(tina_job* job);
void DriftModuleRequestReload(tina_job* job);
#endif

int DriftMain(DriftApp* app);
void DriftAppShowWindow(void);
DriftGfxRenderer* DriftAppBeginFrame(DriftMem* mem);
void DriftAppPresentFrame(DriftGfxRenderer* renderer);
void DriftAppHaltScheduler(void);
void DriftAppToggleFullscreen(void);
