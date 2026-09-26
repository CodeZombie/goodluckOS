#!/usr/bin/env bash
echo "[*] Cleaning build output (keeps downloaded source code in out/dl/)..."
make -C buildroot O="/src/out" BR2_EXTERNAL="/src" BR2_DL_DIR="/src/dl" clean
