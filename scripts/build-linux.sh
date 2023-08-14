set -ex

cp /drift/$1/*.inc /drift/$1/resources.zip .
cmake -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DDRIFT_RESOURCES=0 /drift
ninja
#strip veridian-expanse

cp veridian-expanse /drift/$1/veridian-expanse-linux
