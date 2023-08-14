set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

SET(CMAKE_FIND_ROOT_PATH /cross-pi-gcc-10.3.0-64/aarch64-linux-gnu/)
set(CMAKE_C_COMPILER /cross-pi-gcc-10.3.0-64/bin/aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER /cross-pi-gcc-10.3.0-64/bin/aarch64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(SDL2_INCLUDE_DIRS /SDL/include)
set(SDL2_LIBRARIES /drift-rpi64/libSDL2-2.0.so)
set(RESOURCE_MK_COMMANDS
	ASEPRITE=aseprite
	GLSLANG=glslangValidator
	SPIRV_OPT=spirv-opt
	SPIRV_CROSS=spirv-cross
	NULL=/dev/null
)
