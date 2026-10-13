#!/bin/sh
set -eu
HELPER_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ ! -x "$HELPER_DIR/build/CatDeskHelper" ]; then "$HELPER_DIR/build.sh"; fi
exec "$HELPER_DIR/build/CatDeskHelper" "$@"
