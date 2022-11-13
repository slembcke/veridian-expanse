set -ex

cp SDL2-2.0.20/lib/x64/SDL2.dll .
cmake -DCMAKE_TOOLCHAIN_FILE=/drift/scripts/mingw-toolchain.cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo /drift
ninja

cp veridian-expanse.exe resources.zip SDL2.dll *.inc /drift/$1
