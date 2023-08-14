/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if __unix__ || __APPLE__
	#include <unistd.h>
	#include <signal.h>
	#include <sys/mman.h>
	#if __linux__
		#include <sys/prctl.h>
	#endif
#elif __WIN64__
	#include <windows.h>
#endif

#include <SDL.h>

#include "tracy/TracyC.h"

#define _TINA_MUTEX_T SDL_mutex*
#define _TINA_MUTEX_INIT(_LOCK_) _LOCK_ = SDL_CreateMutex()
#define _TINA_MUTEX_DESTROY(_LOCK_) SDL_DestroyMutex(_LOCK_)
#define _TINA_MUTEX_LOCK(_LOCK_) {TracyCZoneN(ZONE, "Lock", true); SDL_LockMutex(_LOCK_); TracyCZoneEnd(ZONE);}
#define _TINA_MUTEX_UNLOCK(_LOCK_) {TracyCZoneN(ZONE, "Unlock", true); SDL_UnlockMutex(_LOCK_); TracyCZoneEnd(ZONE);}
#define _TINA_COND_T SDL_cond*
#define _TINA_COND_INIT(_SIG_) _SIG_ = SDL_CreateCond()
#define _TINA_COND_DESTROY(_SIG_) SDL_DestroyCond(_SIG_)
#define _TINA_COND_WAIT(_SIG_, _LOCK_) SDL_CondWait(_SIG_, _LOCK_)
#define _TINA_COND_SIGNAL(_SIG_) {TracyCZoneN(ZONE, "Signal", true); SDL_CondSignal(_SIG_); TracyCZoneEnd(ZONE);}
#define _TINA_COND_BROADCAST(_SIG_) SDL_CondBroadcast(_SIG_)

#ifdef TRACY_FIBERS
#define _TINA_PROFILE_ENTER(_JOB_) TracyCFiberEnter(job->fiber->name)
#define _TINA_PROFILE_LEAVE(_JOB_, _STATUS) TracyCFiberLeave
#endif

#define TINA_IMPLEMENTATION
#include "tina/tina.h"

#define TINA_JOBS_IMPLEMENTATION
#include "tina/tina_jobs.h"

#include "drift_base.h"

DriftApp* APP;

void DriftThrottledParallelFor(tina_job* job, tina_job_func func, void* user_data, uint count){
	tina_scheduler* sched = tina_job_get_scheduler(job);
	tina_group jobs = {};
	for(uint idx = 0; idx < count; idx++){
		tina_job_wait(job, &jobs, DRIFT_APP_MAX_THREADS);
		tina_scheduler_enqueue(sched, func, user_data, idx, DRIFT_JOB_QUEUE_WORK, &jobs);
	}
	tina_job_wait(job, &jobs, 0);
}

void DriftParallelFor(tina_job* job, tina_job_func func, void* user_data, uint count){
	tina_group group = {};
	tina_scheduler_enqueue_n(tina_job_get_scheduler(job), func, user_data, count, DRIFT_JOB_QUEUE_WORK, &group);
	tina_job_wait(job, &group, 0);
}

#if DRIFT_MODULES
static void DriftModuleLoad(void){
	SDL_UnloadObject(APP->module);
	
	char libname[256];
#if __unix__
	snprintf(libname, sizeof(libname), "./%s.so", APP->module_libname);
#elif __APPLE__
	snprintf(libname, sizeof(libname), "./%s.dylib", APP->module_libname);
#elif __WIN64__
	snprintf(libname, sizeof(libname), "%s-tmp.dll", APP->module_libname);
	// Windows can't replace an open file. So need to copy the lib before opening it.
	char srcname[256];
	snprintf(srcname, sizeof(srcname), "%s.dll", APP->module_libname);
	DRIFT_ASSERT_HARD(CopyFile(srcname, libname, false), "Failed to copy lib");
#else
	#error Unhandled platform.
#endif
	
	while((APP->module = SDL_LoadObject(libname)) == NULL){
		DRIFT_LOG("Failed to load module. (%s)\nRebuild and press the any key to try again.", SDL_GetError());
		getchar();
	}
	
	DriftAssetsReset();
}

void DriftModuleRun(tina_job* job){
	tina_job_func* entrypoint = SDL_LoadFunction(APP->module, APP->module_entrypoint);
	DRIFT_ASSERT_HARD(entrypoint, "Failed to find entrypoint function. (%s)", SDL_GetError());
	
	APP->module_status = DRIFT_MODULE_RUNNING;
	tina_scheduler_enqueue(APP->scheduler, entrypoint, NULL, 0, DRIFT_JOB_QUEUE_MAIN, &APP->module_entrypoint_jobs);
}

static void module_reload(tina_job* job){
	// Rebuild the module
	if(system(APP->module_build_command)){
		APP->module_status = DRIFT_MODULE_ERROR;
	} else {
		APP->module_status = DRIFT_MODULE_READY;
		
		// Wait for the previous entrypoint to exit.
		tina_job_wait(job, &APP->module_entrypoint_jobs, 0);
		DriftModuleLoad();
		DriftModuleRun(job);
	}
}

void DriftModuleRequestReload(tina_job* job){
	if(APP->module_status == DRIFT_MODULE_RUNNING || APP->module_status == DRIFT_MODULE_ERROR){
		APP->module_status = DRIFT_MODULE_BUILDING;
		tina_scheduler_enqueue(APP->scheduler, module_reload, APP, 0, DRIFT_JOB_QUEUE_WORK, &APP->module_rebuild_jobs);
	}
}
#endif

typedef struct {
	uint id, queue;
	SDL_Thread* thread;
	const char* name;
} DriftThread;

static _Thread_local uint ThreadID;
uint DriftGetThreadID(void){return ThreadID;}

static int DriftAppWorkerThreadBody(void* user_data){
	DriftThread* thread = user_data;
	ThreadID = thread->id;
	
#if __linux__
	sigset_t mask;
	sigfillset(&mask);
	DRIFT_ASSERT(pthread_sigmask(SIG_BLOCK, &mask, NULL) == 0, "Failed to block signals");
#endif

#if TRACY_ENABLE
	TracyCSetThreadName(thread->name);
#endif
	
	tina_scheduler_run(APP->scheduler, thread->queue, false);
	return 0;
}

static void* DriftAppFiberBody(tina* fiber, void* value){
	while(true){
		tina_job* job = value;
		
		// TracyCZoneN(ZONE_JOB, "DriftJob", true);
		// TracyCZoneName(ZONE_JOB, job->desc.name, strlen(job->desc.name));
		// TracyCZoneValue(ZONE_JOB, job->desc.user_idx);
		job->desc.func(job);
		// TracyCZoneEnd(ZONE_JOB);
		
		value = tina_yield(fiber, (void*)_TINA_STATUS_COMPLETED);
	}
	
	DRIFT_ABORT("Unreachable");
}

#define JOB_COUNT 1024
#define FIBER_COUNT 64
char FIBER_NAMES[FIBER_COUNT][64];

static tina* DriftAppFiberFactory(tina_scheduler* sched, unsigned fiber_idx, void* buffer, size_t stack_size, void* user_ptr){
	stack_size = 256*1024;
	
#if __unix__
	int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK;
	buffer = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, flags, -1, 0);
	
	// Might as well add guard pages for predictable crashes.
	size_t page_size = getpagesize();
	stack_size -= page_size;
	mprotect((u8*)buffer + page_size, page_size, PROT_NONE);
	mprotect((u8*)buffer + stack_size, page_size, PROT_NONE);
#else
	buffer = DriftAlloc(DriftSystemMem, stack_size);
#endif

	snprintf(FIBER_NAMES[fiber_idx], sizeof(FIBER_NAMES[0]), "TINA JOBS FIBER %02d", fiber_idx);
	tina* fiber = tina_init(buffer, stack_size, DriftAppFiberBody, sched);
	fiber->name = FIBER_NAMES[fiber_idx];
	
	return fiber;
}

static void DriftThreadInit(DriftThread* thread, uint id, uint queue, const char* name){
	*thread = (DriftThread){.id = id, .queue = queue, .name = name};
	thread->thread = SDL_CreateThread(DriftAppWorkerThreadBody, name, thread);
}

void DriftPrefsIO(DriftIO* io){
	DriftPreferences* prefs = io->user_ptr;
	DriftIOBlock(io, "prefs", prefs, sizeof(*prefs));
}

#if __unix__ || __APPLE__
	static unsigned DriftAppGetCPUCount(void){return sysconf(_SC_NPROCESSORS_ONLN);}
#elif __WIN64__
	static unsigned DriftAppGetCPUCount(void){
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		return sysinfo.dwNumberOfProcessors;
	}
#else
	#error Not implemented for this platform.
#endif

int DriftMain(DriftApp* app){
	APP = app;
	
	TracyCZoneN(ZONE_STARTUP, "Startup", true);
	TracyCZoneN(ZONE_JOBS, "Jobs", true);
	// Setup jobs.
	uint thread_count = DRIFT_MAX(DRIFT_MIN(DriftAppGetCPUCount() + 1u, DRIFT_APP_MAX_THREADS), 3u);
	DRIFT_LOG("DriftApp thread count: %d", thread_count);
	
	
	size_t sched_size = tina_scheduler_size(JOB_COUNT, _DRIFT_JOB_QUEUE_COUNT, FIBER_COUNT, 0);
	void* sched_buffer = DriftAlloc(DriftSystemMem, sched_size);
	APP->scheduler = _tina_scheduler_init2(sched_buffer, JOB_COUNT, _DRIFT_JOB_QUEUE_COUNT, FIBER_COUNT, 0, DriftAppFiberFactory, NULL);
	tina_scheduler_queue_priority(APP->scheduler, DRIFT_JOB_QUEUE_MAIN, DRIFT_JOB_QUEUE_WORK);
	
	DriftThread threads[DRIFT_APP_MAX_THREADS];
	DriftThreadInit(threads + 1, DRIFT_THREAD_ID_GFX, DRIFT_JOB_QUEUE_GFX, "gfx");
	char thread_names[DRIFT_APP_MAX_THREADS][64];
	for(uint i = DRIFT_THREAD_ID_WORKER0; i < thread_count; i++){
		snprintf(thread_names[i], sizeof(thread_names[0]), "work %d", i);
		DriftThreadInit(threads + i, i, DRIFT_JOB_QUEUE_WORK, thread_names[i]);
	}
	TracyCZoneEnd(ZONE_JOBS);
	
	// Setup memory.
	APP->zone_heap = DriftZoneMemHeapNew(DriftSystemMem, "Global");
	
	TracyCZoneN(ZONE_RESOURCES, "Resources", true);
	DriftAssetsReset();
	TracyCZoneEnd(ZONE_RESOURCES);
	
#if DRIFT_MODULES
	DriftModuleLoad();
	void (*DriftTerrainLoadBase)(tina_scheduler* sched) = SDL_LoadFunction(APP->module, "DriftTerrainLoadBase");
#else
	void DriftTerrainLoadBase(tina_scheduler* sched);
#endif

	// Start terrain loading before opening the shell since it takes a long time.
	DriftTerrainLoadBase(APP->scheduler);
	
	// Start shell and module.
	TracyCZoneN(ZONE_START, "Start", true)
	APP->shell_func(DRIFT_SHELL_START, NULL);
	
	APP->prefs = (DriftPreferences){
		.master_volume = 1, .music_volume = 0.5f, .effects_volume = 1.0f,
		.sharpening = 2, .lightfield_scale = 4, .hires = false,
		.mouse_sensitivity = 1.0f, .joy_deadzone = 0.15f,
	};
	DriftIOFileRead(TMP_PREFS_FILENAME, DriftPrefsIO, &APP->prefs);
	
	TracyCZoneN(ZONE_OPEN_AUDIO, "Open Audio", true);
	APP->audio = DriftAudioContextNew(APP->scheduler);
	DriftAudioSetParams(APP->prefs.master_volume, APP->prefs.music_volume, APP->prefs.effects_volume);
	TracyCZoneEnd(ZONE_OPEN_AUDIO);
		
	tina_scheduler_enqueue(APP->scheduler, APP->entry_func, app, 0, DRIFT_JOB_QUEUE_MAIN, NULL);
	TracyCZoneEnd(ZONE_START);
	TracyCZoneEnd(ZONE_STARTUP);
	
	// Run the main queue until shutdown.
	tina_scheduler_run(APP->scheduler, DRIFT_JOB_QUEUE_MAIN, false);
	
	// Gracefully shut down worker threads.
	for(uint i = DRIFT_THREAD_ID_GFX; i < thread_count; i++) SDL_WaitThread(threads[i].thread, NULL);
	tina_scheduler_destroy(APP->scheduler);
	DriftDealloc(DriftSystemMem, sched_buffer, sched_size);
	
	APP->shell_func(DRIFT_SHELL_STOP, NULL);
	DriftZoneMemHeapFree(APP->zone_heap);
	
	DriftAudioContextFree(APP->audio);
	
	if(APP->shell_restart){
		DRIFT_LOG("Restarting...");
		APP->shell_func = APP->shell_restart;
		APP->shell_restart = NULL;
		return DriftMain(app);
	} else {
		return EXIT_SUCCESS;
	}
}

void DriftAppShowWindow(void){
	DriftAssertMainThread();
	APP->shell_func(DRIFT_SHELL_SHOW_WINDOW, NULL);
}

void DriftAppHaltScheduler(void){
	tina_scheduler_interrupt(APP->scheduler, DRIFT_JOB_QUEUE_MAIN);
	tina_scheduler_interrupt(APP->scheduler, DRIFT_JOB_QUEUE_WORK);
	tina_scheduler_interrupt(APP->scheduler, DRIFT_JOB_QUEUE_GFX);
}

DriftGfxRenderer* DriftAppBeginFrame(DriftMem* mem){
	DriftAssertMainThread();
	return APP->shell_func(DRIFT_SHELL_BEGIN_FRAME, mem);
}

void DriftAppPresentFrame(DriftGfxRenderer* renderer){
	DriftAssertGfxThread();
	APP->shell_func(DRIFT_SHELL_PRESENT_FRAME, renderer);
}

void DriftAppToggleFullscreen(void){
	DriftAssertMainThread();
	APP->fullscreen = !APP->fullscreen;
	APP->shell_func(DRIFT_SHELL_TOGGLE_FULLSCREEN, NULL);
}

void DriftAssertMainThread(void){
	DRIFT_ASSERT_HARD(ThreadID == DRIFT_THREAD_ID_MAIN, "Must be called from the main queue.");
}

void DriftAssertGfxThread(void){
	DRIFT_ASSERT_HARD(ThreadID == DRIFT_THREAD_ID_GFX, "Must be called from the gfx queue.");
}

void* DriftShellConsole(DriftShellEvent event, void* shell_value){
	switch(event){
		case DRIFT_SHELL_START:{
			DRIFT_LOG("Using Console");
		} break;
		
		case DRIFT_SHELL_STOP:{
			DRIFT_LOG("Console Shutdown.");
		} break;
		
		default: break;
	}
	
	return NULL;
}
