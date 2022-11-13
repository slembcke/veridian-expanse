FROM ubuntu:22.04

RUN \
	apt-get update &&\
	DEBIAN_FRONTEND=noninteractive apt-get install -y \
		git cmake ninja-build gcc g++ python3 glslang-tools spirv-tools mingw-w64 zip curl pngquant \
		libx11-dev libxcursor-dev libxi-dev &&\
	apt-get clean

RUN \
	cd /tmp &&\
	git clone --branch sdk-1.3.224.1 --depth 1 https://github.com/KhronosGroup/SPIRV-Cross.git &&\
	cmake -B build -G Ninja SPIRV-Cross/ &&\
	cd build && ninja install &&\
	rm -rf /tmp/*

RUN \
	cd /tmp &&\
	git clone --branch v1.2.40 --depth 1 --shallow-submodules --recursive https://github.com/aseprite/aseprite.git &&\
	cmake -B build -G Ninja -DLAF_BACKEND=none aseprite &&\
	cd build && ninja install &&\
	rm -rf /tmp/*

WORKDIR /build
RUN curl https://libsdl.org/release/SDL2-devel-2.0.20-VC.zip > SDL2.zip && unzip SDL2.zip && rm SDL2.zip
VOLUME /drift
