set -ex

mkdir -p build
cd build

curl https://libsdl.org/release/SDL2-devel-2.0.20-VC.zip > SDL2.zip
unzip -o SDL2.zip
cp SDL2-2.0.20/lib/x64/SDL2.dll .
