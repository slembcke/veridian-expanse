set -ex

pushd build
	ninja drift
	./drift --gen
popd

mkdir -p tiles
convert -size 32x32 -depth 8 GRAY:resources/bin/terrain0.bin tiles/tile0.png &
convert -size 32x32 -depth 8 GRAY:resources/bin/terrain1.bin tiles/tile1.png &
convert -size 32x32 -depth 8 GRAY:resources/bin/terrain2.bin tiles/tile2.png &
convert -size 32x32 -depth 8 GRAY:resources/bin/terrain3.bin tiles/tile3.png &
convert -size 32x32 -depth 8 GRAY:resources/bin/terrain4.bin tiles/tile4.png &
convert -size 32x32 -depth 8 GRAY:resources/bin/terrain5.bin tiles/tile5.png &
convert -size 32x32 -depth 8 GRAY:resources/bin/terrain6.bin tiles/tile6.png &
convert -size 32x32 -depth 8 GRAY:resources/bin/terrain7.bin tiles/tile7.png &
wait
