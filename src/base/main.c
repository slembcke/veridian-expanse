#include <stdlib.h>
#include <string.h>

#include "drift_base.h"

int main(int argc, const char* argv[argc+1]){
	DriftUtilInit();

#if DRIFT_DEBUG
	// unit_test_util();
	// unit_test_math();
	// unit_test_entity();
	// unit_test_map();
	// unit_test_component();
	// unit_test_rtree();
#endif

	DriftApp app = {};
	
#if DRIFT_MODULES
	app.module_libname = "libdrift-game";
	app.module_entrypoint = "DriftGameContextStart";
	app.module_build_command = "ninja drift-game resources.zip";
	app.entry_func = DriftModuleRun;
#else
	extern tina_job_func DriftGameContextStart;
	app.entry_func = DriftGameContextStart;
#endif

	app.shell_func = DriftShellSDLVk;
	
	for(int i = 0; i < argc; i++){
		if(strcmp(argv[i], "--gl") == 0) app.shell_func = DriftShellSDLGL;
		if(strcmp(argv[i], "--fullscreen") == 0) app.fullscreen = true;
		
#if DRIFT_VULKAN
		if(strcmp(argv[i], "--vk") == 0) app.shell_func = DriftShellSDLVk;
#endif

#if DRIFT_MODULES
		if(strcmp(argv[i], "--gen") == 0){
			app.shell_func = DriftShellConsole;
			app.module_entrypoint = "DriftTerrainGen";
		}
#endif

#if DRIFT_DEBUG
		if(strcmp(argv[i], "--test-only") == 0) exit(0);
#endif
	}
	
	return DriftMain(&app);
}
