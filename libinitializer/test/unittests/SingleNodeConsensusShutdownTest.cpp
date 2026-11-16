/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */
#include <bcos-framework/testutils/faker/FakeLedger.h>
#include <bcos-single-consensus/SingleNodeConsensus.h>
#include <bcos-task/OneShot.h>
#include <boost/test/unit_test.hpp>
#include <future>

namespace
{
class PendingLedger : public bcos::test::FakeLedger
{
public:
    bcos::task::OneShot<bcos::protocol::BlockNumber>::Ptr numberGate =
        std::make_shared<bcos::task::OneShot<bcos::protocol::BlockNumber>>();
    bcos::task::OneShot<bcos::protocol::Block::Ptr>::Ptr blockGate =
        std::make_shared<bcos::task::OneShot<bcos::protocol::Block::Ptr>>();
    std::promise<void> numberStarted;
    std::promise<void> blockStarted;

    bcos::task::Task<bcos::protocol::BlockNumber> getBlockNumber() override
    {
        numberStarted.set_value();
        auto [blockNumber] = co_await *numberGate;
        co_return blockNumber;
    }

    bcos::task::Task<bcos::crypto::HashType> getBlockHashByNumber(
        bcos::protocol::BlockNumber) override
    {
        co_return bcos::crypto::HashType{};
    }

    bcos::task::Task<bcos::protocol::Block::Ptr> getBlockDataByNumber(
        bcos::protocol::BlockNumber, int32_t) override
    {
        blockStarted.set_value();
        auto [block] = co_await *blockGate;
        co_return block;
    }
};

void checkStopReturns(bcos::single_consensus::SingleNodeConsensus& consensus,
    std::future<void>& started, std::function<void()> completePending)
{
    BOOST_REQUIRE(started.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    auto stop = std::async(std::launch::async, [&consensus] { consensus.stop(); });
    auto status = stop.wait_for(std::chrono::seconds(1));
    completePending();
    if (status != std::future_status::ready)
    {
        stop.wait();
    }
    BOOST_REQUIRE(status == std::future_status::ready);
    stop.get();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(SingleNodeConsensusShutdownTest)

BOOST_AUTO_TEST_CASE(stopInterruptsPendingBlockNumberRead)
{
    bcos::engine::AnyEngineService engine;
    auto ledger = std::make_shared<PendingLedger>();
    auto started = ledger->numberStarted.get_future();
    bcos::single_consensus::SingleNodeConsensus consensus(engine, ledger);
    consensus.start();
    checkStopReturns(consensus, started, [&ledger] { ledger->numberGate->complete(0); });
}

BOOST_AUTO_TEST_CASE(stopInterruptsPendingHeadBlockRead)
{
    bcos::engine::AnyEngineService engine;
    auto ledger = std::make_shared<PendingLedger>();
    ledger->numberGate->complete(0);
    auto started = ledger->blockStarted.get_future();
    bcos::single_consensus::SingleNodeConsensus consensus(engine, ledger);
    consensus.start();
    checkStopReturns(consensus, started, [&ledger] { ledger->blockGate->complete(nullptr); });
}

BOOST_AUTO_TEST_SUITE_END()
