set -ex

docker build -t drift-build-linux -f scripts/linux.dockerfile . \
	&& docker run --rm -v $PWD:/drift drift-build-linux bash /drift/scripts/build-linux.sh

docker build -t drift-build-windows -f scripts/windows.dockerfile . \
	&& docker run --rm -v $PWD:/drift drift-build-windows bash /drift/scripts/build-windows.sh
