#!/usr/bin/env bash
# Package the MCP server + native addon for non-Linux platforms.
# Usage: package-mcp.sh <build-dir> <version> <platform> <VST3-SDK-license>
set -euo pipefail

if [[ $# -lt 4 ]]; then
  echo "Usage: $0 <build-directory> <version> <platform> <VST3-SDK-license>" >&2
  exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$(cd "$1" && pwd)"
VERSION="$2"
PLATFORM="$3"
VST3_LICENSE="$4"

if [[ ! "$VERSION" =~ ^[0-9A-Za-z][0-9A-Za-z._-]*$ ]]; then
  echo "Version may contain only letters, digits, dots, underscores, and hyphens." >&2
  exit 2
fi

# Find the addon — name differs by platform (.node on all, but may be in subdir)
ADDON_FILE=""
for candidate in \
    "$BUILD_DIR/transmission_native.node" \
    "$BUILD_DIR/Release/transmission_native.node"; do
  if [[ -f "$candidate" ]]; then
    ADDON_FILE="$candidate"
    break
  fi
done
if [[ -z "$ADDON_FILE" ]]; then
  echo "transmission_native.node not found in $BUILD_DIR" >&2
  exit 1
fi

PACKAGE_NAME="transmission-${VERSION}-${PLATFORM}"
OUTPUT_DIR="$ROOT_DIR/dist"
WORK_DIR="$(mktemp -d)"
PACKAGE_DIR="$WORK_DIR/$PACKAGE_NAME"

cleanup() { rm -rf -- "$WORK_DIR"; }
trap cleanup EXIT

mkdir -p "$PACKAGE_DIR/lib" "$OUTPUT_DIR"

cp "$ADDON_FILE" "$PACKAGE_DIR/lib/transmission_native.node"
cp -r "$ROOT_DIR/src" "$PACKAGE_DIR/src"
cp -r "$ROOT_DIR/scripts" "$PACKAGE_DIR/scripts"
cp -r "$ROOT_DIR/profiles" "$PACKAGE_DIR/profiles"
cp "$ROOT_DIR/package.json" "$ROOT_DIR/package-lock.json" \
   "$ROOT_DIR/LICENSE" "$ROOT_DIR/README.md" "$PACKAGE_DIR/"
cp "$VST3_LICENSE" "$PACKAGE_DIR/VST3-SDK-LICENSE.txt"

npm ci --omit=dev --ignore-scripts --prefix "$PACKAGE_DIR"

ARCHIVE="$OUTPUT_DIR/$PACKAGE_NAME.tar.gz"
tar -C "$WORK_DIR" -czf "$ARCHIVE" "$PACKAGE_NAME"

# Portable SHA-256 via Node.js (sha256sum not guaranteed on all platforms)
node -e "
  const { createHash } = require('crypto');
  const { readFileSync, writeFileSync } = require('fs');
  const file = process.argv[1];
  const hash = createHash('sha256').update(readFileSync(file)).digest('hex');
  writeFileSync(file + '.sha256', hash + '  ' + require('path').basename(file) + '\n');
" "$ARCHIVE"

echo "$ARCHIVE"
