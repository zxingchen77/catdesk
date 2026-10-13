#!/bin/sh
set -eu
HELPER_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
HELPER_ARCH=$(uname -m)
case "$HELPER_ARCH" in
  arm64|x86_64) ;;
  *) printf 'Unsupported Mac architecture: %s\n' "$HELPER_ARCH" >&2; exit 1 ;;
esac
mkdir -p "$HELPER_DIR/build"
set --
HELPER_SWIFT_BIN=$(dirname -- "$(xcrun --find swiftc)")
HELPER_OLD_MAP="${HELPER_SWIFT_BIN%/bin}/include/swift/module.modulemap"
HELPER_NEW_MAP="${HELPER_SWIFT_BIN%/bin}/include/swift/bridging.modulemap"
if [ -f "$HELPER_OLD_MAP" ] && [ -f "$HELPER_NEW_MAP" ] && \
   grep -q 'module SwiftBridging' "$HELPER_OLD_MAP" && \
   grep -q 'module SwiftBridging' "$HELPER_NEW_MAP"; then
  # Some upgraded CLT installs retain the 2023 map beside the new Swift 6 map.
  # Mask the duplicate only inside this compiler invocation, never in /Library.
  : > "$HELPER_DIR/build/empty.modulemap"
  HELPER_MAP_YAML=$(printf '%s' "$HELPER_OLD_MAP" | sed "s/'/''/g")
  HELPER_EMPTY_YAML=$(printf '%s' "$HELPER_DIR/build/empty.modulemap" | sed "s/'/''/g")
  cat > "$HELPER_DIR/build/modulemap-overlay.yaml" <<EOF
version: 0
roots:
  - type: file
    name: '$HELPER_MAP_YAML'
    external-contents: '$HELPER_EMPTY_YAML'
EOF
  set -- -vfsoverlay "$HELPER_DIR/build/modulemap-overlay.yaml" \
    -module-cache-path "$HELPER_DIR/build/module-cache"
fi
swiftc -O -warnings-as-errors -target "$HELPER_ARCH-apple-macos15.0" "$@" \
  -framework Foundation -framework IOKit \
  "$HELPER_DIR/CatDeskHelper.swift" -o "$HELPER_DIR/build/CatDeskHelper"
printf 'Built %s\n' "$HELPER_DIR/build/CatDeskHelper"
