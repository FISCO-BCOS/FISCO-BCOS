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
 * @file MPTBuilder.h
 * @brief Commit-time MPT entry point — subsequent-touch, first-touch and tombstone paths
 *        (spec §5.3, §5.4, §5.11)
 */
#pragma once

#include "Account.h"
#include "AccountDelta.h"
#include "Errors.h"
#include "HashBuilder.h"
#include "MPTReadView.h"
#include "PreheatManifest.h"
#include "StorageValueCodec.h"
// Named directly for the HasherT default template argument (also reachable transitively).
#include <bcos-crypto/hasher/OpenSSLHasher.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/throw_exception.hpp>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt
{

/// One block's MPT build product: the new state root plus the node delta the commit path must
/// persist alongside the flat state. The two node sets carry the same contract as
/// HashBuilder::drainNewNodes()/drainObsoletedNodes(); PR-13's MPTDeltaLayer will absorb this
/// shape (the card's forward-declared return type cannot be returned by value, so the struct
/// lives here until then).
struct MPTBuildOutput
{
    bcos::h256 stateRoot;
    std::unordered_map<bcos::h256, bcos::bytes> newNodes;
    std::unordered_set<bcos::h256> obsoletedNodes;
    /// Accounts whose preheat manifest record was consumed by this build (first-touch path 2b).
    /// The commit flow (PR-14b) deletes each record via PreheatManifest::remove in the same
    /// batch that persists the block — a consumed root must not seed a second first-touch.
    std::vector<bcos::Address> preheatManifestsToDelete;
};

/// An account's baseline nonce/balance/codeHash as stored in the flat KV, for first-touch
/// fields the block did not update (a first-touch account has no parent MPT leaf to merge onto).
struct FlatAccountMeta
{
    bcos::u256 nonce{};
    bcos::u256 balance{};
    /// Implementations MUST return emptyCodeHash() — the hash of empty code — for accounts
    /// without code, never a zero h256: the account leaf encodes this field verbatim
    /// (Yellow Paper §4.1), and a zero here would produce a wrong leaf hash.
    bcos::h256 codeHash{};
};

/// Injected flat-KV access the first-touch path needs (spec §5.3 path 2). All callbacks are
/// optional: default-constructed backends keep MPTBuilder in subsequent-touch-only mode, and
/// each callback is only required by the sub-path that actually consumes it.
///
/// This machinery exists for scenario A — a legacy chain activating feature_mpt_state_root
/// mid-flight (spec §5.10): only there does an account own flat-KV state that predates the MPT
/// and must be bootstrapped into it. A scenario-B chain (L2, MPT from genesis) has no pre-MPT
/// state; its first-touch deltas only ever see an empty scan and never profit from preheating.
struct MPTBuilderBackends
{
    /// Enumerate ALL existing storage slots of an account from the flat KV:
    /// (raw 32-byte slot key → raw stored value). Required on a manifest miss (path 2a).
    std::function<bcos::task::Task<std::vector<std::pair<bcos::h256, bcos::bytes>>>(
        bcos::Address const&)>
        flatSlotScanner;
    /// Baseline nonce/balance/codeHash from the flat KV. Required when a first-touch delta
    /// leaves any of the three fields Unchanged.
    std::function<bcos::task::Task<FlatAccountMeta>(bcos::Address const&)> flatAccountMeta;
    /// Preheat manifest KV reader (PreheatManifest::read is called over it). Unset = no
    /// manifest lookup; every first-touch takes the flat-scan path 2a.
    PreheatManifest::BackendReader manifestReader;
};

/// Builds the post-block MPT from a classify()-produced MPTBuildInput on top of the parent
/// block's state root. Templated on the same storage2 ReadWriteStorage the trie primitives use:
/// baseline reads (parent account leaves, prior storage-trie nodes) and node flushes all go
/// through @p Storage.
///
/// Covers all three delta shapes (spec §5.3): subsequent-touch (account already in the parent
/// MPT), first-touch (absent from the parent MPT — bootstrap from the flat KV, or from a
/// preheat-manifest hit) and tombstone (SELFDESTRUCT — the account leaf is removed).
///
/// @tparam HasherT the node/key hash function (Hasher concept), owned here and threaded into
/// slotKeyHash and both HashBuilder instantiations; keccak256 by default, SM3 for guomi
/// deployments (same end-to-end caveat as HashBuilder: the from-empty core, accountKeyHash and
/// Account's default storageRoot/codeHash still need parameterizing before SM3 is complete).
template <bcos::storage2::ReadWriteStorage<bcos::h256, bcos::bytes> Storage,
    bcos::crypto::hasher::Hasher HasherT = bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher>
class MPTBuilder
{
public:
    /// @param storage         node storage shared by reads and writes.
    /// @param parentStateRoot the parent block's MPT state root (emptyRootHash() = empty state).
    MPTBuilder(Storage& storage, bcos::h256 parentStateRoot)
      : m_storage(storage), m_parentStateRoot(parentStateRoot)
    {}

    /// As above, plus the flat-KV backends the first-touch path reads through. Without them
    /// (the 2-arg ctor) any first-touch delta throws for lack of a scanner.
    MPTBuilder(Storage& storage, bcos::h256 parentStateRoot, MPTBuilderBackends backends)
      : m_storage(storage), m_parentStateRoot(parentStateRoot), m_backends(std::move(backends))
    {}

    /// Apply every AccountDelta in @p input and return the new state root plus the node delta.
    /// @throws MPTInvariantViolation on a first-touch delta whose required backend callback is
    ///         missing, on a subsequent-touch account missing from the parent state, and on a
    ///         Deleted field state outside a tombstone (spec §5.4 treats that as an error).
    bcos::task::Task<MPTBuildOutput> buildAndCollect(MPTBuildInput const& input)
    {
        MPTBuildOutput output;
        MPTReadView<Storage> view(m_storage.get(), m_parentStateRoot);
        // Spelled explicitly (no CTAD): CTAD would silently fall back to the default keccak
        // hasher even when this MPTBuilder is instantiated with another HasherT.
        HashBuilder<Storage, HasherT> accountBuilder(m_storage.get(), m_parentStateRoot);

        for (auto const& [addr, delta] : input.perAccount)
        {
            // Tombstone before firstTouch: destruction wins over creation when a pathological
            // delta carries both flags (spec §5.4 priority).
            if (delta.tombstone)
            {
                // storageChanges/field states are ignored on a tombstone (classify already
                // folded the account's fate). Record the prior storage root for future pathdb
                // pruning — the full subtree walk is deferred to the pruning spec, the root
                // hash is the ledger entry.
                auto prior = co_await view.readAccount(addr);
                if (prior && prior->storageRoot != emptyRootHash<HasherT>())
                {
                    output.obsoletedNodes.insert(prior->storageRoot);
                }
                // Absent prior account (the pathological tombstone+firstTouch input): removing
                // an absent key is a no-op in HashBuilder — same net effect, no special case.
                co_await accountBuilder.remove(accountKeyHash(addr));
                continue;
            }
            if (delta.firstTouch)
            {
                co_await buildFirstTouch(addr, delta, accountBuilder, output);
                continue;
            }

            // Subsequent-touch baseline: the parent block's account leaf must exist —
            // classify() only clears firstTouch when readView.hasAccount() was true.
            auto baseline = co_await view.readAccount(addr);
            if (!baseline)
            {
                BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                          "MPTBuilder: subsequent-touch account missing from "
                                          "parent state"));
            }

            Account updated = *baseline;
            updated.storageRoot =
                co_await applyStorageChanges(baseline->storageRoot, delta, output);
            applyField(delta.nonceState, delta.nonce, updated.nonce);
            applyField(delta.balanceState, delta.balance, updated.balance);
            applyField(delta.codeHashState, delta.codeHash, updated.codeHash);

            co_await accountBuilder.put(accountKeyHash(addr), updated.encode());
        }

        output.stateRoot = co_await accountBuilder.commit();
        mergeNodeDelta(accountBuilder, output);

        // MPTBuildOutput aggregates several HashBuilder commits (a first-touch bootstrap plus
        // the same block's apply, plus the account trie), so unlike a single mergeTrie() result
        // the two sets can intersect: a node emitted by one commit and obsoleted by a later one.
        // Re-establish disjointness the way mergeTrie() does (end-subtraction): the node stays
        // in newNodes — it is already flushed and may be referenced by another trie — and leaves
        // the prune ledger, so a consumer never writes and deletes the same node in one batch.
        for (auto const& [hash, raw] : output.newNodes)
        {
            output.obsoletedNodes.erase(hash);
        }
        co_return output;
    }

private:
    /// The first-touch path (spec §5.3 path 2): the account has no parent-MPT leaf, so its
    /// baseline storage trie comes from either a preheat-manifest hit (2b — the recorded root,
    /// pre-built nodes already in storage) or a full flat-KV slot scan (2a — bootstrap from the
    /// empty root). This block's slot changes then apply over that baseline, and account fields
    /// the block left Unchanged come from the flat KV via flatAccountMeta.
    bcos::task::Task<void> buildFirstTouch(bcos::Address const& addr, AccountDelta const& delta,
        HashBuilder<Storage, HasherT>& accountBuilder, MPTBuildOutput& output)
    {
        auto baselineStorageRoot = emptyRootHash<HasherT>();
        bool manifestHit = false;
        if (m_backends.manifestReader)
        {
            auto preheated = co_await PreheatManifest::read(m_backends.manifestReader, addr);
            if (preheated.has_value())
            {
                // Path 2b: the preheat run already built and flushed this account's storage
                // trie; adopt its root and schedule the consumed record for deletion.
                baselineStorageRoot = *preheated;
                output.preheatManifestsToDelete.push_back(addr);
                manifestHit = true;
            }
        }
        if (!manifestHit)
        {
            // Path 2a: bootstrap the storage trie from every existing flat slot.
            if (!m_backends.flatSlotScanner)
            {
                BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                          "MPTBuilder: first-touch needs flatSlotScanner"));
            }
            HashBuilder<Storage, HasherT> bootstrap(m_storage.get(), emptyRootHash<HasherT>());
            // Bind the awaited result to a local before iterating: iterating a co_await
            // temporary directly is the same GCC template-coroutine hazard class as the member
            // access noted in MPTReadView::hasAccount.
            auto flatSlots = co_await m_backends.flatSlotScanner(addr);
            for (auto& [slot, value] : flatSlots)
            {
                auto encoded = encodeStorageValue(bcos::ref(value));
                if (encoded.empty())
                {
                    continue;  // zero value: never part of the storage trie
                }
                co_await bootstrap.put(slotKeyHash(slot, m_hasher), std::move(encoded));
            }
            baselineStorageRoot = co_await bootstrap.commit();
            mergeNodeDelta(bootstrap, output);
        }

        Account updated;  // default storageRoot/codeHash — a fresh EOA until proven otherwise
        updated.storageRoot = co_await applyStorageChanges(baselineStorageRoot, delta, output);

        // Fields the block left Unchanged have no parent leaf to fall back on: fetch the flat
        // baseline, once, only when actually needed.
        std::optional<FlatAccountMeta> flatMeta;
        if (delta.nonceState == AccountDelta::FieldState::Unchanged ||
            delta.balanceState == AccountDelta::FieldState::Unchanged ||
            delta.codeHashState == AccountDelta::FieldState::Unchanged)
        {
            if (!m_backends.flatAccountMeta)
            {
                BOOST_THROW_EXCEPTION(
                    MPTInvariantViolation{} << bcos::errinfo_comment(
                        "MPTBuilder: first-touch with Unchanged fields needs flatAccountMeta"));
            }
            flatMeta = co_await m_backends.flatAccountMeta(addr);
        }
        FlatAccountMeta const meta = flatMeta.value_or(FlatAccountMeta{});
        applyFirstTouchField(delta.nonceState, delta.nonce, meta.nonce, updated.nonce);
        applyFirstTouchField(delta.balanceState, delta.balance, meta.balance, updated.balance);
        applyFirstTouchField(delta.codeHashState, delta.codeHash, meta.codeHash, updated.codeHash);

        co_await accountBuilder.put(accountKeyHash(addr), updated.encode());
    }

    /// First-touch counterpart of applyField(): Updated takes the delta value, Unchanged takes
    /// the flat-KV baseline (only reachable with flatMeta fetched), Deleted outside a tombstone
    /// throws — same rule as applyField.
    template <typename Field>
    static void applyFirstTouchField(AccountDelta::FieldState state, Field const& deltaValue,
        Field const& flatValue, Field& field)
    {
        switch (state)
        {
        case AccountDelta::FieldState::Unchanged:
            field = flatValue;
            break;
        case AccountDelta::FieldState::Updated:
            field = deltaValue;
            break;
        case AccountDelta::FieldState::Deleted:
            BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                      "MPTBuilder: field Deleted outside a tombstone"));
        }
    }

    /// Overwrite @p field from @p value when the block updated it. A Deleted state outside a
    /// tombstone means classify() and the builder disagree about the account's fate — throw.
    template <typename Field>
    static void applyField(AccountDelta::FieldState state, Field const& value, Field& field)
    {
        switch (state)
        {
        case AccountDelta::FieldState::Unchanged:
            break;
        case AccountDelta::FieldState::Updated:
            field = value;
            break;
        case AccountDelta::FieldState::Deleted:
            BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                      "MPTBuilder: field Deleted outside a tombstone"));
        }
    }

    /// Rebuild the account's storage trie from its prior root with this block's slot changes
    /// applied (spec §5.3 path 1). No changes → the prior root stands, nothing is read.
    bcos::task::Task<bcos::h256> applyStorageChanges(
        bcos::h256 priorStorageRoot, AccountDelta const& delta, MPTBuildOutput& output)
    {
        if (delta.storageChanges.empty())
        {
            co_return priorStorageRoot;
        }
        HashBuilder<Storage, HasherT> storageBuilder(m_storage.get(), priorStorageRoot);
        for (auto const& [slot, valueOpt] : delta.storageChanges)
        {
            auto const keyHash = slotKeyHash(slot, m_hasher);
            // nullopt and a value that trims to nothing both mean "slot leaves the trie".
            bcos::bytes encoded;
            if (valueOpt.has_value())
            {
                encoded = encodeStorageValue(bcos::ref(*valueOpt));
            }
            if (encoded.empty())
            {
                co_await storageBuilder.remove(keyHash);
            }
            else
            {
                co_await storageBuilder.put(keyHash, std::move(encoded));
            }
        }
        auto newRoot = co_await storageBuilder.commit();
        mergeNodeDelta(storageBuilder, output);
        co_return newRoot;
    }

    static void mergeNodeDelta(HashBuilder<Storage, HasherT>& builder, MPTBuildOutput& output)
    {
        for (auto& [hash, raw] : builder.drainNewNodes())
        {
            output.newNodes.insert_or_assign(hash, std::move(raw));
        }
        output.obsoletedNodes.merge(builder.drainObsoletedNodes());
    }

    std::reference_wrapper<Storage> m_storage;
    bcos::h256 m_parentStateRoot;
    /// Flat-KV access for the first-touch path; default-constructed (all callbacks empty) for
    /// subsequent-touch-only usage via the 2-arg ctor.
    MPTBuilderBackends m_backends;
    /// One hash context per builder, reused for every slot-key transform of this block
    /// (slotKeyHash injection form). Single-threaded, like buildAndCollect itself.
    HasherT m_hasher;
};

}  // namespace bcos::ledger::mpt
