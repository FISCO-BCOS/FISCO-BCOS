// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

#include <benchmark/benchmark.h>
#include <cstdlib>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#endif

namespace
{
void malloc_(size_t size) noexcept
{
    auto m = std::malloc(size);
    benchmark::DoNotOptimize(m);
    std::free(m);
}

void calloc_(size_t size) noexcept
{
    auto m = std::calloc(1, size);
    benchmark::DoNotOptimize(m);
    std::free(m);
}

void os_specific(size_t size) noexcept
{
#if defined(__unix__) || defined(__APPLE__)
    auto m = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    benchmark::DoNotOptimize(m);
    munmap(m, size);
#else
    (void)size;
#endif
}


template <void (*F)(size_t) noexcept>
void allocate(benchmark::State& state)
{
    const auto size = static_cast<size_t>(state.range(0)) * 1024;

    for (auto _ : state)
        F(size);
}

#define ARGS ->RangeMultiplier(2)->Range(1, 128 * 1024)

BENCHMARK_TEMPLATE(allocate, malloc_) ARGS;
BENCHMARK_TEMPLATE(allocate, calloc_) ARGS;
BENCHMARK_TEMPLATE(allocate, os_specific) ARGS;

}  // namespace
