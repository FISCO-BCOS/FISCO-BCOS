/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 *  @file MultiVersionSchedulerTest.cpp
 *  @brief Pin the setVersion guard's keying: the OP freeze reads the RUNNING slot,
 *         never feature_l2_ethereum_compat (that flag is the ledger's L2 state shape
 *         and the Eth lane may carry it too).
 */

#define BOOST_TEST_MODULE MultiVersionSchedulerTest

#include "MultiVersionScheduler.h"
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace
{
/// Records stop() and status() so the test can observe both contracts: which slots the
/// shutdown sweep reaches, and which single slot the dispatcher forwards traffic to.
class RecordingScheduler : public bcos::scheduler::SchedulerInterface
{
public:
    unsigned stopped = 0;
    unsigned routed = 0;

    void executeBlock(bcos::protocol::Block::Ptr, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> callback)
        override
    {
        callback(nullptr, nullptr, false);
    }
    void commitBlock(bcos::protocol::BlockHeader::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback) override
    {
        callback(nullptr, nullptr);
    }
    void status(
        std::function<void(bcos::Error::Ptr, bcos::protocol::Session::ConstPtr)> callback) override
    {
        ++routed;
        callback(nullptr, nullptr);
    }
    void call(bcos::protocol::Transaction::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr)> callback)
        override
    {
        callback(nullptr, nullptr);
    }
    void reset(std::function<void(bcos::Error::Ptr)> callback) override { callback(nullptr); }
    void getCode(
        std::string_view, std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) override
    {
        callback(nullptr, {});
    }
    void getABI(
        std::string_view, std::function<void(bcos::Error::Ptr, std::string)> callback) override
    {
        callback(nullptr, {});
    }
    bcos::task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, bcos::protocol::BlockNumber) override
    {
        co_return std::nullopt;
    }
    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(bcos::Error::Ptr)> callback) override
    {
        callback(nullptr);
    }
    void stop() override { ++stopped; }
};

struct Ladder
{
    /// A slot the node does not wire at all. Initializer publishes a null slot 3 on non-OP
    /// nodes (there is no OP scheduler to build), so the dispatcher has to tolerate it.
    static constexpr unsigned c_unwiredSlot3 = 3;

    std::array<bcos::scheduler::SchedulerInterface::Ptr, 4> slots{};
    std::shared_ptr<bcos::scheduler_v1::MultiVersionScheduler> dispatcher;

    explicit Ladder(std::optional<unsigned> unwiredSlot = std::nullopt)
    {
        std::ranges::transform(
            std::array<std::shared_ptr<RecordingScheduler>, 4>{
                std::make_shared<RecordingScheduler>(), std::make_shared<RecordingScheduler>(),
                std::make_shared<RecordingScheduler>(), std::make_shared<RecordingScheduler>()},
            slots.begin(), [](auto&& slot) { return slot; });
        if (unwiredSlot)
        {
            slots.at(*unwiredSlot) = nullptr;
        }
        dispatcher = std::make_shared<bcos::scheduler_v1::MultiVersionScheduler>(
            slots, std::make_shared<bcos::ledger::LedgerConfigState>());
    }
    RecordingScheduler& at(unsigned slot)
    {
        return *static_cast<RecordingScheduler*>(slots.at(slot).get());
    }
    /// The slot the dispatcher forwards traffic to, observed without knowing m_currentIndex.
    static void routeOnce(Ladder& ladder)
    {
        ladder.dispatcher->status([](bcos::Error::Ptr, bcos::protocol::Session::ConstPtr) {});
    }
};

bcos::ledger::LedgerConfig::Ptr l2FeatureConfig()
{
    auto config = std::make_shared<bcos::ledger::LedgerConfig>();
    bcos::ledger::Features features;
    features.set(bcos::ledger::Features::Flag::feature_l2_ethereum_compat);
    config->setFeatures(features);
    return config;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(MultiVersionSchedulerTest)

// A chain RUNNING the OP executor must not be moved off it by a governance write: the
// freeze keys on the running slot, with or without the L2 feature flag in the config.
// Observed through the slot that serves traffic (status()), not through stop(): the shutdown
// sweep deliberately reaches every WIRED slot (see stopStopsEveryWiredSlotAndSkipsUnwiredOnes),
// so a per-slot stop() count cannot say which slot is current.
BOOST_AUTO_TEST_CASE(opRunningSlotIsFrozenAgainstGovernanceWrites)
{
    Ladder ladder;
    ladder.dispatcher->setVersion(bcos::scheduler_v1::OPSTACK_EXECUTOR_VERSION, {});

    ladder.dispatcher->setVersion(bcos::scheduler_v1::ETHEREUM_EXECUTOR_VERSION, {});
    ladder.dispatcher->setVersion(1, l2FeatureConfig());
    Ladder::routeOnce(ladder);
    BOOST_CHECK_EQUAL(ladder.at(3).routed, 1u);  // still the OP slot
    BOOST_CHECK_EQUAL(ladder.at(2).routed, 0u);
    BOOST_CHECK_EQUAL(ladder.at(1).routed, 0u);
}

// The discriminator for the guard's keying: an Eth-lane chain may legitimately carry
// feature_l2_ethereum_compat (it is the ledger's L2 state shape, not an OP-mode marker).
// Flag-keyed freezing (the pre-fix guard) froze and mislabelled such a chain on an
// ordinary executor_version switch; slot-keyed freezing must let it switch.
BOOST_AUTO_TEST_CASE(ethLaneL2FeatureDoesNotFreezeVersionSwitches)
{
    Ladder ladder;
    ladder.dispatcher->setVersion(bcos::scheduler_v1::ETHEREUM_EXECUTOR_VERSION, {});

    ladder.dispatcher->setVersion(1, l2FeatureConfig());
    Ladder::routeOnce(ladder);
    BOOST_CHECK_EQUAL(ladder.at(1).routed, 1u);  // switched: the flag is not a freeze key
    BOOST_CHECK_EQUAL(ladder.at(2).routed, 0u);
}

// The shutdown sweep reaches every wired slot — an inactive slot left running keeps
// dereferencing the shared MPT commit observer that Initializer::stop() is about to drop —
// and skips the slots the node never wired. A null slot 3 is the normal shape on a non-OP
// node, and calling stop() through it is a virtual call on address 0: this case is the
// regression pin for the shutdown crash that the sweep's missing null check produced.
BOOST_AUTO_TEST_CASE(stopStopsEveryWiredSlotAndSkipsUnwiredOnes)
{
    Ladder wired;
    wired.dispatcher->stop();
    BOOST_CHECK_EQUAL(wired.at(3).stopped, 1u);
    BOOST_CHECK_EQUAL(wired.at(2).stopped, 1u);
    BOOST_CHECK_EQUAL(wired.at(1).stopped, 1u);
    BOOST_CHECK_EQUAL(wired.at(0).stopped, 1u);

    Ladder nonOp(Ladder::c_unwiredSlot3);
    BOOST_REQUIRE(nonOp.slots.at(Ladder::c_unwiredSlot3) == nullptr);
    nonOp.dispatcher->setVersion(bcos::scheduler_v1::ETHEREUM_EXECUTOR_VERSION, {});
    nonOp.dispatcher->stop();  // must not dereference the unwired slot
    BOOST_CHECK_EQUAL(nonOp.at(2).stopped, 1u);
    BOOST_CHECK_EQUAL(nonOp.at(1).stopped, 1u);
    BOOST_CHECK_EQUAL(nonOp.at(0).stopped, 1u);
}

BOOST_AUTO_TEST_SUITE_END()
