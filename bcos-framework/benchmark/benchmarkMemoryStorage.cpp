#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/AnyStorage.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/StringPool.h>
#include <bcos-task/Wait.h>
#include <benchmark/benchmark.h>
#include <fmt/format.h>
#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_map.h>
#include <tbb/concurrent_unordered_map.h>
#include <transaction-executor/TransactionExecutor.h>
#include <boost/container_hash/hash_fwd.hpp>
#include <any>
#include <variant>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::storage2::memory_storage;
using namespace bcos::transaction_executor;
using namespace bcos::storage2::string_pool;
using namespace bcos::storage2::any_storage;

using Key = StateKey;

struct Fixture
{
    Fixture() = default;

    void prepareData(int64_t count)
    {
        allKeys.clear();
        allValues.clear();
        for (auto i : RANGES::views::iota(0, count))
        {
            auto tableName = fmt::format("Table-{}", i % 1000);  // All 1000 tables
            auto key = fmt::format("Key-{}", i);
            allKeys.emplace_back(makeStringID(stringPool, tableName), key);

            storage::Entry entry;
            entry.set(fmt::format("value is {}", i));
            allValues.emplace_back(std::move(entry));
        }
    }

    std::vector<Key> allKeys;
    std::vector<storage::Entry> allValues;
    FixedStringPool stringPool;
};

void setCapacityForMRU(auto& storage)
{
    if constexpr (std::is_same_v<std::remove_cvref_t<decltype(storage)>,
                      MemoryStorage<Key, storage::Entry,
                          memory_storage::Attribute(ORDERED | CONCURRENT | MRU), std::hash<Key>>> ||
                  std::is_same_v<std::remove_cvref_t<decltype(storage)>,
                      MemoryStorage<Key, storage::Entry,
                          memory_storage::Attribute(CONCURRENT | MRU), std::hash<Key>>>)
    {
        storage.setMaxCapacity(1000 * 1000 * 1000);
    }
}

Fixture fixture;
std::variant<MemoryStorage<Key, storage::Entry, memory_storage::Attribute(ORDERED)>,
    MemoryStorage<Key, storage::Entry, memory_storage::Attribute(ORDERED | CONCURRENT),
        std::hash<Key>>,
    MemoryStorage<Key, storage::Entry, memory_storage::Attribute(ORDERED | CONCURRENT | MRU),
        std::hash<Key>>,
    MemoryStorage<Key, storage::Entry>,
    MemoryStorage<Key, storage::Entry, memory_storage::Attribute(CONCURRENT), std::hash<Key>>,
    MemoryStorage<Key, storage::Entry, memory_storage::Attribute(CONCURRENT | MRU), std::hash<Key>>>
    allStorage;
std::optional<AnyStorage<Key, storage::Entry,
    any_storage::Attribute(any_storage::READABLE | any_storage::WRITEABLE)>>
    anyStorageView;

template <class Storage>
static void read(benchmark::State& state)
{
    if (state.thread_index() == 0)
    {
        fixture.prepareData(state.range(0));
        allStorage.emplace<Storage>();

        task::syncWait([&]() -> task::Task<void> {
            co_await std::visit(
                [&](auto& storage) -> task::Task<void> {
                    setCapacityForMRU(storage);
                    co_await storage.write(fixture.allKeys, fixture.allValues);
                },
                allStorage);
        }());
    }

    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        co_await std::visit(
            [&](auto& storage) -> task::Task<void> {
                int i = (state.range(0) / state.threads()) * state.thread_index();
                for (auto const& it : state)
                {
                    auto itAwaitable = co_await storage.read(storage2::singleView(
                        fixture.allKeys[(i + fixture.allKeys.size()) % fixture.allKeys.size()]));
                    co_await itAwaitable.next();
                    [[maybe_unused]] auto& value = co_await itAwaitable.value();
                    // [[maybe_unused]] auto value = co_await storage2::readOne(storage,
                    //     fixture.allKeys[(i + fixture.allKeys.size()) % fixture.allKeys.size()]);

                    ++i;
                }
                co_return;
            },
            allStorage);

        co_return;
    }(state));
}

template <class Storage>
static void readAny(benchmark::State& state)
{
    if (state.thread_index() == 0)
    {
        fixture.prepareData(state.range(0));
        allStorage.emplace<Storage>();

        task::syncWait([&]() -> task::Task<void> {
            co_await std::visit(
                [&](auto& storage) -> task::Task<void> {
                    setCapacityForMRU(storage);
                    co_await storage.write(fixture.allKeys, fixture.allValues);
                },
                allStorage);
        }());
    }

    std::visit([&](auto& storage) { anyStorageView.emplace(storage); }, allStorage);
    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        int i = (state.range(0) / state.threads()) * state.thread_index();
        for (auto const& it : state)
        {
            auto itAwaitable = co_await anyStorageView->read(storage2::singleView(
                fixture.allKeys[(i + fixture.allKeys.size()) % fixture.allKeys.size()]));
            co_await itAwaitable.next();
            [[maybe_unused]] auto value = co_await itAwaitable.value();

            ++i;
        }

        co_return;
    }(state));
}

template <class Storage>
static void write(benchmark::State& state)
{
    if (state.thread_index() == 0)
    {
        fixture.prepareData(1000 * 10000);
        allStorage.emplace<Storage>();
    }

    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        co_await std::visit(
            [&](auto& storage) -> task::Task<void> {
                setCapacityForMRU(storage);

                auto i = 100 * 10000 * state.thread_index();
                for (auto const& it : state)
                {
                    auto index = (i + fixture.allKeys.size()) % fixture.allKeys.size();
                    co_await storage.write(storage2::singleView(fixture.allKeys[index]),
                        storage2::singleView(fixture.allValues[index]));
                    ++i;
                }
                co_return;
            },
            allStorage);

        co_return;
    }(state));
}

struct StateKeyHash
{
    static size_t hash(const StateKey& key) { return std::hash<StateKey>{}(key); }
    static bool equal(const StateKey& lhs, const StateKey& rhs) { return lhs == rhs; }
};
tbb::concurrent_hash_map<Key, storage::Entry, StateKeyHash> tbbHashMap;
static void readTBBHashMap(benchmark::State& state)
{
    if (state.thread_index() == 0)
    {
        fixture.prepareData(state.range(0));
        tbbHashMap.clear();

        for (auto&& [key, value] : RANGES::views::zip(fixture.allKeys, fixture.allValues))
        {
            tbbHashMap.emplace(key, value);
        }
    }

    int i = (tbbHashMap.size() / state.threads()) * state.thread_index();
    for (auto const& _ : state)
    {
        decltype(tbbHashMap)::const_accessor it;
        tbbHashMap.find(it, fixture.allKeys[(i + fixture.allKeys.size()) % fixture.allKeys.size()]);
        ++i;
    }
}

tbb::concurrent_unordered_map<Key, storage::Entry, std::hash<StateKey>> tbbUnorderedMap;
static void readTBBUnorderedMap(benchmark::State& state)
{
    if (state.thread_index() == 0)
    {
        fixture.prepareData(state.range(0));
        tbbUnorderedMap.clear();

        for (auto&& [key, value] : RANGES::views::zip(fixture.allKeys, fixture.allValues))
        {
            tbbUnorderedMap.emplace(key, value);
        }
    }

    int i = (tbbUnorderedMap.size() / state.threads()) * state.thread_index();
    for (auto const& _ : state)
    {
        [[maybe_unused]] auto it = tbbUnorderedMap.find(
            fixture.allKeys[(i + fixture.allKeys.size()) % fixture.allKeys.size()]);
        ++i;
    }
}

tbb::concurrent_map<Key, storage::Entry> tbbMap;
static void readTBBMap(benchmark::State& state)
{
    if (state.thread_index() == 0)
    {
        fixture.prepareData(state.range(0));
        tbbMap.clear();

        for (auto&& [key, value] : RANGES::views::zip(fixture.allKeys, fixture.allValues))
        {
            tbbMap.emplace(key, value);
        }
    }

    int i = (tbbMap.size() / state.threads()) * state.thread_index();
    for (auto const& _ : state)
    {
        [[maybe_unused]] auto it =
            tbbMap.find(fixture.allKeys[(i + fixture.allKeys.size()) % fixture.allKeys.size()]);
        ++i;
    }
}

BENCHMARK(readTBBHashMap)->Arg(100000)->Arg(1000000)->Threads(1)->Threads(8);
BENCHMARK(readTBBUnorderedMap)->Arg(100000)->Arg(1000000)->Threads(1)->Threads(8);
BENCHMARK(readTBBMap)->Arg(100000)->Arg(1000000)->Threads(1)->Threads(8);
BENCHMARK(read<MemoryStorage<Key, storage::Entry, memory_storage::Attribute(ORDERED)>>)
    ->Arg(100000)
    ->Arg(1000000);
BENCHMARK(read<MemoryStorage<Key, storage::Entry, memory_storage::Attribute(ORDERED | CONCURRENT),
              std::hash<Key>>>)
    ->Arg(100000)
    ->Arg(1000000)
    ->Threads(1)
    ->Threads(8);
BENCHMARK(read<MemoryStorage<Key, storage::Entry,
              memory_storage::Attribute(ORDERED | CONCURRENT | MRU), std::hash<Key>>>)
    ->Arg(100000)
    ->Arg(1000000)
    ->Threads(1)
    ->Threads(8);
BENCHMARK(read<MemoryStorage<Key, storage::Entry>>)->Arg(100000)->Arg(1000000);
BENCHMARK(
    read<MemoryStorage<Key, storage::Entry, memory_storage::Attribute(CONCURRENT), std::hash<Key>>>)
    ->Arg(100000)
    ->Arg(1000000)
    ->Threads(1)
    ->Threads(8);

BENCHMARK(readAny<MemoryStorage<Key, storage::Entry, memory_storage::Attribute(ORDERED)>>)
    ->Arg(100000)
    ->Arg(1000000);
BENCHMARK(readAny<MemoryStorage<Key, storage::Entry,
              memory_storage::Attribute(ORDERED | CONCURRENT), std::hash<Key>>>)
    ->Arg(100000)
    ->Arg(1000000)
    ->Threads(1)
    ->Threads(8);
BENCHMARK(readAny<MemoryStorage<Key, storage::Entry,
              memory_storage::Attribute(ORDERED | CONCURRENT | MRU), std::hash<Key>>>)
    ->Arg(100000)
    ->Arg(1000000)
    ->Threads(1)
    ->Threads(8);
BENCHMARK(readAny<MemoryStorage<Key, storage::Entry>>)->Arg(100000)->Arg(1000000);
BENCHMARK(
    readAny<
        MemoryStorage<Key, storage::Entry, memory_storage::Attribute(CONCURRENT), std::hash<Key>>>)
    ->Arg(100000)
    ->Arg(1000000)
    ->Threads(1)
    ->Threads(8);

BENCHMARK(write<MemoryStorage<Key, storage::Entry, memory_storage::Attribute(ORDERED)>>);
BENCHMARK(write<MemoryStorage<Key, storage::Entry, memory_storage::Attribute(ORDERED | CONCURRENT),
              std::hash<Key>>>)
    ->Threads(1)
    ->Threads(8);
BENCHMARK(write<MemoryStorage<Key, storage::Entry>>);
BENCHMARK(
    write<
        MemoryStorage<Key, storage::Entry, memory_storage::Attribute(CONCURRENT), std::hash<Key>>>)
    ->Threads(1)
    ->Threads(8);
BENCHMARK(read<MemoryStorage<Key, storage::Entry, memory_storage::Attribute(CONCURRENT | MRU),
              std::hash<Key>>>)
    ->Arg(100000)
    ->Arg(1000000)
    ->Threads(1)
    ->Threads(8);

BENCHMARK_MAIN();