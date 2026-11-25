#!/usr/bin/env bash
# Usage: ./start.sh [pytest args...]   e.g. ./start.sh -k app_cache -s
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p results
exec podman compose run --rm --build tests "$@"
