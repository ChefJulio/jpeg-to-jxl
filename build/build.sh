#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------------------------------------
# Build jpeg-to-jxl WASM module
#
# Prerequisites:
#   1. Emscripten SDK installed and activated (emsdk activate latest)
#   2. libjxl cloned into deps/libjxl (with submodules)
#
# Quick setup:
#   git clone --recursive https://github.com/nicostap/libjxl.git deps/libjxl
#   # Install Emscripten: https://emscripten.org/docs/getting_started/downloads.html
#   source /path/to/emsdk/emsdk_env.sh
#   bash build/build.sh
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build/out"
DIST_DIR="$PROJECT_DIR/dist"

# Check Emscripten is available
if ! command -v emcmake &> /dev/null; then
  echo "ERROR: Emscripten not found. Install and activate emsdk first."
  echo "  https://emscripten.org/docs/getting_started/downloads.html"
  exit 1
fi

# Check libjxl is cloned
if [ ! -f "$PROJECT_DIR/deps/libjxl/CMakeLists.txt" ]; then
  echo "ERROR: libjxl not found at deps/libjxl/"
  echo "  Run: git clone --recursive https://github.com/libjxl/libjxl.git deps/libjxl"
  exit 1
fi

echo "==> Configuring with Emscripten..."
mkdir -p "$BUILD_DIR"

emcmake cmake \
  -S "$SCRIPT_DIR" \
  -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release

echo "==> Building WASM..."
cmake --build "$BUILD_DIR" --parallel

echo "==> Copying output to dist/"
mkdir -p "$DIST_DIR"
cp "$BUILD_DIR/transcode.js" "$DIST_DIR/"
cp "$BUILD_DIR/transcode.wasm" "$DIST_DIR/"
cp "$PROJECT_DIR/src/index.js" "$DIST_DIR/"
cp "$PROJECT_DIR/src/index.d.ts" "$DIST_DIR/"

echo ""
echo "==> Build complete!"
echo "    dist/transcode.wasm  (WASM binary)"
echo "    dist/transcode.js    (Emscripten glue)"
echo "    dist/index.js        (API wrapper)"
echo "    dist/index.d.ts      (TypeScript types)"

# Print WASM size
WASM_SIZE=$(wc -c < "$DIST_DIR/transcode.wasm" 2>/dev/null || echo "unknown")
echo ""
echo "    WASM size: $WASM_SIZE bytes"
