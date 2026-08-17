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
 * @file test_GenesisEmptyAllocL2.cpp
 * @brief Empty-alloc L2 genesis must publish mpt::emptyRootHash() as the genesis
 *        stateRoot, not a zero h256. The combination (feature_l2_ethereum_compat
 *        + no allocs) is rejected by NodeConfig::validateL2Invariants, but
 *        buildGenesisBlock is callable directly, and commitTrie() recognizes
 *        only emptyRootHash() as the from-empty marker — a zero parent root
 *        would send block 1's incremental build down the node-reading merge
 *        path and abort on the nonexistent zero-hash node.
 */
#include "L2GenesisTestStorage.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-ledger/Ledger.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/mpt/Constants.h"
#include "bcos-ledger/mpt/HashBuilder.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <boost/test/unit_test.hpp>
#include <map>
#include <memory>
#include <optional>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::protocol;

namespace bcos::test
{
namespace
{
struct GenesisEmptyAllocL2Fixture
{
    GenesisEmptyAllocL2Fixture() { m_blockFactory = createBlockFactory(createNormalCryptoSuite()); }

    // l2 flag on, allocs empty — constructed directly, bypassing
    // NodeConfig::validateL2Invariants (which forbids this combination).
    static GenesisConfig makeEmptyAllocL2Genesis()
    {
        GenesisConfig genesisConfig;
        genesisConfig.m_txGasLimit = 3000000000;
        genesisConfig.m_compatibilityVersion =
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_6_VERSION);
        genesisConfig.m_features.push_back(
            FeatureSet{Features::Flag::feature_l2_ethereum_compat, 1});
        genesisConfig.m_chainID = "901";
        genesisConfig.m_groupID = "group0";
        return genesisConfig;
    }

    task::Task<bcos::h256> buildAndReadGenesisStateRoot()
    {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(HashType(""));
        param.setBlockTxCountLimit(0);

        auto ok = co_await ledger::buildGenesisBlock(*ledger, makeEmptyAllocL2Genesis(), param);
        BOOST_REQUIRE(ok);

        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        co_return bcos::h256(block->blockHeader()->stateRoot());
    }

    BlockFactory::Ptr m_blockFactory;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(GenesisEmptyAllocL2Test, GenesisEmptyAllocL2Fixture)

// The genesis header must carry the canonical empty-trie root (keccak(rlp(""))),
// not a default-constructed zero h256.
BOOST_AUTO_TEST_CASE(EmptyAllocL2GenesisPublishesEmptyRootHash)
{
    task::syncWait([this]() -> task::Task<void> {
        auto stateRoot = co_await buildAndReadGenesisStateRoot();
        BOOST_CHECK_EQUAL(stateRoot, mpt::emptyRootHash());
        BOOST_CHECK_NE(stateRoot, bcos::h256{});
    }());
}

// Closing the gap end to end: block 1 hands the genesis stateRoot to commitTrie()
// as the parent root. With emptyRootHash() it takes the from-empty build (no node
// reads); a zero root would go down the incremental merge path and throw
// MPTInvariantViolation on the missing zero-hash node.
BOOST_AUTO_TEST_CASE(GenesisRootIsUsableAsCommitTrieParent)
{
    task::syncWait([this]() -> task::Task<void> {
        auto priorRoot = co_await buildAndReadGenesisStateRoot();

        // Empty node storage: exactly what a fresh empty-alloc chain has.
        bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes> nodeStorage;
        std::map<bcos::h256, std::optional<bcos::bytes>> changes;
        bcos::h256 key{};
        key.data()[0] = 0xab;
        changes[key] = bcos::bytes{0x42};

        auto result = co_await mpt::commitTrie(nodeStorage, priorRoot, changes);

        // Same root the stateless from-empty core derives for this single entry.
        std::map<bcos::h256, bcos::bytes> entries{{key, bcos::bytes{0x42}}};
        BOOST_CHECK_EQUAL(result.root, mpt::computeTrieRoot(entries).root);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
