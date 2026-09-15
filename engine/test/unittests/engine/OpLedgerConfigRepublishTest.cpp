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
 * @file OpLedgerConfigRepublishTest.cpp
 * @brief OP-lane pin for the admission holder after a commit (engine/bcos-engine/
 *        OpLedgerConfigRepublish.h).
 *
 * The OP lane has no engine-side holder writer, by design: OpEngineService takes no
 * LedgerConfigState, because the only configuration its commit callback could publish is
 * OpScheduler::loadCommitLedgerConfig's number+timestamp stub. The holder is kept complete by
 * the notifier OpScheduler fires after every durable commit, which is what these cases drive.
 * The last case asserts the stub's own shape, so a change that publishes the callback's object
 * instead of reading the ledger fails here rather than only in the C2 e2e leg.
 */

#include "engine/bcos-engine/OpLedgerConfigRepublish.h"

#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerConfigState.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-framework/testutils/faker/FakeLedger.h>
#include <bcos-utilities/Error.h>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace bcos;

namespace op_ledger_config_republish_test
{
namespace
{
constexpr char const* c_seededChainId = "1234";
constexpr auto c_seededFeature = bcos::ledger::Features::Flag::feature_sharding;

/// A ledger whose current-block-number read fails, standing in for a storage fault during the
/// post-commit refetch.
struct FailingBlockNumberLedger : bcos::test::FakeLedger
{
    using FakeLedger::FakeLedger;

    void asyncGetBlockNumber(std::function<void(Error::Ptr, BlockNumber)> callback) override
    {
        callback(BCOS_ERROR_PTR(-1, "storage read failed"), 0);
    }
};

bcos::test::FakeLedger::Ptr makeLedger()
{
    auto blockFactory = bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    auto ledger = std::make_shared<bcos::test::FakeLedger>(blockFactory, 20, 10, 10);
    ledger->setSystemConfig(bcos::ledger::SYSTEM_KEY_WEB3_CHAIN_ID, c_seededChainId);
    bcos::ledger::Features features;
    features.set(c_seededFeature);
    ledger->setFeatures(features);
    return ledger;
}

/// The state the holder is in when the commit path starts: the snapshot published at boot,
/// which carries neither the chain id nor the features the ledger holds, and still sits on the
/// pre-commit block number.
std::shared_ptr<bcos::ledger::LedgerConfigState> makeBootHolder(
    protocol::BlockNumber committedNumber)
{
    auto config = std::make_shared<bcos::ledger::LedgerConfig>();
    config->setBlockNumber(committedNumber - 1);
    auto holder = std::make_shared<bcos::ledger::LedgerConfigState>();
    holder->set(std::move(config));
    return holder;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpLedgerConfigRepublishTest)

/// Drives one commit the way OpScheduler::commitBlock does -- invoke the installed notifier with
/// the committed number, then hand back its own LedgerConfig -- and checks the holder afterwards.
/// Both notifiers must run: composing the RPC notifier in must not replace the republish.
BOOST_AUTO_TEST_CASE(commit_republish_keeps_the_holder_complete)
{
    auto ledger = makeLedger();
    auto const committedNumber = ledger->blockNumber();
    auto holder = makeBootHolder(committedNumber);
    std::vector<protocol::BlockNumber> rpcCalls;
    std::vector<std::string> republishFailures;

    // Exactly what Initializer installs on the OpScheduler delegate (the republish), plus the
    // RPC notifier it composes in afterwards.
    bcos::engine::BlockNumberNotifier schedulerNotifier;
    auto installer = bcos::engine::composeOpBlockNumberNotifier(
        [&schedulerNotifier](bcos::engine::BlockNumberNotifier notifier) {
            schedulerNotifier = std::move(notifier);
        },
        bcos::engine::makeOpLedgerConfigRepublisher(
            holder, ledger, [&republishFailures](protocol::BlockNumber, bcos::Error::Ptr error) {
                republishFailures.push_back(error->errorMessage());
            }));
    installer([&rpcCalls](protocol::BlockNumber number) { rpcCalls.push_back(number); });

    BOOST_REQUIRE(schedulerNotifier);
    schedulerNotifier(committedNumber);

    BOOST_CHECK(republishFailures.empty());
    BOOST_REQUIRE_EQUAL(rpcCalls.size(), 1u);
    BOOST_CHECK_EQUAL(rpcCalls.at(0), committedNumber);

    auto published = holder->get();
    BOOST_REQUIRE_MESSAGE(published->chainId().has_value(),
        "admission reads chainId from the holder and nowhere else: an unset chainId refuses "
        "every EIP-155 transaction from the first committed block on (-32602 invalid chain id "
        "for signer)");
    BOOST_CHECK(published->features().get(c_seededFeature));
    // The published object is the ledger's, not the boot holder's: its number advanced off the
    // pre-commit value the holder started on.
    BOOST_CHECK_EQUAL(published->blockNumber(), ledger->blockNumber());
    BOOST_CHECK_EQUAL(published->blockNumber(), committedNumber);
}

/// A failing refetch must leave the previous snapshot in place and must not escape the notifier:
/// OpScheduler invokes it inside coCommitBlock, after the block is already durable, so a throw
/// would report a committed block as failed and leave op-node retrying a block that is on disk.
BOOST_AUTO_TEST_CASE(failed_republish_keeps_the_previous_snapshot)
{
    auto blockFactory = bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());
    auto failingLedger = std::make_shared<FailingBlockNumberLedger>(blockFactory, 20, 10, 10);
    auto const committedNumber = failingLedger->blockNumber();
    auto holder = makeBootHolder(committedNumber);
    auto const before = holder->get();

    std::vector<std::string> failures;
    auto republish = bcos::engine::makeOpLedgerConfigRepublisher(
        holder, failingLedger, [&failures](protocol::BlockNumber, bcos::Error::Ptr error) {
            failures.push_back(error->errorMessage());
        });

    republish(committedNumber);  // must not throw

    BOOST_REQUIRE_EQUAL(failures.size(), 1u);
    BOOST_CHECK_MESSAGE(
        failures.at(0).find("republish ledger config after OP commit failed") != std::string::npos,
        "the diagnostic must name the failed republish, got: " + failures.at(0));
    BOOST_CHECK(holder->get() == before);
    BOOST_CHECK_EQUAL(holder->get()->blockNumber(), committedNumber - 1);
}

/// Negative control: the object OpScheduler::loadCommitLedgerConfig hands to its commit callback
/// carries no chainId and none of the ledger's features. Publishing that object -- instead of
/// reading the ledger -- is what refused every user transaction with -32602; the case above is
/// what catches a regression to it, and this one states why admission cannot accept it.
BOOST_AUTO_TEST_CASE(scheduler_stub_config_is_not_an_admissible_snapshot)
{
    auto ledger = makeLedger();
    auto const committedNumber = ledger->blockNumber();

    // What OpScheduler::loadCommitLedgerConfig builds: number + timestamp, nothing else.
    auto stub = std::make_shared<bcos::ledger::LedgerConfig>();
    stub->setBlockNumber(committedNumber);
    stub->setTimestamp(1'700'000'000'000);

    BOOST_CHECK(!stub->chainId().has_value());
    BOOST_CHECK(!stub->features().get(c_seededFeature));

    // The same commit, republished from the ledger, yields what admission needs.
    auto holder = makeBootHolder(committedNumber);
    auto republish = bcos::engine::makeOpLedgerConfigRepublisher(
        holder, ledger, [](protocol::BlockNumber, bcos::Error::Ptr) {});
    republish(committedNumber);

    BOOST_REQUIRE(holder->get()->chainId().has_value());
    BOOST_CHECK(holder->get()->features().get(c_seededFeature));
    BOOST_CHECK_EQUAL(holder->get()->blockNumber(), stub->blockNumber());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace op_ledger_config_republish_test
