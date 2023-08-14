/*
This file is part of Veridian Expanse.

Veridian Expanse is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Veridian Expanse is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Veridian Expanse. If not, see <https://www.gnu.org/licenses/>.
*/

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

	extern tina_job_func DriftGameStart;
	DriftApp app = {
#if DRIFT_MODULES
		.module_libname = "libdrift-game",
		.module_entrypoint = "DriftGameStart",
		.module_build_command = "ninja drift-game resources.zip",
		.entry_func = DriftModuleRun,
#else
		.entry_func = DriftGameStart,
#endif
		
		app.shell_func = DriftShellSDLVk,
	};

	
	for(int i = 0; i < argc; i++){
		if(strcmp(argv[i], "--gl") == 0) app.shell_func = DriftShellSDLGL;
		if(strcmp(argv[i], "--fullscreen") == 0) app.fullscreen = true;
		if(strcmp(argv[i], "--quickstart") == 0) app.no_splash = true;
		
#if DRIFT_VULKAN
		if(strcmp(argv[i], "--vk") == 0) app.shell_func = DriftShellSDLVk;
#endif

#if DRIFT_DEBUG
		if(strcmp(argv[i], "--test-only") == 0) exit(0);
#endif
	}
	
	return DriftMain(&app);
}
