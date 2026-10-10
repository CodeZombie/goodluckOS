#!/bin/bash

mkdir -p ./staging

cp ../kernel/out/android_boot_a23_panel1.img ./staging/
cp ../kernel/out/android_boot_a23_panel2.img ./staging/
cp ../kernel/out/android_boot_a33_panel1.img ./staging/
cp ../kernel/out/android_boot_a33_panel2.img ./staging/
cp ../rootfs/out/images/rootfs.tar ./staging/
cp ../home/out/home.tar ./staging/

mkdir -p out

if command -v podman &> /dev/null; then
    podman build --build-arg VARIANT=a23_panel1 -o ./out .
    podman build --build-arg VARIANT=a23_panel2 -o ./out .
    podman build --build-arg VARIANT=a33_panel1 -o ./out .
    podman build --build-arg VARIANT=a33_panel2 -o ./out .
elif command -v docker &> /dev/null; then
    docker buildx build --build-arg VARIANT=a23_panel1 --output type=local,dest=./out .
    docker buildx build --build-arg VARIANT=a23_panel2 --output type=local,dest=./out .
    docker buildx build --build-arg VARIANT=a33_panel1 --output type=local,dest=./out .
    docker buildx build --build-arg VARIANT=a33_panel2 --output type=local,dest=./out .
else
    echo "Error: Neither podman nor docker was found in your PATH." >&2
    exit 1
fi
echo "Done"
