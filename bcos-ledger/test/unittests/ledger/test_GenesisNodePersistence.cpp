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
 * @file test_GenesisNodePersistence.cpp
 * @brief L2 (scenario B) genesis trie-node persistence: buildGenesisBlock must write every
 *        account-trie and storage-sub-trie node as a path-addressed state row, so the block-1
 *        incremental MPT build (buildAndCollect over the genesis root) can read its parents.
 */
#include "../mpt/TestHelpers.h"
#include "GenesisFeatureFlagsHelper.h"
#include "L2GenesisTestStorage.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/storage/LegacyStorageMethods.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-ledger/Ledger.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/mpt/Account.h"
#include "bcos-ledger/mpt/Classify.h"
#include "bcos-ledger/mpt/Constants.h"
#include "bcos-ledger/mpt/HashBuilder.h"
#include "bcos-ledger/mpt/MPTBuilder.h"
#include "bcos-ledger/mpt/NodeDecoder.h"
#include "bcos-ledger/mpt/PathKey.h"
#include "bcos-ledger/mpt/StorageValueCodec.h"
#include "bcos-ledger/mpt/TrieNode.h"
#include "bcos-task/Wait.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-table/src/StateStorage.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace bcos;
using namespace bcos::ledger;
using namespace bcos::protocol;

namespace bcos::test
{
namespace
{

struct GenesisNodeFixture
{
    GenesisNodeFixture() { m_blockFactory = createBlockFactory(createNormalCryptoSuite()); }

    static LedgerConfig makeParam()
    {
        LedgerConfig param;
        param.setBlockNumber(0);
        param.setHash(crypto::HashType(""));
        param.setBlockTxCountLimit(0);
        return param;
    }

    static GenesisConfig baseConfig()
    {
        GenesisConfig genesis;
        genesis.m_txGasLimit = 3000000000;
        genesis.m_compatibilityVersion =
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_6_VERSION);
        genesis.m_chainID = "901";
        genesis.m_groupID = "group0";
        return genesis;
    }

    BlockFactory::Ptr m_blockFactory;
};

// Contract account: code + two storage slots (the storage sub-trie has real nodes).
constexpr std::string_view c_contractAddress = "43000000000000000000000000000000000000c0";
constexpr std::string_view c_contractCode = "6080604052";
// EOA account: balance only.
constexpr std::string_view c_eoaAddress = "1100000000000000000000000000000000000011";

std::string slotHex(char lastNibble)
{
    return std::string(63, '0') + lastNibble;
}

Alloc contractAlloc()
{
    return Alloc{.address = std::string(c_contractAddress),
        .balance = u256(500),
        .nonce = "1",
        .code = std::string(c_contractCode),
        .storage = {{slotHex('0'), std::string(60, '0') + "0385"},
            {slotHex('2'), std::string(62, '0') + "77"}}};
}

Alloc eoaAlloc()
{
    return Alloc{.address = std::string(c_eoaAddress),
        .balance = u256(1000),
        .nonce = "0",
        .code = "",
        .storage = {}};
}

// Count the persisted trie-node rows in the test storage (both node tables).
size_t countMPTRows(storage::StateStorage& storage)
{
    // parallelTraverse runs its callback from multiple TBB worker threads (one per bucket
    // group); the counter MUST be atomic — a plain size_t loses increments under concurrent
    // read-modify-write (intermittent undercounts, e.g. once the TBB pool is warm on CI).
    std::atomic<size_t> count{0};
    storage.parallelTraverse(false, [&](std::string_view table, std::string_view, auto const&) {
        if (table == ledger::mpt::kMPTAccountTable || table == ledger::mpt::kMPTStorageTable)
        {
            ++count;
        }
        return true;
    });
    return count.load();
}

// Read one persisted trie-node row BY POSITION; REQUIRE it exists and its content hashes back to
// the digest the parent (or the block header, for a root) recorded for it.
task::Task<bcos::bytes> readNodeRowChecked(
    storage::StorageInterface& storage, ledger::mpt::PathKey const& key, h256 expected)
{
    auto entry = co_await storage2::readOne(storage, ledger::mpt::pathNodeStateKey(key));
    BOOST_REQUIRE_MESSAGE(entry.has_value(),
        "missing genesis trie node row at position 0x" + bcos::toHex(key.position));
    auto raw = entry->get();
    bcos::bytes rlp(raw.begin(), raw.end());
    BOOST_CHECK_EQUAL(crypto::keccak256Hash(bcos::ref(rlp)).hex(), expected.hex());
    co_return rlp;
}

task::Task<void> walkNodeAt(storage::StorageInterface& storage, ledger::mpt::PathKey key,
    h256 expected, std::set<ledger::mpt::PathKey>& visited, size_t& storageTrieNodes);

// Walk one decoded node's children (inline children recurse in-memory; hash children go back
// through the stored rows, addressed by the child's position — parent position plus the nibbles
// the step consumes, spec A.1).
task::Task<void> walkDecoded(storage::StorageInterface& storage, ledger::mpt::TrieNode const& node,
    ledger::mpt::PathKey const& position, std::set<ledger::mpt::PathKey>& visited,
    size_t& storageTrieNodes)
{
    namespace mpt = ledger::mpt;
    auto childKey = [&](bcos::bytesConstRef consumed) {
        mpt::PathKey out{.scope = position.scope, .position = position.position};
        out.position.insert(out.position.end(), consumed.begin(), consumed.end());
        return out;
    };
    if (auto const* leaf = std::get_if<mpt::LeafNode>(&node))
    {
        if (position.scope.kind == mpt::TrieKind::Account)
        {
            auto account = mpt::Account::decode(bcos::ref(leaf->value));
            if (account.storageRoot != mpt::emptyRootHash())
            {
                // The owner falls out of the walk itself: the position walked to this leaf,
                // concatenated with the leaf's suffix, IS the account's trie key (spec §8.3).
                auto ownerNibbles = position.position;
                ownerNibbles.insert(
                    ownerNibbles.end(), leaf->keyNibbles.begin(), leaf->keyNibbles.end());
                auto ownerBytes = mpt::nibblesToBytes(bcos::ref(ownerNibbles));
                h256 const owner{bcos::ref(ownerBytes)};
                co_await walkNodeAt(storage, mpt::storageRootPathKey(owner), account.storageRoot,
                    visited, storageTrieNodes);
            }
        }
        co_return;
    }
    if (auto const* ext = std::get_if<mpt::ExtensionNode>(&node))
    {
        if (ext->child.size() == mpt::HASH_REF_ENCODED_SIZE &&
            ext->child.front() == mpt::RLP_HASH_REF_PREFIX)
        {
            auto childHash = h256(bcos::bytesConstRef(ext->child.data() + 1, h256::SIZE));
            co_await walkNodeAt(storage, childKey(bcos::ref(ext->sharedNibbles)), childHash,
                visited, storageTrieNodes);
        }
        else
        {
            auto child = mpt::decodeNode(bcos::ref(ext->child));
            co_await walkDecoded(
                storage, child, childKey(bcos::ref(ext->sharedNibbles)), visited, storageTrieNodes);
        }
        co_return;
    }
    if (auto const* branch = std::get_if<mpt::BranchNode>(&node))
    {
        for (size_t nibble = 0; nibble < mpt::NIBBLE_RANGE; ++nibble)
        {
            auto const& ref = branch->children[nibble];
            if (ref.isAbsent())
            {
                continue;
            }
            bcos::bytes const consumed{static_cast<bcos::byte>(nibble)};
            if (ref.kind() == mpt::NodeRef::Kind::Hash)
            {
                co_await walkNodeAt(
                    storage, childKey(bcos::ref(consumed)), ref.hash(), visited, storageTrieNodes);
            }
            else
            {
                auto child = mpt::decodeNode(ref.inlineRef());
                co_await walkDecoded(
                    storage, child, childKey(bcos::ref(consumed)), visited, storageTrieNodes);
            }
        }
    }
    co_return;
}

task::Task<void> walkNodeAt(storage::StorageInterface& storage, ledger::mpt::PathKey key,
    h256 expected, std::set<ledger::mpt::PathKey>& visited, size_t& storageTrieNodes)
{
    if (!visited.insert(key).second)
    {
        co_return;
    }
    if (key.scope.kind == ledger::mpt::TrieKind::Storage)
    {
        ++storageTrieNodes;
    }
    auto rlp = co_await readNodeRowChecked(storage, key, expected);
    auto node = ledger::mpt::decodeNode(bcos::ref(rlp));
    co_await walkDecoded(storage, node, key, visited, storageTrieNodes);
}

// storage2 node-storage adapter over the ledger's StateStorage: the same path-addressed rows
// ViewNodeStorage (transaction-scheduler) reads at block 1, expressed over the test storage.
class LedgerNodeStorage
{
public:
    using Key = ledger::mpt::PathKey;
    using Value = bcos::bytes;

    explicit LedgerNodeStorage(storage::StateStorage& backend) : m_backend(&backend) {}

    task::Task<std::optional<bcos::bytes>> readOne(Key key)
    {
        auto entry = co_await storage2::readOne(*m_backend, ledger::mpt::pathNodeStateKey(key));
        if (!entry)
        {
            co_return std::nullopt;
        }
        auto raw = entry->get();
        co_return bcos::bytes(raw.begin(), raw.end());
    }

    task::Task<std::vector<std::optional<bcos::bytes>>> readSome(::ranges::input_range auto keys)
    {
        std::vector<std::optional<bcos::bytes>> values;
        for (auto const& key : keys)
        {
            values.emplace_back(co_await readOne(key));
        }
        co_return values;
    }

    task::Task<void> writeOne(Key key, bcos::bytes value)
    {
        storage::Entry entry;
        entry.set(std::move(value));
        co_await storage2::writeOne(
            *m_backend, ledger::mpt::pathNodeStateKey(key), std::move(entry));
    }

    task::Task<void> writeSome(::ranges::input_range auto keyValues)
    {
        for (auto&& keyValue : keyValues)
        {
            auto const& [key, value] = keyValue;
            co_await writeOne(key, bcos::bytes(value.begin(), value.end()));
        }
    }

    task::Task<void> removeOne(Key key)
    {
        // StateStorage expresses deletion as a DELETED-status entry rather than a removeOne.
        storage::Entry entry;
        entry.setStatus(storage::Entry::DELETED);
        co_await storage2::writeOne(
            *m_backend, ledger::mpt::pathNodeStateKey(key), std::move(entry));
    }

    task::Task<void> removeSome(::ranges::input_range auto keys)
    {
        for (auto const& key : keys)
        {
            co_await removeOne(key);
        }
    }

    /// Seek range over the node rows. StateStorage has no ordered iterator, so this snapshots
    /// every node row through parallelTraverse and sorts — fine for a genesis-sized fixture, and
    /// it gives buildAndCollect's storage-trie drop the same view a real ordered store would.
    struct Iterator
    {
        std::vector<std::pair<Key, bcos::bytes>> rows;
        size_t index = 0;

        task::Task<std::optional<std::tuple<Key, storage2::StorageValueType<bcos::bytes>>>> next()
        {
            if (index >= rows.size())
            {
                co_return std::nullopt;
            }
            auto& [key, value] = rows[index++];
            co_return std::make_tuple(key, storage2::StorageValueType<bcos::bytes>{value});
        }
    };

    task::Task<Iterator> range(storage2::RANGE_SEEK_TYPE /*unused*/, Key const& start)
    {
        Iterator iterator;
        m_backend->parallelTraverse(false, [&](std::string_view table, std::string_view rowKey,
                                               auto const& entry) {
            executor_v1::StateKey const stateKey{table, rowKey};
            if (auto parsed = ledger::mpt::parsePathNodeStateKey(stateKey))
            {
                auto raw = entry.get();
                iterator.rows.emplace_back(std::move(*parsed), bcos::bytes(raw.begin(), raw.end()));
            }
            return true;
        });
        std::sort(iterator.rows.begin(), iterator.rows.end(),
            [](auto const& lhs, auto const& rhs) { return lhs.first < rhs.first; });
        iterator.rows.erase(iterator.rows.begin(),
            std::lower_bound(iterator.rows.begin(), iterator.rows.end(), start,
                [](auto const& row, Key const& key) { return row.first < key; }));
        co_return iterator;
    }

private:
    storage::StateStorage* m_backend;
};

}  // namespace

BOOST_FIXTURE_TEST_SUITE(GenesisNodePersistenceTest, GenesisNodeFixture)

// (a) L2 genesis with non-empty allocs: every node reachable from the genesis state root —
// account trie AND per-account storage sub-tries — exists as a "/mpt/" row whose content
// hashes back to its key, and no unreachable garbage rows are written.
BOOST_AUTO_TEST_CASE(L2GenesisPersistsAllTrieNodes)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        auto genesisConfig = baseConfig();
        genesisConfig.m_features.push_back(
            FeatureSet{Features::Flag::feature_l2_ethereum_compat, 1});
        genesisConfig.m_allocs.push_back(contractAlloc());
        genesisConfig.m_allocs.push_back(eoaAlloc());
        appendGenesisFeatureFlagsSlot(genesisConfig);

        auto ok = co_await ledger::buildGenesisBlock(*ledger, genesisConfig, makeParam());
        BOOST_REQUIRE(ok);

        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        h256 root = block->blockHeader()->stateRoot();
        BOOST_REQUIRE_NE(root, h256{});
        BOOST_REQUIRE_NE(root, ledger::mpt::emptyRootHash());

        std::set<ledger::mpt::PathKey> visited;
        size_t storageTrieNodes = 0;
        co_await walkNodeAt(
            *storage, ledger::mpt::accountRootPathKey(), root, visited, storageTrieNodes);

        // The contract's storage sub-trie must be persisted too: block 1 reads its parent
        // nodes when the account's slots change.
        BOOST_CHECK_GE(storageTrieNodes, 1);
        // Exactly the reachable nodes are stored: no missing rows (walk REQUIREs each), and
        // no unreachable extras.
        BOOST_CHECK_EQUAL(countMPTRows(*storage), visited.size());
    }());
}

// (b) L2 flag with an empty alloc set (root = empty-trie root): nothing to persist.
BOOST_AUTO_TEST_CASE(EmptyAllocsWriteNoNodeRows)
{
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        auto genesisConfig = baseConfig();
        genesisConfig.m_features.push_back(
            FeatureSet{Features::Flag::feature_l2_ethereum_compat, 1});

        auto ok = co_await ledger::buildGenesisBlock(*ledger, genesisConfig, makeParam());
        BOOST_REQUIRE(ok);
        BOOST_CHECK_EQUAL(countMPTRows(*storage), 0);
    }());
}

// (c) Non-MPT chains write no node rows: neither a plain pbft genesis nor an alloc-carrying
// genesis without feature_l2_ethereum_compat (the header still pins the alloc root, but no
// block will ever build an MPT on top of it).
BOOST_AUTO_TEST_CASE(NonL2ChainsWriteNoNodeRows)
{
    task::syncWait([this]() -> task::Task<void> {
        {
            auto storage = makeL2GenesisTestStorage();
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            auto ok = co_await ledger::buildGenesisBlock(*ledger, baseConfig(), makeParam());
            BOOST_REQUIRE(ok);
            BOOST_CHECK_EQUAL(countMPTRows(*storage), 0);
        }
        {
            auto storage = makeL2GenesisTestStorage();
            auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);
            auto genesisConfig = baseConfig();
            genesisConfig.m_allocs.push_back(contractAlloc());
            appendGenesisFeatureFlagsSlot(genesisConfig);
            auto ok = co_await ledger::buildGenesisBlock(*ledger, genesisConfig, makeParam());
            BOOST_REQUIRE(ok);
            BOOST_CHECK_EQUAL(countMPTRows(*storage), 0);
        }
    }());
}

// (d) End-to-end block-1 shape: an incremental buildAndCollect with the genesis root as
// parent must succeed (it reads the persisted genesis nodes) and produce the same root as a
// from-scratch build over the expected post-block state.
BOOST_AUTO_TEST_CASE(BlockOneIncrementalBuildOverGenesisRoot)
{
    namespace mpt = ledger::mpt;
    namespace mpttest = ledger::mpt::test;
    task::syncWait([this]() -> task::Task<void> {
        auto storage = makeL2GenesisTestStorage();
        auto ledger = std::make_shared<Ledger>(m_blockFactory, storage, 1);

        auto genesisConfig = baseConfig();
        genesisConfig.m_features.push_back(
            FeatureSet{Features::Flag::feature_l2_ethereum_compat, 1});
        genesisConfig.m_allocs.push_back(contractAlloc());
        genesisConfig.m_allocs.push_back(eoaAlloc());
        appendGenesisFeatureFlagsSlot(genesisConfig);

        auto ok = co_await ledger::buildGenesisBlock(*ledger, genesisConfig, makeParam());
        BOOST_REQUIRE(ok);

        auto block = co_await ledger::getBlockData(*ledger, 0, HEADER);
        BOOST_REQUIRE(block);
        h256 genesisRoot = block->blockHeader()->stateRoot();

        // Block-1 delta: subsequent-touch of the contract account — balance update + one NEW
        // storage slot. The build must read the genesis account-trie nodes (parent leaf) and
        // the genesis storage-sub-trie nodes (slot merge) through the persisted rows.
        bcos::Address contractAddr;
        boost::algorithm::unhex(
            c_contractAddress.begin(), c_contractAddress.end(), contractAddr.data());

        h256 newSlot{};
        newSlot.data()[31] = 0x05;
        bcos::bytes newSlotValue(32, 0);
        newSlotValue[31] = 0x99;

        mpttest::FlatBackendStorage flatBackend;
        auto view = mpttest::makeFlatView(flatBackend);
        mpttest::writeFlatRow(view, mpttest::accountFieldKey(contractAddr, mpt::ROW_BALANCE),
            mpttest::makeEntry("777"));
        mpttest::writeFlatRow(view, mpttest::accountSlotKey(contractAddr, newSlot),
            mpttest::makeEntry(std::string_view{
                reinterpret_cast<char const*>(newSlotValue.data()), newSlotValue.size()}));

        LedgerNodeStorage nodeStorage(*storage);
        auto output =
            co_await mpt::buildAndCollect(nodeStorage, genesisRoot, view, /*l2Mode=*/true);

        // Oracle: from-scratch trie over the expected post-block state.
        auto keccak = [](bcos::bytes const& data) {
            return crypto::keccak256Hash(bcos::ref(data));
        };

        std::map<h256, bcos::bytes> storageEntries;
        // iterate the ACTUAL genesis alloc (includes the feature_flags Entry
        // slot the helper appended), not a fresh contractAlloc()
        for (auto const& [slotHexKey, valueHex] : genesisConfig.m_allocs[0].storage)
        {
            auto slotBytes = bcos::fromHex(slotHexKey);
            auto valueBytes = bcos::fromHex(valueHex);
            storageEntries[keccak(slotBytes)] = mpt::encodeStorageValue(bcos::ref(valueBytes));
        }
        auto newSlotBytes = bcos::bytes(newSlot.begin(), newSlot.end());
        storageEntries[keccak(newSlotBytes)] = mpt::encodeStorageValue(bcos::ref(newSlotValue));
        auto expectedStorageRoot = mpt::computeTrieRoot(storageEntries).root;

        auto codeBytes = bcos::fromHex(std::string(c_contractCode));
        std::map<h256, bcos::bytes> accountEntries;
        {
            bcos::bytes rlp;
            codec::rlp::encode(rlp, uint64_t(1), u256(777), expectedStorageRoot, keccak(codeBytes));
            auto addrBytes = bcos::fromHex(std::string(c_contractAddress));
            accountEntries[keccak(addrBytes)] = std::move(rlp);
        }
        {
            bcos::bytes rlp;
            codec::rlp::encode(
                rlp, uint64_t(0), u256(1000), mpt::emptyRootHash(), mpt::emptyCodeHash());
            auto addrBytes = bcos::fromHex(std::string(c_eoaAddress));
            accountEntries[keccak(addrBytes)] = std::move(rlp);
        }
        auto expectedRoot = mpt::computeTrieRoot(accountEntries).root;

        BOOST_CHECK_EQUAL(output.stateRoot.hex(), expectedRoot.hex());
        BOOST_CHECK_NE(output.stateRoot, genesisRoot);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
