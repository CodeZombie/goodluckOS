#!/bin/bash
set -e

mkdir -p local-out

# Fetch Dear ImGui if missing
if [ ! -d "imgui" ]; then
    echo "=> Fetching Dear ImGui..."
    git clone --depth 1 -b master https://github.com/ocornut/imgui.git
fi

echo "=> Compiling system-settings locally..."

g++ -O2 -std=c++11 \
    -I./imgui \
    -I./imgui/backends \
    $(pkg-config --cflags sdl2) \
    system-settings.cpp \
    imgui/imgui.cpp \
    imgui/imgui_draw.cpp \
    imgui/imgui_tables.cpp \
    imgui/imgui_widgets.cpp \
    imgui/backends/imgui_impl_sdl2.cpp \
    imgui/backends/imgui_impl_sdlrenderer2.cpp \
    -o local-out/system-settings \
    $(pkg-config --libs sdl2)

echo "=> Success! Connect a Gamepad and run ./system-settings to test."
