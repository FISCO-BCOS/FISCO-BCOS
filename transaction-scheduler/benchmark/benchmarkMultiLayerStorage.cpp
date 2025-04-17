#include "bcos-framework/transaction-executor/StateKey.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/MultiLayerStorage.h>
#include <benchmark/benchmark.h>
#include <fmt/format.h>

using namespace bcos;
using namespace bcos::storage2::memory_storage;
using namespace bcos::scheduler_v1;

using namespace std::string_view_literals;

struct Fixture
{
    Fixture() : multiLayerStorage(m_backendStorage) {}

    void prepareData(int64_t count, int layer = 0)
    {
        // Write count data
        task::syncWait([this](int64_t count) -> task::Task<void> {
            auto view = fork(multiLayerStorage);
            view.newMutable();
            allKeys = RANGES::views::iota(0, count) | RANGES::views::transform([](int num) {
                auto key = fmt::format("key: {}", num);
                return executor_v1::StateKey{"test_table"sv, std::string_view(key)};
            }) | RANGES::to<decltype(allKeys)>();

            auto allValues = RANGES::views::iota(0, count) | RANGES::views::transform([](int num) {
                storage::Entry entry;
                entry.set(fmt::format("value: {}", num));

                return entry;
            });

            co_await storage2::writeSome(view, ::ranges::views::zip(allKeys, allValues));
            pushView(multiLayerStorage, std::move(view));
        }(count));

        for (auto i = 0; i < layer; ++i)
        {
            auto view = fork(multiLayerStorage);
            view.newMutable();
            pushView(multiLayerStorage, std::move(view));
        }
    }

    using MutableStorage = MemoryStorage<executor_v1::StateKey, executor_v1::StateValue,
        Attribute(ORDERED | LOGICAL_DELETION)>;
    using BackendStorage = MemoryStorage<executor_v1::StateKey, executor_v1::StateValue,
        Attribute(ORDERED | CONCURRENT), std::hash<executor_v1::StateKey>>;

    BackendStorage m_backendStorage;
    MultiLayerStorage<MutableStorage, void, BackendStorage> multiLayerStorage;
    std::vector<bcos::executor_v1::StateKey> allKeys;
};

static void read1(benchmark::State& state)
{
    auto dataCount = state.range(0);
    Fixture fixture;
    fixture.prepareData(dataCount);

    int i = 0;
    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        auto view = fork(fixture.multiLayerStorage);
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
        auto view = fork(fixture.multiLayerStorage);
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

    int i = 0;
    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        auto view = fork(fixture.multiLayerStorage);
        view.newMutable();
        for (auto const& it : state)
        {
            storage::Entry entry;
            entry.set(fmt::format("value: {}", i));
            auto key = fmt::format("key: {}", i);
            co_await storage2::writeOne(view,
                executor_v1::StateKey{"test_table"sv, std::string_view(key)}, std::move(entry));
            ++i;
        }

        co_return;
    }(state));
}


BENCHMARK(read1)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(read10)->Arg(10000)->Arg(100000)->Arg(1000000);
BENCHMARK(write1);

BENCHMARK_MAIN();