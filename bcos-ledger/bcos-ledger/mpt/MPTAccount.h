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
 * @file MPTAccount.h
 * @brief Account-concept reader over MPT state at a fixed root, for historical calls (spec §5.13)
 */
#pragma once

#include "Account.h"
#include "Constants.h"
#include "Errors.h"
#include "MPTReadView.h"
#include "StorageValueCodec.h"
#include "Trie.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-concepts/ByteBuffer.h>
// EVMAccount.h supplies bcos::ledger::account::NonceNotInitialized; increaseNonce() throws the
// SAME exception type as EVMAccount so callers need not distinguish the two implementations.
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <evmc/evmc.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace bcos::ledger::mpt
{

/// Account-concept implementation that reads state through the MPT at a FIXED stateRoot — the
/// historical-call counterpart of ledger::account::EVMAccount (spec §5.13). HostContext is coded
/// against the Account concept (bcos-framework/ledger/Account.h), so an eth_call against block N
/// swaps this in for EVMAccount and executes on block N's trie instead of the latest flat KV.
///
/// Reads walk the trie: balance()/nonce()/codeHash() come from the account leaf 4-tuple
/// (MPTReadView::readAccount); storage(key) descends the account's storage trie along
/// slotKeyHash(key); code() resolves the leaf's codeHash through the flat s_code_binary table —
/// hash-addressed and append-only, so its rows are valid for ANY historical block (spec §4.5).
///
/// Absence semantics are Ethereum semantics, NOT errors: an account missing from the trie reads
/// as exists()==false / balance 0 / nonce nullopt / zero codeHash; a slot missing from the
/// storage trie reads as the zero value. This class targets scenario B (genesis-enabled MPT,
/// spec §4.4), where the trie is the COMPLETE state and exclusion genuinely means zero/empty.
/// Scenario-A cold data (dormant accounts, cold slots of touched accounts) makes exclusion
/// ambiguous — gating historical calls to scenario B is the eth_call plumbing layer's job
/// (PR-43, per OQ6 resolution (c)+(a)), not this class's.
///
/// Writes are simulation-only: a historical call may SSTORE / set balances, but block N is
/// immutable, so create/setCode/setBalance/setNonce/setStorage land in an internal in-memory
/// overlay that reads consult before the trie. Nothing ever touches the node storage — MPTAccount
/// only reads it. The overlay dies with the account object (== with the call).
///
/// Like MPTReadView, each read walks from the root with a fresh Trie: no trie state is cached
/// (node-level caching is the Storage layer's concern). The account+storage tries are keccak
/// paths end-to-end (accountKeyHash / slotKeyHash defaults), matching what MPTBuilder committed.
///
/// The interface is the de-facto superset of the Account concept that HostContext and
/// BaselineScheduler actually call (spec §5.13): concept methods plus storage(key, DIRECT_TYPE),
/// storageEntry(), increaseNonce().
///
/// @tparam NodeStorage resolves trie node hashes (same concept MPTReadView eats).
/// @tparam CodeStorage resolves s_code_binary rows by executor_v1::StateKeyView — the flat KV
/// store (or any layer over it).
template <bcos::storage2::ReadableStorage<bcos::h256> NodeStorage, class CodeStorage>
class MPTAccount
{
public:
    /// @param nodeStorage trie node store the walks read against.
    /// @param codeStorage flat store holding s_code_binary (code bytes by code hash).
    /// @param stateRoot   block N's state root; emptyRootHash() means "no accounts".
    /// @param address     the 20-byte account address.
    MPTAccount(NodeStorage& nodeStorage, CodeStorage& codeStorage, bcos::h256 stateRoot,
        bcos::Address address)
      : m_nodeStorage(nodeStorage),
        m_codeStorage(codeStorage),
        m_stateRoot(stateRoot),
        m_address(address),
        m_addressHex(address.hex())
    {}

    MPTAccount(const MPTAccount&) = delete;
    MPTAccount(MPTAccount&&) noexcept = default;
    MPTAccount& operator=(const MPTAccount&) = delete;
    MPTAccount& operator=(MPTAccount&&) noexcept = default;
    ~MPTAccount() noexcept = default;

    /// True when the account has a leaf in the trie, or create() was called on this object.
    /// Mirrors EVMAccount: value writes (setBalance etc.) alone do not create the account.
    bcos::task::Task<bool> exists()
    {
        if (m_created)
        {
            co_return true;
        }
        auto const account = co_await readLeaf();
        co_return account.has_value();
    }

    /// Simulation-only: marks the account created in the overlay. The trie is never written.
    bcos::task::Task<void> create()
    {
        m_created = true;
        co_return;
    }

    /// The code bytes: overlay first (a simulated setCode), then the trie leaf's codeHash
    /// resolved through s_code_binary. An absent account or an EOA (emptyCodeHash) has no code.
    bcos::task::Task<std::optional<bcos::storage::Entry>> code()
    {
        if (m_codeOverlay)
        {
            co_return bcos::storage::Entry{m_codeOverlay->code};
        }
        auto const account = co_await readLeaf();
        if (!account || account->codeHash == emptyCodeHash())
        {
            co_return std::nullopt;
        }
        co_return co_await bcos::storage2::readOne(
            m_codeStorage.get(), bcos::executor_v1::StateKeyView{bcos::ledger::SYS_CODE_BINARY,
                                     bcos::concepts::bytebuffer::toView(account->codeHash)});
    }

    /// Simulation-only: the code/abi/hash land in the overlay; s_code_binary is never written.
    bcos::task::Task<void> setCode(
        bcos::bytes code, std::string abi, const bcos::crypto::HashType& codeHash)
    {
        m_codeOverlay = CodeOverlay{std::move(code), std::move(abi), codeHash};
        co_return;
    }

    /// Overlay codeHash (after a simulated setCode), else the leaf's codeHash. Zero for an
    /// absent account (mirrors EVMAccount's missing-field default).
    bcos::task::Task<bcos::h256> codeHash()
    {
        if (m_codeOverlay)
        {
            co_return m_codeOverlay->codeHash;
        }
        auto const account = co_await readLeaf();
        if (!account)
        {
            co_return bcos::h256{};
        }
        co_return account->codeHash;
    }

    /// The Ethereum account 4-tuple has no abi field — abi is a BCOS extension excluded from the
    /// MPT (spec §5.13). Only a simulated setCode's abi is visible; historical reads are empty.
    bcos::task::Task<std::optional<bcos::storage::Entry>> abi()
    {
        if (m_codeOverlay && !m_codeOverlay->abi.empty())
        {
            co_return bcos::storage::Entry{m_codeOverlay->abi};
        }
        co_return std::nullopt;
    }

    /// Overlay balance, else the leaf's balance, else 0 for an absent account.
    bcos::task::Task<bcos::u256> balance()
    {
        if (m_balanceOverlay)
        {
            co_return *m_balanceOverlay;
        }
        auto const account = co_await readLeaf();
        if (!account)
        {
            co_return bcos::u256{};
        }
        co_return account->balance;
    }

    /// Simulation-only: lands in the overlay.
    bcos::task::Task<void> setBalance(const bcos::u256& balance)
    {
        m_balanceOverlay = balance;
        co_return;
    }

    /// Overlay nonce, else the leaf's nonce as a decimal string (EVMAccount's representation),
    /// else nullopt for an absent account.
    bcos::task::Task<std::optional<std::string>> nonce()
    {
        if (m_nonceOverlay)
        {
            co_return *m_nonceOverlay;
        }
        auto const account = co_await readLeaf();
        if (!account)
        {
            co_return std::nullopt;
        }
        co_return account->nonce.template convert_to<std::string>();
    }

    /// Simulation-only: lands in the overlay.
    bcos::task::Task<void> setNonce(std::string nonce)
    {
        m_nonceOverlay = std::move(nonce);
        co_return;
    }

    /// nonce()+1 into the overlay; throws account::NonceNotInitialized when the account has no
    /// nonce (absent from the trie and never setNonce'd) — same contract as EVMAccount.
    bcos::task::Task<void> increaseNonce()
    {
        if (auto currentNonce = co_await nonce())
        {
            const auto newNonce = bcos::u256(currentNonce.value()) + 1;
            co_await setNonce(newNonce.convert_to<std::string>());
        }
        else
        {
            BOOST_THROW_EXCEPTION(bcos::ledger::account::NonceNotInitialized{});
        }
    }

    /// SLOAD at block N: overlay first, then the account's storage trie along slotKeyHash(key).
    /// Absent account, empty storage trie, or absent slot all read as the zero value.
    bcos::task::Task<evmc_bytes32> storage(const evmc_bytes32& key)
    {
        auto const slot = toSlot(key);
        if (auto const it = m_storageOverlay.find(slot); it != m_storageOverlay.end())
        {
            co_return it->second;
        }
        auto const value = co_await readTrieSlot(slot);
        if (!value)
        {
            co_return evmc_bytes32{};
        }
        co_return toEvmcBytes(bcos::h256{*value});
    }

    /// DIRECT-tagged variant (see EVMAccount): metadata-only reads that must bypass read-set
    /// tracking. MPTAccount tracks no read set, so it is exactly storage(key).
    bcos::task::Task<evmc_bytes32> storage(
        const evmc_bytes32& key, storage2::DIRECT_TYPE /*direct*/)
    {
        co_return co_await storage(key);
    }

    /// Simulation-only: lands in the overlay; the storage trie is never written.
    bcos::task::Task<void> setStorage(const evmc_bytes32& key, const evmc_bytes32& value)
    {
        m_storageOverlay[toSlot(key)] = value;
        co_return;
    }

    /// The raw 32-byte slot value as a storage Entry (the shape BaselineScheduler's
    /// getPendingStorageAt reads), or nullopt when the slot is absent. @p key is the raw 32-byte
    /// slot key exactly as the flat KV stores it; only 32-byte keys can exist in a storage trie,
    /// so any other length is absent by construction.
    bcos::task::Task<std::optional<bcos::storage::Entry>> storageEntry(const std::string_view& key)
    {
        if (key.size() != bcos::h256::SIZE)
        {
            co_return std::nullopt;
        }
        bcos::h256 const slot{
            bcos::bytesConstRef{reinterpret_cast<const bcos::byte*>(key.data()), key.size()}};
        if (auto const it = m_storageOverlay.find(slot); it != m_storageOverlay.end())
        {
            co_return bcos::storage::Entry{std::string_view{
                reinterpret_cast<const char*>(it->second.bytes), sizeof(it->second.bytes)}};
        }
        auto const value = co_await readTrieSlot(slot);
        if (!value)
        {
            co_return std::nullopt;
        }
        bcos::h256 const padded{*value};
        co_return bcos::storage::Entry{bcos::concepts::bytebuffer::toView(padded)};
    }

    /// The lowercase-hex address (no 0x). MPTAccount has no flat table, so unlike EVMAccount
    /// there is no /apps/ table-name prefix to return.
    bcos::task::Task<std::string_view> path() { co_return std::string_view{m_addressHex}; }

    std::string_view address() const { return m_addressHex; }

    /// The state root this account reads against.
    bcos::h256 root() const noexcept { return m_stateRoot; }

private:
    struct CodeOverlay
    {
        bcos::bytes code;
        std::string abi;
        bcos::h256 codeHash;
    };

    /// The account leaf 4-tuple at m_stateRoot, or nullopt when absent. Fresh walk per call.
    bcos::task::Task<std::optional<Account>> readLeaf() const
    {
        MPTReadView<NodeStorage> const view{m_nodeStorage.get(), m_stateRoot};
        co_return co_await view.readAccount(m_address);
    }

    /// The slot value from the account's storage trie, decoded from its RLP leaf (the
    /// big-endian-trimmed integer encodeStorageValue produced). nullopt when the account is
    /// absent, its storage trie is empty, or the slot has no leaf.
    bcos::task::Task<std::optional<bcos::u256>> readTrieSlot(bcos::h256 const& slot) const
    {
        auto const account = co_await readLeaf();
        if (!account || account->storageRoot == emptyRootHash())
        {
            co_return std::nullopt;
        }
        Trie<NodeStorage> const trie{m_nodeStorage.get(), account->storageRoot};
        auto const leaf = co_await trie.get(slotKeyHash(slot));
        if (!leaf)
        {
            co_return std::nullopt;
        }
        co_return decodeStorageLeaf(*leaf);
    }

    /// Inverse of encodeStorageValue (StorageValueCodec): one RLP string holding the
    /// big-endian-trimmed slot value. @throws MPTDecodeError on malformed input.
    static bcos::u256 decodeStorageLeaf(bcos::bytes const& leaf)
    {
        bcos::bytesRef cursor{const_cast<bcos::byte*>(leaf.data()), leaf.size()};
        bcos::u256 value;
        if (auto error = bcos::codec::rlp::decode(cursor, value); error)
        {
            BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                      "storage leaf: bad RLP value: " + error->errorMessage()));
        }
        if (!cursor.empty())
        {
            BOOST_THROW_EXCEPTION(MPTDecodeError{} << bcos::errinfo_comment(
                                      "storage leaf: trailing bytes after the RLP value"));
        }
        return value;
    }

    static bcos::h256 toSlot(const evmc_bytes32& key)
    {
        return bcos::h256{bcos::bytesConstRef{key.bytes, sizeof(key.bytes)}};
    }

    static evmc_bytes32 toEvmcBytes(bcos::h256 const& value)
    {
        evmc_bytes32 out{};
        std::copy(value.begin(), value.end(), out.bytes);
        return out;
    }

    std::reference_wrapper<NodeStorage> m_nodeStorage;
    std::reference_wrapper<CodeStorage> m_codeStorage;
    bcos::h256 m_stateRoot;
    bcos::Address m_address;
    std::string m_addressHex;

    // Simulation overlay: writes land here, reads consult it before the trie. Never persisted.
    bool m_created{false};
    std::optional<bcos::u256> m_balanceOverlay;
    std::optional<std::string> m_nonceOverlay;
    std::optional<CodeOverlay> m_codeOverlay;
    std::unordered_map<bcos::h256, evmc_bytes32> m_storageOverlay;
};

}  // namespace bcos::ledger::mpt
