set -ex

cmake -Bmingw-build -DCMAKE_TOOLCHAIN_FILE=scripts/mingw-toolchain.cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug

cd mingw-build
curl https://libsdl.org/release/SDL2-devel-2.0.20-VC.zip > SDL2.zip && unzip SDL2.zip
cp SDL2-2.0.20/lib/x64/SDL2.dll .
