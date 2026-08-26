#!/usr/bin/env bash
echo "[*] Starting build..."
echo "[*] Note: You can use this script to rebuild specific packages. (eg. ./scripts/build.sh puppy-rebuild)"
make -C buildroot O="/src/out" BR2_EXTERNAL="/src" "$@"
