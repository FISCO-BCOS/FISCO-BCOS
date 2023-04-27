#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/MultiLayerStorage.h>
#include <benchmark/benchmark.h>
#include <fmt/format.h>

using namespace bcos;
using namespace bcos::storage2::memory_storage;
using namespace bcos::transaction_scheduler;

struct TableNameHash
{
    size_t operator()(const bcos::transaction_executor::StateKey& key) const
    {
        auto const& tableID = std::get<0>(key);
        return std::hash<bcos::transaction_executor::TableNameID>{}(tableID);
    }
};

struct Fixture
{
    Fixture() : multiLayerStorage(m_backendStorage) {}

    void prepareData(int64_t count, int layer = 0)
    {
        multiLayerStorage.newMutable();

        // Write count data
        task::syncWait([this](int64_t count) -> task::Task<void> {
            auto view = multiLayerStorage.fork(true);
            allKeys = RANGES::views::iota(0, count) |
                      RANGES::views::transform([tableNamePool = &m_tableNamePool](int num) {
                          return transaction_executor::StateKey{
                              storage2::string_pool::makeStringID(*tableNamePool, "test_table"),
                              fmt::format("key: {}", num)};
                      }) |
                      RANGES::to<decltype(allKeys)>();

            auto allValues = RANGES::views::iota(0, count) | RANGES::views::transform([](int num) {
                storage::Entry entry;
                entry.set(fmt::format("value: {}", num));

                return entry;
            });

            co_await view.write(allKeys, allValues);
        }(count));

        for (auto i = 0; i < layer; ++i)
        {
            multiLayerStorage.pushMutableToImmutableFront();
            multiLayerStorage.newMutable();
        }
    }

    using MutableStorage = MemoryStorage<transaction_executor::StateKey,
        transaction_executor::StateValue, Attribute(ORDERED | LOGICAL_DELETION)>;
    using BackendStorage = MemoryStorage<transaction_executor::StateKey,
        transaction_executor::StateValue, Attribute(ORDERED | CONCURRENT), TableNameHash>;

    transaction_executor::TableNamePool m_tableNamePool;
    BackendStorage m_backendStorage;
    MultiLayerStorage<MutableStorage, void, BackendStorage> multiLayerStorage;
    std::vector<bcos::transaction_executor::StateKey> allKeys;
};

static void read1(benchmark::State& state)
{
    auto dataCount = state.range(0);
    Fixture fixture;
    fixture.prepareData(dataCount);

    int i = 0;
    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        auto view = fixture.multiLayerStorage.fork(false);
        for (auto const& it : state)
        {
            [[maybe_unused]] auto data =
                co_await storage2::readOne(view, fixture.allKeys[(i + dataCount) % dataCount]);
            ++i;
        }

        co_return;
    }(state));
}

static void read10(benchmark::State& state)
{
    auto dataCount = state.range(0);
    Fixture fixture;
    fixture.prepareData(dataCount, 10);

    int i = 0;
    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        auto view = fixture.multiLayerStorage.fork(false);
        for (auto const& it : state)
        {
            [[maybe_unused]] auto data =
                co_await storage2::readOne(view, fixture.allKeys[(i + dataCount) % dataCount]);
            ++i;
        }

        co_return;
    }(state));
}

static void write1(benchmark::State& state)
{
    Fixture fixture;
    fixture.multiLayerStorage.newMutable();

    int i = 0;
    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        auto view = fixture.multiLayerStorage.fork(true);
        for (auto const& it : state)
        {
            storage::Entry entry;
            entry.set(fmt::format("value: {}", i));
            co_await storage2::writeOne(view,
                transaction_executor::StateKey{
                    storage2::string_pool::makeStringID(fixture.m_tableNamePool, "test_table"),
                    fmt::format("key: {}", i)},
                std::move(entry));
            ++i;
        }

        co_return;
    }(state));
}


BENCHMARK(read1)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(read10)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(write1);

BENCHMARK_MAIN();