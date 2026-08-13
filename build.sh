#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VST3_SDK_ROOT="${HOME}/VST_SDK/vst3sdk"
cd "$ROOT_DIR"

echo "Checking JavaScript sources"
npm run check
npm test

echo "Building the default native engine and running CTest"
cmake -S native -B native/build \
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
  -DTRANSMISSION_BUILD_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build native/build-napi-vst3 --target transmission_native --parallel

echo "Build complete. Launch with ./transmission"
