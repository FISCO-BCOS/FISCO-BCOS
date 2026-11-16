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
 * @brief Issue #5772: a transaction the DAG path sends back is re-dispatched at seq 0 under
 *        bugfix_tx_intrinsic_gas. The executor recognises a transaction's top-level frame by
 *        seq 0, so a re-dispatch at seq 1 skipped the intrinsic gas.
 */
// FakeBlock.h pulls in range-v3 and has to come before mock/MockDmcExecutor.h, which opens
// namespace std at file scope and so makes ::ranges ambiguous for anything included after it.
// clang-format off
#include <bcos-framework/testutils/faker/FakeBlock.h>
// clang-format on
#include "../src/Executive.h"
#include "bcos-framework/executor/NativeExecutionMessage.h"
#include "bcos-scheduler/src/DmcExecutor.h"
#include "bcos-scheduler/src/DmcStepRecorder.h"
#include "bcos-scheduler/src/GraphKeyLocks.h"
#include "mock/MockDmcExecutor.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
namespace
{
struct SendBackOutcome
{
    int64_t currentSeq = -1;
    std::string to;
};

struct Issue5772SendBackFixture
{
    crypto::Hash::Ptr hashImpl = std::make_shared<crypto::Keccak256>();
    crypto::CryptoSuite::Ptr cryptoSuite = std::make_shared<crypto::CryptoSuite>(
        hashImpl, std::make_shared<crypto::Secp256k1Crypto>(), nullptr);
    protocol::BlockFactory::Ptr blockFactory = createBlockFactory(cryptoSuite);

    // A creation handed to a DmcExecutor, either fresh (MESSAGE, seq never advanced) or sent
    // back from the DAG path (SEND_BACK, in the state BlockExecutive::DAGExecute leaves it in:
    // currentSeq advanced to 1, nothing executed). Returns what the DmcExecutor schedules out:
    // the creation's address belongs to another contract's executor.
    SendBackOutcome dispatch(bool restartSeq, bool sentBack)
    {
        const auto type =
            sentBack ? protocol::ExecutionMessage::SEND_BACK : protocol::ExecutionMessage::MESSAGE;
        auto block = blockFactory->createBlock();
        auto blockHeader = blockFactory->blockHeaderFactory()->createBlockHeader();
        blockHeader->setNumber(1);
        blockHeader->calculateHash(*hashImpl);
        block->setBlockHeader(blockHeader);
        auto dmcExecutor = std::make_shared<scheduler::DmcExecutor>("DmcExecutor", "0xaabbccdd",
            block, std::make_shared<MockDmcExecutor>("executor"),
            std::make_shared<scheduler::GraphKeyLocks>(), hashImpl,
            std::make_shared<scheduler::DmcStepRecorder>(), false);
        dmcExecutor->setRestartSeqOnSendBack(restartSeq);

        SendBackOutcome outcome;
        dmcExecutor->setSchedulerOutHandler([&outcome](scheduler::ExecutiveState::Ptr state) {
            outcome.currentSeq = state->currentSeq;
            outcome.to = std::string(state->message->to());
        });

        auto message = std::make_unique<executor::NativeExecutionMessage>();
        message->setType(type);
        message->setContextID(7);
        message->setSeq(0);
        message->setFrom("0xeeffaabb");
        message->setCreate(true);
        auto state = std::make_shared<scheduler::ExecutiveState>(7, std::move(message), true);
        if (sentBack)
        {
            state->callStack.push(state->currentSeq++);
        }
        dmcExecutor->scheduleIn(state);
        dmcExecutor->prepare();
        return outcome;
    }
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(Issue5772_SendBackSeqTest, Issue5772SendBackFixture)

BOOST_AUTO_TEST_CASE(sentBackTransactionRestartsAtSeqZeroUnderTheFlag)
{
    // what a transaction that never took the DAG path gets: seq 0, address derived from seq 0
    auto fresh = dispatch(false, false);
    BOOST_CHECK_EQUAL(fresh.currentSeq, 0);
    BOOST_REQUIRE(!fresh.to.empty());

    // flag off: the re-dispatch keeps the seq the DAG send advanced to, and so another address
    auto legacy = dispatch(false, true);
    BOOST_CHECK_EQUAL(legacy.currentSeq, 1);
    BOOST_CHECK_NE(legacy.to, fresh.to);

    // flag on: it starts over exactly like the fresh one
    auto restarted = dispatch(true, true);
    BOOST_CHECK_EQUAL(restarted.currentSeq, 0);
    BOOST_CHECK_EQUAL(restarted.to, fresh.to);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
