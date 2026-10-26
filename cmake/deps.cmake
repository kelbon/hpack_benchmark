include(cmake/get_cpm.cmake)

# cmake tools
CPMAddPackage(
  NAME CCOZY
  GITHUB_REPOSITORY kelbon/ccozy
  GIT_TAG v0.8.2
)

include(${CCOZY_SOURCE_DIR}/ccozy_tools.cmake)

CPMAddPackage(
  NAME BENCHMARK
  GITHUB_REPOSITORY google/benchmark
  GIT_TAG v1.9.4
  OPTIONS "BENCHMARK_ENABLE_TESTING Off"
          "BENCHMARK_ENABLE_WERROR OFF"
          "BENCHMARK_FORCE_WERROR OFF"
          "BENCHMARK_INSTALL_DOCS OFF"
)

# library under test. For benchmarking local changes:
#   cmake . --preset=release -DCPM_HPACK_SOURCE=/path/to/hpack
CPMAddPackage(
  NAME HPACK
  GITHUB_REPOSITORY kelbon/hpack
  GIT_TAG fixx
)

# reference implementation (nghttp2_hd_* API), only library
CPMAddPackage(
  NAME NGHTTP2
  GITHUB_REPOSITORY nghttp2/nghttp2
  GIT_TAG v1.66.0
  OPTIONS "ENABLE_LIB_ONLY ON"
          "BUILD_SHARED_LIBS OFF"
          "BUILD_STATIC_LIBS ON"
          "ENABLE_DOC OFF"
          "BUILD_TESTING OFF"
)

find_package(Threads REQUIRED)
