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
 * @file FIB166_TreeBroadcastMetadataTest.cpp
 * @brief Regression test for FIB-166: broadcastTransactionBufferByTree must defensively
 *        validate frontService->groupNodeInfo() and its nodeProtocolList() before
 *        dereferencing them. When the metadata is unavailable or empty the function
 *        must fall back to the flood broadcast safely instead of crashing or making
 *        unsafe routing decisions.
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
BOOST_FIXTURE_TEST_SUITE(FIB166_TreeBroadcastMetadata, TxPoolFixture)

// With enableTree=true (default fixture) the tree router is installed, but we explicitly
// install an EMPTY-protocol GroupNodeInfo to simulate the "metadata absent / not
// initialised yet" condition. Pre-FIB-166 this caused undefined behaviour at the
// nodeProtocolList()->iterate point (and unsafe dereference at the log line); with the
// fix the function falls back to the standard flood broadcast.
BOOST_AUTO_TEST_CASE(emptyProtocolListFallsBackToFloodBroadcast)
{
    auto& txpool = dynamic_cast<TxPool&>(*this->txpool());
    BOOST_REQUIRE(txpool.treeRouter() != nullptr);

    this->appendSealer(this->m_nodeId);
    for (const auto& nid : this->m_nodeIdList)
    {
        this->appendSealer(nid);
    }

    // Replace the populated FakeGroupInfo with one that has no protocol entries.
    auto emptyGroupInfo = std::make_shared<FakeGroupInfo>();
    m_frontService->setGroupInfo(emptyGroupInfo);

    auto baseline = m_frontService->totalSendMsgSize();

    auto tx = fakeTransaction(this->m_cryptoSuite, std::to_string(utcSteadyTime()));
    bcos::bytes data;
    tx->encode(data);

    // Should NOT throw / crash. With the fix it falls back to flood; before the fix it
    // would dereference protocolList[*].get() (or worse) when iterating an empty/invalid
    // protocol list.
    BOOST_CHECK_NO_THROW(
        task::syncWait(txpool.broadcastTransactionBufferByTree(bcos::ref(data), true, nullptr)));

    BOOST_CHECK_GT(m_frontService->totalSendMsgSize(), baseline);
}

// Even more defensive case: explicitly null groupNodeInfo. The fix detects this and
// falls back to flood.
BOOST_AUTO_TEST_CASE(nullGroupNodeInfoFallsBackToFloodBroadcast)
{
    auto& txpool = dynamic_cast<TxPool&>(*this->txpool());
    BOOST_REQUIRE(txpool.treeRouter() != nullptr);

    this->appendSealer(this->m_nodeId);
    for (const auto& nid : this->m_nodeIdList)
    {
        this->appendSealer(nid);
    }

    m_frontService->setGroupInfo(nullptr);
    auto baseline = m_frontService->totalSendMsgSize();

    auto tx = fakeTransaction(this->m_cryptoSuite, std::to_string(utcSteadyTime()));
    bcos::bytes data;
    tx->encode(data);

    BOOST_CHECK_NO_THROW(
        task::syncWait(txpool.broadcastTransactionBufferByTree(bcos::ref(data), true, nullptr)));

    BOOST_CHECK_GT(m_frontService->totalSendMsgSize(), baseline);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
