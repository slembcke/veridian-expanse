set -ex

DIRNAME=veridian-expanse-`date -I`
mkdir -p $DIRNAME

export DOCKER_BUILDKIT=1
UGID=`id -u $USER`:`id -g $USER`

docker build -t drift-build-cross -f scripts/cross.dockerfile scripts
docker run --rm -v $PWD:/drift:z drift-build-cross bash /drift/scripts/build-cross.sh $DIRNAME

docker build -t drift-build-linux -f scripts/linux.dockerfile scripts
docker run --rm -v $PWD:/drift:z drift-build-linux bash /drift/scripts/build-linux.sh $DIRNAME

rm $DIRNAME/*.inc
cp README.html $DIRNAME
cd $DIRNAME && zip -r9 ../$DIRNAME.zip .

# To run them by hand for debugging ex:
# docker run -it -v $PWD:/drift drift-build-cross bash
