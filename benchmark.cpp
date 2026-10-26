
#include <benchmark/benchmark.h>

#include "common.hpp"

int foo() {
  return 1;
}

int bar() {
  return 2;
}

static void bm_foo(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(foo());
  }
}

static void bm_bar(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(bar());
  }
}

BENCHMARK(bm_foo);
BENCHMARK(bm_bar);

BENCHMARK_MAIN();
