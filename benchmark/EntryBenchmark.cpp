/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief Benchmark: Proxy-based Entry vs Legacy AnyHolder-based Entry
 * @file EntryBenchmark.cpp
 */

#include "LegacyEntry.h"
#include "bcos-framework/storage/Entry.h"
#include <benchmark/benchmark.h>
#include <memory>
#include <string>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════
// LegacyEntry
// ═══════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════
// Benchmark helpers
// ═══════════════════════════════════════════════════════════════════════

using namespace bcos;
using namespace bcos::storage;

// Pre-built test data at various sizes to hit all buffer models
static const std::string kSmallStr(10, 'x');        // → SmallBuffer
static const std::string kBoundaryStr(31, 'y');     // → SmallBuffer (max)
static const std::string kFixed32Str(32, 'z');      // → Fixed32Buffer
static const std::string kLargeStr(200, 'L');       // → BufferModel<std::string>
static const std::string kVeryLargeStr(2048, 'B');  // → BufferModel<std::string> (big)

static const std::vector<char> kSmallVec(10, 'v');  // → SmallBuffer

// ═══════════════════════════════════════════════════════════════════════
// Set benchmarks: create Entry with various data sizes
// ═══════════════════════════════════════════════════════════════════════

static void ProxyEntry_set_small(benchmark::State& state)
{
    for (auto _ : state)
    {
        Entry entry;
        entry.set(kSmallStr);
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(ProxyEntry_set_small);

static void LegacyEntry_set_small(benchmark::State& state)
{
    for (auto _ : state)
    {
        bcos::benchmark_legacy::LegacyEntry entry;
        entry.set(kSmallStr);
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(LegacyEntry_set_small);

static void ProxyEntry_set_boundary31(benchmark::State& state)
{
    for (auto _ : state)
    {
        Entry entry;
        entry.set(kBoundaryStr);
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(ProxyEntry_set_boundary31);

static void LegacyEntry_set_boundary31(benchmark::State& state)
{
    for (auto _ : state)
    {
        bcos::benchmark_legacy::LegacyEntry entry;
        entry.set(kBoundaryStr);
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(LegacyEntry_set_boundary31);

static void ProxyEntry_set_fixed32(benchmark::State& state)
{
    for (auto _ : state)
    {
        Entry entry;
        entry.set(kFixed32Str);
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(ProxyEntry_set_fixed32);

static void LegacyEntry_set_fixed32(benchmark::State& state)
{
    for (auto _ : state)
    {
        bcos::benchmark_legacy::LegacyEntry entry;
        entry.set(kFixed32Str);
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(LegacyEntry_set_fixed32);

static void ProxyEntry_set_large(benchmark::State& state)
{
    for (auto _ : state)
    {
        Entry entry;
        entry.set(kLargeStr);
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(ProxyEntry_set_large);

static void LegacyEntry_set_large(benchmark::State& state)
{
    for (auto _ : state)
    {
        bcos::benchmark_legacy::LegacyEntry entry;
        entry.set(kLargeStr);
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(LegacyEntry_set_large);

static void ProxyEntry_set_veryLarge(benchmark::State& state)
{
    for (auto _ : state)
    {
        Entry entry;
        entry.set(kVeryLargeStr);
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(ProxyEntry_set_veryLarge);

static void LegacyEntry_set_veryLarge(benchmark::State& state)
{
    for (auto _ : state)
    {
        bcos::benchmark_legacy::LegacyEntry entry;
        entry.set(kVeryLargeStr);
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(LegacyEntry_set_veryLarge);

// ═══════════════════════════════════════════════════════════════════════
// Set benchmarks: vector<char> input
// ═══════════════════════════════════════════════════════════════════════

static void ProxyEntry_set_smallVec(benchmark::State& state)
{
    for (auto _ : state)
    {
        Entry entry;
        entry.set(kSmallVec);
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(ProxyEntry_set_smallVec);

static void LegacyEntry_set_smallVec(benchmark::State& state)
{
    for (auto _ : state)
    {
        bcos::benchmark_legacy::LegacyEntry entry;
        entry.set(kSmallVec);
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(LegacyEntry_set_smallVec);

// ═══════════════════════════════════════════════════════════════════════
// Get benchmarks: read data back
// ═══════════════════════════════════════════════════════════════════════

static void ProxyEntry_get_small(benchmark::State& state)
{
    Entry entry;
    entry.set(kSmallStr);
    for (auto _ : state)
    {
        auto view = entry.get();
        benchmark::DoNotOptimize(view);
    }
}
BENCHMARK(ProxyEntry_get_small);

static void LegacyEntry_get_small(benchmark::State& state)
{
    bcos::benchmark_legacy::LegacyEntry entry;
    entry.set(kSmallStr);
    for (auto _ : state)
    {
        auto view = entry.get();
        benchmark::DoNotOptimize(view);
    }
}
BENCHMARK(LegacyEntry_get_small);

static void ProxyEntry_get_large(benchmark::State& state)
{
    Entry entry;
    entry.set(kLargeStr);
    for (auto _ : state)
    {
        auto view = entry.get();
        benchmark::DoNotOptimize(view);
    }
}
BENCHMARK(ProxyEntry_get_large);

static void LegacyEntry_get_large(benchmark::State& state)
{
    bcos::benchmark_legacy::LegacyEntry entry;
    entry.set(kLargeStr);
    for (auto _ : state)
    {
        auto view = entry.get();
        benchmark::DoNotOptimize(view);
    }
}
BENCHMARK(LegacyEntry_get_large);

// ═══════════════════════════════════════════════════════════════════════
// Update benchmarks: overwrite existing data
// ═══════════════════════════════════════════════════════════════════════

static void ProxyEntry_update_smallToLarge(benchmark::State& state)
{
    for (auto _ : state)
    {
        Entry entry;
        entry.set(kSmallStr);
        entry.set(kLargeStr);  // overwrite SmallBuffer → BufferModel
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(ProxyEntry_update_smallToLarge);

static void LegacyEntry_update_smallToLarge(benchmark::State& state)
{
    for (auto _ : state)
    {
        bcos::benchmark_legacy::LegacyEntry entry;
        entry.set(kSmallStr);
        entry.set(kLargeStr);
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(LegacyEntry_update_smallToLarge);

static void ProxyEntry_update_sameSize(benchmark::State& state)
{
    for (auto _ : state)
    {
        Entry entry;
        entry.set(kLargeStr);
        entry.set(std::string(200, 'M'));  // same size, different content
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(ProxyEntry_update_sameSize);

static void LegacyEntry_update_sameSize(benchmark::State& state)
{
    for (auto _ : state)
    {
        bcos::benchmark_legacy::LegacyEntry entry;
        entry.set(kLargeStr);
        entry.set(std::string(200, 'M'));
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(LegacyEntry_update_sameSize);

// ═══════════════════════════════════════════════════════════════════════
// Copy benchmarks
// ═══════════════════════════════════════════════════════════════════════

static void ProxyEntry_copy_small(benchmark::State& state)
{
    Entry src;
    src.set(kSmallStr);
    for (auto _ : state)
    {
        Entry copy(src);
        benchmark::DoNotOptimize(copy);
    }
}
BENCHMARK(ProxyEntry_copy_small);

static void LegacyEntry_copy_small(benchmark::State& state)
{
    bcos::benchmark_legacy::LegacyEntry src;
    src.set(kSmallStr);
    for (auto _ : state)
    {
        bcos::benchmark_legacy::LegacyEntry copy(src);
        benchmark::DoNotOptimize(copy);
    }
}
BENCHMARK(LegacyEntry_copy_small);

static void ProxyEntry_copy_large(benchmark::State& state)
{
    Entry src;
    src.set(kLargeStr);
    for (auto _ : state)
    {
        Entry copy(src);
        benchmark::DoNotOptimize(copy);
    }
}
BENCHMARK(ProxyEntry_copy_large);

static void LegacyEntry_copy_large(benchmark::State& state)
{
    bcos::benchmark_legacy::LegacyEntry src;
    src.set(kLargeStr);
    for (auto _ : state)
    {
        bcos::benchmark_legacy::LegacyEntry copy(src);
        benchmark::DoNotOptimize(copy);
    }
}
BENCHMARK(LegacyEntry_copy_large);

// ═══════════════════════════════════════════════════════════════════════
// Move benchmarks
// ═══════════════════════════════════════════════════════════════════════

static void ProxyEntry_move_small(benchmark::State& state)
{
    for (auto _ : state)
    {
        Entry src;
        src.set(kSmallStr);
        Entry dst(std::move(src));
        benchmark::DoNotOptimize(dst);
    }
}
BENCHMARK(ProxyEntry_move_small);

static void LegacyEntry_move_small(benchmark::State& state)
{
    for (auto _ : state)
    {
        bcos::benchmark_legacy::LegacyEntry src;
        src.set(kSmallStr);
        bcos::benchmark_legacy::LegacyEntry dst(std::move(src));
        benchmark::DoNotOptimize(dst);
    }
}
BENCHMARK(LegacyEntry_move_small);

static void ProxyEntry_move_large(benchmark::State& state)
{
    for (auto _ : state)
    {
        Entry src;
        src.set(kLargeStr);
        Entry dst(std::move(src));
        benchmark::DoNotOptimize(dst);
    }
}
BENCHMARK(ProxyEntry_move_large);

static void LegacyEntry_move_large(benchmark::State& state)
{
    for (auto _ : state)
    {
        bcos::benchmark_legacy::LegacyEntry src;
        src.set(kLargeStr);
        bcos::benchmark_legacy::LegacyEntry dst(std::move(src));
        benchmark::DoNotOptimize(dst);
    }
}
BENCHMARK(LegacyEntry_move_large);

// ═══════════════════════════════════════════════════════════════════════
// SharedPtr set benchmarks
// ═══════════════════════════════════════════════════════════════════════

static void ProxyEntry_set_sharedPtr(benchmark::State& state)
{
    auto sp = std::make_shared<std::string>("shared data for benchmark");
    for (auto _ : state)
    {
        Entry entry;
        entry.set(sp);
        benchmark::DoNotOptimize(entry);
    }
}
BENCHMARK(ProxyEntry_set_sharedPtr);

BENCHMARK_MAIN();
