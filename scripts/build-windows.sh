set -ex

cp SDL2-2.0.20/lib/x64/SDL2.dll .
cmake -DCMAKE_TOOLCHAIN_FILE=/drift/scripts/mingw-toolchain.cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo /drift \
	&& ninja \
	&& zip -9 /drift/project-drift-windows-`date -I`.zip drift.exe resources.zip SDL2.dll
