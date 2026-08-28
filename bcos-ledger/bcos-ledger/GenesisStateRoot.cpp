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
 * @file GenesisStateRoot.cpp
 */
#include "GenesisStateRoot.h"
#include "bcos-storage/KeyPrefixes.h"
#include "mpt/Constants.h"
#include "mpt/HashBuilder.h"
#include "mpt/StorageValueCodec.h"
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <evmc/evmc.h>
#include <map>
#include <set>

using namespace bcos;
using namespace bcos::ledger;

executor_v1::StateKey bcos::ledger::mptNodeStateKey(bcos::h256 const& hash)
{
    return storage2::mptNodeStateKey(hash);
}

namespace
{
bcos::h256 keccak(bcos::bytesConstRef data)
{
    return crypto::keccak256Hash(data);
}

// storageRoot over one account's storage slots, as a secure trie:
//   key   = keccak256(slot bytes as configured)
//   value = RLP(value-with-leading-zero-bytes-trimmed)   (mpt::encodeStorageValue)
// Zero-valued slots are skipped (no-op in Ethereum state). Empty -> emptyRoot.
// The slot keccak stays over the raw configured bytes (not mpt::slotKeyHash, which
// right-aligns into a fixed 32 bytes) to keep genesis hashing byte-identical.
bcos::task::Task<mpt::TrieBuildResult> storageTrieOf(std::vector<Alloc::State> const& storage)
{
    std::map<bcos::h256, bcos::bytes> entries;
    std::set<bcos::h256> seenSlots;
    for (auto const& [slotHex, valueHex] : storage)
    {
        // Exact-width decode shared with the importers, BEFORE the zero-value
        // skip: a padded/truncated slot here would root the trie over bytes the
        // import later rejects — and a malformed key must fail even when its
        // value is zero (the importer validates it before writing).
        evmc_bytes32 slotValue{};
        unhexAllocExact(valueHex, "storage slot value", slotValue.bytes, sizeof(slotValue.bytes));
        evmc_bytes32 slot{};
        unhexAllocExact(slotHex, "storage slot key", slot.bytes, sizeof(slot.bytes));
        auto slotKeyHash = keccak(bcos::bytesConstRef(slot.bytes, sizeof(slot.bytes)));
        // Reject a repeated slot key BEFORE the zero-value skip: the importers
        // apply every slot in order (setStorage overwrites, a zero value
        // included) so duplicates are last-wins for the flat state — but the
        // skip below makes them NOT last-wins for the trie ((K, 5) then (K, 0)
        // would root at K=5 while the flat state holds K=0). Same guard as the
        // duplicate-address check in computeGenesisStateTrie.
        if (!seenSlots.insert(slotKeyHash).second)
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "genesis alloc storage slot key " + slotHex +
                                      " is duplicated"));
        }
        auto rlpValue =
            mpt::encodeStorageValue(bcos::bytesConstRef(slotValue.bytes, sizeof(slotValue.bytes)));
        if (rlpValue.empty())
        {
            continue;  // zero value: not part of the storage trie
        }
        entries[slotKeyHash] = std::move(rlpValue);
    }
    if (entries.empty())
    {
        co_return mpt::TrieBuildResult{.root = mpt::emptyRootHash(), .newNodes = {}};
    }
    // From-empty build through the stateless core: root + every produced node.
    co_return mpt::computeTrieRoot(entries);
}
}  // namespace

bcos::task::Task<bcos::h256> bcos::ledger::computeGenesisStateRoot(GenesisConfig const& genesis)
{
    auto trie = co_await computeGenesisStateTrie(genesis);
    co_return trie.root;
}

bcos::task::Task<bcos::ledger::GenesisStateTrie> bcos::ledger::computeGenesisStateTrie(
    GenesisConfig const& genesis)
{
    std::map<bcos::h256, bcos::bytes> stateEntries;
    std::unordered_map<bcos::h256, bcos::bytes> nodes;
    // Reject a repeated address: the map below is last-wins (the root would commit only the
    // final alloc) while both importers apply EVERY alloc in order (storage slots accumulate,
    // later nonce/balance/code overwrite). A duplicated address therefore makes the returned
    // root and the written flat state disagree — exactly the failure the caller's root
    // comparison exists to catch. NodeConfig::loadAllocs guards the INI path; this is the same
    // guard for direct GenesisConfig callers (which run this pass before their first write).
    std::set<bcos::h256> seenAddresses;

    for (auto const& alloc : genesis.m_allocs)
    {
        auto storageTrie = co_await storageTrieOf(alloc.storage);
        auto storageRoot = storageTrie.root;
        nodes.merge(storageTrie.newNodes);

        auto codeBytes = unhexAllocBytes(alloc.code, "code");
        bcos::h256 codeHash = codeBytes.empty() ?
                                  mpt::emptyCodeHash() :
                                  keccak(bcos::bytesConstRef(codeBytes.data(), codeBytes.size()));

        // Ethereum StateAccount RLP: [nonce, balance, storageRoot, codeHash].
        // nonce/balance encode as minimal big-endian RLP integers (0 -> 0x80);
        // storageRoot/codeHash as 32-byte RLP strings (0xa0 || 32 bytes).
        // NodeConfig::loadAllocs guards the INI path; direct GenesisConfig callers
        // are guarded here the same way as every other alloc field, so a malformed
        // nonce fails with the field-naming InvalidConfig contract instead of an
        // unnamed boost::bad_lexical_cast.
        uint64_t nonce = 0;
        if (!alloc.nonce.empty())
        {
            try
            {
                nonce = boost::lexical_cast<uint64_t>(alloc.nonce);
            }
            catch (boost::bad_lexical_cast const&)
            {
                BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                          "genesis alloc nonce is not a valid uint64: " +
                                          alloc.nonce));
            }
        }
        bcos::bytes accountRlp;
        codec::rlp::encode(accountRlp, nonce, alloc.balance, storageRoot, codeHash);

        // Secure state trie: leaf key = keccak256(address20).
        evmc_address addr{};
        unhexAllocExact(alloc.address, "address", addr.bytes, sizeof(addr.bytes));

        // Reject an alloc at a FISCO system address: EVMAccount routes those to the /sys/
        // table prefix, while the state trie hashes every alloc as an ordinary /apps/
        // account — the flat state and the returned root would disagree and the caller's
        // root comparison would still pass. No known target chain trips this (Ethereum
        // precompiles are 0x01-0x0a, OP predeploys are 0x4200...), but importEthereumGenesis
        // State loads arbitrary L1 alloc sets not authored by FISCO tooling.
        std::string addressHexLower(ledger::stripHexPrefix(alloc.address));
        std::transform(addressHexLower.begin(), addressHexLower.end(), addressHexLower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (bcos::precompiled::contains(bcos::precompiled::c_systemTxsAddress,
                std::string_view{addressHexLower}))
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "genesis alloc address is a FISCO system address: " +
                                      alloc.address +
                                      " (EVMAccount would write it to /sys/ but the state "
                                      "root hashes it as an ordinary /apps/ account)"));
        }

        auto addrKeyHash = keccak(bcos::bytesConstRef(addr.bytes, sizeof(addr.bytes)));
        if (!seenAddresses.insert(addrKeyHash).second)
        {
            BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                      "genesis alloc address is duplicated: " + alloc.address));
        }
        stateEntries[addrKeyHash] = std::move(accountRlp);
    }

    auto accountTrie = mpt::computeTrieRoot(stateEntries);
    nodes.merge(accountTrie.newNodes);
    co_return GenesisStateTrie{.root = accountTrie.root, .nodes = std::move(nodes)};
}
