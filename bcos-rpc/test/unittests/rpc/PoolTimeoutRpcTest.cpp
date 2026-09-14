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
 * @file PoolTimeoutRpcTest.cpp
 * @author: kyonGuo
 * @date 2026/9/9
 */

#include "../common/RPCFixture.h"
#include "../common/ThrowingTxPool.h"
#include "../common/Web3TxSamples.h"
#include <bcos-framework/testutils/faker/FakeTransaction.h>
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EthEndpoint.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Error.h>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <string>

using namespace bcos;
using namespace bcos::rpc;

namespace bcos::test
{
// A receipt-wait (waitForReceipt = true: BCOS sendTransaction always, eth_sendRawTransaction
// under sync_transaction=true) ends when the pool calls the transaction's submit callback. Block
// commit does so with a receipt (TxPool::asyncNotifyBlockResult); the expiry sweep
// (MemoryStorage::removeInvalidTxs) does so the way a refusal ends it: an Error carrying
// TransactionPoolTimeout, thrown from submitTransaction's await_resume. That shape is pinned in
// TxpoolMemoryStorageTest; the pool here (ThrowingTxPool) produces it for every submission, so
// the cases take microseconds instead of txs_expiration_time and pin the two JSON faces' side of
// it: the catch that answers a refusal answers a swept wait too. The tars face
// (RPCServer::sendTransaction) has no harness in this tree; its catch is read, not run.
class PoolTimeoutFixture : public RPCFixture
{
public:
    PoolTimeoutFixture()
    {
        constexpr auto swept = protocol::TransactionStatus::TransactionPoolTimeout;
        pool = std::make_shared<ThrowingTxPool>(
            static_cast<int32_t>(swept), protocol::toString(swept));
        service = std::make_shared<rpc::NodeService>(
            m_ledger, scheduler, pool, nullptr, nullptr, m_blockFactory, nullptr);
    }
    std::shared_ptr<ThrowingTxPool> pool;
    rpc::NodeService::Ptr service;
};

BOOST_FIXTURE_TEST_SUITE(PoolTimeoutRpcTest, PoolTimeoutFixture)

// The txpool branch's catch maps the Error's status through the admission-error table, where
// TransactionPoolTimeout has no geth words and keeps its name at -32000. Not the "VM Exception"
// reply: nothing executed.
BOOST_AUTO_TEST_CASE(web3SweptReceiptWaitIsAnError)
{
    EthEndpoint endpoint(service, nullptr, /*syncTransaction=*/true);
    Json::Value params(Json::arrayValue);
    params.append(std::string(c_unprotectedRawTx));
    Json::Value response;
    BOOST_CHECK_EXCEPTION(task::syncWait(endpoint.sendRawTransaction(params, response)),
        JsonRpcException, [](JsonRpcException const& e) {
            return e.code() == Web3DefaultError && e.msg() == "TransactionPoolTimeout";
        });
    BOOST_TEST(pool->m_waitedForReceipt);
}

// The BCOS face answers a pool refusal as the Error itself (its code is the TransactionStatus);
// a swept wait is answered the same way, not as a receipt that was never produced.
BOOST_AUTO_TEST_CASE(bcosSweptReceiptWaitIsAnError)
{
    auto localRpc = factory->buildLocalRpc(groupInfo, service);
    auto tx = fakeTransaction(cryptoSuite, "1", 1000023, chainId, groupId);
    bytes encoded;
    tx->encode(encoded);
    Error::Ptr answer;
    bool answered = false;
    localRpc->jsonRpcImpl()->sendTransaction(
        groupId, "", toHexStringWithPrefix(encoded), false, [&](Error::Ptr error, Json::Value&) {
            answered = true;
            answer = std::move(error);
        });
    BOOST_REQUIRE(answered);
    BOOST_REQUIRE(answer != nullptr);
    BOOST_CHECK_EQUAL(answer->errorCode(),
        static_cast<int64_t>(protocol::TransactionStatus::TransactionPoolTimeout));
    BOOST_CHECK_EQUAL(answer->errorMessage(), "TransactionPoolTimeout");
    BOOST_TEST(pool->m_waitedForReceipt);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
