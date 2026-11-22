#!/bin/sh
set -eu
cd -- "$(dirname -- "$0")"

# Buildroot must have produced its ARM toolchain before using this helper.
exec make all \
    AR="$PWD/../../out/host/bin/arm-buildroot-linux-gnueabihf-ar" \
    CXX="$PWD/../../out/host/bin/arm-buildroot-linux-gnueabihf-g++" \
    BUILD_DIR=local-out/arm OUTPUT=puppy "$@"
