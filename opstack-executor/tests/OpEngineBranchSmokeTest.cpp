// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpEngineBranchSmokeTest — compile-and-run verification that OpEngineService instantiates
// against OpSchedulerSeam on the Eth/Op split branch. The -38005 pre-Isthmus gate is the runtime
// assertion; the compile-time instantiation of the OP body is the point.
#include "support/OpEngineE2eFixture.h"

#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineHelper.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace opstack_e2e;

BOOST_AUTO_TEST_SUITE(OpEngineBranchSmokeSuite)

BOOST_AUTO_TEST_CASE(OpModeInstantiatesAndGatesV4)
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS storage{checkpointBackend};

    EngineOpScheduler scheduler(std::make_shared<bcos::evm::opstack::OpForkSchedule>(
                                    bcos::evm::opstack::OpForkSchedule::legacy(false)),
        {});
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

    BOOST_CHECK_THROW(
        bcos::task::syncWait(engine.newPayload(request, 3)), bcos::engine::UnsupportedFork);
}

BOOST_AUTO_TEST_SUITE_END()
