# jpeg-to-jxl

JPEG &lt;-&gt; JPEG XL transcoding in the browser via WASM.

**[Live Demo](https://chefjulio.github.io/jpeg-to-jxl/)** -- try it now, no install needed.

**What it does:**
- **JPEG -> JXL:** Repackages JPEG's DCT coefficients into JXL format. ~20% smaller, byte-perfect reconstruction.
- **JXL -> JPEG:** Reconstructs the original JPEG if possible, otherwise decodes to pixels and re-encodes as JPEG.

**How it works:** Uses libjxl's `JxlEncoderAddJPEGFrame` API compiled to WebAssembly. The JPEG->JXL path operates directly on compressed data (no pixel decoding), which is why it's both fast and perfectly reversible.

## Install

```bash
npm install jpeg-to-jxl
```

## Usage

```js
import { jpegToJxl, jxlToJpeg, init } from 'jpeg-to-jxl';

// Optional: pre-warm WASM to avoid cold-start latency
await init();

// JPEG -> JXL (~20% smaller, lossless)
const jpegBytes = await fetch('photo.jpg').then(r => r.arrayBuffer());
const jxlBytes = await jpegToJxl(jpegBytes);

// JXL -> JPEG (byte-identical if created from JPEG, lossy fallback otherwise)
const jpegBack = await jxlToJpeg(jxlBytes);

// JXL -> JPEG with custom quality (for lossy fallback path)
const converted = await jxlToJpeg(someJxlFile, { quality: 85 });
```

## API

### `jpegToJxl(jpegData, options?)`

Lossless JPEG to JXL transcoding.

- `jpegData`: `ArrayBuffer | Uint8Array` -- raw JPEG file bytes
- `options.effort`: `number` (1-9, default 3) -- encoding effort. Higher = slower but smaller output.
- Returns: `Promise<ArrayBuffer>` -- JXL file bytes

### `jxlToJpeg(jxlData, options?)`

Convert a JXL file to JPEG. Works with **any** JXL file.

- `jxlData`: `ArrayBuffer | Uint8Array` -- JXL file bytes
- `options.quality`: `number` (1-100, default 90) -- JPEG quality for lossy fallback path
- Returns: `Promise<ArrayBuffer>` -- JPEG file bytes

If the JXL was created from a JPEG source (via `jpegToJxl` or `cjxl`), returns the byte-identical original JPEG. Otherwise, decodes to pixels and re-encodes as JPEG via libjpeg.

### `init()`

Pre-initialize the WASM module. Call early to avoid cold-start latency on first use.

## Demo

The [live demo](https://chefjulio.github.io/jpeg-to-jxl/) supports both directions -- drop a JPEG or JXL file and convert. Runs entirely client-side via WebAssembly.

To run locally:

```bash
# Build first (or download dist/ from CI artifacts)
bash build/build.sh

# Serve with any static server (needed for ES module imports)
npx serve .
# Then open http://localhost:3000/examples/
```

## Building from source

### Prerequisites

1. [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) installed and activated
2. libjxl cloned with submodules

### Steps

```bash
# Clone libjxl into deps/
git clone --recursive https://github.com/libjxl/libjxl.git deps/libjxl

# Activate Emscripten
source /path/to/emsdk/emsdk_env.sh

# Build
bash build/build.sh
```

Output goes to `dist/`:
- `transcode.wasm` -- the WASM binary (~2.7 MB, ~1.05 MB gzipped)
- `transcode.js` -- Emscripten glue code
- `index.js` -- JS API wrapper
- `index.d.ts` -- TypeScript types

### CI

Push to GitHub and the WASM builds automatically via GitHub Actions. Tag a version (`git tag v0.x.0 && git push origin v0.x.0`) to auto-publish to npm.

## How is this different from @jsquash/jxl?

`@jsquash/jxl` encodes images at the **pixel level** -- it decodes JPEG to raw pixels, then re-encodes to JXL. This means:
- You lose the original JPEG (can't reconstruct it)
- Lossless mode (`quality: 100`) produces files **larger** than the original JPEG
- It's slower (full decode + encode)

`jpeg-to-jxl` operates at the **bitstream level** -- it repackages JPEG's internal DCT data directly. This means:
- ~20% smaller than the original JPEG
- Original JPEG can be reconstructed byte-for-byte
- Faster (no pixel processing)

## License

BSD-3-Clause (matches libjxl)
