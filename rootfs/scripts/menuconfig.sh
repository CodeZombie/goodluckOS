#!/usr/bin/env bash
echo "[*] Opening Buildroot menuconfig..."
make -C buildroot O="/src/out" BR2_EXTERNAL="/src" BR2_DL_DIR="/src/dl" BR2_CCACHE_DIR="/src/ccache" menuconfig
make -C buildroot O="/src/out" BR2_EXTERNAL="/src" BR2_DL_DIR="/src/dl" BR2_CCACHE_DIR="/src/ccache" BR2_DEFCONFIG="/src/configs/goodluck_defconfig" savedefconfig
