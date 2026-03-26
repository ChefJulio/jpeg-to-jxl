/**
 * Lossless JPEG to JXL transcoding.
 *
 * Repackages JPEG's DCT coefficients into JXL format without decoding
 * to pixels. Result is ~20% smaller with byte-perfect JPEG reconstruction.
 *
 * @param jpegData - Raw JPEG file bytes
 * @param options.effort - Encoding effort 1-9 (default: 3, higher = slower + smaller)
 * @returns JXL file bytes
 */
export declare function jpegToJxl(
  jpegData: ArrayBuffer | Uint8Array,
  options?: { effort?: number }
): Promise<ArrayBuffer>;

/**
 * Convert a JXL file to JPEG.
 *
 * If the JXL contains JPEG reconstruction metadata (created via jpegToJxl
 * or cjxl), returns the byte-identical original JPEG.
 * Otherwise, decodes to pixels and re-encodes as JPEG via jpegli.
 *
 * @param jxlData - JXL file bytes
 * @param options.quality - JPEG quality 1-100 (default: 90, only used for lossy fallback)
 * @returns JPEG file bytes
 */
export declare function jxlToJpeg(
  jxlData: ArrayBuffer | Uint8Array,
  options?: { quality?: number }
): Promise<ArrayBuffer>;

/**
 * Pre-initialize the WASM module to avoid cold-start latency.
 */
export declare function init(): Promise<void>;
