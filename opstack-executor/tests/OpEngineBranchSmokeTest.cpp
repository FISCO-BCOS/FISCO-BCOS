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
 * @file OpEngineBranchSmokeTest.cpp
 * @brief Smoke coverage of the engine fork-resolution branches. */

// OpEngineBranchSmokeTest — compile-and-run verification that OpEngineService instantiates
// against OpSchedulerSeam on the Eth/Op split branch. The -38005 pre-Isthmus gate is the runtime
// assertion; the compile-time instantiation of the OP body is the point.
#include "support/OpEngineE2eFixture.h"

#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>

using namespace opstack_e2e;

BOOST_AUTO_TEST_SUITE(OpEngineBranchSmokeSuite)

BOOST_AUTO_TEST_CASE(OpModeInstantiatesAndRejectsV3)
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS storage{checkpointBackend};

    EngineOpScheduler scheduler(bcos::ledger::OpForkSchedule{},  // all-Isthmus baseline
        bcos::executor_v1::opstack::OpEthL1BlockInfo{});
    StubMemPool memPool;
    static auto blockFactory =
        bcos::test::createBlockFactory(bcos::test::createNormalCryptoSuite());

    OpEngine engine(memPool, storage, scheduler, blockFactory);

    bcos::engine::NewPayloadRequest request;
    request.executionPayload.timestamp = 1000;
    request.executionPayload.blockNumber = 1;
    request.executionPayload.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    request.executionPayload.withdrawalsRoot = bcos::h256{};
    request.executionPayload.excessBlobGas = bcos::u256(0);
    request.executionPayload.blobGasUsed = bcos::u256(0);
    request.parentBeaconBlockRoot = bcos::h256{};

    // Discriminating message: this is the newPayload version gate (Isthmus+ requires V4),
    // not any of the other UnsupportedFork producers.
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(engine.newPayload(request, 3)),
        bcos::engine::UnsupportedFork, [](bcos::engine::UnsupportedFork const& e) {
            return std::string(e.what()).find("Isthmus+ payloads require engine_newPayloadV4") !=
                   std::string::npos;
        });
}

BOOST_AUTO_TEST_SUITE_END()
