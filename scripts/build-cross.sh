set -ex

BUILD_FLAGS="-G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel /drift"
MINGW_TOOLCHAIN="-DCMAKE_TOOLCHAIN_FILE=/drift/scripts/mingw-toolchain.cmake"
RPI64_TOOLCHAIN="-DCMAKE_TOOLCHAIN_FILE=/drift/scripts/rpi64-toolchain.cmake"
RPI32_TOOLCHAIN="-DCMAKE_TOOLCHAIN_FILE=/drift/scripts/rpi32-toolchain.cmake"

cmake $BUILD_FLAGS $MINGW_TOOLCHAIN -DMAKE_COMMAND=make -DZIP_FLAGS=-9 -B /drift-mingw
cmake --build /drift-mingw
#strip /drift-mingw/veridian-expanse.exe
FILES="/drift-mingw/veridian-expanse.exe /sdl-mingw/SDL2.dll /drift-mingw/resources.zip /drift-mingw/*.inc"
cp $FILES /drift/$1

cmake $BUILD_FLAGS $RPI64_TOOLCHAIN -DDRIFT_RESOURCES=0 -B /drift-rpi64
cp /drift-mingw/*.inc /drift-rpi64/
cmake --build /drift-rpi64
#/cross-pi-gcc-10.3.0-64/bin/aarch64-linux-gnu-strip /drift-rpi64/veridian-expanse
cp /drift-rpi64/veridian-expanse /drift/$1/veridian-expanse-rpi64

# cmake $BUILD_FLAGS $RPI32_TOOLCHAIN -DDRIFT_RESOURCES=0 -B /drift-rpi32
# cp /drift-mingw/*.inc /drift-rpi32/
# cmake --build /drift-rpi32
# #/cross-pi-gcc-10.3.0-2/bin/arm-linux-gnueabihf-strip /drift-rpi32/veridian-expanse
# cp /drift-rpi32/veridian-expanse /drift/$1/veridian-expanse-rpi32
