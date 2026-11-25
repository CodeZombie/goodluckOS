#!/usr/bin/env bash
echo "[*] Configuring Buildroot with 'goodluck_defconfig'..."
make -C buildroot O="/src/out" BR2_EXTERNAL="/src" BR2_DL_DIR="/src/dl" BR2_CCACHE_DIR="/src/ccache" goodluck_defconfig
