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
 * Reconstruct the original JPEG from a JXL file.
 *
 * Only works with JXL files containing JPEG reconstruction metadata.
 * Output is byte-identical to the original JPEG.
 *
 * @param jxlData - JXL file bytes (must contain JPEG reconstruction data)
 * @returns Original JPEG bytes (byte-identical)
 */
export declare function jxlToJpeg(
  jxlData: ArrayBuffer | Uint8Array
): Promise<ArrayBuffer>;

/**
 * Pre-initialize the WASM module to avoid cold-start latency.
 */
export declare function init(): Promise<void>;
