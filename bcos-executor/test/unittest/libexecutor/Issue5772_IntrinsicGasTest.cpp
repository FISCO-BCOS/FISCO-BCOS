/*
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
 * @brief Issue #5772 on executor_version 0: bugfix_tx_intrinsic_gas charges Ethereum's intrinsic
 *        gas on a transaction's top-level frame (seq 0) and 200 gas per byte of deployed code.
 *
 * The expected numbers are worked out by hand from the Yellow Paper constants, not read back
 * from TxGasModel.h, so a change to the shared model shows up here.
 */
#include "bcos-framework/ledger/Features.h"
#include "bcos-protocol/TransactionStatus.h"
#include "evmone/compat/CompatExecutorSmokeHarness.h"
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
namespace
{
// PUSH1 0x2a PUSH1 0 SSTORE | PUSH1 10 PUSH1 0x11 PUSH1 0 CODECOPY | PUSH1 10 PUSH1 0 RETURN,
// then 10 bytes of runtime code that begins with STOP. 27 bytes: 8 zero, 19 non-zero.
constexpr std::string_view c_issue5772InitCode =
    "602a600055600a6011600039600a6000f300000000000101010101";
constexpr int64_t c_issue5772RuntimeSize = 10;
// 21000 base + 32000 creation + (8 * 4 + 19 * 16) calldata; the lane's default revision is
// before Shanghai, so there is no init code word cost
constexpr int64_t c_issue5772CreateIntrinsic = 21000 + 32000 + 336;
constexpr std::string_view c_issue5772Sender = "11ac3ca85a307ae2aff614e83949ab691ba019c5";
constexpr std::string_view c_issue5772Contract = "ff6f30856ad3bae00b1169808488502786a13e3c";

struct Issue5772V0Fixture : CompatExecutorSmokeFixture
{
    static ledger::Features features(bool intrinsicGas)
    {
        ledger::Features features;
        if (intrinsicGas)
        {
            features.set(ledger::Features::Flag::bugfix_tx_intrinsic_gas);
        }
        return features;
    }

    ExecutorSmokeEnv lane(bool intrinsicGas)
    {
        auto env = buildExecutorWithFeatures(features(intrinsicGas));
        advanceBlock(env, 1, features(intrinsicGas));
        return env;
    }

    // the shape BlockExecutive::buildMessage hands the executor: a MESSAGE at seq 0
    static NativeExecutionMessage::UniquePtr topLevel(int64_t contextID, int64_t gasAvailable)
    {
        auto params = std::make_unique<NativeExecutionMessage>();
        params->setType(NativeExecutionMessage::MESSAGE);
        params->setContextID(contextID);
        params->setSeq(0);
        params->setDepth(0);
        params->setOrigin(std::string(c_issue5772Sender));
        params->setFrom(std::string(c_issue5772Sender));
        params->setTo(std::string(c_issue5772Contract));
        params->setStaticCall(false);
        params->setGasAvailable(gasAvailable);
        return params;
    }

    protocol::ExecutionMessage::UniquePtr deploy(ExecutorSmokeEnv& env, int64_t gasAvailable)
    {
        bytes code;
        boost::algorithm::unhex(c_issue5772InitCode, std::back_inserter(code));
        auto params = topLevel(100, gasAvailable);
        params->setCreate(true);
        params->setData(std::move(code));
        return dmcExecute(*env.executor, std::move(params));
    }
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(Issue5772_IntrinsicGasV0Test, Issue5772V0Fixture)

// The failing scenario of the issue: a deployment that costs less than a transfer.
BOOST_AUTO_TEST_CASE(deploymentPaysIntrinsicGasAndEthereumCodeDeposit)
{
    auto off = lane(false);
    auto on = lane(true);
    auto offResult = deploy(off, gas);
    auto onResult = deploy(on, gas);
    BOOST_REQUIRE_EQUAL(offResult->status(), 0);
    BOOST_REQUIRE_EQUAL(onResult->status(), 0);
    BOOST_REQUIRE(!onResult->newEVMContractAddress().empty());

    const auto offUsed = gas - offResult->gasAvailable();
    const auto onUsed = gas - onResult->gasAvailable();
    // flag off: nothing but the EVM run and 20 gas per byte, which is below a transfer
    BOOST_CHECK_LT(offUsed, 21000);
    // flag on: the same run plus the intrinsic gas and the 180 gas per byte the deposit grew by
    BOOST_CHECK_EQUAL(
        onUsed - offUsed, c_issue5772CreateIntrinsic + c_issue5772RuntimeSize * (200 - 20));
    BOOST_CHECK_GT(onUsed, 21000);
}

// A call paid nothing up front on this lane; the flag charges the base cost and its calldata.
BOOST_AUTO_TEST_CASE(callPaysBaseAndCalldata)
{
    for (bool flag : {false, true})
    {
        auto env = lane(flag);
        BOOST_REQUIRE_EQUAL(deploy(env, gas)->status(), 0);
        advanceBlock(env, 2, features(flag));

        auto params = topLevel(101, gas);
        params->setData(bytes{1, 2, 0, 0});
        auto result = dmcExecute(*env.executor, std::move(params));
        BOOST_REQUIRE_EQUAL(result->status(), 0);
        // the runtime code is a STOP, so the frame itself costs nothing
        BOOST_CHECK_EQUAL(gas - result->gasAvailable(), flag ? 21000 + (2 * 16 + 2 * 4) : 0);
    }
}

// A budget below the intrinsic gas: the EVM does not run, the whole budget is consumed.
BOOST_AUTO_TEST_CASE(budgetBelowIntrinsicGasFailsWithoutExecuting)
{
    auto env = lane(true);
    auto result = deploy(env, c_issue5772CreateIntrinsic - 1);
    BOOST_CHECK_EQUAL(
        result->status(), static_cast<int32_t>(protocol::TransactionStatus::OutOfGas));
    BOOST_CHECK_EQUAL(result->type(), protocol::ExecutionMessage::REVERT);
    BOOST_CHECK_EQUAL(result->gasAvailable(), 0);
    BOOST_CHECK(result->newEVMContractAddress().empty());
}

// A frame at seq > 0 (a nested call or a DMC hop) is not a transaction's first frame. The
// executor cannot tell otherwise, so the scheduler has to dispatch every first frame at seq 0;
// Issue5772_SendBackSeqTest in bcos-scheduler pins that for the DAG send-back path.
BOOST_AUTO_TEST_CASE(nestedFrameIsNotCharged)
{
    auto env = lane(true);
    BOOST_REQUIRE_EQUAL(deploy(env, gas)->status(), 0);
    advanceBlock(env, 2, features(true));

    auto params = topLevel(101, gas);
    params->setSeq(1);
    params->setData(bytes{1, 2, 0, 0});
    auto result = dmcExecute(*env.executor, std::move(params));
    BOOST_REQUIRE_EQUAL(result->status(), 0);
    BOOST_CHECK_EQUAL(gas - result->gasAvailable(), 0);
}

// A top-level value transfer paid 21000 in transferBalance; the intrinsic charge now holds that
// base cost, so the transfer costs the same 21000 and not twice that.
BOOST_AUTO_TEST_CASE(valueTransferPaysTheBaseCostOnce)
{
    for (bool flag : {false, true})
    {
        auto withBalance = features(flag);
        withBalance.set(ledger::Features::Flag::feature_balance);
        withBalance.set(ledger::Features::Flag::feature_balance_precompiled);
        auto env = buildExecutorWithFeatures(withBalance);

        // fund the sender: its account table with a balance row
        const auto senderTable = "/apps/" + std::string(c_issue5772Sender);
        std::promise<bool> created;
        env.storage->asyncCreateTable(senderTable, "value",
            [&created](Error::UniquePtr&& error, std::optional<storage::Table>&& table) {
                created.set_value(!error && table.has_value());
            });
        BOOST_REQUIRE(created.get_future().get());
        std::promise<bool> funded;
        env.storage->asyncSetRow(senderTable, executor::ACCOUNT_BALANCE, storage::Entry("1000"),
            [&funded](Error::UniquePtr&& error) { funded.set_value(!error); });
        BOOST_REQUIRE(funded.get_future().get());
        advanceBlock(env, 1, withBalance);

        auto params = topLevel(100, gas);
        params->setTo("00000000000000000000000000000000000057ee");
        params->setValue("0x7");
        auto result = dmcExecute(*env.executor, std::move(params));
        BOOST_REQUIRE_EQUAL(result->status(), 0);
        BOOST_CHECK_EQUAL(gas - result->gasAvailable(), 21000);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
