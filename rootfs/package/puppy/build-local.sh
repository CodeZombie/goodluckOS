#!/bin/bash
mkdir -p local-out
g++ puppy.cpp -o local-out/puppy -O2 -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_gfx
