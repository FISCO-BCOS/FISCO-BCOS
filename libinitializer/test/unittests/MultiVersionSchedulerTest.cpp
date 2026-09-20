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
 * @file MultiVersionSchedulerTest.cpp
 * @brief Slot selection of MultiVersionScheduler with an unwired OP slot, plus the OP
 * running-slot freeze keying pinned by the karst integration.
 */
#include "libinitializer/MultiVersionScheduler.h"
#include <boost/test/unit_test.hpp>
#include <array>
#include <memory>

using namespace bcos;
using namespace bcos::scheduler_v1;

namespace
{
bcos::ledger::LedgerConfig::Ptr l2FeatureConfig()
{
    auto config = std::make_shared<bcos::ledger::LedgerConfig>();
    bcos::ledger::Features features;
    features.set(bcos::ledger::Features::Flag::feature_l2_ethereum_compat);
    config->setFeatures(features);
    return config;
}

/// Records which forwarded call reached this slot.
class RecordingScheduler : public bcos::scheduler::SchedulerInterface
{
public:
    explicit RecordingScheduler(int tag) : m_tag(tag) {}

    int m_tag;
    int m_callAtBlockCount = 0;
    int m_adoptCount = 0;
    int m_stopCount = 0;
    int m_executeCount = 0;

    void executeBlock(bcos::protocol::Block::Ptr, bool,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> callback)
        override
    {
        ++m_executeCount;
        callback(nullptr, nullptr, false);
    }
    void commitBlock(bcos::protocol::BlockHeader::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)>) override
    {}
    void status(std::function<void(Error::Ptr, bcos::protocol::Session::ConstPtr)>) override {}
    void call(protocol::Transaction::Ptr,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)>) override
    {}
    void callAtBlock(protocol::Transaction::Ptr, protocol::BlockNumber,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)>) override
    {
        ++m_callAtBlockCount;
    }
    void adoptProbeAsPending(bcos::protocol::Block::Ptr,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)>) override
    {
        ++m_adoptCount;
    }
    void stop() override { ++m_stopCount; }
    void reset(std::function<void(Error::Ptr)>) override {}
    void getCode(std::string_view, std::function<void(Error::Ptr, bcos::bytes)>) override {}
    void getABI(std::string_view, std::function<void(Error::Ptr, std::string)>) override {}
    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view, std::string_view, bcos::protocol::BlockNumber) override
    {
        co_return std::nullopt;
    }
    void preExecuteBlock(bcos::protocol::Block::Ptr, bool, std::function<void(Error::Ptr)>) override
    {}
};

struct Fixture
{
    std::array<std::shared_ptr<RecordingScheduler>, 4> slots{
        std::make_shared<RecordingScheduler>(0), std::make_shared<RecordingScheduler>(1),
        std::make_shared<RecordingScheduler>(2), std::make_shared<RecordingScheduler>(3)};

    /// @param wireOpSlot false leaves slot 3 null, the shape of a non-OP node.
    std::shared_ptr<MultiVersionScheduler> make(bool wireOpSlot)
    {
        return std::make_shared<MultiVersionScheduler>(
            std::to_array<bcos::scheduler::SchedulerInterface::Ptr>({slots[0], slots[1], slots[2],
                wireOpSlot ? bcos::scheduler::SchedulerInterface::Ptr(slots[3]) : nullptr}),
            std::make_shared<ledger::LedgerConfigState>());
    }
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(MultiVersionSchedulerTest, Fixture)

// From 3.18.0 a governance tx naming executor_version=3 is refused by SystemConfigPrecompiled
// (OP mode is a genesis property), so this path is the second line of defence: it still has to
// hold for a build that did not wire an in-range slot and for replay of a pre-3.18.0 block that
// already wrote such a value. It must not throw -- the runtime callers catch-and-log and would
// then stop advancing the chain.
BOOST_AUTO_TEST_CASE(setVersionKeepsCurrentIndexOnUnwiredSlot)
{
    auto scheduler = make(false);
    scheduler->setVersion(ETHEREUM_EXECUTOR_VERSION, {});
    BOOST_CHECK_NO_THROW(scheduler->setVersion(OPSTACK_EXECUTOR_VERSION, {}));

    scheduler->callAtBlock(nullptr, 0, {});
    BOOST_CHECK_EQUAL(slots[2]->m_callAtBlockCount, 1);
    BOOST_CHECK_EQUAL(slots[3]->m_callAtBlockCount, 0);
}

BOOST_AUTO_TEST_CASE(setVersionSelectsWiredOpSlot)
{
    auto scheduler = make(true);
    scheduler->setVersion(OPSTACK_EXECUTOR_VERSION, {});

    scheduler->callAtBlock(nullptr, 0, {});
    BOOST_CHECK_EQUAL(slots[3]->m_callAtBlockCount, 1);
}

// Above the array: saturate down to the newest NON-NULL slot, not to the empty one.
BOOST_AUTO_TEST_CASE(setVersionSaturatesToNewestWiredSlot)
{
    auto unwired = make(false);
    unwired->setVersion(99, {});
    unwired->callAtBlock(nullptr, 0, {});
    BOOST_CHECK_EQUAL(slots[2]->m_callAtBlockCount, 1);

    Fixture other;
    auto wired = other.make(true);
    wired->setVersion(99, {});
    wired->callAtBlock(nullptr, 0, {});
    BOOST_CHECK_EQUAL(other.slots[3]->m_callAtBlockCount, 1);
}

BOOST_AUTO_TEST_CASE(schedulerAccessorThrowsOnUnwiredSlot)
{
    auto scheduler = make(false);
    BOOST_CHECK_THROW(scheduler->scheduler(OPSTACK_EXECUTOR_VERSION), ExecutorVersionNotSupported);
    BOOST_CHECK_THROW(scheduler->scheduler(-1), ExecutorVersionNotSupported);
    BOOST_CHECK_THROW(scheduler->scheduler(99), ExecutorVersionNotSupported);
    BOOST_CHECK_NO_THROW(scheduler->scheduler(ETHEREUM_EXECUTOR_VERSION));
}

// Without the forwarders these would hit SchedulerInterface's defaults (call / executeBlock)
// instead of the selected scheduler's own implementation.
BOOST_AUTO_TEST_CASE(callAtBlockAndAdoptProbeReachSelectedScheduler)
{
    auto scheduler = make(true);
    scheduler->setVersion(OPSTACK_EXECUTOR_VERSION, {});

    scheduler->callAtBlock(nullptr, 7, {});
    scheduler->adoptProbeAsPending(nullptr, {});
    BOOST_CHECK_EQUAL(slots[3]->m_callAtBlockCount, 1);
    BOOST_CHECK_EQUAL(slots[3]->m_adoptCount, 1);
    BOOST_CHECK_EQUAL(slots[2]->m_callAtBlockCount, 0);
    BOOST_CHECK_EQUAL(slots[2]->m_adoptCount, 0);
}

// The shutdown sweep reaches every WIRED slot and tolerates the unwired one. Initializer
// publishes a null slot for a lane this node did not wire -- the normal shape of slot 3 on a
// non-OP node -- and calling through that null pointer is a virtual call on address 0, on the
// shutdown path, before the observer teardown the sweep exists to protect. The inactive wired
// slots still have to be stopped: each holds the shared MPT commit observer and stop() is what
// detaches it.
BOOST_AUTO_TEST_CASE(stopSweepsEveryWiredSlotAndSkipsTheUnwiredOne)
{
    auto wired = make(true);
    wired->stop();
    BOOST_CHECK_EQUAL(slots[0]->m_stopCount, 1);
    BOOST_CHECK_EQUAL(slots[1]->m_stopCount, 1);
    BOOST_CHECK_EQUAL(slots[2]->m_stopCount, 1);
    BOOST_CHECK_EQUAL(slots[3]->m_stopCount, 1);

    Fixture nonOpNode;  // a non-OP node: slot 3 is null
    auto unwired = nonOpNode.make(false);
    unwired->stop();
    BOOST_CHECK_EQUAL(nonOpNode.slots[0]->m_stopCount, 1);
    BOOST_CHECK_EQUAL(nonOpNode.slots[1]->m_stopCount, 1);
    BOOST_CHECK_EQUAL(nonOpNode.slots[2]->m_stopCount, 1);
    BOOST_CHECK_EQUAL(nonOpNode.slots[3]->m_stopCount, 0);
}

// A chain RUNNING the OP executor must not be moved off it by a governance write: the
// freeze keys on the running slot, with or without the L2 feature flag in the config.
BOOST_AUTO_TEST_CASE(opRunningSlotIsFrozenAgainstGovernanceWrites)
{
    auto scheduler = make(true);
    scheduler->setVersion(OPSTACK_EXECUTOR_VERSION, {});
    scheduler->setVersion(ETHEREUM_EXECUTOR_VERSION, {});
    scheduler->setVersion(1, l2FeatureConfig());

    // Both governance writes were rejected: execution still routes to the OP slot.
    scheduler->executeBlock(
        {}, false, [](bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool) {});
    BOOST_CHECK_EQUAL(slots[3]->m_executeCount, 1);
    BOOST_CHECK_EQUAL(slots[2]->m_executeCount, 0);
    BOOST_CHECK_EQUAL(slots[1]->m_executeCount, 0);
    // stop() tears down every WIRED slot (see stopSweepsEveryWiredSlotAndSkipsTheUnwiredOne),
    // so stop counts do not discriminate the freeze either.
    scheduler->stop();
    BOOST_CHECK_EQUAL(slots[3]->m_stopCount, 1);
    BOOST_CHECK_EQUAL(slots[2]->m_stopCount, 1);
    BOOST_CHECK_EQUAL(slots[1]->m_stopCount, 1);
}

// A requested version ABOVE the OP slot while running OP is not a "move off": isOpLaneVersion
// holds on both sides of the freeze, so the request falls through to the saturation arm and
// lands back on the OP slot — execution stays on slot 3 through saturation, not rejection.
BOOST_AUTO_TEST_CASE(opRunningSlotSaturatesRequestsAboveTheNewestLane)
{
    auto scheduler = make(true);
    scheduler->setVersion(OPSTACK_EXECUTOR_VERSION, {});
    scheduler->setVersion(OPSTACK_EXECUTOR_VERSION + 5, {});

    scheduler->executeBlock(
        {}, false, [](bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool) {});
    BOOST_CHECK_EQUAL(slots[3]->m_executeCount, 1);
    BOOST_CHECK_EQUAL(slots[2]->m_executeCount, 0);
}

// The discriminator for the guard's keying: an Eth-lane chain may legitimately carry
// feature_l2_ethereum_compat (it is the ledger's L2 state shape, not an OP-mode marker).
// Flag-keyed freezing (the pre-fix guard) froze and mislabelled such a chain on an
// ordinary executor_version switch; slot-keyed freezing must let it switch.
BOOST_AUTO_TEST_CASE(ethLaneL2FeatureDoesNotFreezeVersionSwitches)
{
    auto scheduler = make(true);
    scheduler->setVersion(ETHEREUM_EXECUTOR_VERSION, {});
    scheduler->setVersion(1, l2FeatureConfig());

    // The flag is not a freeze key: the switch to slot 1 landed and execution follows it.
    scheduler->executeBlock(
        {}, false, [](bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool) {});
    BOOST_CHECK_EQUAL(slots[1]->m_executeCount, 1);
    BOOST_CHECK_EQUAL(slots[2]->m_executeCount, 0);
    scheduler->stop();
    BOOST_CHECK_EQUAL(slots[1]->m_stopCount, 1);
}

BOOST_AUTO_TEST_SUITE_END()
