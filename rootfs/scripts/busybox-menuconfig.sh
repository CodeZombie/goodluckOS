#!/usr/bin/env bash
echo "[*] Opening busybox menuconfig..."
make -C buildroot O="/src/out" BR2_EXTERNAL="/src" busybox-menuconfig
make -C buildroot O="/src/out" BR2_EXTERNAL="/src" busybox-update-config
