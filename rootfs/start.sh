#!/bin/bash

if command -v podman &> /dev/null; then
    podman compose run --build --rm goodluck-builder bash --init-file /src/scripts/configure.sh
elif command -v docker &> /dev/null; then
    docker compose run --build --rm goodluck-builder bash --init-file /src/scripts/configure.sh
else
    echo "Error: Neither podman nor docker was found in your PATH." >&2
    exit 1
fi
echo "Done"
