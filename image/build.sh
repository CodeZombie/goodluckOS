#!/bin/bash

cp ../kernel/out/android_boot.img ./staging/
cp ../rootfs/out/images/rootfs.tar ./staging/
cp ../home/out/home.tar ./staging/

mkdir -p out

if command -v podman &> /dev/null; then
    podman build -o ./out .
elif command -v docker &> /dev/null; then
    docker buildx build --output type=local,dest=./out .
else
    echo "Error: Neither podman nor docker was found in your PATH." >&2
    exit 1
fi
echo "Done"
