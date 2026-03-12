/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief unit test for TxsSyncMsg
 * @file TxsSyncMsgTest.h
 */
#include "FakeTxsSyncMsg.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-txpool/sync/protocol/proto/TxsSync.pb.h>
#include <bcos-txpool/sync/utilities/Common.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::sync;
using namespace bcos::crypto;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(TxsSyncMsgTest, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testTxsSyncMsg)
{
    auto faker = std::make_shared<FakeTxsSyncMsg>();
    int32_t type = 100;
    int32_t version = 1000;
    HashList hashList;
    auto hashImpl = std::make_shared<Keccak256>();
    for (int i = 0; i < 10; i++)
    {
        auto value = std::to_string(i);
        hashList.emplace_back(hashImpl->hash(bytesConstRef(value)));
    }
    std::string data = "adflwerjw39ewelrew";
    bytes txsData = bytes(data.begin(), data.end());
    faker->fakeTxsMsg(type, version, hashList, txsData);
}
BOOST_AUTO_TEST_CASE(testHashSizeValidation)
{
    // Construct a raw protobuf message with hash entries of various sizes
    auto rawMsg = std::make_shared<TxsSyncMessage>();
    rawMsg->set_version(1);
    rawMsg->set_type(0);

    // 16-byte hash (too short) — should be rejected
    std::string shortHash(16, '\x01');
    rawMsg->add_txshash(shortHash.data(), shortHash.size());

    // 32-byte hash (correct) — should be accepted
    std::string validHash(HashType::SIZE, '\x02');
    rawMsg->add_txshash(validHash.data(), validHash.size());

    // 48-byte hash (too long) — should be rejected
    std::string longHash(48, '\x03');
    rawMsg->add_txshash(longHash.data(), longHash.size());

    // Serialize and decode through TxsSyncMsg
    auto size = rawMsg->ByteSizeLong();
    bytes serialized(size);
    rawMsg->SerializeToArray(serialized.data(), static_cast<int>(size));

    auto txsSyncMsg = std::make_shared<TxsSyncMsg>(ref(serialized));

    // Only the 32-byte entry should survive
    BOOST_CHECK_EQUAL(txsSyncMsg->txsHash().size(), 1);
    BOOST_CHECK(
        txsSyncMsg->txsHash()[0] == HashType((byte const*)validHash.data(), HashType::SIZE));
}

BOOST_AUTO_TEST_CASE(testHashCountLimit)
{
    // Construct a raw protobuf message with more hashes than the limit
    auto rawMsg = std::make_shared<TxsSyncMessage>();
    rawMsg->set_version(1);
    rawMsg->set_type(0);

    std::string validHash(HashType::SIZE, '\xAB');
    for (size_t i = 0; i < MAX_SYNC_TXSHASH_COUNT + 1; i++)
    {
        rawMsg->add_txshash(validHash.data(), validHash.size());
    }

    // Serialize and decode through TxsSyncMsg
    auto size = rawMsg->ByteSizeLong();
    bytes serialized(size);
    rawMsg->SerializeToArray(serialized.data(), static_cast<int>(size));

    auto txsSyncMsg = std::make_shared<TxsSyncMsg>(ref(serialized));

    // Entire message should be rejected — txsHash empty
    BOOST_CHECK(txsSyncMsg->txsHash().empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos