include(cmake/get_cpm.cmake)

# cmake tools
CPMAddPackage(
  NAME CCOZY
  GITHUB_REPOSITORY kelbon/ccozy
  GIT_TAG v0.8.2
)

include(${CCOZY_SOURCE_DIR}/ccozy_tools.cmake)

# C++20 coroutines (kelcorolib)
CPMAddPackage(
  NAME KELCORO
  GITHUB_REPOSITORY kelbon/kelcoro
  GIT_TAG v1.4.3
)

# move_only_function, function ref etc (anyanylib)
CPMAddPackage(
  NAME ANYANY
  GITHUB_REPOSITORY kelbon/AnyAny
  GIT_TAG v1.2.1
)

# on_scope_exit / on_scope_failure (zallib)
CPMAddPackage(
  NAME LOGIC_GUARDS
  GITHUB_REPOSITORY kelbon/logic_guards
  GIT_TAG        v1.0.0
)

CPMAddPackage(
  NAME BENCHMARK
  GITHUB_REPOSITORY google/benchmark
  GIT_TAG v1.9.4
  OPTIONS "BENCHMARK_ENABLE_TESTING Off"
          "BENCHMARK_ENABLE_WERROR OFF"
          "BENCHMARK_FORCE_WERROR OFF"
          "BENCHMARK_INSTALL_DOCS OFF"
)

find_package(Threads REQUIRED)
