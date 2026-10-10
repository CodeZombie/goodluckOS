#!/bin/bash
mkdir -p local-out
g++ credits.cpp -o local-out/credits -O2 -lSDL2 -lSDL2_ttf
