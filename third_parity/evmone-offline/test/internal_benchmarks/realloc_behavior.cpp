// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace std::chrono;
using timer = high_resolution_clock;

struct result
{
    size_t size;
    void* memory_ptr;
    decltype(timer::now() - timer::now()) duration;
};

void benchmark_realloc()
{
    constexpr int repeats = 6;
    constexpr size_t realloc_multiplier = 2;
    constexpr size_t size_start = 128 * 1024;
    constexpr size_t size_end = 8 * 1024 * 1024;

    auto results = std::vector<result>{};
    results.reserve(size_end / size_start);

    void* m = nullptr;

    for (int i = 0; i < repeats; ++i)
    {
        for (auto size = size_start; size <= size_end; size *= realloc_multiplier)
        {
            const auto start_time = timer::now();
            m = std::realloc(m, size);
            const auto duration = timer::now() - start_time;
            results.push_back({size, m, duration});
        }
        std::free(m);
        m = nullptr;
    }

    for (auto r : results)
    {
        std::cout << (r.size / 1024) << "k\t " << r.memory_ptr << "\t"
                  << duration_cast<nanoseconds>(r.duration).count() << "\n";
    }
}

int main()
{
    benchmark_realloc();
    return 0;
}
