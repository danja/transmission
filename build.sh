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

echo "Building the GTK/JACK/VST3 graph UI"
cmake -S native -B native/build-ui-jack-vst3 \
  -DTRANSMISSION_WITH_GTK_UI=ON \
  -DTRANSMISSION_WITH_JACK=ON \
  -DTRANSMISSION_WITH_VST3=ON \
  -DVST3_SDK_ROOT="$VST3_SDK_ROOT" \
  -DTRANSMISSION_BUILD_TESTS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build native/build-ui-jack-vst3 --target transmission_graph_ui --parallel

echo "Build complete. Launch with ./transmission"
