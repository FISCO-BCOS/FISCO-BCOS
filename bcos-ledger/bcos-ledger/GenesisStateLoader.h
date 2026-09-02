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
 * @file GenesisStateLoader.h
 * @brief Load an Ethereum genesis world state (alloc-based) into storage: flat
 *        /apps/ account rows plus every genesis MPT trie node as "/mpt/" rows —
 *        the pre-condition for verifying blocks incrementally from genesis
 *        (buildAndCollect reads the parent trie through the /mpt/ rows).
 * @date 2026/8/18
 */
#pragma once

#include "GenesisStateRoot.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/Task.h"
#include "bcos-tool/Exceptions.h"
#include <bcos-utilities/Exceptions.h>
#include <boost/algorithm/hex.hpp>
#include <boost/throw_exception.hpp>
#include <optional>
#include <string>
#include <vector>

namespace bcos::ledger
{
/// Write the flat /apps/ account rows for one alloc and persist every genesis trie node.
///
/// Returns the op-geth-compatible genesis state ROOT (not the trie itself: the node map is
/// consumed by the /mpt/ persistence loop below, so returning it would hand the caller a
/// map whose values have all been moved from). Callers SHOULD compare the returned root
/// against the chain's canonical genesis state root (e.g. Sepolia's 0x5eb6e371...) to prove
/// the loaded state is complete.
///
/// @param allocs  addresses are 40-hex (with or without 0x); nonce is a DECIMAL string;
///                code is hex; storage slots/values are 32-byte hex.
template <class Storage>
task::Task<bcos::h256> importEthereumGenesisState(
    Storage& storage, std::vector<Alloc> const& allocs, crypto::Hash const& hashImpl,
    Features const& features)
{
    // Build the full genesis trie FIRST: genesis import is not transactional,
    // and computeGenesisStateTrie validates every alloc hex field (address /
    // code / storage slots, through the shared unhexAllocExact/unhexAllocBytes
    // guards) plus the nonce. Running it before the first create() means a
    // malformed alloc — anywhere in the set — cannot leave partially-written
    // /apps/ rows behind (production Ledger::buildGenesisBlock orders it the
    // same way). The per-alloc decode below then never fires on bad config;
    // it stays as the defensive invariant.
    GenesisConfig genesis;
    genesis.m_allocs = allocs;
    auto trie = co_await computeGenesisStateTrie(genesis);

    for (auto const& alloc : allocs)
    {
        // Decode & validate EVERY hex field of the alloc BEFORE the first
        // write: genesis import is not transactional, so a bad code/slot hex
        // discovered after create() would leave a partially-written account in
        // the genesis batch.
        evmc_address address{};
        unhexAllocExact(alloc.address, "address", address.bytes, sizeof(address.bytes));

        bcos::bytes binaryCode;
        std::optional<crypto::HashType> codeHash;
        if (!stripHexPrefix(alloc.code).empty())
        {
            binaryCode = unhexAllocBytes(alloc.code, "code");
            codeHash = hashImpl.hash(binaryCode);
        }
        std::vector<std::pair<evmc_bytes32, evmc_bytes32>> slots;
        slots.reserve(alloc.storage.size());
        for (auto const& [key, value] : alloc.storage)
        {
            evmc_bytes32 evmKey{};
            unhexAllocExact(key, "storage slot key", evmKey.bytes, sizeof(evmKey.bytes));
            evmc_bytes32 evmValue{};
            unhexAllocExact(value, "storage slot value", evmValue.bytes, sizeof(evmValue.bytes));
            slots.emplace_back(evmKey, evmValue);
        }

        account::EVMAccount account(
            storage, address, features.get(Features::Flag::feature_raw_address));
        co_await account.create();

        if (codeHash.has_value())
        {
            co_await account.setCode(std::move(binaryCode), std::string{}, *codeHash);
        }
        if (!alloc.nonce.empty())
        {
            co_await account.setNonce(alloc.nonce);
        }
        if (alloc.balance > 0)
        {
            co_await account.setBalance(alloc.balance);
        }
        for (auto const& [evmKey, evmValue] : slots)
        {
            co_await account.setStorage(evmKey, evmValue);
        }
    }

    // Persist every produced genesis trie node as a "/mpt/" state row, exactly
    // like Ledger::buildGenesisBlock does for Scenario-B (L2) chains — block 1's
    // incremental MPT build resolves parent-version nodes through storage, and a
    // missing node aborts loudly (MPTInvariantViolation). The nodes are consumed
    // here (moved into the Entry), which is why the root is returned alone.
    for (auto& [nodeHash, nodeRlp] : trie.nodes)
    {
        storage::Entry nodeEntry;
        nodeEntry.set(std::move(nodeRlp));
        co_await storage2::writeOne(storage, mptNodeStateKey(nodeHash), std::move(nodeEntry));
    }
    co_return trie.root;
}
}  // namespace bcos::ledger
