cmake_minimum_required(VERSION 3.20)
project(hpack_benchmark)

include(cmake/deps.cmake)

file(GLOB_RECURSE hpack_benchmark_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(hpack_benchmarklib ${hpack_benchmark_SOURCES})

target_include_directories(hpack_benchmarklib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(hpack_benchmarklib PUBLIC
    ${CMAKE_THREAD_LIBS_INIT}
    anyanylib
    kelcorolib
    zallib
    benchmark::benchmark
)

add_executable(run_benchmark benchmark.cpp)

target_link_libraries(run_benchmark PUBLIC hpack_benchmarklib)

set_target_properties(hpack_benchmarklib run_benchmark
 PROPERTIES
	LINKER_LANGUAGE CXX
	CMAKE_CXX_STANDARD_REQUIRED ON
	CXX_STANDARD 20)

# generates launch.json, places compile_commands.json correctly for clangd
ccozy_on_configuration_end_vscode()
