#include "bcos-tars-protocol/protocol/BlockImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <benchmark/benchmark.h>

struct Fixture
{
    std::shared_ptr<bcostars::protocol::BlockImpl> block;

    Fixture() : block(std::make_shared<bcostars::protocol::BlockImpl>())
    {
        for (auto i : ::ranges::views::iota(0, 10 * 10000))
        {
            auto transaction = std::make_shared<bcostars::protocol::TransactionImpl>();
            block->appendTransaction(std::move(transaction));
        }
    }
};

static Fixture fixture;

static void testByIndex(benchmark::State& state)
{
    std::atomic_int64_t index = 0;
    auto totalSize = fixture.block->transactionsSize();
    for (auto const& i : state)
    {
        auto tx = fixture.block->transaction(index++ % totalSize);
    }
}

static void testByRange(benchmark::State& state)
{
    std::atomic_int64_t index = 0;
    auto totalSize = fixture.block->transactionsSize();
    auto range = fixture.block->transactions();
    for (auto const& i : state)
    {
        auto tx = range[index++ % totalSize];
    }
}

BENCHMARK(testByIndex)->Threads(1)->Threads(4)->Threads(8);
BENCHMARK(testByRange)->Threads(1)->Threads(4)->Threads(8);

BENCHMARK_MAIN();