FROM ubuntu:20.04

COPY scripts/setup-windows.sh /scripts/
RUN bash scripts/setup-windows.sh

WORKDIR /build
RUN curl https://libsdl.org/release/SDL2-devel-2.0.20-VC.zip > SDL2.zip && unzip SDL2.zip && rm SDL2.zip
VOLUME /drift
