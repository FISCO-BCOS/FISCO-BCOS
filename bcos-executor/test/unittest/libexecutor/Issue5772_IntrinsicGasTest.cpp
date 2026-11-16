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

    // the path of eth_call and eth_estimateGas: TransactionExecutor::createBlockContextForCall
    // builds the context at the last committed block, then swaps in the next block's features
    protocol::ExecutionMessage::UniquePtr callDeploy(ExecutorSmokeEnv& env, int64_t gasAvailable)
    {
        bytes code;
        boost::algorithm::unhex(c_issue5772InitCode, std::back_inserter(code));
        auto params = topLevel(100, gasAvailable);
        params->setCreate(true);
        params->setData(std::move(code));
        std::promise<protocol::ExecutionMessage::UniquePtr> promise;
        env.executor->call(std::move(params),
            [&promise](Error::UniquePtr&& error, protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                promise.set_value(std::move(result));
            });
        return promise.get_future().get();
    }

    // hands a frame's result, or the next frame it asked for, back to the executor
    protocol::ExecutionMessage::UniquePtr resume(
        ExecutorSmokeEnv& env, protocol::ExecutionMessage::UniquePtr message)
    {
        std::promise<protocol::ExecutionMessage::UniquePtr> promise;
        env.executor->dmcExecuteTransaction(std::move(message),
            [&promise](Error::UniquePtr&& error, protocol::ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                promise.set_value(std::move(result));
            });
        return promise.get_future().get();
    }

    // an init code that RETURNs maxEvmCodeSize + 1 zero bytes: PUSH3 size PUSH1 0 RETURN
    protocol::ExecutionMessage::UniquePtr deployTooLarge(
        ExecutorSmokeEnv& env, int64_t gasAvailable)
    {
        auto params = topLevel(100, gasAvailable);
        params->setCreate(true);
        params->setData(bytes{0x62, 0x04, 0x00, 0x01, 0x60, 0x00, 0xf3});
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

// A budget that runs the init code but cannot pay the deposit fails with the whole budget gone
// and nothing deployed, as on executor_version 1. The cost is measured, then the budget is set
// one unit either side of it.
BOOST_AUTO_TEST_CASE(depositTheBudgetCannotPayBurnsTheBudget)
{
    auto measured = lane(true);
    auto measuredResult = deploy(measured, gas);
    BOOST_REQUIRE_EQUAL(measuredResult->status(), 0);
    const auto cost = gas - measuredResult->gasAvailable();

    auto exact = lane(true);
    auto exactResult = deploy(exact, cost);
    BOOST_CHECK_EQUAL(exactResult->status(), 0);
    BOOST_CHECK_EQUAL(exactResult->gasAvailable(), 0);

    auto tooLittle = lane(true);
    auto failed = deploy(tooLittle, cost - 1);
    BOOST_CHECK_EQUAL(
        failed->status(), static_cast<int32_t>(protocol::TransactionStatus::OutOfGas));
    BOOST_CHECK_EQUAL(failed->type(), protocol::ExecutionMessage::REVERT);
    BOOST_CHECK_EQUAL(failed->gasAvailable(), 0);
    BOOST_CHECK(failed->newEVMContractAddress().empty());

    // without the flag the same abort keeps charging what was used: 199 of the 200 the deposit
    // costs at 20 gas per byte stay with the sender. A chain that has not upgraded is unchanged.
    auto offMeasured = lane(false);
    const auto offCost = gas - deploy(offMeasured, gas)->gasAvailable();
    auto offLane = lane(false);
    auto offFailed = deploy(offLane, offCost - 1);
    BOOST_CHECK_EQUAL(
        offFailed->status(), static_cast<int32_t>(protocol::TransactionStatus::OutOfGas));
    BOOST_CHECK_EQUAL(offFailed->gasAvailable(), c_issue5772RuntimeSize * 20 - 1);
}

// A nested creation that cannot pay its deposit: the child frame's budget is gone, the parent
// sees address 0 and carries on with its 1/64, as on Ethereum and executor_version 1. Driven the
// way the scheduler drives a DMC transaction, one message at a time.
BOOST_AUTO_TEST_CASE(nestedDepositFailureBurnsTheChildsBudgetOnly)
{
    // The factory's runtime CREATEs a child and returns the address it got:
    //   PUSH1 6 PUSH1 22 PUSH1 0 CODECOPY | PUSH1 6 PUSH1 0 PUSH1 0 CREATE
    //   PUSH1 0 MSTORE | PUSH1 32 PUSH1 0 RETURN
    // followed by the child's init code, which returns 10000 zero bytes of runtime code:
    //   PUSH2 10000 PUSH1 0 RETURN
    // The factory's own init code copies those 28 bytes and returns them.
    constexpr std::string_view factoryInit =
        "601c600c600039601c6000f3"  // 12 bytes: returns the next 28
        "60066016600039"            // copy the child's init code to memory
        "600660006000f0"            // CREATE
        "60005260206000f3"          // return the address
        "6127106000f3";             // the child's init code
    bytes factoryCode;
    boost::algorithm::unhex(factoryInit, std::back_inserter(factoryCode));

    // What the parent's EVM gets is the same either way, once the intrinsic 21000 is paid. It
    // cannot buy the deposit at either price: 200000 at 20 gas per byte, 2000000 at 200.
    constexpr int64_t evmBudget = 100000;
    int64_t usedOff = 0;
    int64_t usedOn = 0;
    int64_t childLeftOff = 0;
    for (bool flag : {false, true})
    {
        BOOST_TEST_INFO_SCOPE("flag " << flag);
        auto env = lane(flag);
        auto deployParams = topLevel(100, gas);
        deployParams->setCreate(true);
        deployParams->setData(bytes(factoryCode));
        BOOST_REQUIRE_EQUAL(dmcExecute(*env.executor, std::move(deployParams))->status(), 0);
        advanceBlock(env, 2, features(flag));

        const int64_t budget = flag ? evmBudget + 21000 : evmBudget;
        auto childRequest = dmcExecute(*env.executor, topLevel(101, budget));
        BOOST_REQUIRE_EQUAL(childRequest->type(), protocol::ExecutionMessage::MESSAGE);
        BOOST_REQUIRE(childRequest->create());
        childRequest->setSeq(1);
        childRequest->setKeyLocks({});
        childRequest->setTo("ee6f30856ad3bae00b1169808488502786a13e3c");

        auto childResult = resume(env, std::move(childRequest));
        BOOST_CHECK_EQUAL(childResult->type(), protocol::ExecutionMessage::REVERT);
        BOOST_CHECK_EQUAL(
            childResult->status(), static_cast<int32_t>(protocol::TransactionStatus::OutOfGas));
        BOOST_CHECK(childResult->newEVMContractAddress().empty());
        if (flag)
        {
            BOOST_CHECK_EQUAL(childResult->gasAvailable(), 0);
        }
        else
        {
            childLeftOff = childResult->gasAvailable();
            BOOST_CHECK_GT(childLeftOff, 0);
        }

        childResult->setSeq(0);
        auto parent = resume(env, std::move(childResult));
        BOOST_CHECK_EQUAL(parent->type(), protocol::ExecutionMessage::FINISHED);
        BOOST_CHECK_EQUAL(parent->status(), 0);
        BOOST_CHECK(parent->data().toBytes() == bytes(32, 0));
        (flag ? usedOn : usedOff) = budget - parent->gasAvailable();
    }
    // the only differences: the intrinsic charge, and what the child would have handed back
    BOOST_CHECK_EQUAL(usedOn - usedOff, 21000 + childLeftOff);
}

// Code over the size limit is the other exceptional abort of the deploy path.
BOOST_AUTO_TEST_CASE(codeTooLargeBurnsTheBudget)
{
    for (bool flag : {false, true})
    {
        auto env = lane(flag);
        auto result = deployTooLarge(env, gas);
        BOOST_CHECK_EQUAL(
            result->status(), static_cast<int32_t>(protocol::TransactionStatus::OutOfGas));
        BOOST_CHECK(result->newEVMContractAddress().empty());
        if (flag)
        {
            BOOST_CHECK_EQUAL(result->gasAvailable(), 0);
        }
        else
        {
            BOOST_CHECK_GT(result->gasAvailable(), 0);
        }
    }
}

// The upgrade boundary: the flag activates at the next block, so the last committed block's
// features do not have it and the call path's do. An estimate there has to price the deployment
// as the next block will execute it: intrinsic gas and 200 gas per byte, not one without the other.
BOOST_AUTO_TEST_CASE(callAtTheUpgradeBoundaryPricesTheDepositLikeTheNextBlock)
{
    auto executed = lane(true);
    auto executedResult = deploy(executed, gas);
    BOOST_REQUIRE_EQUAL(executedResult->status(), 0);

    // buildExecutorWithFeatures records the flag as enabled at block 1 and nothing has been
    // committed, so the constructor reads the features of block 0 and the call path those of 1
    auto boundary = buildExecutorWithFeatures(features(true));
    auto called = callDeploy(boundary, gas);
    BOOST_REQUIRE_EQUAL(called->status(), 0);
    BOOST_CHECK_EQUAL(called->gasAvailable(), executedResult->gasAvailable());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
