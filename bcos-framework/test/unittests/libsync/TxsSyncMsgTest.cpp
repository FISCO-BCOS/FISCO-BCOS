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
#include "testutils/TestPromptFixture.h"
#include "testutils/crypto/HashImpl.h"
#include "testutils/sync/FakeTxsSyncMsg.h"
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
    auto hashImpl = std::make_shared<Keccak256Hash>();
    for (int i = 0; i < 10; i++)
    {
        hashList.emplace_back(hashImpl->hash(std::to_string(i)));
    }
    std::string data = "adflwerjw39ewelrew";
    bytes txsData = bytes(data.begin(), data.end());
    faker->fakeTxsMsg(type, version, hashList, txsData);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos