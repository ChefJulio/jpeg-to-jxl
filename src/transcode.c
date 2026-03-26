/*
 * jpeg-to-jxl: JPEG <-> JXL transcoding via libjxl
 *
 * Three conversion paths:
 * 1. jpeg_to_jxl:       JPEG -> JXL lossless transcode (~20% smaller, reversible)
 * 2. jxl_to_jpeg:        JXL -> JPEG byte-perfect reconstruction (requires metadata)
 * 3. jxl_to_jpeg_lossy: JXL -> pixels -> JPEG re-encode via jpegli (any JXL file)
 */

#include <stdlib.h>
#include <string.h>
#include <jxl/encode.h>
#include <jxl/decode.h>
#include <jpeglib.h>

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
  size_t jpeg_cap = jxl_size * 2;  /* Over-estimate; grows if needed */
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
        if (JXL_DEC_SUCCESS != JxlDecoderSetJPEGBuffer(dec,
            jpeg_buf + jpeg_total, remaining)) {
          free(jpeg_buf);
          JxlDecoderDestroy(dec);
          return NULL;
        }
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
 * JXL -> JPEG (lossy re-encode via pixel decoding + jpegli)
 *
 * Works with ANY JXL file -- decodes to pixels, then re-encodes as JPEG.
 * Unlike jxl_to_jpeg, this does NOT produce a byte-identical JPEG.
 * Use this as a fallback when the JXL lacks JPEG reconstruction metadata.
 *
 * quality: JPEG quality 1-100 (default 90 if out of range)
 * -------------------------------------------------------------------------- */
EXPORT
uint8_t* jxl_to_jpeg_lossy(const uint8_t* jxl_data, size_t jxl_size,
                            size_t* out_size, int quality) {
  *out_size = 0;

  if (!jxl_data || jxl_size == 0) return NULL;
  if (quality < 1 || quality > 100) quality = 90;

  JxlDecoder* dec = JxlDecoderCreate(NULL);
  if (!dec) return NULL;

  if (JXL_DEC_SUCCESS != JxlDecoderSubscribeEvents(dec,
      JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE)) {
    JxlDecoderDestroy(dec);
    return NULL;
  }

  if (JXL_DEC_SUCCESS != JxlDecoderSetInput(dec, jxl_data, jxl_size)) {
    JxlDecoderDestroy(dec);
    return NULL;
  }
  JxlDecoderCloseInput(dec);

  JxlBasicInfo info;
  uint8_t* pixels = NULL;
  JxlPixelFormat format = { 3, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };

  for (;;) {
    JxlDecoderStatus status = JxlDecoderProcessInput(dec);

    switch (status) {
      case JXL_DEC_BASIC_INFO: {
        if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec, &info)) {
          JxlDecoderDestroy(dec);
          return NULL;
        }
        break;
      }

      case JXL_DEC_NEED_IMAGE_OUT_BUFFER: {
        size_t pixel_buf_size;
        if (JXL_DEC_SUCCESS != JxlDecoderImageOutBufferSize(dec, &format,
            &pixel_buf_size)) {
          JxlDecoderDestroy(dec);
          return NULL;
        }
        pixels = (uint8_t*)malloc(pixel_buf_size);
        if (!pixels) {
          JxlDecoderDestroy(dec);
          return NULL;
        }
        if (JXL_DEC_SUCCESS != JxlDecoderSetImageOutBuffer(dec, &format,
            pixels, pixel_buf_size)) {
          free(pixels);
          JxlDecoderDestroy(dec);
          return NULL;
        }
        break;
      }

      case JXL_DEC_FULL_IMAGE: {
        /* Pixels decoded, now encode as JPEG via jpegli */
        break;
      }

      case JXL_DEC_SUCCESS: {
        JxlDecoderDestroy(dec);

        if (!pixels) return NULL;

        /* Encode pixels to JPEG using jpegli */
        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr jerr;
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_compress(&cinfo);

        uint8_t* jpeg_buf = NULL;
        unsigned long jpeg_size_out = 0;
        jpeg_mem_dest(&cinfo, &jpeg_buf, &jpeg_size_out);

        cinfo.image_width = info.xsize;
        cinfo.image_height = info.ysize;
        cinfo.input_components = 3;
        cinfo.in_color_space = JCS_RGB;

        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, quality, TRUE);
        jpeg_start_compress(&cinfo, TRUE);

        size_t row_stride = info.xsize * 3;
        JSAMPROW row_pointer[1];
        while (cinfo.next_scanline < cinfo.image_height) {
          row_pointer[0] = &pixels[cinfo.next_scanline * row_stride];
          jpeg_write_scanlines(&cinfo, row_pointer, 1);
        }

        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);
        free(pixels);

        /* Copy to our own buffer (jpeg_mem_dest uses libjpeg's allocator) */
        uint8_t* result = (uint8_t*)malloc(jpeg_size_out);
        if (!result) {
          free(jpeg_buf);
          return NULL;
        }
        memcpy(result, jpeg_buf, jpeg_size_out);
        free(jpeg_buf);

        *out_size = (size_t)jpeg_size_out;
        return result;
      }

      default: {
        if (pixels) free(pixels);
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
