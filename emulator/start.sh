#!/usr/bin/env bash
set -euo pipefail
exec podman compose run --rm --build --service-ports emulator "$@"
