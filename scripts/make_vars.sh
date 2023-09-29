set -x
export ASEPRITE="aseprite"
export GLSLANG="glslangValidator"
export SPIRV_OPT="spirv-opt"
export SPIRV_CROSS="spirv-cross"
export CMAKE_COMMAND="cmake"
export VPATH=`dirname $0`/..
export NULL=/dev/null

DBG_LUA=1 WRITE_PNG=1 ZIP_FLAGS=-9 make -f $VPATH/resources.mk drift-game-assets
