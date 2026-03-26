/*
 * jpeg-to-jxl: Lossless JPEG <-> JXL transcoding via libjxl
 *
 * This is the C glue that exposes JxlEncoderAddJPEGFrame and the reverse
 * JxlDecoderSetJPEGBuffer path as simple byte-buffer functions callable
 * from JavaScript via Emscripten.
 *
 * The key insight: libjxl can repackage JPEG's DCT coefficients into JXL's
 * more efficient container WITHOUT decoding to pixels. This gives ~20% size
 * reduction while preserving the ability to reconstruct the exact original
 * JPEG byte-for-byte.
 */

#include <stdlib.h>
#include <string.h>
#include <jxl/encode.h>
#include <jxl/decode.h>
#include <jxl/thread_parallel_runner.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT
#endif

/* --------------------------------------------------------------------------
 * JPEG -> JXL (lossless transcoding)
 *
 * Takes raw JPEG bytes, produces JXL bytes that:
 * 1. Are ~20% smaller than the input JPEG
 * 2. Contain reconstruction metadata so the original JPEG can be recovered
 *
 * Returns: pointer to output buffer (caller must call jxl_free)
 * Sets *out_size to the output length, or 0 on error.
 * -------------------------------------------------------------------------- */
EXPORT
uint8_t* jpeg_to_jxl(const uint8_t* jpeg_data, size_t jpeg_size,
                      size_t* out_size, int effort) {
  *out_size = 0;

  if (!jpeg_data || jpeg_size == 0) return NULL;
  if (effort < 1) effort = 3;  /* Default: balanced speed/size */
  if (effort > 9) effort = 9;

  /* Create encoder */
  JxlEncoder* enc = JxlEncoderCreate(NULL);
  if (!enc) return NULL;

  /* Single-threaded for WASM (no pthread overhead) */
  /* If built with -pthread, could use JxlThreadParallelRunner here */

  /* Enable JPEG reconstruction metadata -- this is what makes round-trip
   * possible. Without this, the JXL file would still be valid but you
   * couldn't reconstruct the original JPEG. */
  if (JXL_ENC_SUCCESS != JxlEncoderStoreJPEGMetadata(enc, JXL_TRUE)) {
    JxlEncoderDestroy(enc);
    return NULL;
  }

  /* Set encoding effort (speed vs compression tradeoff) */
  JxlEncoderFrameSettings* settings = JxlEncoderFrameSettingsCreate(enc, NULL);
  JxlEncoderFrameSettingsSetOption(settings,
    JXL_ENC_FRAME_SETTING_EFFORT, effort);

  /* Add the JPEG frame -- libjxl parses the JPEG internally and extracts
   * the DCT coefficients, Huffman tables, quantization tables, etc.
   * No pixel decoding happens. */
  if (JXL_ENC_SUCCESS != JxlEncoderAddJPEGFrame(settings, jpeg_data, jpeg_size)) {
    JxlEncoderDestroy(enc);
    return NULL;
  }

  /* Signal that we're done adding frames */
  JxlEncoderCloseInput(enc);

  /* Collect output -- grow buffer as needed */
  size_t buf_cap = jpeg_size;  /* Start with input size as estimate */
  uint8_t* buf = (uint8_t*)malloc(buf_cap);
  if (!buf) {
    JxlEncoderDestroy(enc);
    return NULL;
  }

  size_t total = 0;
  JxlEncoderStatus status;

  for (;;) {
    size_t avail = buf_cap - total;
    uint8_t* next = buf + total;

    status = JxlEncoderProcessOutput(enc, &next, &avail);

    total = (size_t)(next - buf);

    if (status == JXL_ENC_SUCCESS) {
      break;  /* Done */
    }

    if (status == JXL_ENC_NEED_MORE_OUTPUT) {
      /* Grow buffer */
      buf_cap *= 2;
      uint8_t* newbuf = (uint8_t*)realloc(buf, buf_cap);
      if (!newbuf) {
        free(buf);
        JxlEncoderDestroy(enc);
        return NULL;
      }
      buf = newbuf;
      continue;
    }

    /* Error */
    free(buf);
    JxlEncoderDestroy(enc);
    return NULL;
  }

  JxlEncoderDestroy(enc);

  /* Shrink to fit */
  uint8_t* result = (uint8_t*)realloc(buf, total);
  *out_size = total;
  return result ? result : buf;
}

/* --------------------------------------------------------------------------
 * JXL -> JPEG (reconstruct original JPEG)
 *
 * Takes JXL bytes that were created via jpeg_to_jxl (or cjxl from a JPEG)
 * and reconstructs the exact original JPEG byte-for-byte.
 *
 * Returns NULL if the JXL file doesn't contain JPEG reconstruction data.
 * -------------------------------------------------------------------------- */
EXPORT
uint8_t* jxl_to_jpeg(const uint8_t* jxl_data, size_t jxl_size,
                      size_t* out_size) {
  *out_size = 0;

  if (!jxl_data || jxl_size == 0) return NULL;

  JxlDecoder* dec = JxlDecoderCreate(NULL);
  if (!dec) return NULL;

  /* We only need JPEG reconstruction output, not pixel data */
  if (JXL_DEC_SUCCESS != JxlDecoderSubscribeEvents(dec,
      JXL_DEC_JPEG_RECONSTRUCTION | JXL_DEC_FULL_IMAGE)) {
    JxlDecoderDestroy(dec);
    return NULL;
  }

  /* Feed the entire JXL buffer */
  if (JXL_DEC_SUCCESS != JxlDecoderSetInput(dec, jxl_data, jxl_size)) {
    JxlDecoderDestroy(dec);
    return NULL;
  }
  JxlDecoderCloseInput(dec);

  /* Initial JPEG output buffer */
  size_t jpeg_cap = jxl_size * 2;  /* Estimate: JPEG is ~25% larger */
  uint8_t* jpeg_buf = (uint8_t*)malloc(jpeg_cap);
  if (!jpeg_buf) {
    JxlDecoderDestroy(dec);
    return NULL;
  }

  size_t jpeg_total = 0;
  int jpeg_set = 0;

  for (;;) {
    JxlDecoderStatus status = JxlDecoderProcessInput(dec);

    switch (status) {
      case JXL_DEC_JPEG_RECONSTRUCTION: {
        /* Set the JPEG output buffer */
        size_t remaining = jpeg_cap - jpeg_total;
        if (JXL_DEC_SUCCESS != JxlDecoderSetJPEGBuffer(dec,
            jpeg_buf + jpeg_total, remaining)) {
          free(jpeg_buf);
          JxlDecoderDestroy(dec);
          return NULL;
        }
        jpeg_set = 1;
        break;
      }

      case JXL_DEC_JPEG_NEED_MORE_OUTPUT: {
        /* Grow JPEG buffer */
        size_t used = jpeg_cap - JxlDecoderReleaseJPEGBuffer(dec);
        jpeg_total = used;
        jpeg_cap *= 2;
        uint8_t* newbuf = (uint8_t*)realloc(jpeg_buf, jpeg_cap);
        if (!newbuf) {
          free(jpeg_buf);
          JxlDecoderDestroy(dec);
          return NULL;
        }
        jpeg_buf = newbuf;
        size_t remaining = jpeg_cap - jpeg_total;
        JxlDecoderSetJPEGBuffer(dec, jpeg_buf + jpeg_total, remaining);
        break;
      }

      case JXL_DEC_FULL_IMAGE: {
        /* JPEG reconstruction complete */
        if (jpeg_set) {
          size_t remaining = JxlDecoderReleaseJPEGBuffer(dec);
          jpeg_total = jpeg_cap - remaining;
        }
        break;
      }

      case JXL_DEC_SUCCESS: {
        /* All done */
        JxlDecoderDestroy(dec);
        if (jpeg_total == 0) {
          /* No JPEG reconstruction data in this JXL */
          free(jpeg_buf);
          return NULL;
        }
        uint8_t* result = (uint8_t*)realloc(jpeg_buf, jpeg_total);
        *out_size = jpeg_total;
        return result ? result : jpeg_buf;
      }

      default: {
        /* Error or unsupported event */
        free(jpeg_buf);
        JxlDecoderDestroy(dec);
        return NULL;
      }
    }
  }
}

/* --------------------------------------------------------------------------
 * Memory management -- called from JS to free returned buffers
 * -------------------------------------------------------------------------- */
EXPORT
void jxl_free(void* ptr) {
  free(ptr);
}
