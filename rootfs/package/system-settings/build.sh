#!/bin/sh

CXX="../../out/host/bin/arm-buildroot-linux-gnueabihf-g++"
STAGING="../../out/host/arm-buildroot-linux-gnueabihf/sysroot"

IMGUI_DIR="./imgui"

$CXX -std=c++11 -o system-settings \
    system-settings.cpp \
    $IMGUI_DIR/imgui.cpp \
    $IMGUI_DIR/imgui_draw.cpp \
    $IMGUI_DIR/imgui_tables.cpp \
    $IMGUI_DIR/imgui_widgets.cpp \
    $IMGUI_DIR/backends/imgui_impl_sdl2.cpp \
    $IMGUI_DIR/backends/imgui_impl_sdlrenderer2.cpp \
    -I$IMGUI_DIR \
    -I$IMGUI_DIR/backends \
    -I$STAGING/usr/include/SDL2 \
    -I$STAGING/usr/include/libdrm \
    -I$STAGING/usr/include/libdrm/freedreno \
    -I$STAGING/usr/include/libdrm/vc4 \
    -lpthread -ldrm -lSDL2 -lGLESv2 -lEGL -lasound
