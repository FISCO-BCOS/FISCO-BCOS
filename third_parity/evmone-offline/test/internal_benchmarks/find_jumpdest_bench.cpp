// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

#include <benchmark/benchmark.h>
#include <array>
#include <random>
#include <unordered_map>

#pragma GCC diagnostic error "-Wconversion"

namespace
{
constexpr size_t jumpdest_map_size = 0x6000;

using jumpdest_t = std::pair<int, int>;
using jumpdest_map = std::array<jumpdest_t, jumpdest_map_size>;
using find_fn = int (*)(const jumpdest_t*, size_t, int) noexcept;

template <typename T>
inline T linear(const std::pair<T, T>* it, size_t size, T offset) noexcept
{
    for (const auto end = it + size; it != end; ++it)
    {
        if (it->first == offset)
            return it->second;
    }
    return T(-1);
}

template <typename T>
inline T lower_bound(const std::pair<T, T>* begin, size_t size, T offset) noexcept
{
    const auto end = begin + size;
    const auto it = std::lower_bound(
        begin, end, offset, [](std::pair<int, int> p, int v) noexcept { return p.first < v; });
    return (it != end && it->first == offset) ? it->second : T(-1);
}

template <typename T>
inline T binary_search(const std::pair<T, T>* arr, size_t size, T offset) noexcept
{
    int first = 0;
    int last = static_cast<int>(size) - 1;

    while (first <= last)
    {
        const auto middle = (first + last) / 2;
        const auto& e = arr[middle];
        if (e.first == offset)
            return e.second;
        else if (e.first < offset)
            first = middle + 1;
        else
            last = middle - 1;
    }
    return T(-1);
}

template <typename T>
inline T binary_search2(const std::pair<T, T>* begin, size_t size, T offset) noexcept
{
    while (size > 0)
    {
        const auto half = size / 2;
        const auto it = begin + half;
        if (it->first == offset)
            return it->second;
        else if (it->first < offset)
        {
            begin = it + 1;
            size -= half + 1;
        }
        else
            size = half;
    }

    return T(-1);
}

template <typename T>
struct map_builder
{
    static const std::array<std::pair<T, T>, jumpdest_map_size> map;
};

template <typename T>
const std::array<std::pair<T, T>, jumpdest_map_size> map_builder<T>::map = []() noexcept
{
    auto m = std::array<std::pair<T, T>, jumpdest_map_size>{};
    for (size_t i = 0; i < m.size(); ++i)
        m[i] = {static_cast<T>(2 * i + 1), static_cast<T>(2 * i + 2)};
    return m;
}
();

template <typename T, T (*F)(const std::pair<T, T>*, size_t, T) noexcept>
void find_jumpdest(benchmark::State& state)
{
    const auto& map = map_builder<T>::map;
    const auto begin = map.data();
    const auto size = static_cast<size_t>(state.range(0));
    const auto needle = static_cast<T>(state.range(1));
    benchmark::ClobberMemory();

    int x = -1;
    for (auto _ : state)
    {
        x = F(begin, size, needle);
        benchmark::DoNotOptimize(x);
    }

    if (needle % 2 == 1)
    {
        if (x != needle + 1)
            state.SkipWithError("incorrect element found");
    }
    else if (x != T(-1))
        state.SkipWithError("element should not have been found");
}

#define ARGS                                  \
    ->Args({0, 0})                            \
        ->Args({3, 0})                        \
        ->Args({16, 0})                       \
        ->Args({256, 1})                      \
        ->Args({256, 255})                    \
        ->Args({256, 511})                    \
        ->Args({256, 0})                      \
        ->Args({int{jumpdest_map_size}, 1})   \
        ->Args({int{jumpdest_map_size}, 359}) \
        ->Args({int{jumpdest_map_size}, 0})

BENCHMARK_TEMPLATE(find_jumpdest, int, linear) ARGS;
BENCHMARK_TEMPLATE(find_jumpdest, int, lower_bound) ARGS;
BENCHMARK_TEMPLATE(find_jumpdest, int, binary_search) ARGS;
BENCHMARK_TEMPLATE(find_jumpdest, int, binary_search2) ARGS;
BENCHMARK_TEMPLATE(find_jumpdest, uint16_t, linear) ARGS;
BENCHMARK_TEMPLATE(find_jumpdest, uint16_t, lower_bound) ARGS;
BENCHMARK_TEMPLATE(find_jumpdest, uint16_t, binary_search) ARGS;
BENCHMARK_TEMPLATE(find_jumpdest, uint16_t, binary_search2) ARGS;


std::array<uint16_t, 1000> get_random_indexes()
{
    auto gen = std::mt19937_64{std::random_device{}()};
    auto dist = std::uniform_int_distribution<uint16_t>(0, jumpdest_map_size - 1);
    auto res = std::array<uint16_t, 1000>{};
    for (auto& x : res)
        x = dist(gen);
    return res;
}

const auto random_indexes = get_random_indexes();

template <typename T, T (*F)(const std::pair<T, T>*, size_t, T) noexcept>
void find_jumpdest_random(benchmark::State& state)
{
    const auto indexes = random_indexes;
    const auto& map = map_builder<T>::map;
    const auto begin = map.data();
    benchmark::ClobberMemory();

    for (auto _ : state)
    {
        for (auto i : indexes)
        {
            auto x = F(begin, jumpdest_map_size, i);
            benchmark::DoNotOptimize(x);
        }
    }
}

BENCHMARK_TEMPLATE(find_jumpdest_random, int, linear);
BENCHMARK_TEMPLATE(find_jumpdest_random, int, lower_bound);
BENCHMARK_TEMPLATE(find_jumpdest_random, int, binary_search);
BENCHMARK_TEMPLATE(find_jumpdest_random, int, binary_search2);
BENCHMARK_TEMPLATE(find_jumpdest_random, uint16_t, linear);
BENCHMARK_TEMPLATE(find_jumpdest_random, uint16_t, lower_bound);
BENCHMARK_TEMPLATE(find_jumpdest_random, uint16_t, binary_search);
BENCHMARK_TEMPLATE(find_jumpdest_random, uint16_t, binary_search2);


template <typename T>
struct split_map
{
    std::array<T, jumpdest_map_size> key;
    std::array<T, jumpdest_map_size> value;
};

template <typename T>
struct split_map_builder
{
    static const split_map<T> map;
};

template <typename T>
const split_map<T> split_map_builder<T>::map = []() noexcept
{
    auto m = split_map<T>{};
    for (size_t i = 0; i < m.key.size(); ++i)
    {
        m.key[i] = static_cast<T>(2 * i + 1);
        m.value[i] = static_cast<T>(2 * i + 2);
    }
    return m;
}
();

template <typename T>
inline T lower_bound(const T* begin, const T* values, size_t size, T offset) noexcept
{
    const auto end = begin + size;
    const auto it = std::lower_bound(begin, end, offset);
    return (it != end && *it == offset) ? values[it - begin] : T(-1);
}

template <typename T>
inline T binary_search2(const T* begin, const T* values, size_t size, T offset) noexcept
{
    const auto begin_copy = begin;
    while (size > 0)
    {
        const auto half = size / 2;
        const auto it = begin + half;
        if (*it == offset)
            return values[it - begin_copy];
        else if (*it < offset)
        {
            begin = it + 1;
            size -= half + 1;
        }
        else
            size = half;
    }

    return T(-1);
}


template <typename T, T (*F)(const T*, const T*, size_t, T) noexcept>
void find_jumpdest_split(benchmark::State& state)
{
    const auto& map = split_map_builder<T>::map;
    const auto begin = map.key.data();
    const auto values = map.value.data();
    const auto size = static_cast<size_t>(state.range(0));
    const auto needle = static_cast<T>(state.range(1));
    benchmark::ClobberMemory();

    int x = -1;
    for (auto _ : state)
    {
        x = F(begin, values, size, needle);
        benchmark::DoNotOptimize(x);
    }

    if (needle % 2 == 1)
    {
        if (x != needle + 1)
            state.SkipWithError("incorrect element found");
    }
    else if (x != T(-1))
        state.SkipWithError("element should not have been found");
}

template <typename T, T (*F)(const T*, const T*, size_t, T) noexcept>
void find_jumpdest_split_random(benchmark::State& state)
{
    const auto indexes = random_indexes;
    const auto& map = split_map_builder<T>::map;
    const auto key = map.key.data();
    const auto value = map.value.data();
    benchmark::ClobberMemory();

    for (auto _ : state)
    {
        for (auto i : indexes)
        {
            auto x = F(key, value, jumpdest_map_size, i);
            benchmark::DoNotOptimize(x);
        }
    }
}

BENCHMARK_TEMPLATE(find_jumpdest_split, uint16_t, lower_bound) ARGS;
BENCHMARK_TEMPLATE(find_jumpdest_split, uint16_t, binary_search2) ARGS;
BENCHMARK_TEMPLATE(find_jumpdest_split_random, uint16_t, lower_bound);
BENCHMARK_TEMPLATE(find_jumpdest_split_random, uint16_t, binary_search2);


template <typename T>
void find_jumpdest_hashmap_random(benchmark::State& state)
{
    const auto indexes = random_indexes;
    const auto& map = map_builder<T>::map;

    const auto hashmap = std::unordered_map<T, T>{map.begin(), map.end()};
    benchmark::ClobberMemory();

    for (auto _ : state)
    {
        for (auto i : indexes)
        {
            auto it = hashmap.find(i);
            auto x = it != hashmap.end() ? it->second : -1;
            benchmark::DoNotOptimize(x);
        }
    }
}

BENCHMARK_TEMPLATE(find_jumpdest_hashmap_random, int);
BENCHMARK_TEMPLATE(find_jumpdest_hashmap_random, uint16_t);

}  // namespace

BENCHMARK_MAIN();