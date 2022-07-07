set -ex

apt-get update
apt-get install -y gnupg curl
curl https://packages.lunarg.com/lunarg-signing-key-pub.asc | apt-key add -
curl https://packages.lunarg.com/vulkan/1.2.189/lunarg-vulkan-1.2.189-focal.list > /etc/apt/sources.list.d/lunarg-vulkan-1.2.189-focal.list

apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y \
	git cmake ninja-build gcc g++ spirv-tools mingw-w64 zip \
	libx11-dev libxcursor-dev libxi-dev vulkan-sdk
apt-get clean

cd /tmp

git clone --recursive https://github.com/aseprite/aseprite.git
cd aseprite
git checkout v1.2.30
git submodule update
pushd laf
	git checkout 3ad116e5c34a5525b686f4e006feff24483760e5
popd
mkdir build
cd build
cmake -G Ninja -DLAF_BACKEND=none ..
ninja install

rm -rf /tmp/*
