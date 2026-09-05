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
 * @brief EVMAccount plus opt-in historical reads through the MPT at a caller-supplied root
 * (spec §5.13)
 */
#pragma once

#include "Account.h"
#include "Constants.h"
#include "Errors.h"
#include "MPTReadView.h"
#include "StorageValueCodec.h"
#include "Trie.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/ledger/Account.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <evmc/evmc.h>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::ledger::mpt
{

/// A Storage that carries the MPT node/backend handles, letting MPTAccount keep EVMAccount's
/// fixed (storage, address, binaryAddress) construction shape — the one HostContext uses.
///
/// Reserved for PR-43; nothing outside the tests satisfies it yet. It exists so a historical
/// storage stack can hand HostContext an Account type without widening HostContext's three
/// construction sites, and it constrains that swap to storages that actually carry MPT handles.
template <class Storage>
concept HistoricalStorageContext = requires(Storage& storage) {
    { storage.mptNodeStorage() };
    { storage.mptBackendStorage() };
};

/// EVMAccount with an opt-in historical read path (spec §5.13).
///
/// Every read takes an OPTIONAL state root, empty by default:
///   - empty (the default, and what HostContext's no-argument calls produce) → the inherited
///     EVMAccount behaviour, unchanged: flat KV reads and writes against @p Storage. Reads see
///     this call's own writes, so read-your-writes, nonce advance and CREATE all behave exactly
///     as they do on the latest state.
///   - a value → a historical read at that root: the account leaf 4-tuple through
///     MPTReadView::readAccount, storage slots through the leaf's storageRoot along
///     slotKeyHash(key), code through the leaf's codeHash. Nothing is written; the MPT is only
///     ever read.
///
/// The historical path is a point-query facility (eth_getBalance / eth_getStorageAt / eth_getCode
/// at block N, and the state source a historical-call storage stack resolves misses from), NOT a
/// mode the object is locked into: a caller that never passes a root cannot tell this class from
/// EVMAccount.
///
/// A rooted read answers from the MPT and from nothing else. What the trie does not hold at that
/// root does not exist as far as this path is concerned — the flat tables are never consulted to
/// paper over a gap, because they hold TODAY's values and cannot answer a question about block N.
///
/// Absence at a given root is therefore Ethereum semantics, not an error: an account with no leaf
/// reads as exists()==false / balance 0 / nonce nullopt / zero codeHash, and a slot with no leaf
/// reads as zero. That is exact for scenario B (genesis-enabled MPT, spec §4.4), where the trie is
/// the complete state. Scenario-A cold data makes exclusion ambiguous, so gating historical
/// queries to scenario B is the caller's job (PR-43, per OQ6 resolution (c)+(a)).
///
/// One consequence to know about: exists() means different things on the two paths, and the rooted
/// one is the Ethereum meaning. With a root it is leaf presence, exactly as an Ethereum client
/// answers "does this account exist". The inherited flat exists() (EVMAccount.h:27-31) tests for a
/// SYS_TABLES row, a BCOS notion with no Ethereum counterpart, and the two can disagree: an
/// account touched only by rows carrying no Ethereum state never produces a leaf (MPTBuilder.h:
/// 92-97, 215-227 keep that one member of the EIP-161 empty-account class out of the trie), so it
/// reads as absent historically and present flatly. Ethereum agrees with the historical answer.
///
/// abi() and increaseNonce() are inherited unchanged. abi is BCOS metadata outside the committed
/// 4-tuple and is deliberately not historicised — a historical query returns the CURRENT abi,
/// because the base resolves it through today's flat codeHash. increaseNonce() is a write helper
/// that reads the base's flat nonce, which is what the default (latest) path wants.
///
/// The object caches the leaf it last decoded, keyed by root, so K reads of one historical query
/// cost one trie descent. That makes it stateful, unlike EVMAccount: one instance must not be
/// shared across concurrent coroutines. HostContext's construct-per-use pattern (HostContext.h:117
/// /271/363) keeps that trivially true, and the cache costs nothing there because it is per-object.
///
/// @tparam Storage        the flat KV the inherited EVMAccount behaviour reads and writes.
/// @tparam NodeStorage    resolves trie node hashes (the concept MPTReadView eats).
/// @tparam BackendStorage flat store holding s_code_binary (hash-addressed, so its rows are
///                        valid for any historical block, spec §4.5).
template <class Storage, bcos::storage2::ReadableStorage<bcos::h256> NodeStorage,
    class BackendStorage>
class MPTAccount : public bcos::ledger::account::EVMAccount<Storage>
{
private:
    using Base = bcos::ledger::account::EVMAccount<Storage>;

public:
    /// Direct construction, for callers that already hold the trie handles (tests, tooling).
    /// @param binaryAddress the caller's feature_raw_address setting, exactly as EVMAccount
    /// takes it — deliberately without a default: it decides the flat table name every
    /// no-root read and every inherited write uses, and guessing it wrong makes those reads
    /// silently miss on a raw-address chain.
    MPTAccount(Storage& storage, NodeStorage& nodeStorage, BackendStorage& backendStorage,
        bcos::Address address, bool binaryAddress)
      : Base(storage, address, binaryAddress, /*treatSystemAsUser=*/true),
        m_nodeStorage(nodeStorage),
        m_backendStorage(backendStorage),
        m_address(address)
    {}

    /// EVMAccount's construction shape, for the historical storage stack PR-43 builds: the trie
    /// handles ride on the storage type instead of being extra arguments. No production caller
    /// yet — HostContext still names EVMAccount at HostContext.h:100.
    ///
    /// Swapping this class in there does NOT by itself make a call historical: HostContext passes
    /// no root, so every read lands on the inherited path and behaves byte-for-byte like
    /// EVMAccount. Historical base state comes from the storage stack PR-43 builds (a writable
    /// layer over a read-through layer that resolves misses through the rooted reads below), not
    /// from the account type.
    MPTAccount(Storage& storage, const evmc_address& address, bool binaryAddress)
        requires HistoricalStorageContext<Storage>
      : Base(storage, address, binaryAddress, /*treatSystemAsUser=*/true),
        m_nodeStorage(storage.mptNodeStorage()),
        m_backendStorage(storage.mptBackendStorage()),
        m_address(bcos::bytesConstRef{address.bytes, sizeof(address.bytes)})
    {}

    MPTAccount(const MPTAccount&) = delete;
    MPTAccount(MPTAccount&&) noexcept = default;
    MPTAccount& operator=(const MPTAccount&) = delete;
    MPTAccount& operator=(MPTAccount&&) noexcept = default;
    ~MPTAccount() noexcept = default;

    /// Whether the account exists — in the flat state, or (with a root) in that block's trie.
    bcos::task::Task<bool> exists(std::optional<bcos::h256> stateRoot = {})
    {
        if (!stateRoot)
        {
            co_return co_await Base::exists();
        }
        auto const account = co_await readLeaf(*stateRoot);
        co_return account.has_value();
    }

    /// The code bytes, resolved exactly as Ethereum resolves them: `codeHash == keccak256("")`
    /// means the account has no code, and otherwise the code is whatever the one content-addressed
    /// store holds under that hash. There is no second place to look and no fallback.
    ///
    /// That is a deliberate narrowing of what the flat path does. `EVMAccount::code()` also probes
    /// the account table's CODE field, because two pre-3.18 shapes put code there: the old-logic
    /// deploy (HostContext.cpp:483-495, blockVersion < 3.1) and internal create with
    /// bugfix_internal_create_redundant_storage off (TransactionExecutive.cpp:963-967, which
    /// writes CODE and no CODE_HASH at all, so FlatToMPT commits emptyCodeHash for an account
    /// that does have code). Neither shape is producible at 3.18.0 — `EVMAccount::setCode`
    /// (EVMAccount.h:65-88) always writes s_code_binary — and a future fork that changes code
    /// storage again gets its own feature flag. Serving them here would mean reading TODAY's flat
    /// tables to answer a question about block N, which cannot be right in general.
    ///
    /// A missing blob under a non-empty codeHash is an invariant violation, not an absence: the
    /// leaf asserts the code existed at that block, so failing to find it means the store is
    /// corrupt or pruned. Returning nullopt would run the contract as an EOA and answer calls to
    /// it with success and empty output — a wrong result the caller cannot distinguish from a
    /// right one, and geth likewise fails block processing rather than degrading. The historical
    /// call path (PR-43) must catch this at the RPC boundary, as it must MPTDecodeError.
    bcos::task::Task<std::optional<bcos::storage::Entry>> code(
        std::optional<bcos::h256> stateRoot = {})
    {
        if (!stateRoot)
        {
            co_return co_await Base::code();
        }
        auto const account = co_await readLeaf(*stateRoot);
        if (!account || account->codeHash == emptyCodeHash())
        {
            co_return std::nullopt;
        }
        if (auto codeEntry = co_await bcos::storage2::readOne(m_backendStorage.get(),
                bcos::executor_v1::StateKeyView{bcos::ledger::SYS_CODE_BINARY,
                    bcos::concepts::bytebuffer::toView(account->codeHash)}))
        {
            co_return codeEntry;
        }
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                  "historical code(): account leaf commits codeHash " +
                                  account->codeHash.hex() + " but s_code_binary has no such row"));
    }

    /// The code hash: the leaf's, or (no root) the flat account row's.
    bcos::task::Task<bcos::h256> codeHash(std::optional<bcos::h256> stateRoot = {})
    {
        if (!stateRoot)
        {
            co_return co_await Base::codeHash();
        }
        auto const account = co_await readLeaf(*stateRoot);
        if (!account)
        {
            co_return bcos::h256{};
        }
        co_return account->codeHash;
    }

    /// The balance: the leaf's, or (no root) the flat account row's. Absent reads as 0.
    bcos::task::Task<bcos::u256> balance(std::optional<bcos::h256> stateRoot = {})
    {
        if (!stateRoot)
        {
            co_return co_await Base::balance();
        }
        auto const account = co_await readLeaf(*stateRoot);
        if (!account)
        {
            co_return bcos::u256{};
        }
        co_return account->balance;
    }

    /// The nonce as a decimal string (EVMAccount's representation): the leaf's, or (no root) the
    /// flat account row's. Absent reads as nullopt.
    bcos::task::Task<std::optional<std::string>> nonce(std::optional<bcos::h256> stateRoot = {})
    {
        if (!stateRoot)
        {
            co_return co_await Base::nonce();
        }
        auto const account = co_await readLeaf(*stateRoot);
        if (!account)
        {
            co_return std::nullopt;
        }
        co_return account->nonce.template convert_to<std::string>();
    }

    // The inherited flat reads, including the tag-forwarding overload HostContext calls with
    // BYPASS_READ_SET / BYPASS_MULTILAYER. Re-exported because the historical overload below
    // would otherwise hide the whole storage() overload set.
    using Base::storage;

    /// SLOAD at @p stateRoot: the leaf's storageRoot, then the storage trie along
    /// slotKeyHash(key). Absent account, empty storage trie, or absent slot all read as zero.
    /// An empty root means the flat read, identical to the inherited storage(key).
    bcos::task::Task<evmc_bytes32> storage(
        const evmc_bytes32& key, std::optional<bcos::h256> stateRoot)
    {
        if (!stateRoot)
        {
            co_return co_await Base::storage(key);
        }
        auto const value = co_await readTrieSlot(*stateRoot, bcos::ledger::account::toH256(key));
        if (!value)
        {
            co_return evmc_bytes32{};
        }
        co_return bcos::ledger::account::toEvmcBytes32(bcos::h256{*value});
    }

    /// Same read for a plain root. This overload is REQUIRED, not sugar: without it a bare
    /// h256 is an exact match for the inherited tag pack and only a conversion away from the
    /// optional above, so storage(key, root) would silently resolve to the flat tag-forwarding
    /// read and return today's value for a historical query.
    bcos::task::Task<evmc_bytes32> storage(const evmc_bytes32& key, bcos::h256 const& stateRoot)
    {
        co_return co_await storage(key, std::optional<bcos::h256>{stateRoot});
    }

    /// Likewise for a literal nullopt, which would otherwise be deduced as a tag and forwarded
    /// into readOneRaw (harmless there, but by accident rather than by design).
    bcos::task::Task<evmc_bytes32> storage(const evmc_bytes32& key, std::nullopt_t /*latest*/)
    {
        co_return co_await Base::storage(key);
    }

    /// A root and storage tags CANNOT be combined. Without the deletion below,
    /// storage(key, root, BYPASS_READ_SET) matches none of the overloads above on arity, lands on
    /// the inherited tag pack, forwards the root as if it were a tag and reads today's flat value
    /// — the same silent latest-for-historical failure the plain-root overload exists to prevent,
    /// one arity up. Deleting it makes the call a compile error instead. Read the historical slot
    /// and the tagged flat slot as two separate calls.
    template <class... Tags>
    bcos::task::Task<evmc_bytes32> storage(
        const evmc_bytes32&, bcos::h256 const&, Tags&&...) = delete;
    template <class... Tags>
    bcos::task::Task<evmc_bytes32> storage(
        const evmc_bytes32&, std::optional<bcos::h256> const&, Tags&&...) = delete;

    /// The raw 32-byte slot value as a storage Entry, or nullopt when the slot is absent. With a
    /// root, @p key must be a 32-byte slot key — nothing else can exist in a storage trie — so
    /// any other length is absent by construction; the flat path keeps the base's behaviour of
    /// reading any account-table field name.
    bcos::task::Task<std::optional<bcos::storage::Entry>> storageEntry(
        const std::string_view& key, std::optional<bcos::h256> stateRoot = {})
    {
        if (!stateRoot)
        {
            co_return co_await Base::storageEntry(key);
        }
        if (key.size() != bcos::h256::SIZE)
        {
            co_return std::nullopt;
        }
        bcos::h256 const slot{
            bcos::bytesConstRef{reinterpret_cast<const bcos::byte*>(key.data()), key.size()}};
        auto const value = co_await readTrieSlot(*stateRoot, slot);
        if (!value)
        {
            co_return std::nullopt;
        }
        bcos::h256 const padded{*value};
        co_return bcos::storage::Entry{bcos::concepts::bytebuffer::toView(padded)};
    }

private:
    /// The account leaf 4-tuple at @p stateRoot, or nullopt when absent. Cached per root: the
    /// trie at a fixed root is immutable and reads never change it, so one walk serves every
    /// field read and every slot read of the same historical query.
    bcos::task::Task<std::optional<Account>> readLeaf(bcos::h256 const& stateRoot)
    {
        if (m_cachedLeafRoot == stateRoot)
        {
            co_return m_cachedLeaf;
        }
        MPTReadView<NodeStorage> const view{m_nodeStorage.get(), stateRoot};
        auto account = co_await view.readAccount(m_address);
        m_cachedLeafRoot = stateRoot;
        m_cachedLeaf = account;
        co_return account;
    }

    /// The slot value from the account's storage trie at @p stateRoot. nullopt when the account
    /// is absent, its storage trie is empty, or the slot has no leaf.
    bcos::task::Task<std::optional<bcos::u256>> readTrieSlot(
        bcos::h256 const& stateRoot, bcos::h256 const& slot)
    {
        auto const account = co_await readLeaf(stateRoot);
        if (!account || account->storageRoot == emptyRootHash())
        {
            co_return std::nullopt;
        }
        Trie<NodeStorage> const trie{m_nodeStorage.get(), account->storageRoot};
        // The hasher-injection form, per StorageValueCodec.h's hot-path convention: this is the
        // per-SLOAD path of a historical call, and the convenience overload builds a fresh
        // OpenSSL context every time. Constructed on first use rather than held by value, so the
        // default (flat) path keeps EVMAccount's allocation-free, non-throwing construction.
        if (!m_hasher)
        {
            m_hasher.emplace();
        }
        auto const leaf = co_await trie.get(slotKeyHash(slot, *m_hasher));
        if (!leaf)
        {
            co_return std::nullopt;
        }
        co_return decodeStorageValue(bcos::ref(*leaf));
    }

    std::reference_wrapper<NodeStorage> m_nodeStorage;
    std::reference_wrapper<BackendStorage> m_backendStorage;
    bcos::Address m_address;

    std::optional<bcos::h256> m_cachedLeafRoot;
    std::optional<Account> m_cachedLeaf;
    /// Reused slot-key hash context, built on the first rooted slot read (see readTrieSlot).
    std::optional<bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher> m_hasher;
};

}  // namespace bcos::ledger::mpt
