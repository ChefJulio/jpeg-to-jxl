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

# --- wasm-opt pass (binaryen) ---
# Squeezes out another 5-15% on top of Emscripten's -Oz.
# Prefer Emscripten's bundled wasm-opt (matches the WASM features it emits)
# over the system one (which may be too old for bulk-memory etc.).
WASM_OPT=""
if [ -n "${EMSDK:-}" ] && [ -x "$EMSDK/upstream/bin/wasm-opt" ]; then
  WASM_OPT="$EMSDK/upstream/bin/wasm-opt"
elif command -v wasm-opt &> /dev/null; then
  WASM_OPT="wasm-opt"
fi

if [ -n "$WASM_OPT" ]; then
  BEFORE=$(wc -c < "$DIST_DIR/transcode.wasm")
  echo "==> Running wasm-opt -Oz ($("$WASM_OPT" --version 2>&1 || true))..."
  "$WASM_OPT" -Oz \
    --enable-bulk-memory \
    --enable-sign-ext \
    --enable-mutable-globals \
    --enable-nontrapping-float-to-int \
    "$DIST_DIR/transcode.wasm" -o "$DIST_DIR/transcode.wasm"
  AFTER=$(wc -c < "$DIST_DIR/transcode.wasm")
  echo "    wasm-opt: $BEFORE -> $AFTER bytes (saved $((BEFORE - AFTER)) bytes)"
else
  echo "==> wasm-opt not found, skipping (install binaryen for smaller WASM)"
fi

echo ""
echo "==> Build complete!"
echo "    dist/transcode.wasm  (WASM binary)"
echo "    dist/transcode.js    (Emscripten glue)"
echo "    dist/index.js        (API wrapper)"
echo "    dist/index.d.ts      (TypeScript types)"

# Print final sizes
WASM_SIZE=$(wc -c < "$DIST_DIR/transcode.wasm" 2>/dev/null || echo "unknown")
GZIP_SIZE=$(gzip -c "$DIST_DIR/transcode.wasm" 2>/dev/null | wc -c || echo "unknown")
echo ""
echo "    WASM size: $WASM_SIZE bytes ($GZIP_SIZE gzipped)"
