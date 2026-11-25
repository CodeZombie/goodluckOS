#!/usr/bin/env bash
echo "[*] Cleaning build output (Keeps ccache and ./dl)..."
make -C buildroot O="/src/out" BR2_EXTERNAL="/src" BR2_DL_DIR="/src/dl" BR2_CCACHE_DIR="/src/ccache" clean
