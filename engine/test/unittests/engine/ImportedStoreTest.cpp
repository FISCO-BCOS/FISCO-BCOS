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
 * @file ImportedStoreTest.cpp
 * @brief Hash-keyed in-memory imported-payload store (S5 plan Task 1).
 */

#include "engine/bcos-engine/ImportedStore.h"

#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::engine;

BOOST_AUTO_TEST_SUITE(ImportedStoreTest)

BOOST_AUTO_TEST_CASE(PutThenHasBlockAndBody)
{
    ImportedStore store;
    auto hash = h256(1);
    ImportedBlock b{.hash = hash,
        .parent = h256(0),
        .number = 1,
        .headerBytes = {},
        .txs = {},
        .txHashes = {},
        .encodedTxs = {},
        .receipts = {},
        .txRecipients = {},
        .storageDelta = {},
        .postStateFlat = {}};
    BOOST_REQUIRE(store.put(b));
    BOOST_CHECK(store.hasBlock(hash));
    BOOST_CHECK(store.hasState(hash));
    BOOST_REQUIRE(store.get(hash).has_value());
    BOOST_CHECK_EQUAL(store.get(hash)->number, 1);
}

BOOST_AUTO_TEST_CASE(OverwriteWithDescendantsIsRejected)
{
    ImportedStore store;
    store.put({.hash = h256(1),
        .parent = h256(0),
        .number = 1,
        .headerBytes = {},
        .txs = {},
        .txHashes = {},
        .encodedTxs = {},
        .receipts = {},
        .txRecipients = {},
        .storageDelta = {},
        .postStateFlat = {}});
    store.put({.hash = h256(2),
        .parent = h256(1),
        .number = 2,
        .headerBytes = {},
        .txs = {},
        .txHashes = {},
        .encodedTxs = {},
        .receipts = {},
        .txRecipients = {},
        .storageDelta = {},
        .postStateFlat = {}});
    BOOST_CHECK(!store.put({.hash = h256(3),
        .parent = h256(0),
        .number = 1,
        .headerBytes = {},
        .txs = {},
        .txHashes = {},
        .encodedTxs = {},
        .receipts = {},
        .txRecipients = {},
        .storageDelta = {},
        .postStateFlat = {}}));  // 同高，1 已有子孙
}

// 未 FCU 的同一哈希重复 newPayload 必须幂等 VALID，不双写。
BOOST_AUTO_TEST_CASE(SameHashReputIsIdempotent)
{
    ImportedStore store;
    ImportedBlock b{.hash = h256(1),
        .parent = h256(0),
        .number = 1,
        .headerBytes = {},
        .txs = {},
        .txHashes = {},
        .encodedTxs = {},
        .receipts = {},
        .txRecipients = {},
        .storageDelta = {},
        .postStateFlat = {}};
    BOOST_REQUIRE(store.put(b));
    BOOST_CHECK(store.put(b));
    BOOST_CHECK_EQUAL(store.size(), 1U);
}

// 同高但旧块没有 imported 子孙 → 允许占位（规范祖先 sibling 落 ImportedStore 的前置）。
BOOST_AUTO_TEST_CASE(SameHeightWithoutDescendantsIsAccepted)
{
    ImportedStore store;
    BOOST_REQUIRE(store.put({.hash = h256(1),
        .parent = h256(0),
        .number = 1,
        .headerBytes = {},
        .txs = {},
        .txHashes = {},
        .encodedTxs = {},
        .receipts = {},
        .txRecipients = {},
        .storageDelta = {},
        .postStateFlat = {}}));
    BOOST_CHECK(store.put({.hash = h256(9),
        .parent = h256(0),
        .number = 1,
        .headerBytes = {},
        .txs = {},
        .txHashes = {},
        .encodedTxs = {},
        .receipts = {},
        .txRecipients = {},
        .storageDelta = {},
        .postStateFlat = {}}));
}

BOOST_AUTO_TEST_SUITE_END()
