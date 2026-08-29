#!/bin/sh

CXX="../../out/host/bin/arm-buildroot-linux-gnueabihf-g++"
STAGING="../../out/host/arm-buildroot-linux-gnueabihf/sysroot"

$CXX -std=c++17 -o puppy \
    puppy.cpp \
    -I$STAGING/usr/include/SDL2 \
    -O2 -lpthread -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_gfx
