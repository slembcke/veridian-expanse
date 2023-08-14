set -x
export ASEPRITE="aseprite"
export GLSLANG="glslangValidator"
export SPIRV_OPT="spirv-opt"
export SPIRV_CROSS="spirv-cross"
export CMAKE_COMMAND="cmake"
export VPATH=`dirname $0`/..
export NULL=/dev/null

WRITE_PNG=1 ZIP_FLAGS=-9 make -f $VPATH/resources.mk resources/art/cryo_nautilus_large_idle.png
