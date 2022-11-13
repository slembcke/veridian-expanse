FROM registry.gitlab.steamos.cloud/steamrt/scout/sdk:latest
ENV CC /usr/bin/gcc-9
ENV CXX /usr/bin/g++-9

WORKDIR /build
VOLUME /drift
