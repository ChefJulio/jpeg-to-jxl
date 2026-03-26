# jpeg-to-jxl Development Plan

## What This Project Is

A standalone npm package that provides **lossless JPEG-to-JXL transcoding** via WASM. This is the first browser-usable library to expose libjxl's `JxlEncoderAddJPEGFrame` API, which repackages JPEG DCT coefficients into JXL format without pixel decoding. Result: ~20% smaller files with byte-perfect JPEG reconstruction.

**GitHub:** https://github.com/ChefJulio/jpeg-to-jxl
**Consuming project:** [Overtooled](https://github.com/ChefJulio/overtooled) (c:\Dev\BosDev\overtooled) -- a collection of 179+ browser-based tools that already has pixel-based JXL support via @jsquash/jxl.

---

## Current State (2026-03-26)

### What's Done (Phase 1 COMPLETE)

- [x] Project scaffolded with full source code
- [x] `src/transcode.c` -- C glue around `JxlEncoderAddJPEGFrame` (encode) and `JxlDecoderSetJPEGBuffer` (decode/reconstruct)
- [x] `src/index.js` -- JS wrapper with clean async API: `jpegToJxl()`, `jxlToJpeg()`, `init()`
- [x] `src/index.d.ts` -- TypeScript type declarations
- [x] `build/CMakeLists.txt` -- Emscripten/CMake config that pulls libjxl as subdirectory
- [x] `build/build.sh` -- One-command build script with prereq validation
- [x] `.github/workflows/build.yml` -- GitHub Actions CI (Emscripten setup, libjxl clone, build, test, artifact upload)
- [x] `test/round-trip.test.js` -- Node.js test suite (compression ratio, byte-identical reconstruction, error cases)
- [x] Git repo created, CI fully green, all 6 tests passing
- [x] WASM builds successfully: **2.7 MB raw, 1.0 MB gzipped**
- [x] Verified compression: **92,436 byte JPEG -> 73,001 byte JXL (21.0% smaller)**
- [x] Verified byte-identical JPEG reconstruction
- [x] Verified effort parameter works: effort=1 -> 80,754 bytes, effort=9 -> 72,484 bytes
- [x] WASM artifact uploaded to GitHub Actions for download

### Build details

- Emscripten SDK 3.1.51
- libjxl built as CMake subdirectory with highway, brotli dependencies
- `ENVIRONMENT='web,worker,node'` -- works in browser, Web Worker, and Node.js
- `FILESYSTEM=0`, `ALLOW_MEMORY_GROWTH=1`, `-O3`, `--closure 1`
- Node.js WASM loading uses `import.meta.url` with file:// URL-to-path conversion
- Test fixture: 512x512 gradient+noise JPEG generated via Pillow in CI

### Issues encountered and resolved

1. **ImageMagick not on ubuntu-latest** -- Switched to Pillow via `pip install`
2. **`test/fixtures/` directory missing in checkout** -- Added `mkdir -p` in CI
3. **Node.js WASM file resolution** -- `locateFile` was returning a URL string; Node.js needs a file path. Fixed by detecting `file://` protocol and converting.
4. **Emscripten ENVIRONMENT flag** -- Initially set to `'web,worker'` which excluded Node.js. Added `'node'` for test compatibility.
5. **Tiny test JPEG larger after transcoding** -- 64x64 solid-color JPEG (694 bytes) had less data than JXL container overhead. Switched to 512x512 gradient+noise image (92KB) which shows the expected ~21% savings.

---

## Phase 2: Optimize (NEXT)

The build works but there's room to improve:

1. **WASM size** -- Currently 2.7 MB raw / 1.0 MB gzipped. Target: under 800KB gzipped.
   - Try `-Oz` instead of `-O3` (prioritize size over speed)
   - Run `wasm-opt -Oz` on the output (binaryen optimizer)
   - Investigate if we're pulling in unused libjxl code (full pixel encoder vs. just JPEG transcode path)
   - The `ALLOW_MEMORY_GROWTH=1` flag currently combined with `INITIAL_MEMORY=16777216` (16MB). Could reduce initial memory.

2. **Threading overhead** -- The generated Emscripten glue has pthread/SharedArrayBuffer code even though we don't use threads in our code. This is because libjxl/highway may be requesting threads at build time. Consider:
   - Adding `-s USE_PTHREADS=0` or `-pthread` exclusion flags
   - Setting `set(HWY_ENABLE_THREADS OFF)` in CMake
   - This would also shrink the JS glue file

3. **Benchmark** -- Measure transcoding speed in browser for various JPEG sizes (100KB, 1MB, 10MB). Should be fast since no pixel processing.

4. **Memory usage** -- Check peak WASM memory for large JPEGs. Verify it doesn't OOM on 20MB+ JPEGs.

---

## Phase 3: Browser Testing & Polish

1. **Create a minimal HTML test page** (`examples/index.html`) that:
   - Loads the WASM module
   - Lets user drop/select a JPEG
   - Shows original size, JXL size, savings percentage
   - Offers download of the JXL
   - Reconstructs the JPEG and verifies byte-identity
2. **Test in browsers:** Chrome, Firefox, Safari, Edge
3. **Verify the `locateFile` callback** works correctly for different bundler setups (Vite, Webpack, bare `<script>`)
4. **CORS / COOP / COEP headers** -- If the WASM build has SharedArrayBuffer references (from highway threading), browsers may require specific headers. Test and document.

---

## Phase 4: npm Publish

1. Download the `dist/` artifact from CI (or set up CI to auto-publish on tag)
2. Ensure `dist/` contains all needed files: `index.js`, `index.d.ts`, `transcode.js`, `transcode.wasm`
3. Update `package.json` repository URL (already set to `https://github.com/ChefJulio/jpeg-to-jxl`)
4. `npm publish`
5. Verify installation works: `npm install jpeg-to-jxl` in a fresh project
6. Consider adding a `postinstall` or `prepublishOnly` script note about the WASM file needing to be served alongside the JS

---

## Phase 5: Integrate into Overtooled

Once published to npm:

1. `npm install jpeg-to-jxl` in Overtooled
2. Add a "Lossless JPEG Recompression" mode to the Image Converter:
   - Detect when input is JPEG and output is JXL
   - Offer a toggle: "Lossless transcode (preserve original, ~20% smaller)" vs "Re-encode (adjustable quality)"
   - Show that the original JPEG can be reconstructed
3. Could also add a standalone "JPEG Optimizer" tool that uses this for archival

### Where the integration points are in Overtooled

- `src/utils/jxl.js` -- Add `transcodeJpegToJxl()` and `reconstructJpeg()` wrappers
- `src/pages/tools/ImageFormatConverterPage.jsx` -- Add lossless transcode option when source is JPEG and target is JXL
- `src/core/tools/file-converter/engine-image.js` -- Add lossless path for JPEG->JXL conversion

---

## Architecture Decisions

| Decision | Rationale |
|----------|-----------|
| Separate repo from Overtooled | Different build toolchain (Emscripten/C++ vs Vite/React). Reusable by anyone. |
| Single-threaded WASM | Simpler, no SharedArrayBuffer/COOP/COEP requirements. Transcoding is fast enough single-threaded since no pixel processing. |
| `FILESYSTEM=0` | We pass byte buffers directly, no need for Emscripten's virtual filesystem. Saves WASM size. |
| `ENVIRONMENT='web,worker,node'` | Works in main thread, Web Workers, and Node.js (needed for tests). |
| Effort default = 3 | Good speed/size tradeoff for interactive use. User can increase for archival. |
| libjxl as subdirectory build | Single CMake invocation. Worked on first try with Emscripten. |
| CI-built WASM | Avoids requiring Emscripten locally. Push code, CI builds, download artifact. |

---

## Key Files Reference

| File | Purpose |
|------|---------|
| `src/transcode.c` | The C glue -- only ~200 lines. `jpeg_to_jxl()`, `jxl_to_jpeg()`, `jxl_free()` |
| `src/index.js` | JS API wrapper. Lazy WASM init, buffer management, error handling |
| `src/index.d.ts` | TypeScript types for the public API |
| `build/CMakeLists.txt` | CMake config. Emscripten flags, libjxl options, exported functions |
| `build/build.sh` | Build script. Validates prereqs, runs cmake, copies output to dist/ |
| `.github/workflows/build.yml` | CI. Clones libjxl, sets up Emscripten, builds, tests, uploads artifact |
| `test/round-trip.test.js` | Tests. Needs built WASM + test fixtures in test/fixtures/ |

---

## libjxl API Reference (the functions we wrap)

```c
// Encode: JPEG -> JXL (lossless transcoding)
JxlEncoderStoreJPEGMetadata(enc, JXL_TRUE);  // Enable reconstruction
JxlEncoderAddJPEGFrame(settings, jpeg_bytes, jpeg_size);  // Feed raw JPEG

// Decode: JXL -> JPEG (reconstruct original)
JxlDecoderSubscribeEvents(dec, JXL_DEC_JPEG_RECONSTRUCTION);
JxlDecoderSetJPEGBuffer(dec, output_buf, buf_size);  // Collect JPEG output
```

Full docs: https://libjxl.readthedocs.io/en/latest/api_encoder.html
