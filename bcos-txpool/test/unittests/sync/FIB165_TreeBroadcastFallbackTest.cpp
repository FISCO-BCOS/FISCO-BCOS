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
 * @file FIB165_TreeBroadcastFallbackTest.cpp
 * @brief Regression test for FIB-165: when m_treeRouter is null,
 *        broadcastTransactionBufferByTree must NOT silently return; it must fall back
 *        to the standard flood broadcast so that callers do not lose transactions when
 *        the tree topology is unavailable.
 */

#include "bcos-framework/bcos-framework/testutils/faker/FakeTransaction.h"
#include "bcos-task/Wait.h"
#include "test/unittests/txpool/TxPoolFixture.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::sync;
using namespace bcos::crypto;
using namespace bcos::protocol;

namespace bcos::test
{
// Local fixture: build a TxPoolFixture with enableTree=false so that no TreeTopology is
// installed on m_txpool (the default TxPoolFixture() ctor enables tree mode).
class FIB165Fixture : public TxPoolFixture
{
public:
    FIB165Fixture()
      : TxPoolFixture(std::make_shared<Secp256k1Crypto>()->generateKeyPair()->publicKey(),
            std::make_shared<CryptoSuite>(
                std::make_shared<Keccak256>(), std::make_shared<Secp256k1Crypto>(), nullptr),
            "groupId", "chainId", 100000000, std::make_shared<FakeGateWay>(), /*enableTree=*/false)
    {}
};

BOOST_FIXTURE_TEST_SUITE(FIB165_TreeBroadcastFallback, FIB165Fixture)

BOOST_AUTO_TEST_CASE(noTreeRouterFallsBackToFloodBroadcast)
{
    auto& txpool = dynamic_cast<TxPool&>(*this->txpool());
    BOOST_REQUIRE(txpool.treeRouter() == nullptr);

    // Populate the connected node list so that the FakeFrontService::broadcastMessage
    // iterator has peers to fan out to. Without this, the fallback path would still be
    // taken but no peer messages would be sent, making the test signal weak.
    this->appendSealer(this->m_nodeId);
    for (const auto& nid : this->m_nodeIdList)
    {
        this->appendSealer(nid);
    }

    auto baseline = m_frontService->totalSendMsgSize();

    auto tx = fakeTransaction(this->m_cryptoSuite, std::to_string(utcSteadyTime()));
    bcos::bytes data;
    tx->encode(data);

    // Pre-FIB-165 this call returned silently with zero peer sends. With the fix the
    // tree path detects the null router and broadcasts via the standard flood path.
    task::syncWait(txpool.broadcastTransactionBufferByTree(bcos::ref(data), true, nullptr));

    BOOST_CHECK_GT(m_frontService->totalSendMsgSize(), baseline);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
