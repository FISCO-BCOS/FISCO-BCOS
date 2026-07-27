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
 * @brief EVMAccount subclass whose reads walk the MPT at a fixed root, for historical calls
 * (spec §5.13)
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

namespace bcos::ledger::mpt
{

/// A Storage that carries the MPT read context, so MPTAccount stays constructible with
/// HostContext's fixed (storage, address, binaryAddress) shape (HostContext.h getAccount /
/// executeCreate): the historical storage stack PR-43 builds exposes these three getters and
/// MPTAccount pulls its trie context out of them at construction.
template <class Storage>
concept HistoricalStorageContext = requires(Storage& storage) {
    { storage.mptNodeStorage() };
    { storage.mptBackendStorage() };
    { storage.mptStateRoot() } -> std::convertible_to<bcos::h256>;
};

/// EVMAccount subclass for historical calls (spec §5.13): an eth_call against block N swaps this
/// class in for EVMAccount (HostContext is coded against the Account concept, statically
/// dispatched, so the shadowed methods below win) and executes on block N's trie.
///
/// READS are pure MPT walks at the fixed @p stateRoot, never layered over anything:
/// balance()/nonce()/codeHash() come from the account leaf 4-tuple (MPTReadView::readAccount);
/// storage(key) takes the leaf's storageRoot and descends the storage trie along
/// slotKeyHash(key); code()/abi() resolve the leaf's codeHash through the flat
/// s_code_binary / s_contract_abi tables — hash-addressed and append-only, so their rows are
/// valid for ANY historical block (spec §4.5).
///
/// WRITES are inherited from EVMAccount verbatim (create/setCode/setBalance/setNonce/setStorage)
/// and land as flat rows in @p Storage. They exist so a simulated call can execute, but block N
/// is immutable and reads never consult @p Storage: a write in a historical call is NOT visible
/// to a subsequent read of the same field/slot (SSTORE then SLOAD returns the historical value).
/// Historical calls target view-style execution; this boundary is deliberate (spec §5.13).
/// Nothing ever writes the node storage — MPTAccount only reads it.
///
/// Absence semantics are Ethereum semantics, NOT errors: an account missing from the trie reads
/// as exists()==false / balance 0 / nonce nullopt / zero codeHash; a slot missing from the
/// storage trie reads as the zero value. This class targets scenario B (genesis-enabled MPT,
/// spec §4.4), where the trie is the COMPLETE state and exclusion genuinely means zero/empty.
/// Scenario-A cold data makes exclusion ambiguous — gating historical calls to scenario B is the
/// eth_call plumbing layer's job (PR-43, per OQ6 resolution (c)+(a)), not this class's.
///
/// Like MPTReadView, each read walks from the root with a fresh Trie: the object holds no
/// mutable state (node-level caching is the Storage layer's concern), so HostContext's
/// construct-per-use pattern is loss-free. The account+storage tries are keccak paths end-to-end
/// (accountKeyHash / slotKeyHash defaults), matching what MPTBuilder committed.
///
/// @tparam Storage        where the inherited EVMAccount writes land (and the table name comes
///                        from); never consulted by reads.
/// @tparam NodeStorage    resolves trie node hashes (same concept MPTReadView eats).
/// @tparam BackendStorage flat store holding s_code_binary / s_contract_abi (hash-addressed,
///                        history-invariant), read by executor_v1::StateKeyView.
template <class Storage, bcos::storage2::ReadableStorage<bcos::h256> NodeStorage,
    class BackendStorage>
class MPTAccount : public bcos::ledger::account::EVMAccount<Storage>
{
private:
    using Base = bcos::ledger::account::EVMAccount<Storage>;

public:
    /// Direct construction (tests, callers that already hold the trie context).
    /// @param storage   write target for the inherited EVMAccount write methods.
    /// @param stateRoot block N's state root; emptyRootHash() means "no accounts".
    MPTAccount(Storage& storage, NodeStorage& nodeStorage, BackendStorage& backendStorage,
        bcos::h256 stateRoot, bcos::Address address)
      : Base(storage, address, /*binaryAddress*/ false),
        m_nodeStorage(nodeStorage),
        m_backendStorage(backendStorage),
        m_stateRoot(stateRoot),
        m_address(address)
    {}

    /// HostContext-shaped construction: same (storage, address, binaryAddress) signature as
    /// EVMAccount, with the trie context riding on the storage type (PR-43's historical stack).
    MPTAccount(Storage& storage, const evmc_address& address, bool binaryAddress)
        requires HistoricalStorageContext<Storage>
      : Base(storage, address, binaryAddress),
        m_nodeStorage(storage.mptNodeStorage()),
        m_backendStorage(storage.mptBackendStorage()),
        m_stateRoot(storage.mptStateRoot()),
        m_address(bcos::bytesConstRef{address.bytes, sizeof(address.bytes)})
    {}

    MPTAccount(const MPTAccount&) = delete;
    MPTAccount(MPTAccount&&) noexcept = default;
    MPTAccount& operator=(const MPTAccount&) = delete;
    MPTAccount& operator=(MPTAccount&&) noexcept = default;
    ~MPTAccount() noexcept = default;

    /// True when the account has a leaf in block N's trie. A create() in this call writes the
    /// s_tables row into Storage (inherited) but does not change what block N contains.
    bcos::task::Task<bool> exists()
    {
        auto const account = co_await readLeaf();
        co_return account.has_value();
    }

    /// The code bytes at block N: the leaf's codeHash resolved through s_code_binary. An absent
    /// account or an EOA (emptyCodeHash) has no code. A leaf that commits a non-empty codeHash
    /// with NO matching s_code_binary row is a corrupted backend or a mis-wired code write path,
    /// not an EOA — silently returning nullopt would downgrade the account and execute the call
    /// wrong, so it throws instead (the append-only, hash-addressed code table makes the row's
    /// existence an invariant; EIP-7702 delegation designators are ordinary hash-addressed code
    /// and are covered by the same invariant).
    bcos::task::Task<std::optional<bcos::storage::Entry>> code()
    {
        auto const account = co_await readLeaf();
        if (!account || account->codeHash == emptyCodeHash())
        {
            co_return std::nullopt;
        }
        auto codeEntry = co_await bcos::storage2::readOne(
            m_backendStorage.get(), bcos::executor_v1::StateKeyView{bcos::ledger::SYS_CODE_BINARY,
                                        bcos::concepts::bytebuffer::toView(account->codeHash)});
        if (!codeEntry)
        {
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation{} << bcos::errinfo_comment(
                    "historical code(): account leaf commits codeHash " + account->codeHash.hex() +
                    " but s_code_binary has no such row"));
        }
        co_return codeEntry;
    }

    /// The leaf's codeHash; zero for an absent account (mirrors EVMAccount's missing-field
    /// default).
    bcos::task::Task<bcos::h256> codeHash()
    {
        auto const account = co_await readLeaf();
        if (!account)
        {
            co_return bcos::h256{};
        }
        co_return account->codeHash;
    }

    /// The contract abi at block N: the leaf's codeHash resolved through s_contract_abi — like
    /// s_code_binary it is hash-addressed and append-only, so the flat row is history-valid.
    /// Unlike code(), a missing row is NOT an invariant violation: abi is BCOS metadata outside
    /// the committed 4-tuple, so absence simply reads as nullopt.
    bcos::task::Task<std::optional<bcos::storage::Entry>> abi()
    {
        auto const account = co_await readLeaf();
        if (!account || account->codeHash == emptyCodeHash())
        {
            co_return std::nullopt;
        }
        co_return co_await bcos::storage2::readOne(
            m_backendStorage.get(), bcos::executor_v1::StateKeyView{bcos::ledger::SYS_CONTRACT_ABI,
                                        bcos::concepts::bytebuffer::toView(account->codeHash)});
    }

    /// The leaf's balance; 0 for an absent account.
    bcos::task::Task<bcos::u256> balance()
    {
        auto const account = co_await readLeaf();
        if (!account)
        {
            co_return bcos::u256{};
        }
        co_return account->balance;
    }

    /// The leaf's nonce as a decimal string (EVMAccount's representation); nullopt for an absent
    /// account.
    bcos::task::Task<std::optional<std::string>> nonce()
    {
        auto const account = co_await readLeaf();
        if (!account)
        {
            co_return std::nullopt;
        }
        co_return account->nonce.template convert_to<std::string>();
    }

    /// Trie nonce + 1 written through the inherited setNonce (simulation only, like every
    /// write); throws account::NonceNotInitialized for an absent account — same contract as
    /// EVMAccount. Shadowed because the base version reads the flat nonce row, which historical
    /// reads must not consult.
    bcos::task::Task<void> increaseNonce()
    {
        auto const account = co_await readLeaf();
        if (!account)
        {
            BOOST_THROW_EXCEPTION(bcos::ledger::account::NonceNotInitialized{});
        }
        auto const newNonce = account->nonce + 1;
        co_await Base::setNonce(newNonce.template convert_to<std::string>());
    }

    /// SLOAD at block N: the leaf's storageRoot, then the storage trie along slotKeyHash(key).
    /// Absent account, empty storage trie, or absent slot all read as the zero value.
    bcos::task::Task<evmc_bytes32> storage(const evmc_bytes32& key)
    {
        auto const value = co_await readTrieSlot(toSlot(key));
        if (!value)
        {
            co_return evmc_bytes32{};
        }
        co_return toEvmcBytes(bcos::h256{*value});
    }

    /// Tag-taking variant (see EVMAccount's tag-forwarding overload): HostContext passes
    /// composable tags like BYPASS_READ_SET / BYPASS_MULTILAYER for metadata reads. MPTAccount
    /// tracks no read set and has no layers, so every tag is a no-op and this is exactly
    /// storage(key). (Both overloads are shadowed together — shadowing one would hide the
    /// base's other overload.)
    bcos::task::Task<evmc_bytes32> storage(const evmc_bytes32& key, auto... /*tags*/)
    {
        co_return co_await storage(key);
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
        auto const value = co_await readTrieSlot(slot);
        if (!value)
        {
            co_return std::nullopt;
        }
        bcos::h256 const padded{*value};
        co_return bcos::storage::Entry{bcos::concepts::bytebuffer::toView(padded)};
    }

    // path() / address() are inherited: both return the EVMAccount table name ("/apps/<hex>",
    // or the binary/system variants), so historical and latest accounts key transient storage
    // and logs identically.

    /// The state root this account reads against.
    bcos::h256 root() const noexcept { return m_stateRoot; }

private:
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
    std::reference_wrapper<BackendStorage> m_backendStorage;
    bcos::h256 m_stateRoot;
    bcos::Address m_address;
};

}  // namespace bcos::ledger::mpt
