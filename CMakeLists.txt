cmake_minimum_required(VERSION 3.9)
cmake_policy(SET CMP0072 NEW)
cmake_policy(SET CMP0076 NEW)

project(drift VERSION 0.5)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_custom_target(drift DEPENDS veridian-expanse)

set(DRIFT_COMPILE_OPTIONS
	-Wall -Wextra -Werror
	-Wfloat-conversion #-Wdouble-promotion
	-Wmissing-prototypes -Wstrict-prototypes
	-Wno-unused -Wno-unused-result -Wno-unused-parameter
	-Wno-type-limits -Wno-missing-field-initializers -Warray-bounds
	-Wno-maybe-uninitialized -Waggressive-loop-optimizations
)

option(DRIFT_RESOURCES "Build resources.zip and atlas data." ON)
option(DRIFT_MODULES "Build using hot-reloadable modules." OFF)
option(DRIFT_VULKAN "Build using Vulkan." ON)
option(DRIFT_SANITIZE "Enable sanitizers." OFF)
option(DRIFT_TRACY "Enable Tracy profiling." OFF)

if(DRIFT_SANITIZE AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_BUILD_TYPE STREQUAL "Debug")
	add_compile_options(-fsanitize=address)
	add_link_options(-fsanitize=address)

	add_compile_options(-fsanitize=undefined)
	add_link_options(-fsanitize=undefined)
	
	# Squelch warnings about unaligned loads.
	add_compile_definitions(MINIZ_USE_UNALIGNED_LOADS_AND_STORES=0)
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL Linux OR ${CMAKE_SYSTEM_NAME} STREQUAL OpenBSD)
	find_package(SDL2 REQUIRED)
	set(RESOURCE_MK_COMMANDS
		-j8
		ASEPRITE=aseprite
		GLSLANG=glslangValidator
		SPIRV_OPT=spirv-opt
		SPIRV_CROSS=spirv-cross
		NULL=/dev/null
	)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL Darwin)
	set(SDL2_INCLUDE_DIRS /Users/slembcke/Library/Frameworks/SDL2.framework/Headers)
	set(SDL2_LIBRARIES /Users/slembcke/Library/Frameworks/SDL2.framework)
	
	set(Vulkan_SDK /Users/slembcke/VulkanSDK/1.2.182.0/macOS)
	set(RESOURCE_MK_COMMANDS
		ASEPRITE="/Applications/Aseprite.app/Contents/MacOS/aseprite"
		GLSLANG="${Vulkan_SDK}/Bin/glslangValidator"
		SPIRV_OPT="${Vulkan_SDK}/Bin/spirv-opt"
		SPIRV_CROSS="${Vulkan_SDK}/Bin/spirv-cross"
		NULL=/dev/null
	)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL Windows)
	set(SDL2_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/SDL2-2.0.20/include)
	set(SDL2_LIBRARIES ${CMAKE_BINARY_DIR}/SDL2-2.0.20/lib/x64/SDL2.dll)
	
	if(NOT DEFINED RESOURCE_MK_COMMANDS)
		set(Vulkan_SDK C:/VulkanSDK/1.2.182.0)
		set(RESOURCE_MK_COMMANDS
			ASEPRITE="C:/Program Files/Aseprite/Aseprite.exe"
			GLSLANG="${Vulkan_SDK}/Bin/glslangValidator"
			SPIRV_OPT="${Vulkan_SDK}/Bin/spirv-opt"
			SPIRV_CROSS="${Vulkan_SDK}/Bin/spirv-cross"
			NULL=NUL
		)
	endif()
	
	add_link_options(-mconsole)
else()
	message(SEND_ERROR "Unknown build system." ${CMAKE_SYSTEM_NAME})
endif()

if(DRIFT_MODULES)
	add_compile_definitions(DRIFT_MODULES=1)
	set(BUILD_SHARED_LIBS ON)
else()
	set(BUILD_SHARED_LIBS OFF)
endif()

set(DRIFT_INCLUDES
	src ext ext/tracy/public ${CMAKE_BINARY_DIR}
	${SDL2_INCLUDE_DIRS} ${Vulkan_INCLUDE_DIRS}
)

execute_process(
	COMMAND git rev-parse -q HEAD
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	OUTPUT_VARIABLE DRIFT_GIT_SHA
	OUTPUT_STRIP_TRAILING_WHITESPACE
)

add_compile_definitions(
	DRIFT_VERSION_MAJOR=${drift_VERSION_MAJOR}
	DRIFT_VERSION_MINOR=${drift_VERSION_MINOR}
	DRIFT_GIT_SHA="${DRIFT_GIT_SHA}"
)
message("git SHA: ${DRIFT_GIT_SHA}")
# configure_file(drift_config.h.in src/drift_config.h)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(ZIP_FLAGS \"-1 --filesync\")
else()
	set(ZIP_FLAGS -9)
endif()

add_library(drift-game
	src/drift_input.c
	src/drift_terrain.c
	src/drift_draw.c
	src/drift_game_context.c
	src/drift_items.c
	src/drift_scan.c
	src/drift_tools.c
	src/drift_enemies.c
	src/drift_systems.c
	src/drift_physics.c
	src/drift_sprite.c
	src/drift_terrain_gen.c
	src/drift_sdf.c
	src/drift_debug.c
	src/drift_script.c
	src/drift_ui.c
	ext/microui/microui.c
)

target_compile_options(drift-game PRIVATE ${DRIFT_COMPILE_OPTIONS})
target_include_directories(drift-game PUBLIC ${DRIFT_INCLUDES})
target_link_libraries(drift-game drift-core)

# For fast terrain loading during debug.
set_source_files_properties(src/drift_terrain.c PROPERTIES COMPILE_FLAGS -O3)

# Workaround to ignore a warning in the Nuklear demo code.
add_library(drift-nuklear OBJECT src/base/drift_nuklear.c)
target_include_directories(drift-nuklear PUBLIC ${DRIFT_INCLUDES})

if(DRIFT_RESOURCES)
	add_custom_target(resources
		BYPRODUCTS
			${CMAKE_BINARY_DIR}/resources.zip
			${CMAKE_BINARY_DIR}/sprite_enums.inc
			${CMAKE_BINARY_DIR}/sprite_defs.inc
		COMMAND make -s -f ${CMAKE_SOURCE_DIR}/resources.mk drift-game-assets
			CMAKE_COMMAND="${CMAKE_COMMAND}"
			${RESOURCE_MK_COMMANDS}
			VPATH=${CMAKE_SOURCE_DIR}
			ZIP_FLAGS=${ZIP_FLAGS}
	)
	set_directory_properties(PROPERTIES ADDITIONAL_CLEAN_FILES "resources;atlas")
	add_dependencies(drift-game resources)
	add_dependencies(drift-nuklear resources)
endif()

add_library(drift-core
	src/base/drift_app.c
	src/base/drift_util.c
	src/base/drift_math.c
	src/base/drift_rand.c
	src/base/drift_mem.c
	src/base/drift_table.c
	src/base/drift_map.c
	src/base/drift_entity.c
	src/base/drift_component.c
	src/base/drift_rtree.c
	src/base/drift_gfx.c
	src/base/drift_audio.c
	src/base/drift_app_sdl_gl.c
	ext/tinycthread/tinycthread.c
	ext/miniz/miniz.c
)
target_compile_definitions(drift-core PUBLIC MINIZ_NO_STDIO=0)
target_compile_options(drift-core PRIVATE ${DRIFT_COMPILE_OPTIONS})
target_include_directories(drift-core PUBLIC ${DRIFT_INCLUDES})
target_link_libraries(drift-core drift-nuklear ${SDL2_LIBRARIES})

if(${CMAKE_SYSTEM_NAME} STREQUAL Linux)
	target_link_libraries(drift-core m rt pthread)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL OpenBSD)
	target_link_libraries(drift-core m pthread)
endif()

add_executable(veridian-expanse src/base/main.c)
target_include_directories(veridian-expanse PUBLIC ${DRIFT_INCLUDES})
target_link_libraries(veridian-expanse drift-core)

add_custom_target(project-drift ALL DEPENDS drift)

if(DRIFT_VULKAN)
	add_compile_definitions(DRIFT_VULKAN=1)
	set(Vulkan_INCLUDE_DIRS ext/vulkan-headers/include)
	
	add_subdirectory(ext/volk)
	target_include_directories(volk PUBLIC ${Vulkan_INCLUDE_DIRS})
	
	add_library(drift-vk OBJECT src/base/drift_app_sdl_vk.c)
	target_include_directories(drift-vk PUBLIC ${DRIFT_INCLUDES})
	target_link_libraries(drift-vk volk)
	target_link_libraries(drift-core drift-vk)
endif()

if(DRIFT_MODULES)
	add_dependencies(veridian-expanse drift-game)
else()
	target_link_libraries(veridian-expanse drift-core drift-nuklear drift-game)
endif()

if(DRIFT_TRACY)
	add_compile_definitions(TRACY_ENABLE TRACY_FIBERS)
	add_subdirectory(ext/tracy)
	target_link_libraries(drift-core TracyClient)
endif()
