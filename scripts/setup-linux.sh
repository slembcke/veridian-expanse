set -ex

apt-get update -y
apt-get install -y zip
apt-get clean

cd /tmp
curl -L https://github.com/Kitware/CMake/releases/download/v3.21.4/cmake-3.21.4.tar.gz | tar -xz
cd cmake-3.21.4
cmake -G Ninja .
ninja install
export CMAKE=/usr/local/bin/cmake

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
$CMAKE -G Ninja -DLAF_BACKEND=none -DHAVE_WCHAR_H=1 ..
ninja install

cd /tmp
git clone https://github.com/KhronosGroup/SPIRV-Tools.git
cd SPIRV-Tools
git checkout v2021.3
utils/git-sync-deps
$CMAKE -G Ninja .
ninja install

cd /tmp
git clone https://github.com/KhronosGroup/SPIRV-Cross.git
cd SPIRV-Cross
git checkout 2021-01-15
$CMAKE -G Ninja .
ninja install

rm -rf /tmp/*
