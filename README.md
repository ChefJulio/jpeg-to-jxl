# jpeg-to-jxl

Lossless JPEG to JPEG XL transcoding in the browser via WASM.

**[Live Demo](https://chefjulio.github.io/jpeg-to-jxl/)** -- try it now, no install needed.

**What it does:** Takes a JPEG file, repackages its DCT coefficients into JPEG XL format. The result is ~20% smaller, and the original JPEG can be perfectly reconstructed byte-for-byte.

**How it works:** Uses libjxl's `JxlEncoderAddJPEGFrame` API compiled to WebAssembly. No pixel decoding happens -- the transcoding operates directly on the JPEG's compressed data, which is why it's both fast and perfectly reversible.

## Usage

```js
import { jpegToJxl, jxlToJpeg } from 'jpeg-to-jxl';

// Compress: JPEG -> JXL (~20% smaller)
const jpegBytes = await fetch('photo.jpg').then(r => r.arrayBuffer());
const jxlBytes = await jpegToJxl(jpegBytes);

// Reconstruct: JXL -> exact original JPEG (byte-identical)
const originalJpeg = await jxlToJpeg(jxlBytes);
```

## API

### `jpegToJxl(jpegData, options?)`

Lossless JPEG to JXL transcoding.

- `jpegData`: `ArrayBuffer | Uint8Array` -- raw JPEG file bytes
- `options.effort`: `number` (1-9, default 3) -- encoding effort. Higher = slower but smaller output.
- Returns: `Promise<ArrayBuffer>` -- JXL file bytes

### `jxlToJpeg(jxlData)`

Reconstruct the original JPEG from a JXL file.

- `jxlData`: `ArrayBuffer | Uint8Array` -- JXL file bytes (must contain JPEG reconstruction metadata)
- Returns: `Promise<ArrayBuffer>` -- original JPEG bytes (byte-identical)
- Throws if the JXL file was not created from a JPEG source

### `init()`

Pre-initialize the WASM module. Call early to avoid cold-start latency on first use.

## Demo

Open `examples/index.html` in a browser (after building) to try it interactively -- drag and drop a JPEG, see the compression ratio, download the JXL, and verify byte-identical reconstruction. Everything runs client-side via WebAssembly.

To run the demo locally:

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
- `transcode.wasm` -- the WASM binary
- `transcode.js` -- Emscripten glue code
- `index.js` -- JS API wrapper
- `index.d.ts` -- TypeScript types

### CI

Push to GitHub and the WASM builds automatically via GitHub Actions. Download the artifact from the Actions tab.

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
