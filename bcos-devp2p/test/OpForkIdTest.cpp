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
 * @file OpForkIdTest.cpp
 * @brief EIP-2124 fork-id for OP-Stack chains (eth/OpForkId.h): the op-sepolia
 *        ladder, pinned against independently computed CRC32 values.
 * @date 2026/9/22
 */
#include <bcos-devp2p/eth/OpForkId.h>
#include <boost/test/unit_test.hpp>
#include <limits>

using namespace bcos;
using namespace bcos::devp2p;

BOOST_AUTO_TEST_SUITE(OpForkIdTest)

namespace
{
// The REAL op-sepolia inputs (superchain-registry superchain/configs/sepolia/op.toml):
//   chain_id = 11155420, genesis l2_time = 1691802540,
//   genesis.l2 hash = 0x102de6...9887d, block_time = 2.
// Fork-id ladder per op-geth (see OpForkId.h): canyon(=Shanghai), ecotone(=Cancun),
// fjord, granite, holocene, isthmus(=Prague), jovian, karst. Delta has no ChainConfig
// field and RegolithTime is 0, so neither is a fork-id point.
const h256 c_opSepoliaGenesisHash{std::string_view(
    "0x102de6ffb001480cc9b8b548fd05c34cd4f46ae4aa91759393db90ea0409887d"), h256::FromHex};
constexpr uint64_t c_genesisTime = 1691802540;
constexpr uint64_t kNever = std::numeric_limits<uint64_t>::max();
const eth::OpForkIdLadder c_opSepoliaLadder{
    1699981200,  // canyon
    1708534800,  // ecotone
    1716998400,  // fjord
    1723478400,  // granite
    1732633200,  // holocene
    1744905600,  // isthmus
    1763568001,  // jovian
    1781712001,  // karst
};
}  // namespace

// Pinned fork-id values for the real op-sepolia ladder, cross-checked against an
// independent EIP-2124 implementation (python zlib.crc32 chained over 8-byte
// big-endian fork timestamps, exactly geth's checksumUpdate).
BOOST_AUTO_TEST_CASE(opSepoliaLadderPinned)
{
    // Fresh node (head = genesis): checksum is bare crc32(genesisHash); next is the
    // first scheduled fork — every remote accepts this via EIP-2124 rule #2.
    auto atGenesis = eth::computeOpForkId(c_opSepoliaGenesisHash, c_genesisTime,
        c_genesisTime, c_opSepoliaLadder);
    BOOST_CHECK_EQUAL(atGenesis.hash, 0x67a40328u);
    BOOST_CHECK_EQUAL(atGenesis.next, 1699981200ull);  // canyon

    // Head inside each window: one more fork chained per passed activation.
    struct
    {
        uint64_t headTime;
        uint32_t hash;
        uint64_t next;
    } const windows[] = {
        {1699981200, 0xa48d6a00u, 1708534800ull},  // canyon passed, ecotone next
        {1708534800, 0xcc17c7ebu, 1716998400ull},  // ecotone passed, fjord next
        {1716998400, 0x540a8c5du, 1723478400ull},  // fjord passed, granite next
        {1723478400, 0x75dea41eu, 1732633200ull},  // granite passed, holocene next
        {1732633200, 0x4a1c792eu, 1744905600ull},  // holocene passed, isthmus next
        {1744905600, 0x6c625ee1u, 1763568001ull},  // isthmus passed, jovian next
        {1763568001, 0x079f2fd8u, 1781712001ull},  // jovian passed, karst next
        {1781712001, 0x91620456u, 0ull},           // karst passed, no known next
    };
    for (auto const& w : windows)
    {
        auto id = eth::computeOpForkId(c_opSepoliaGenesisHash, c_genesisTime, w.headTime,
            c_opSepoliaLadder);
        BOOST_CHECK_EQUAL(id.hash, w.hash);
        BOOST_CHECK_EQUAL(id.next, w.next);
    }
    // A head strictly between two forks (not exactly at an activation) announces the
    // same id as the window's opening block.
    auto midJovian = eth::computeOpForkId(
        c_opSepoliaGenesisHash, c_genesisTime, 1763568001 + 100, c_opSepoliaLadder);
    BOOST_CHECK_EQUAL(midJovian.hash, 0x079f2fd8u);
    BOOST_CHECK_EQUAL(midJovian.next, 1781712001ull);
}

// The isthmus-baseline shape (every existing OP chain: only the tail forks are
// scheduled, the pre-Isthmus rungs read UINT64_MAX). op-geth's gatherForks never
// collects a nil field, so the ladder compacts to just the scheduled rungs — an
// unscheduled intermediate fork must NOT terminate the checksum chain.
BOOST_AUTO_TEST_CASE(unscheduledIntermediateRungsDropped)
{
    eth::OpForkIdLadder const baseline{kNever, kNever, kNever, kNever, kNever, kNever,
        1763568001 /*jovian*/, 1781712001 /*karst*/};
    // Head past both: both rungs are chained even though six rungs precede them unset.
    auto pastAll = eth::computeOpForkId(c_opSepoliaGenesisHash, c_genesisTime, 1781712001, baseline);
    uint32_t expected = eth::crc32(
        bytesConstRef(c_opSepoliaGenesisHash.data(), c_opSepoliaGenesisHash.size()));
    expected = eth::forkIdAddForkPoint(expected, 1763568001);
    expected = eth::forkIdAddForkPoint(expected, 1781712001);
    BOOST_CHECK_EQUAL(pastAll.hash, expected);
    BOOST_CHECK_EQUAL(pastAll.next, 0ull);
    // Head before jovian: bare genesis checksum, jovian announced as next.
    auto early = eth::computeOpForkId(c_opSepoliaGenesisHash, c_genesisTime, c_genesisTime, baseline);
    BOOST_CHECK_EQUAL(early.hash,
        eth::crc32(bytesConstRef(c_opSepoliaGenesisHash.data(), c_opSepoliaGenesisHash.size())));
    BOOST_CHECK_EQUAL(early.next, 1763568001ull);
}

// op-geth drops time forks at or below the genesis timestamp (gatherForks' genesis
// filter) and dedups forks sharing one timestamp: a chain post-Bedrock-from-genesis
// whose early forks all activate at 0 must not chain any of them into the checksum.
BOOST_AUTO_TEST_CASE(genesisActiveForksSkipped)
{
    eth::OpForkIdLadder const fromGenesis{0, 0, 0, 0, 0, 0, 1763568001, 1781712001};
    auto id = eth::computeOpForkId(c_opSepoliaGenesisHash, c_genesisTime, 1781712001, fromGenesis);
    uint32_t expected = eth::crc32(
        bytesConstRef(c_opSepoliaGenesisHash.data(), c_opSepoliaGenesisHash.size()));
    expected = eth::forkIdAddForkPoint(expected, 1763568001);
    expected = eth::forkIdAddForkPoint(expected, 1781712001);
    BOOST_CHECK_EQUAL(id.hash, expected);
    BOOST_CHECK_EQUAL(id.next, 0ull);

    // Two forks activating at the same timestamp chain into the checksum ONCE
    // (op-geth dedups; e.g. a chain whose canyon and ecotone coincide).
    eth::OpForkIdLadder const dup{1700000000, 1700000000, kNever, kNever, kNever, kNever,
        kNever, kNever};
    auto dupId =
        eth::computeOpForkId(c_opSepoliaGenesisHash, c_genesisTime, 1700000001, dup);
    uint32_t dupExpected = eth::forkIdAddForkPoint(
        eth::crc32(bytesConstRef(c_opSepoliaGenesisHash.data(), c_opSepoliaGenesisHash.size())),
        1700000000);
    BOOST_CHECK_EQUAL(dupId.hash, dupExpected);
    BOOST_CHECK_EQUAL(dupId.next, 0ull);
}

BOOST_AUTO_TEST_SUITE_END()
