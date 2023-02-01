#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/MultiLayerStorage.h>
#include <benchmark/benchmark.h>
#include <fmt/format.h>

using namespace bcos;
using namespace bcos::storage2::memory_storage;
using namespace bcos::transaction_scheduler;

template <>
struct std::hash<bcos::transaction_executor::StateKey>
{
    size_t operator()(const bcos::transaction_executor::StateKey& key) const
    {
        auto const& tableID = std::get<0>(key);
        return std::hash<bcos::transaction_executor::TableNameID>{}(tableID);
    }
};

struct Fixture
{
    Fixture() : levelStorage(backendStorage) {}

    void prepareData(int64_t count, int layer = 0)
    {
        levelStorage.newMutable();
        // Write count data
        task::syncWait([this](int count) -> task::Task<void> {
            allKeys = RANGES::iota_view<int, int>(0, count) |
                      RANGES::views::transform([tableNamePool = &tableNamePool](int num) {
                          return transaction_executor::StateKey{
                              storage2::string_pool::makeStringID(*tableNamePool, "test_table"),
                              fmt::format("key: {}", num)};
                      }) |
                      RANGES::to<decltype(allKeys)>();

            auto allValues =
                RANGES::iota_view<int, int>(0, count) | RANGES::views::transform([](int num) {
                    storage::Entry entry;
                    entry.set(fmt::format("value: {}", num));

                    return entry;
                });

            co_await levelStorage.write(allKeys, allValues);
        }(count));

        for (auto i = 0; i < layer; ++i)
        {
            levelStorage.pushMutableToImmutableFront();
            levelStorage.newMutable();
        }
    }

    using MutableStorage = MemoryStorage<transaction_executor::StateKey,
        transaction_executor::StateValue, Attribute(ORDERED | LOGICAL_DELETION)>;
    using BackendStorage =
        MemoryStorage<transaction_executor::StateKey, transaction_executor::StateValue,
            Attribute(ORDERED | CONCURRENT), std::hash<transaction_executor::StateKey>>;

    transaction_executor::TableNamePool tableNamePool;
    BackendStorage backendStorage;
    MultiLayerStorage<MutableStorage, BackendStorage> levelStorage;
    std::vector<bcos::transaction_executor::StateKey> allKeys;
};

static void read1(benchmark::State& state)
{
    int dataCount = state.range(0);
    Fixture fixture;
    fixture.prepareData(dataCount);

    int i = 0;
    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        for (auto const& it : state)
        {
            [[maybe_unused]] auto data = co_await storage2::readOne(
                fixture.levelStorage, fixture.allKeys[((i++) + dataCount) % dataCount]);
        }

        co_return;
    }(state));
}

static void read10(benchmark::State& state)
{
    int dataCount = state.range(0);
    Fixture fixture;
    fixture.prepareData(dataCount, 10);

    int i = 0;
    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        for (auto const& it : state)
        {
            [[maybe_unused]] auto data = co_await storage2::readOne<transaction_executor::StateKey>(
                fixture.levelStorage, fixture.allKeys[((i++) + dataCount) % dataCount]);
        }

        co_return;
    }(state));
}

static void write1(benchmark::State& state)
{
    Fixture fixture;
    fixture.levelStorage.newMutable();

    int i = 0;
    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        for (auto const& it : state)
        {
            storage::Entry entry;
            entry.set(fmt::format("value: {}", i));
            co_await storage2::writeOne<transaction_executor::StateKey>(fixture.levelStorage,
                transaction_executor::StateKey{
                    storage2::string_pool::makeStringID(fixture.tableNamePool, "test_table"),
                    fmt::format("key: {}", i)},
                std::move(entry));
            ++i;
        }

        co_return;
    }(state));
}

BENCHMARK(read1)->DenseRange(40000, 200000, 40000);
BENCHMARK(read10)->DenseRange(40000, 200000, 40000);
BENCHMARK(write1);

BENCHMARK_MAIN();