set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch32)

set(CMAKE_FIND_ROOT_PATH /cross-pi-gcc-10.3.0-2/arm-linux-gnueabihf/)
set(CMAKE_C_COMPILER /cross-pi-gcc-10.3.0-2/bin/arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER /cross-pi-gcc-10.3.0-2/bin/arm-linux-gnueabihf-g++)

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
