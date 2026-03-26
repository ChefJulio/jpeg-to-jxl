cmake_minimum_required(VERSION 3.16)
project(jpeg_to_jxl_wasm C CXX)

# ---------------------------------------------------------------------------
# Build libjxl for Emscripten, then compile our transcode.c glue into WASM
# ---------------------------------------------------------------------------

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# --- libjxl as a subdirectory build ---
# Disable everything we don't need -- every flag matters for WASM size
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_BENCHMARK OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_DOXYGEN OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_JNI OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_JPEGLI OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_MANPAGES OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_OPENEXR OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_PLUGINS OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_SJPEG OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_SKCMS OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_VIEWERS OFF CACHE BOOL "" FORCE)
# We need the encoder and decoder with JPEG transcoding
set(JPEGXL_ENABLE_TRANSCODE_JPEG ON CACHE BOOL "" FORCE)

# --- Threading: force single-threaded ---
# No pthreads in WASM = no SharedArrayBuffer/COOP/COEP requirements
set(JPEGXL_FORCE_NEON OFF CACHE BOOL "" FORCE)
set(CMAKE_DISABLE_FIND_PACKAGE_Threads TRUE)
set(HWY_ENABLE_THREADS OFF CACHE BOOL "" FORCE)
set(HWY_SYSTEM_GTEST OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/../deps/libjxl ${CMAKE_BINARY_DIR}/libjxl)

# --- Our WASM glue ---
add_executable(transcode ${CMAKE_SOURCE_DIR}/../src/transcode.c)

target_link_libraries(transcode PRIVATE jxl jxl_dec)

target_include_directories(transcode PRIVATE
  ${CMAKE_SOURCE_DIR}/../deps/libjxl/lib/include
  ${CMAKE_BINARY_DIR}/libjxl/lib/include  # For generated jxl/version.h
)

# --- Emscripten link flags ---
# MODULARIZE + EXPORT_ES6: ES module factory function
# ALLOW_MEMORY_GROWTH: heap grows for large images
# MALLOC=emmalloc: smaller allocator (we don't need full dlmalloc)
# SINGLE_FILE=0: keep .wasm separate for lazy loading / caching
# -Oz: optimize for size (transcoding is fast enough, minimize download)
set_target_properties(transcode PROPERTIES
  SUFFIX ".js"
  LINK_FLAGS "\
    -s MODULARIZE=1 \
    -s EXPORT_ES6=1 \
    -s EXPORTED_FUNCTIONS=['_jpeg_to_jxl','_jxl_to_jpeg','_jxl_free','_malloc','_free'] \
    -s EXPORTED_RUNTIME_METHODS=['HEAPU8','HEAPU32'] \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s INITIAL_MEMORY=16777216 \
    -s STACK_SIZE=1048576 \
    -s ENVIRONMENT='web,worker,node' \
    -s FILESYSTEM=0 \
    -s SINGLE_FILE=0 \
    -s MALLOC=emmalloc \
    -s USE_PTHREADS=0 \
    -Oz \
    --closure 1 \
  "
)
