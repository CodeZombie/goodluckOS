#!/bin/bash
set -euo pipefail

usage() {
    echo "Usage: $0 [hw|qemu|all]"
    echo "  hw    Build hardware kernel, DTBs and Android boot images"
    echo "  qemu  Build the QEMU emulator zImage"
    echo "  all   Build everything (default)"
}

case "${1:-all}" in
    hw)      TARGET=artifacts-hw ;;
    qemu)    TARGET=artifacts-qemu ;;
    all)     TARGET=artifacts ;;
    -h|--help) usage; exit 0 ;;
    *)       usage >&2; exit 1 ;;
esac

if command -v podman &> /dev/null; then
    podman build --target "$TARGET" -o ./out .
elif command -v docker &> /dev/null; then
    docker buildx build --target "$TARGET" --output type=local,dest=./out .
else
    echo "Error: Neither podman nor docker was found in your PATH." >&2
    exit 1
fi
echo "Done ($TARGET -> ./out)"
