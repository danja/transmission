#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "Usage: $0 <build-directory> <version> [VST3-SDK-license]" >&2
  exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$(realpath "$1")"
VERSION="$2"
VST3_LICENSE="${3:-}"
if [[ ! "$VERSION" =~ ^[0-9A-Za-z][0-9A-Za-z._-]*$ ]]; then
  echo "Version may contain only letters, digits, dots, underscores, and hyphens." >&2
  exit 2
fi
ARCH="$(uname -m)"
PACKAGE_NAME="transmission-${VERSION}-linux-${ARCH}"
OUTPUT_DIR="$ROOT_DIR/dist"
WORK_DIR="$(mktemp -d)"
PACKAGE_DIR="$WORK_DIR/$PACKAGE_NAME"

cleanup() {
  rm -rf -- "$WORK_DIR"
}
trap cleanup EXIT

required_files=(
  transmission_graph_ui
  transmission_native.node
  transmission_vst3_inspect
  transmission_vst3_offline
  transmission_vst3_project_probe
)

for file in "${required_files[@]}"; do
  if [[ ! -f "$BUILD_DIR/$file" ]]; then
    echo "Release build is missing $BUILD_DIR/$file" >&2
    exit 1
  fi
done

mkdir -p "$PACKAGE_DIR/bin" "$PACKAGE_DIR/lib" "$OUTPUT_DIR"
install -m 0755 "$BUILD_DIR/transmission_graph_ui" "$PACKAGE_DIR/bin/"
install -m 0755 "$BUILD_DIR/transmission_vst3_inspect" "$PACKAGE_DIR/bin/"
install -m 0755 "$BUILD_DIR/transmission_vst3_offline" "$PACKAGE_DIR/bin/"
install -m 0755 "$BUILD_DIR/transmission_vst3_project_probe" "$PACKAGE_DIR/bin/"
install -m 0755 "$BUILD_DIR/transmission_native.node" "$PACKAGE_DIR/lib/"

cp -a \
  "$ROOT_DIR/src" \
  "$ROOT_DIR/scripts" \
  "$ROOT_DIR/profiles" \
  "$ROOT_DIR/patches" \
  "$PACKAGE_DIR/"
install -m 0755 "$ROOT_DIR/transmission" "$PACKAGE_DIR/transmission"
install -m 0644 \
  "$ROOT_DIR/package.json" \
  "$ROOT_DIR/package-lock.json" \
  "$ROOT_DIR/LICENSE" \
  "$ROOT_DIR/README.md" \
  "$PACKAGE_DIR/"
install -m 0644 "$ROOT_DIR/docs/release-bundle.md" "$PACKAGE_DIR/README-BINARY.md"

if [[ -n "$VST3_LICENSE" ]]; then
  install -m 0644 "$VST3_LICENSE" "$PACKAGE_DIR/VST3-SDK-LICENSE.txt"
else
  echo "A VST3 SDK license was not supplied; refusing to package SDK-derived binaries." >&2
  exit 1
fi

npm ci --omit=dev --ignore-scripts --prefix "$PACKAGE_DIR"

tar -C "$WORK_DIR" -czf "$OUTPUT_DIR/$PACKAGE_NAME.tar.gz" "$PACKAGE_NAME"
(
  cd "$OUTPUT_DIR"
  sha256sum "$PACKAGE_NAME.tar.gz" > "$PACKAGE_NAME.tar.gz.sha256"
)
echo "$OUTPUT_DIR/$PACKAGE_NAME.tar.gz"
