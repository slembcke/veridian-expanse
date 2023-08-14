# syntax=docker/dockerfile:1
FROM ubuntu:22.10

RUN \
	apt-get update &&\
	DEBIAN_FRONTEND=noninteractive apt-get install -y \
		git cmake ninja-build gcc g++ python3 glslang-tools spirv-tools mingw-w64 zip curl pngquant \
		libx11-dev libxcursor-dev libxi-dev &&\
	apt-get clean

RUN --mount=type=tmpfs,target=/tmp \
	cd /tmp &&\
	git clone --branch sdk-1.3.224.1 --depth 1 https://github.com/KhronosGroup/SPIRV-Cross.git &&\
	cmake -B build-spirv-cross -G Ninja SPIRV-Cross/ &&\
	cd build-spirv-cross && ninja install

RUN --mount=type=tmpfs,target=/tmp \
	cd /tmp &&\
	git clone --branch v1.2.40 --depth 1 --shallow-submodules --recursive https://github.com/aseprite/aseprite.git &&\
	cmake -B build-aseprite -G Ninja -DLAF_BACKEND=none aseprite &&\
	cd build-aseprite && ninja install

# https://github.com/abhiTronix/raspberry-pi-cross-compilers/wiki/64-Bit-Cross-Compiler:-Installation-Instructions
RUN curl https://versaweb.dl.sourceforge.net/project/raspberry-pi-cross-compilers/Bonus%20Raspberry%20Pi%20GCC%2064-Bit%20Toolchains/Raspberry%20Pi%20GCC%2064-Bit%20Cross-Compiler%20Toolchains/Bullseye/GCC%2010.3.0/cross-gcc-10.3.0-pi_64.tar.gz | tar -xz
RUN curl https://cfhcable.dl.sourceforge.net/project/raspberry-pi-cross-compilers/Raspberry%20Pi%20GCC%20Cross-Compiler%20Toolchains/Bullseye/GCC%2010.3.0/Raspberry%20Pi%203A%2B%2C%203B%2B%2C%204/cross-gcc-10.3.0-pi_3%2B.tar.gz | tar -xz

RUN git clone --branch release-2.26.4 --depth 1 --shallow-submodules --recursive https://github.com/libsdl-org/SDL

COPY mingw-toolchain.cmake .
RUN cmake -DCMAKE_TOOLCHAIN_FILE=/mingw-toolchain.cmake -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -B sdl-mingw SDL && ninja -C sdl-mingw
RUN strip /sdl-mingw/SDL2.dll

COPY rpi64-toolchain.cmake .
RUN cmake -DCMAKE_TOOLCHAIN_FILE=/rpi64-toolchain.cmake -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -B sdl-rpi64 SDL && ninja -C sdl-rpi64
RUN cmake --install /sdl-rpi64/ --prefix /cross-pi-gcc-10.3.0-64/aarch64-linux-gnu/

COPY rpi32-toolchain.cmake .
RUN cmake -DCMAKE_TOOLCHAIN_FILE=/rpi32-toolchain.cmake -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -B sdl-rpi32 SDL && ninja -C sdl-rpi32
RUN cmake --install /sdl-rpi32/ --prefix /cross-pi-gcc-10.3.0-2/arm-linux-gnueabihf/

RUN git config --global --add safe.directory /drift
VOLUME /drift
