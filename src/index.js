/**
 * jpeg-to-jxl: Lossless JPEG <-> JXL transcoding
 *
 * ~20% smaller files with byte-perfect JPEG reconstruction.
 * Uses libjxl's JxlEncoderAddJPEGFrame under the hood via WASM.
 *
 * Usage:
 *   import { jpegToJxl, jxlToJpeg } from 'jpeg-to-jxl';
 *
 *   const jxlBuffer = await jpegToJxl(jpegArrayBuffer);
 *   const originalJpeg = await jxlToJpeg(jxlBuffer);
 */

let _modulePromise = null;

/**
 * Lazy-load and initialize the WASM module.
 * Uses a promise cache to prevent duplicate initialization on concurrent calls.
 */
function getModule() {
  if (_modulePromise) return _modulePromise;

  _modulePromise = (async () => {
    const createModule = (await import('./transcode.js')).default;
    return createModule({
      locateFile: (path) => {
        const url = new URL(path, import.meta.url);
        if (url.protocol === 'file:') {
          return url.pathname.replace(/^\/([A-Z]:)/i, '$1');
        }
        return url.href;
      },
    });
  })();

  return _modulePromise;
}

/**
 * Lossless JPEG to JXL transcoding.
 *
 * Repackages JPEG's DCT coefficients into JXL format without decoding
 * to pixels. The result is ~20% smaller and can be perfectly reversed
 * back to the exact original JPEG.
 *
 * @param {ArrayBuffer|Uint8Array} jpegData - Raw JPEG file bytes
 * @param {Object} [options]
 * @param {number} [options.effort=3] - Encoding effort 1-9 (higher = slower + smaller)
 * @returns {Promise<ArrayBuffer>} JXL file bytes
 * @throws {Error} If input is not a valid JPEG or transcoding fails
 */
export async function jpegToJxl(jpegData, options = {}) {
  const { effort = 3 } = options;

  if (typeof effort !== 'number' || effort < 1 || effort > 9) {
    throw new Error('effort must be a number between 1 and 9');
  }

  const mod = await getModule();

  const input = jpegData instanceof ArrayBuffer
    ? new Uint8Array(jpegData)
    : jpegData;

  if (input.length < 2 || input[0] !== 0xFF || input[1] !== 0xD8) {
    throw new Error('Input does not appear to be a valid JPEG (missing SOI marker)');
  }

  // Allocate input buffer in WASM memory
  const inputPtr = mod._malloc(input.length);
  if (!inputPtr) throw new Error('WASM memory allocation failed (input buffer)');
  mod.HEAPU8.set(input, inputPtr);

  // Allocate output size pointer (size_t = 4 bytes in wasm32)
  const sizePtr = mod._malloc(4);
  if (!sizePtr) { mod._free(inputPtr); throw new Error('WASM memory allocation failed'); }

  try {
    const outPtr = mod._jpeg_to_jxl(inputPtr, input.length, sizePtr, effort);

    if (!outPtr) {
      throw new Error(
        'JPEG to JXL transcoding failed. Input may not be a valid JPEG, '
        + 'or it may use features not supported by the transcoder '
        + '(e.g., arithmetic coding, JPEG 2000).'
      );
    }

    const outSize = mod.HEAPU32[sizePtr >> 2];
    // Copy output to a new ArrayBuffer (so we can free WASM memory)
    const result = new ArrayBuffer(outSize);
    new Uint8Array(result).set(mod.HEAPU8.subarray(outPtr, outPtr + outSize));

    mod._jxl_free(outPtr);
    return result;
  } finally {
    mod._free(inputPtr);
    mod._free(sizePtr);
  }
}

/**
 * Convert a JXL file to JPEG.
 *
 * Two modes:
 * 1. If the JXL contains JPEG reconstruction metadata (created via jpegToJxl
 *    or cjxl), returns the byte-identical original JPEG.
 * 2. Otherwise, decodes to pixels and re-encodes as JPEG via jpegli.
 *    The quality option controls the JPEG quality for this fallback path.
 *
 * @param {ArrayBuffer|Uint8Array} jxlData - JXL file bytes
 * @param {Object} [options]
 * @param {number} [options.quality=90] - JPEG quality 1-100 (only used when reconstruction is unavailable)
 * @returns {Promise<ArrayBuffer>} JPEG file bytes
 * @throws {Error} If decoding fails
 */
export async function jxlToJpeg(jxlData, options = {}) {
  const { quality = 90 } = options;
  const mod = await getModule();

  const input = jxlData instanceof ArrayBuffer
    ? new Uint8Array(jxlData)
    : jxlData;

  const inputPtr = mod._malloc(input.length);
  if (!inputPtr) throw new Error('WASM memory allocation failed (input buffer)');
  mod.HEAPU8.set(input, inputPtr);

  const sizePtr = mod._malloc(4);
  if (!sizePtr) { mod._free(inputPtr); throw new Error('WASM memory allocation failed'); }

  try {
    // Try byte-perfect reconstruction first
    let outPtr = mod._jxl_to_jpeg(inputPtr, input.length, sizePtr);

    if (!outPtr) {
      // No reconstruction metadata -- fall back to lossy re-encode
      if (typeof quality !== 'number' || quality < 1 || quality > 100) {
        throw new Error('quality must be a number between 1 and 100');
      }
      outPtr = mod._jxl_to_jpeg_lossy(inputPtr, input.length, sizePtr, quality);
    }

    if (!outPtr) {
      throw new Error('JXL to JPEG conversion failed. The file may not be a valid JXL.');
    }

    const outSize = mod.HEAPU32[sizePtr >> 2];
    const result = new ArrayBuffer(outSize);
    new Uint8Array(result).set(mod.HEAPU8.subarray(outPtr, outPtr + outSize));

    mod._jxl_free(outPtr);
    return result;
  } finally {
    mod._free(inputPtr);
    mod._free(sizePtr);
  }
}

/**
 * Pre-initialize the WASM module.
 * Call this early to avoid cold-start latency on first use.
 * @returns {Promise<void>}
 */
export async function init() {
  await getModule();
}
