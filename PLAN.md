# jpeg-to-jxl Development Plan

## What This Project Is

A standalone npm package that provides **lossless JPEG-to-JXL transcoding** via WASM. This is the first browser-usable library to expose libjxl's `JxlEncoderAddJPEGFrame` API, which repackages JPEG DCT coefficients into JXL format without pixel decoding. Result: ~20% smaller files with byte-perfect JPEG reconstruction.

**GitHub:** https://github.com/ChefJulio/jpeg-to-jxl
**Consuming project:** [Overtooled](https://github.com/ChefJulio/overtooled) (c:\Dev\BosDev\overtooled) -- a collection of 179+ browser-based tools that already has pixel-based JXL support via @jsquash/jxl.

---

## Current State (2026-03-26)

### What's Done

- [x] Project scaffolded with full source code
- [x] `src/transcode.c` -- C glue around `JxlEncoderAddJPEGFrame` (encode) and `JxlDecoderSetJPEGBuffer` (decode/reconstruct)
- [x] `src/index.js` -- JS wrapper with clean async API: `jpegToJxl()`, `jxlToJpeg()`, `init()`
- [x] `src/index.d.ts` -- TypeScript type declarations
- [x] `build/CMakeLists.txt` -- Emscripten/CMake config that pulls libjxl as subdirectory
- [x] `build/build.sh` -- One-command build script with prereq validation
- [x] `.github/workflows/build.yml` -- GitHub Actions CI (Emscripten setup, libjxl clone, build, test, artifact upload)
- [x] `test/round-trip.test.js` -- Node.js test suite (compression ratio, byte-identical reconstruction, error cases)
- [x] Git repo created and pushed, CI triggered

### What's NOT Done

- [ ] **CI build needs to pass** -- First run was triggered. libjxl is a large C++ project with dependencies (highway, brotli, etc.). The CMake config may need iteration to get the Emscripten cross-compile working cleanly. This is the biggest unknown.
- [ ] **Test fixture** -- CI generates a test JPEG via ImageMagick. May want to also commit a small real-world JPEG for local testing.
- [ ] **WASM size optimization** -- Initial build may be large. May need to strip unused libjxl code paths, use `-Oz` instead of `-O3`, or investigate `wasm-opt`.
- [ ] **Browser testing** -- Tests currently run in Node.js. Need to verify WASM loads and runs correctly in browsers (Chrome, Firefox, Safari).
- [ ] **npm publish** -- Once build passes and tests are green, publish to npm.
- [ ] **Integration into Overtooled** -- Add a "Lossless JPEG Recompression" feature to Overtooled's Image Converter using this package.

---

## Phase 1: Get CI Green

**Priority: This is the critical path. Everything else is blocked on a working WASM build.**

### Likely issues and fixes

1. **libjxl submodule dependencies** -- libjxl uses git submodules for highway, brotli, etc. The CI clones with `--recursive` but the CMake may need hints to find them. Check if `deps/libjxl/third_party/` populated correctly.

2. **CMake variable conflicts** -- libjxl's CMakeLists.txt defines many cache variables. Our overrides in `build/CMakeLists.txt` (lines like `set(BUILD_TESTING OFF CACHE BOOL "" FORCE)`) should suppress most, but new libjxl versions may add new required options.

3. **Emscripten-specific issues:**
   - libjxl uses highway SIMD -- Emscripten supports WASM SIMD but it may need explicit flags (`-msimd128`)
   - Threading: we're building single-threaded (no `-pthread`). If libjxl's CMake tries to enable threads, force it off: `set(JPEGXL_FORCE_NEON OFF)`, `set(CMAKE_DISABLE_FIND_PACKAGE_Threads TRUE)`
   - Some libjxl code uses `mmap` or filesystem APIs that don't exist in Emscripten. The `FILESYSTEM=0` flag should handle this but may cause link errors if libjxl code references them.

4. **Link order** -- `target_link_libraries(transcode PRIVATE jxl jxl_dec)` may need additional libraries like `hwy`, `brotlienc`, `brotlidec`, `brotlicommon` depending on how libjxl's CMake exports its targets.

5. **Emscripten version** -- CI uses emsdk 3.1.51. If libjxl requires newer features, bump the version in `build.yml`.

### Debugging approach

- Check the CI logs carefully for the first failure
- If CMake config fails: look at which target/variable is missing
- If compile fails: check if it's a libjxl source file or our transcode.c
- If link fails: likely missing library in target_link_libraries
- The libjxl repo has `doc/building_wasm.md` -- cross-reference their approach

### If the subdirectory approach doesn't work

Fall back to a two-stage build:
1. Build libjxl as a standalone Emscripten project first (using their own CMake)
2. Link our transcode.c against the resulting static libraries

This is less elegant but more likely to work since it uses libjxl's own tested build path.

---

## Phase 2: Optimize

Once the build passes:

1. **Measure WASM size** -- Check artifact size. Target: under 1MB gzipped. If larger:
   - Use `-Oz` instead of `-O3` for size optimization
   - Run `wasm-opt -Oz` on the output
   - Check if we're pulling in unused libjxl code (e.g., the full pixel encoder)
   - Consider building only the JPEG transcoding subset

2. **Benchmark** -- Measure transcoding speed in browser for various JPEG sizes (100KB, 1MB, 10MB). Should be fast since no pixel processing.

3. **Memory usage** -- Check peak WASM memory for large JPEGs. The `ALLOW_MEMORY_GROWTH` flag handles this but verify it doesn't OOM on 20MB+ JPEGs.

---

## Phase 3: Browser Testing & Polish

1. **Create a minimal HTML test page** that loads the WASM and transcodes a JPEG
2. **Test in browsers:** Chrome, Firefox, Safari, Edge
3. **Verify the `locateFile` callback** works correctly for different bundler setups (Vite, Webpack, bare `<script>`)
4. **Add a browser example** to the repo

---

## Phase 4: npm Publish

1. Update `package.json` repository URL to final GitHub URL
2. Ensure `dist/` contains all needed files (index.js, index.d.ts, transcode.js, transcode.wasm)
3. `npm publish`
4. Verify installation works: `npm install jpeg-to-jxl` in a fresh project

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
| `ENVIRONMENT='web,worker'` | Works in main thread and Web Workers. No Node.js FS support needed. |
| Effort default = 3 | Good speed/size tradeoff for interactive use. User can increase for archival. |
| libjxl as subdirectory build | Single CMake invocation. Alternative: pre-build libjxl separately. |

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
