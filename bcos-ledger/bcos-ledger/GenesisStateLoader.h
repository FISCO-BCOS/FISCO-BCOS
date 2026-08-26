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
#include "bcos-storage/KeyPrefixes.h"
#include "bcos-task/Task.h"
#include <boost/algorithm/hex.hpp>
#include <string>
#include <vector>

namespace bcos::ledger
{
/// Write the flat /apps/ account rows for one alloc and persist every genesis trie node.
///
/// The returned GenesisStateTrie.root is the op-geth-compatible genesis state root —
/// callers SHOULD compare it against the chain's canonical genesis state root (e.g.
/// Sepolia's 0x5eb6e371...) to prove the loaded state is complete.
///
/// @param allocs  addresses are 40-hex (with or without 0x); nonce is a DECIMAL string;
///                code is hex; storage slots/values are 32-byte hex.
template <class Storage>
task::Task<GenesisStateTrie> importEthereumGenesisState(
    Storage& storage, std::vector<Alloc> const& allocs, crypto::Hash const& hashImpl,
    Features const& features)
{
    auto strip0x = [](std::string_view hex) {
        return hex.starts_with("0x") ? hex.substr(2) : hex;
    };

    for (auto const& alloc : allocs)
    {
        auto addressHex = strip0x(alloc.address);
        evmc_address address{};
        boost::algorithm::unhex(addressHex.begin(), addressHex.end(), address.bytes);

        account::EVMAccount account(
            storage, address, features.get(Features::Flag::feature_raw_address));
        co_await account.create();

        if (!alloc.code.empty())
        {
            auto codeHex = strip0x(alloc.code);
            bcos::bytes binaryCode;
            binaryCode.reserve(codeHex.size() / 2);
            boost::algorithm::unhex(
                codeHex.begin(), codeHex.end(), std::back_inserter(binaryCode));
            auto codeHash = hashImpl.hash(binaryCode);
            co_await account.setCode(std::move(binaryCode), std::string{}, codeHash);
        }
        if (!alloc.nonce.empty())
        {
            co_await account.setNonce(alloc.nonce);
        }
        if (alloc.balance > 0)
        {
            co_await account.setBalance(alloc.balance);
        }
        for (auto const& [key, value] : alloc.storage)
        {
            auto keyHex = strip0x(key);
            auto valueHex = strip0x(value);
            evmc_bytes32 evmKey{};
            boost::algorithm::unhex(keyHex.begin(), keyHex.end(), evmKey.bytes);
            evmc_bytes32 evmValue{};
            boost::algorithm::unhex(valueHex.begin(), valueHex.end(), evmValue.bytes);
            co_await account.setStorage(evmKey, evmValue);
        }
    }

    // Build the full genesis trie (account trie + storage sub-tries) and persist every
    // produced node as a "/mpt/" state row, exactly like Ledger::buildGenesisBlock does
    // for Scenario-B (L2) chains — block 1's incremental MPT build resolves parent-version
    // nodes through storage, and a missing node aborts loudly (MPTInvariantViolation).
    GenesisConfig genesis;
    genesis.m_allocs = allocs;
    auto trie = co_await computeGenesisStateTrie(genesis);
    for (auto& [nodeHash, nodeRlp] : trie.nodes)
    {
        storage::Entry nodeEntry;
        nodeEntry.set(std::move(nodeRlp));
        co_await storage2::writeOne(
            storage, storage2::mptNodeStateKey(nodeHash), std::move(nodeEntry));
    }
    co_return trie;
}
}  // namespace bcos::ledger
