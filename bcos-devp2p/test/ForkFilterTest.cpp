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
 * @file ForkFilterTest.cpp
 * @brief EIP-2124 fork-id validation (ForkFilter): the four acceptance rules over a
 *        synthetic block+time ladder, plus the mainnet Frontier/Petersburg checksums.
 * @date 2026/11/3
 */
#include <bcos-devp2p/eth/ForkId.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>
#include <vector>

using namespace bcos;
using namespace bcos::devp2p;

BOOST_AUTO_TEST_SUITE(ForkFilterTest)

namespace
{
h256 const c_mainnetGenesis(
    std::string_view("0xd4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3"),
    h256::FromHex);
// Homestead, DAO, Tangerine, Spurious, Byzantium, Constantinople(=Petersburg), Istanbul.
std::vector<uint64_t> const c_mainnetBlockForks = {
    1150000, 1920000, 2463000, 2675000, 4370000, 7280000, 7280000, 9069000};
}  // namespace

// EIP-2124 mainnet examples: Frontier head -> {0xfc64ec04, 1150000}; Petersburg head
// 7987396 -> {0x668db0af, 9069000}.
BOOST_AUTO_TEST_CASE(mainnetChecksums)
{
    uint32_t sum = eth::crc32(bytesConstRef(c_mainnetGenesis.data(), c_mainnetGenesis.size()));
    BOOST_CHECK_EQUAL(sum, 0xfc64ec04u);
    uint64_t previous = 0;
    for (auto fork : c_mainnetBlockForks)
    {
        if (fork == 9069000 || fork == previous)
        {
            continue;
        }
        previous = fork;
        sum = eth::forkIdAddForkPoint(sum, fork);
    }
    BOOST_CHECK_EQUAL(sum, 0x668db0afu);

    eth::ForkFilter filter(c_mainnetGenesis, 1438269973, c_mainnetBlockForks, {});
    // Local Petersburg (7987396), remote Petersburg announcing Istanbul: accept (#1b).
    BOOST_CHECK(filter.accepts({0x668db0af, 9069000}, 7987396, 0));
    // Remote Byzantium-only announcing Constantinople as next: subset, accept (#2).
    uint32_t byzantium = 0xfc64ec04u;
    for (auto fork : {1150000u, 1920000u, 2463000u, 2675000u, 4370000u})
    {
        byzantium = eth::forkIdAddForkPoint(byzantium, fork);
    }
    BOOST_CHECK(filter.accepts({byzantium, 7280000}, 7987396, 0));
    // Remote Byzantium announcing no next fork: it is stale (#2 mismatch) -> reject.
    BOOST_CHECK(!filter.accepts({byzantium, 0}, 7987396, 0));
    // Local Frontier (head 0), remote Petersburg: superset, accept (#3).
    BOOST_CHECK(filter.accepts({0x668db0af, 9069000}, 0, 0));
    // Remote on an unknown chain: reject (#4).
    BOOST_CHECK(!filter.accepts({0xdeadbeef, 0}, 7987396, 0));
    // Remote Petersburg announcing a next fork (block 7500000) we already passed without
    // knowing it: we are the stale one (#1a) -> reject.
    BOOST_CHECK(!filter.accepts({0x668db0af, 7500000}, 7987396, 0));
}

// Timestamp ladder: the fork walk switches from block numbers to the head timestamp.
BOOST_AUTO_TEST_CASE(timestampLadderRules)
{
    h256 const genesis(
        std::string_view("0x1111111111111111111111111111111111111111111111111111111111111111"),
        h256::FromHex);
    uint64_t const genesisTime = 1000;
    std::vector<uint64_t> const blockForks = {100};
    // 0 = active at genesis (skipped), 1000 = at genesis time (skipped), 2000/3000 real,
    // UINT64_MAX = unscheduled tail (ends the ladder).
    std::vector<uint64_t> const timeForks = {0, 1000, 2000, 3000, UINT64_MAX};
    eth::ForkFilter filter(genesis, genesisTime, blockForks, timeForks);

    uint32_t const s0 = eth::crc32(bytesConstRef(genesis.data(), genesis.size()));
    uint32_t const s1 = eth::forkIdAddForkPoint(s0, 100);
    uint32_t const s2 = eth::forkIdAddForkPoint(s1, 2000);
    uint32_t const s3 = eth::forkIdAddForkPoint(s2, 3000);

    // Local head past block 100 and time 2000, before 3000: our own id is {s2, 3000}.
    BOOST_CHECK(filter.accepts({s2, 3000}, 150, 2500));
    // Remote behind us at the block fork, announcing it: accept; wrong next: stale.
    BOOST_CHECK(filter.accepts({s0, 100}, 150, 2500));
    BOOST_CHECK(!filter.accepts({s0, 2000}, 150, 2500));
    // Remote behind us at the time fork 2000, announcing it: accept.
    BOOST_CHECK(filter.accepts({s1, 2000}, 150, 2500));
    // Remote ahead (passed 3000): superset, accept.
    BOOST_CHECK(filter.accepts({s3, 0}, 150, 2500));
    // Remote matches us but announces a TIME next (2400) that our head time passed: we
    // are stale -> reject (next above the timestamp threshold is compared to time).
    BOOST_CHECK(!filter.accepts({s2, 1438269973ull + 1}, 150, 1438269973ull + 10));
    // Everything passed locally (head beyond every fork): {s3, 0} accepted, {s3, future}
    // accepted too (future fork not yet passed).
    BOOST_CHECK(filter.accepts({s3, 0}, 150, 5000));
    BOOST_CHECK(filter.accepts({s3, 9000}, 150, 5000));
    BOOST_CHECK(!filter.accepts({0x12345678, 0}, 150, 5000));
}

BOOST_AUTO_TEST_SUITE_END()
