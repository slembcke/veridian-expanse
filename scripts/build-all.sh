set -ex

DIRNAME=veridian-expanse-`date -I`
mkdir -p $DIRNAME

docker build -t drift-build-windows -f scripts/windows.dockerfile .
docker run --rm -v $PWD:/drift:z drift-build-windows bash /drift/scripts/build-windows.sh $DIRNAME

docker build -t drift-build-linux -f scripts/linux.dockerfile .
docker run --rm -v $PWD:/drift:z drift-build-linux bash /drift/scripts/build-linux.sh $DIRNAME

cd $DIRNAME && zip -r9 ../$DIRNAME.zip . -x "*.inc"
