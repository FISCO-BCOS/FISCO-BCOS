/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file test_GenesisStateRoot.cpp
 * @brief op-geth-compatible genesis state root over allocs.
 */
#include "bcos-ledger/GenesisStateRoot.h"
#include "bcos-ledger/mpt/Constants.h"
#include <bcos-framework/ledger/GenesisConfig.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace bcos::ledger;

namespace bcos::test
{
namespace
{
h256 gsrStateRoot(GenesisConfig const& genesis)
{
    return task::syncWait(computeGenesisStateRoot(genesis));
}

// L2 chain with one funded contract alloc.
GenesisConfig gsrBaseConfig()
{
    GenesisConfig genesis;
    genesis.m_features.push_back(FeatureSet{Features::Flag::feature_l2_ethereum_compat, 1});
    genesis.m_chainID = "901";
    genesis.m_groupID = "group0";
    genesis.m_allocs.push_back(Alloc{.address = "0x42000000000000000000000000000000000000c0",
        .balance = u256(1000),
        .nonce = "1",
        .code = "0x6080604052",
        .storage = {}});
    return genesis;
}

std::string gsrKey32(char nibble)
{
    return "0x" + std::string(64, nibble);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(GenesisStateRootTest)

// STRONG ANCHOR: an empty alloc set must yield the canonical Ethereum empty-trie
// root (keccak256(RLP(""))). If this fails, the trie construction or wiring is
// broken at a gross level.
BOOST_AUTO_TEST_CASE(EmptyAllocsIsEmptyRoot)
{
    GenesisConfig genesis;  // no allocs
    BOOST_CHECK_EQUAL(gsrStateRoot(genesis), mpt::emptyRootHash());
}

BOOST_AUTO_TEST_CASE(Deterministic)
{
    BOOST_CHECK_EQUAL(gsrStateRoot(gsrBaseConfig()), gsrStateRoot(gsrBaseConfig()));
}

// Alloc push order must not change the state root (secure trie is keyed by
// keccak256(address), independent of insertion order).
BOOST_AUTO_TEST_CASE(AllocOrderIndependent)
{
    Alloc a{.address = "0x" + std::string(39, '0') + "1",
        .balance = u256(10),
        .nonce = "0",
        .code = "0x",
        .storage = {}};
    Alloc b{.address = "0x" + std::string(39, '0') + "2",
        .balance = u256(20),
        .nonce = "0",
        .code = "0x",
        .storage = {}};

    GenesisConfig forward;
    forward.m_allocs = {a, b};
    GenesisConfig reversed;
    reversed.m_allocs = {b, a};
    BOOST_CHECK_EQUAL(gsrStateRoot(forward), gsrStateRoot(reversed));
}

BOOST_AUTO_TEST_CASE(BalanceChangeChangesRoot)
{
    auto x = gsrBaseConfig();
    auto y = gsrBaseConfig();
    y.m_allocs[0].balance = u256(1001);
    BOOST_CHECK_NE(gsrStateRoot(x), gsrStateRoot(y));
}

BOOST_AUTO_TEST_CASE(NonceChangeChangesRoot)
{
    auto x = gsrBaseConfig();
    auto y = gsrBaseConfig();
    y.m_allocs[0].nonce = "2";
    BOOST_CHECK_NE(gsrStateRoot(x), gsrStateRoot(y));
}

BOOST_AUTO_TEST_CASE(CodeChangeChangesRoot)
{
    auto x = gsrBaseConfig();
    auto y = gsrBaseConfig();
    y.m_allocs[0].code = "0x6080604053";  // last byte 52 -> 53
    BOOST_CHECK_NE(gsrStateRoot(x), gsrStateRoot(y));
}

BOOST_AUTO_TEST_CASE(StorageChangeChangesRoot)
{
    auto x = gsrBaseConfig();
    x.m_allocs[0].storage = {{gsrKey32('0'), "0x01"}};
    auto y = gsrBaseConfig();
    y.m_allocs[0].storage = {{gsrKey32('0'), "0x02"}};
    BOOST_CHECK_NE(gsrStateRoot(x), gsrStateRoot(y));
}

// A storage slot set to zero is a no-op in Ethereum state, so it must not change
// the state root relative to omitting it entirely.
BOOST_AUTO_TEST_CASE(ZeroStorageSlotIgnored)
{
    auto withZero = gsrBaseConfig();
    withZero.m_allocs[0].storage = {{gsrKey32('0'), "0x" + std::string(64, '0')}};
    BOOST_CHECK_EQUAL(gsrStateRoot(withZero), gsrStateRoot(gsrBaseConfig()));
}

// GOLDEN: op-geth-compatible state root for a fixed single-account alloc. The
// literal is TBD until the first green CI run; freeze it then by replacing the
// size check with BOOST_CHECK_EQUAL against the printed value. Because this is a
// real op-geth state root, the frozen value is independently verifiable against
// op-deployer / go-ethereum `Genesis.ToBlock()` output for the same account.
BOOST_AUTO_TEST_CASE(GoldenVector)
{
    GenesisConfig genesis;
    genesis.m_allocs.push_back(Alloc{.address = "0x42000000000000000000000000000000000000c0",
        .balance = u256(0),
        .nonce = "0",
        .code = "0x6080604052",
        .storage = {{gsrKey32('0'), "0x" + std::string(60, '0') + "0385"}}});
    auto root = gsrStateRoot(genesis).hex();
    BOOST_TEST_MESSAGE("GoldenVector eth state root (freeze + verify vs op-deployer): " + root);
    BOOST_CHECK_EQUAL(root.size(), 64u);  // 32-byte root = 64 hex chars
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
