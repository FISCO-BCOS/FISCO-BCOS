#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-task/Wait.h>
#include <benchmark/benchmark.h>
#include <fmt/format.h>
#include <tbb/concurrent_hash_map.h>

using namespace bcos;
using namespace bcos::storage2::memory_storage;

struct Fixture
{
    Fixture() = default;

    void prepareData(int64_t count)
    {
        for (auto i : RANGES::iota_view<int, int>(0, count))
        {
            allKeys.emplace_back(i);

            storage::Entry entry;
            entry.set(fmt::format("value is {}", i));
            allValues.emplace_back(std::move(entry));
        }
    }

    std::vector<int> allKeys;
    std::vector<storage::Entry> allValues;
};

template <class Storage>
static void read(benchmark::State& state)
{
    Fixture fixture;
    fixture.prepareData(state.range(0));
    Storage storage;

    int i = 0;
    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        co_await storage.write(fixture.allKeys, fixture.allValues);

        int i = 0;
        for (auto const& it : state)
        {
            [[maybe_unused]] auto value = co_await storage2::readOne(
                storage, fixture.allKeys[(i + fixture.allKeys.size()) % fixture.allKeys.size()]);
        }
        co_return;
    }(state));
}

static void readTBBHashMap(benchmark::State& state)
{
    Fixture fixture;
    fixture.prepareData(state.range(0));

    tbb::concurrent_hash_map<int, storage::Entry> storage;
    for (auto&& [key, value] : RANGES::zip_view(fixture.allKeys, fixture.allValues))
    {
        storage.emplace(key, value);
    }

    int i = 0;
    for (auto const& _ : state)
    {
        decltype(storage)::const_accessor it;
        storage.find(it, fixture.allKeys[(i + fixture.allKeys.size()) % fixture.allKeys.size()]);
    }
}

BENCHMARK(read<MemoryStorage<int, storage::Entry, Attribute(ORDERED)>>)
    ->DenseRange(40000, 200000, 40000);
BENCHMARK(read<MemoryStorage<int, storage::Entry, Attribute(ORDERED | CONCURRENT), std::hash<int>>>)
    ->DenseRange(40000, 200000, 40000);
BENCHMARK(
    read<MemoryStorage<int, storage::Entry, Attribute(ORDERED | CONCURRENT | MRU), std::hash<int>>>)
    ->DenseRange(40000, 200000, 40000);
BENCHMARK(read<MemoryStorage<int, storage::Entry>>)->DenseRange(40000, 200000, 40000);
BENCHMARK(read<MemoryStorage<int, storage::Entry, Attribute(CONCURRENT), std::hash<int>>>)
    ->DenseRange(40000, 200000, 40000);
BENCHMARK(read<MemoryStorage<int, storage::Entry, Attribute(CONCURRENT | MRU), std::hash<int>>>)
    ->DenseRange(40000, 200000, 40000);
BENCHMARK(readTBBHashMap)->DenseRange(40000, 200000, 40000);

BENCHMARK_MAIN();