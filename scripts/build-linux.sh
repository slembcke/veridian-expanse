set -ex

cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo /drift \
	&& ninja \
	&& zip -9 /drift/project-drift-linux-`date -I`.zip drift resources.zip
