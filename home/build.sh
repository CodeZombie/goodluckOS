#!/bin/bash
# TODO: This should be a script that packages up the contents of `overlay` into a .tar file and drops it in `out`
# Later, the `image` script will grab that file and extract it into the HOME partition.

mkdir -p out

# Create placeholder folders if they don't already exist
# We create these directly in the overlay folder and not staging because if they remain empty git won't track them,
# but if you want to stick a file in there, the folder will likely already exist and git will automatically start tracking it.
mkdir -p overlay/roms
mkdir -p overlay/roms/nes/icons
mkdir -p overlay/roms/snes/icons
mkdir -p overlay/roms/gba/icons
mkdir -p overlay/roms/gbc/icons
mkdir -p overlay/roms/gb/icons
mkdir -p overlay/roms/psx/icons
mkdir -p overlay/.config/retroarch/cores

rm -rf staging
rm -rf out/*
mkdir -p staging
cp -r overlay/. staging/
tar -cvf out/home.tar -C staging .
