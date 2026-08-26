#!/usr/bin/env bash
echo "[*] Configuring Buildroot with 'goodluck_defconfig'..."
make -C buildroot O="/src/out" BR2_EXTERNAL="/src" goodluck_defconfig
