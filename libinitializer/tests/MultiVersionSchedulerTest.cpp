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
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace
{
/// Records stop() so the test can observe which slot the dispatcher forwards to.
class RecordingScheduler : public bcos::scheduler::SchedulerInterface
{
public:
    unsigned stopped = 0;

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
    std::array<bcos::scheduler::SchedulerInterface::Ptr, 4> slots{};
    std::shared_ptr<bcos::scheduler_v1::MultiVersionScheduler> dispatcher;

    Ladder()
    {
        std::ranges::transform(
            std::array<std::shared_ptr<RecordingScheduler>, 4>{
                std::make_shared<RecordingScheduler>(), std::make_shared<RecordingScheduler>(),
                std::make_shared<RecordingScheduler>(), std::make_shared<RecordingScheduler>()},
            slots.begin(), [](auto&& slot) { return slot; });
        dispatcher = std::make_shared<bcos::scheduler_v1::MultiVersionScheduler>(
            slots, std::make_shared<bcos::ledger::LedgerConfigState>());
    }
    RecordingScheduler& at(unsigned slot)
    {
        return *static_cast<RecordingScheduler*>(slots.at(slot).get());
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
BOOST_AUTO_TEST_CASE(opRunningSlotIsFrozenAgainstGovernanceWrites)
{
    Ladder ladder;
    ladder.dispatcher->setVersion(bcos::scheduler_v1::OPSTACK_EXECUTOR_VERSION, {});
    BOOST_CHECK_EQUAL(ladder.at(3).stopped, 0u);

    ladder.dispatcher->setVersion(bcos::scheduler_v1::ETHEREUM_EXECUTOR_VERSION, {});
    ladder.dispatcher->setVersion(1, l2FeatureConfig());
    ladder.dispatcher->stop();
    BOOST_CHECK_EQUAL(ladder.at(3).stopped, 1u);  // still the OP slot
    BOOST_CHECK_EQUAL(ladder.at(2).stopped, 0u);
    BOOST_CHECK_EQUAL(ladder.at(1).stopped, 0u);
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
    ladder.dispatcher->stop();
    BOOST_CHECK_EQUAL(ladder.at(1).stopped, 1u);  // switched: the flag is not a freeze key
    BOOST_CHECK_EQUAL(ladder.at(2).stopped, 0u);
}

BOOST_AUTO_TEST_SUITE_END()
