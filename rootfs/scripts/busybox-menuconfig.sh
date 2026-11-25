#!/usr/bin/env bash
echo "[*] Opening busybox menuconfig..."
make -C buildroot O="/src/out" BR2_DL_DIR="/src/dl" BR2_EXTERNAL="/src" BR2_CCACHE_DIR="/src/ccache" busybox-menuconfig
make -C buildroot O="/src/out" BR2_DL_DIR="/src/dl" BR2_EXTERNAL="/src" BR2_CCACHE_DIR="/src/ccache" busybox-update-config
