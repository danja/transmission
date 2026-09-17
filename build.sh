#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JIGDAW_ROOT="${JIGDAW_ROOT:-${HOME}/github/jigdaw}"
HTTPLIB_DIR="${JIGDAW_HTTPLIB_DIR:-${HOME}/github/downspout/third_party/cpp-httplib}"
cd "$ROOT_DIR"

# The Steinberg VST3 SDK. Set VST3_SDK_ROOT to say where it is; otherwise the
# first of these that looks like one wins. Checked here rather than left to
# CMake so that a missing SDK is reported before the test suite and two builds
# have run, and with somewhere to look rather than only what was not found.
VST3_SDK_CANDIDATES=(
  "${HOME}/VST_SDK/vst3sdk"
  /chalet/VST_SDK/vst3sdk
  /opt/VST_SDK/vst3sdk
  /usr/local/share/vst3sdk
)
if [[ -n "${VST3_SDK_ROOT:-}" ]]; then
  VST3_SDK_CANDIDATES=("$VST3_SDK_ROOT")
fi
VST3_SDK_ROOT=""
for candidate in "${VST3_SDK_CANDIDATES[@]}"; do
  if [[ -f "$candidate/CMakeLists.txt" ]]; then
    VST3_SDK_ROOT="$candidate"
    break
  fi
done
if [[ -z "$VST3_SDK_ROOT" ]]; then
  echo "No VST3 SDK found. Looked in:" >&2
  printf '  %s\n' "${VST3_SDK_CANDIDATES[@]}" >&2
  echo "Set VST3_SDK_ROOT=/path/to/vst3sdk and run again." >&2
  exit 1
fi
echo "VST3 SDK: $VST3_SDK_ROOT"

# JigDAW hosting is optional: it needs the jigdaw checkout for the spec and its
# portable core, and cpp-httplib for dereferencing a plugin IRI.
JIGDAW_FLAGS=(-DTRANSMISSION_WITH_JIGDAW=OFF)
if [[ -f "$JIGDAW_ROOT/native/jigdaw-adapter/CMakeLists.txt" && -f "$HTTPLIB_DIR/httplib.h" ]]; then
  JIGDAW_FLAGS=(-DTRANSMISSION_WITH_JIGDAW=ON
                -DJIGDAW_ROOT="$JIGDAW_ROOT"
                -DJIGDAW_HTTPLIB_DIR="$HTTPLIB_DIR")
  echo "JigDAW hosting: enabled ($JIGDAW_ROOT)"
else
  echo "JigDAW hosting: disabled (no checkout at $JIGDAW_ROOT, or no cpp-httplib at $HTTPLIB_DIR)"
fi

echo "Checking JavaScript sources"
npm run check
npm test

echo "Building the default native engine and running CTest"
cmake -S native -B native/build \
  "${JIGDAW_FLAGS[@]}" \
  -DTRANSMISSION_BUILD_TESTS=ON
cmake --build native/build --parallel
ctest --test-dir native/build --output-on-failure

echo "Building the JACK engine tools"
cmake -S native -B native/build-jack \
  -DTRANSMISSION_WITH_JACK=ON \
  -DTRANSMISSION_BUILD_TESTS=ON
cmake --build native/build-jack --parallel

if ! pkg-config --exists libcurl 2>/dev/null && ! command -v curl-config &>/dev/null; then
  echo "Warning: libcurl development headers not found — GTK UI will be built without live server support"
  echo "  Install with: sudo apt install libcurl4-openssl-dev"
fi

echo "Building the GTK/JACK/VST3 graph UI (with live HTTP server when libcurl is available)"
cmake -S native -B native/build-ui-jack-vst3 \
  -DTRANSMISSION_WITH_GTK_UI=ON \
  -DTRANSMISSION_WITH_JACK=ON \
  -DTRANSMISSION_WITH_VST3=ON \
  -DVST3_SDK_ROOT="$VST3_SDK_ROOT" \
  "${JIGDAW_FLAGS[@]}" \
  -DTRANSMISSION_BUILD_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build native/build-ui-jack-vst3 --target transmission_graph_ui --parallel

if grep -q "TRANSMISSION_UI_WITH_LIVE_SERVER" native/build-ui-jack-vst3/CMakeFiles/transmission_graph_ui.dir/flags.make 2>/dev/null; then
  echo "  Live server support: enabled (libcurl linked)"
else
  echo "  Live server support: disabled (libcurl not found at configure time)"
fi

echo "Building the N-API MCP addon (JACK + VST3)"
cmake -S native -B native/build-napi-vst3 \
  -DTRANSMISSION_WITH_NAPI=ON \
  -DTRANSMISSION_WITH_JACK=ON \
  -DTRANSMISSION_WITH_VST3=ON \
  -DVST3_SDK_ROOT="$VST3_SDK_ROOT" \
  "${JIGDAW_FLAGS[@]}" \
  -DTRANSMISSION_BUILD_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build native/build-napi-vst3 --target transmission_native --parallel

echo "Build complete. Launch with ./transmission"
