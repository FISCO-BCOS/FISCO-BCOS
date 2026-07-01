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
 * @file MPTReadView.h
 * @brief Read-only account lookups over an MPT state root (spec §5.6, §7.1)
 */
#pragma once

#include "Account.h"
#include "Constants.h"
#include "Trie.h"
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <functional>
#include <optional>

namespace bcos::ledger::mpt
{

/// The "secure trie" key transform for accounts: the trie path is keccak256 of the 20 raw address
/// bytes. Both the reader (MPTReadView) and the builder (PR-10) must apply this identical
/// transform, so it lives here as a shared free function.
bcos::h256 accountKeyHash(bcos::Address const& addr);

/// Read-only view of account state at a fixed MPT root. Walks the trie over any @p Storage
/// satisfying storage2::ReadableStorage<h256> — the storage layer (a MemoryStorage cache, a
/// RocksDBStorage2, or a layered MultiLayerStorage) resolves each node hash, so a fresh process can
/// serve reads straight from its node store with no separate backend. Holds no mutable state.
template <bcos::storage2::ReadableStorage<bcos::h256> Storage>
class MPTReadView
{
public:
    /// @param storage node storage the trie reads against.
    /// @param root    the state root to read against; emptyRootHash() means "no accounts".
    MPTReadView(Storage& storage, bcos::h256 root) : m_storage(storage), m_root(root) {}

    /// Returns the decoded Account at @p addr, or nullopt if the account is absent.
    /// @throws MPTDecodeError if a leaf is present but its RLP is malformed (Account::decode).
    /// @note Each call walks from the root with a fresh Trie; no traversal state is cached or
    /// shared across readAccount calls. Any node-level caching is the Storage layer's concern.
    bcos::task::Task<std::optional<Account>> readAccount(bcos::Address const& addr) const
    {
        if (m_root == emptyRootHash())
        {
            co_return std::nullopt;
        }

        Trie<Storage> trie(m_storage.get(), m_root);
        auto leaf = co_await trie.get(accountKeyHash(addr));
        if (!leaf)
        {
            co_return std::nullopt;
        }
        co_return Account::decode(bcos::ref(*leaf));
    }

    /// Whether @p addr has an account record. Equivalent to readAccount(addr).has_value().
    bcos::task::Task<bool> hasAccount(bcos::Address const& addr) const
    {
        // Bind the awaited result to a local before .has_value(): GCC cannot resolve a member
        // access applied directly to a co_await subexpression in a template coroutine (Clang
        // accepts it).
        auto const account = co_await readAccount(addr);
        co_return account.has_value();
    }

    /// The state root this view reads against.
    bcos::h256 root() const noexcept { return m_root; }

private:
    std::reference_wrapper<Storage> m_storage;
    bcos::h256 m_root;
};

}  // namespace bcos::ledger::mpt
