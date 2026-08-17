/**
 *  Copyright (C) 2021 bcos-sync.
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
 * @brief regression test for FIB-19: DownloadRequestQueue::mergeAndPop() must cap the number of
 *        generated block requests so a malicious peer cannot trigger unbounded iteration.
 * @file FIB19_MergeAndPopHardCapTest.cpp
 */

#include "SyncFixture.h"
#include "bcos-sync/state/DownloadRequestQueue.h"
#include "bcos-sync/utilities/Common.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>

#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::sync;
using namespace bcos::crypto;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(FIB19MergeAndPopHardCapTest, TestPromptFixture)

static DownloadRequestQueue::Ptr makeQueue()
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto gateWay = std::make_shared<FakeGateWay>();
    auto peer = std::make_shared<SyncFixture>(cryptoSuite, gateWay, 1);
    return std::make_shared<DownloadRequestQueue>(peer->syncConfig(), peer->syncConfig()->nodeID());
}

// A single request whose range dwarfs the legitimate maximum must be capped, not fully expanded.
BOOST_AUTO_TEST_CASE(mergeAndPopEnforcesHardCap)
{
    auto queue = makeQueue();

    // size is far above MAX_MERGED_REQUEST_BLOCKS_COUNT but small enough that the pre-fix code
    // still completes quickly, so the cap is demonstrated deterministically (not via OOM).
    constexpr size_t kMaliciousSize = MAX_MERGED_REQUEST_BLOCKS_COUNT * 4;
    queue->push(/*fromNumber*/ 1, /*size*/ kMaliciousSize, /*interval*/ 1);

    auto fetchSet = queue->mergeAndPop();
    // Without the cap this set would hold kMaliciousSize entries.
    BOOST_CHECK_EQUAL(fetchSet.size(), MAX_MERGED_REQUEST_BLOCKS_COUNT);
    // The queue is fully drained once the cap is hit.
    BOOST_CHECK(queue->empty());
}

// A legitimate, below-cap merge is returned unchanged.
BOOST_AUTO_TEST_CASE(mergeAndPopBelowCapUnchanged)
{
    auto queue = makeQueue();

    queue->push(/*fromNumber*/ 1, /*size*/ 5, /*interval*/ 1);   // blocks 1..5
    queue->push(/*fromNumber*/ 10, /*size*/ 3, /*interval*/ 1);  // blocks 10..12

    auto fetchSet = queue->mergeAndPop();
    BOOST_CHECK_EQUAL(fetchSet.size(), 8);
    BOOST_CHECK(queue->empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
