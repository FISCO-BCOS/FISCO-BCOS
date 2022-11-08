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
 */
/**
 * @brief : unitest for LedgerCache
 * @author: jimmyshi
 * @date: 2022-11-08
 */

#include "../mock/MockLedger.h"
#include "bcos-executor/src/executive/LedgerCache.h"
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace bcos;
using namespace bcos::executor;

namespace bcos
{
namespace test
{
struct LedgerCacheFixture
{
    LedgerCacheFixture()
    {
        mockLedger = std::make_shared<MockLedger>();
        ledgerCache = std::make_shared<LedgerCache>(mockLedger);
    }

    MockLedger::Ptr mockLedger;
    LedgerCache::Ptr ledgerCache;
};

BOOST_FIXTURE_TEST_SUITE(LedgerCacheTest, LedgerCacheFixture)

BOOST_AUTO_TEST_CASE(fetchBlockHashTest)
{
    for (int i = 1; i < 10; i++)
    {
        ledgerCache->setBlockNumber2Hash(i, h256(100 + i));
    }

    BOOST_CHECK_EQUAL(h256(0), ledgerCache->fetchBlockHash(0));

    for (int i = 1; i < 10; i++)
    {
        BOOST_CHECK_EQUAL(h256(100 + i), ledgerCache->fetchBlockHash(i));
    }

    ledgerCache->clearCacheByNumber(5);

    for (int i = 0; i < 5; i++)
    {
        BOOST_CHECK_EQUAL(h256(i), ledgerCache->fetchBlockHash(i));
    }

    for (int i = 5; i < 10; i++)
    {
        BOOST_CHECK_EQUAL(h256(100 + i), ledgerCache->fetchBlockHash(i));
    }
}

BOOST_AUTO_TEST_CASE(fetchGasLimitTest)
{
    auto gasLimit = MockLedger::TX_GAS_LIMIT;
    BOOST_CHECK_EQUAL(gasLimit, ledgerCache->fetchTxGasLimit());
}


BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
