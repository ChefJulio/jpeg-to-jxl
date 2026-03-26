# jpeg-to-jxl

WASM wrapper around libjxl's lossless JPEG transcoding (`JxlEncoderAddJPEGFrame`).

## Project Structure

```
src/
  transcode.c    - C glue code (the only C file -- ~200 lines)
  index.js       - JS API wrapper
  index.d.ts     - TypeScript types
build/
  CMakeLists.txt - CMake config for Emscripten build
  build.sh       - Build script
test/
  round-trip.test.js - Node.js test (requires built WASM)
deps/
  libjxl/        - libjxl checkout (git cloned, not committed)
dist/            - Build output (generated, not committed)
```

## Key Concepts

- `JxlEncoderAddJPEGFrame` takes raw JPEG bytes and transcodes at the DCT coefficient level (no pixel decoding)
- `JxlEncoderStoreJPEGMetadata(enc, JXL_TRUE)` enables byte-perfect JPEG reconstruction
- `JxlDecoderSetJPEGBuffer` + `JXL_DEC_JPEG_RECONSTRUCTION` event reconstructs the original JPEG
- Single-threaded WASM (no pthread) to keep things simple for browser use

## Build

Requires Emscripten SDK. See README.md for setup. CI builds automatically via GitHub Actions.
