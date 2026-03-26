cmake_minimum_required(VERSION 3.16)
project(jpeg_to_jxl_wasm C CXX)

# ---------------------------------------------------------------------------
# Build libjxl for Emscripten, then compile our transcode.c glue into WASM
# ---------------------------------------------------------------------------

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# --- libjxl as a subdirectory build ---
# Disable parts we don't need to speed up compilation
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_BENCHMARK OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_DOXYGEN OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_JNI OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_MANPAGES OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_OPENEXR OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_PLUGINS OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_SJPEG OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_SKCMS OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
set(JPEGXL_ENABLE_VIEWERS OFF CACHE BOOL "" FORCE)
# We need the encoder and decoder
set(JPEGXL_ENABLE_TRANSCODE_JPEG ON CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/../deps/libjxl ${CMAKE_BINARY_DIR}/libjxl)

# --- Our WASM glue ---
add_executable(transcode ${CMAKE_SOURCE_DIR}/../src/transcode.c)

target_link_libraries(transcode PRIVATE jxl jxl_dec)

target_include_directories(transcode PRIVATE
  ${CMAKE_SOURCE_DIR}/../deps/libjxl/lib/include
  ${CMAKE_BINARY_DIR}/libjxl/lib/include  # For generated jxl/version.h
)

# --- Emscripten link flags ---
# MODULARIZE: wraps output in a factory function (required for ES module usage)
# EXPORT_ES6: emit ES6 module syntax
# EXPORTED_FUNCTIONS: expose our C functions to JS
# ALLOW_MEMORY_GROWTH: heap can grow (images vary in size)
# SINGLE_FILE=0: keep .wasm separate (better for lazy loading / caching)
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
    -s ENVIRONMENT='web,worker' \
    -s FILESYSTEM=0 \
    -s SINGLE_FILE=0 \
    -O3 \
    --closure 1 \
  "
)
