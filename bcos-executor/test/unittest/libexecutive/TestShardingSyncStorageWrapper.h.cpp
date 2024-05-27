/**
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @file TestShardingSyncStorageWrapper.h.cpp
 * @author: Jimmy Shi
 * @date 2024/3/21
 */

#include "../src/executive/ShardingSyncStorageWrapper.h"
#include <boost/test/unit_test.hpp>


using namespace std;
using namespace bcos;
using namespace bcos::executor;

namespace bcos
{
namespace test
{
BOOST_AUTO_TEST_SUITE(TestShardingSyncStorageWrapper)

BOOST_AUTO_TEST_CASE(test_keyName)
{
    std::string address = "74d6307a016c7ed2ee8733cb07dfc4533c46c5ab";
    std::string key = "key";
    std::string expected = address + "-" + key;
    BOOST_CHECK_EQUAL(ShardingKeyLocks::keyName(address, key), expected);
}

BOOST_AUTO_TEST_CASE(test_toAddressAndKey)
{
    std::string keyName = "74d6307a016c7ed2ee8733cb07dfc4533c46c5ab-key";
    std::pair<std::string_view, std::string_view> result =
        ShardingKeyLocks::toAddressAndKey(keyName);
    BOOST_CHECK_EQUAL(result.first, "74d6307a016c7ed2ee8733cb07dfc4533c46c5ab");
    BOOST_CHECK_EQUAL(result.second, "key");
}

BOOST_AUTO_TEST_CASE(test_existsKeyLock)
{
    ShardingKeyLocks locks;
    std::vector<std::string> keyLocks = {"70b93b07646fd256572d3c8efeef3c8de4a7e7a4-key1",
        "bf158a2ae101139d73a0eab88127905eefd3d2b7-key2"};
    locks.importExistsKeyLocks(keyLocks);
    BOOST_CHECK(locks.existsKeyLock("70b93b07646fd256572d3c8efeef3c8de4a7e7a4", "key1"));
    BOOST_CHECK(locks.existsKeyLock("bf158a2ae101139d73a0eab88127905eefd3d2b7", "key2"));
    BOOST_CHECK(!locks.existsKeyLock("070410a34a92d856b53db2c5c700244724597300", "key3"));
}

BOOST_AUTO_TEST_CASE(test_importMyKeyLock)
{
    ShardingKeyLocks locks;
    locks.importMyKeyLock("74d6307a016c7ed2ee8733cb07dfc4533c46c5ab", "key");
    locks.importMyKeyLock("74d6307a016c7ed2ee8733cb07dfc4533c46c5ab", "key");
    auto cmpLocks = locks.exportKeyLocks();
    BOOST_CHECK_EQUAL(cmpLocks.size(), 1);  // should not add duplicate key
    BOOST_CHECK_EQUAL(cmpLocks[0], "74d6307a016c7ed2ee8733cb07dfc4533c46c5ab-key");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
