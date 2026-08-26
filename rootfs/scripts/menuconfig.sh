#!/usr/bin/env bash
echo "[*] Opening Buildroot menuconfig..."
make -C buildroot O="/src/out" BR2_EXTERNAL="/src" menuconfig
make -C buildroot O="/src/out" BR2_EXTERNAL="/src" BR2_DEFCONFIG="/src/configs/goodluck_defconfig" savedefconfig
