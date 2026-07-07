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
 * @file FlatToMPT.h
 * @brief Flat-KV → MPT migration: first-touch baseline construction and the preheat manifest
 *        (spec §5.3 path 2, §5.10, §5.11)
 */
#pragma once

#include "Errors.h"
#include "HashBuilder.h"
#include "MPTDeltaLayer.h"
#include "StorageValueCodec.h"
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/throw_exception.hpp>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bcos::ledger::mpt
{

// Everything in this header serves scenario A — a legacy chain activating
// feature_mpt_state_root mid-flight (spec §5.10): only there does an account own flat-KV state
// that predates the MPT and must be migrated into it. The migration is lazy (spec §5.3 path 2):
// an account enters the MPT the first time a block touches it; dormant accounts stay in the
// flat KV indefinitely. A scenario-B chain (L2, MPT from genesis) has no pre-MPT state; its
// first-touch deltas only ever see an empty scan and never profit from preheating.

/// The preheat manifest is one flat-KV record per pre-built account storage trie:
///   key   = "__mpt_preheat:" + lowercase-hex address (40 chars, no 0x)
///   value = the 32-byte storage root the preheat run committed for that account
/// A hit lets the first-touch path adopt the recorded root as its baseline instead of scanning
/// every flat slot (spec §5.3 path 2b). This class owns the schema plus the read/remove side;
/// the manifest-writing CLI is Plan C scope and does not exist yet.
///
/// Preheating moves a large account's one-off migration cost off the consensus commit path into
/// an operator-chosen offline window. It is purely an accelerator — correctness never depends on
/// a manifest being present.
///
/// Backend access is via injected callbacks rather than a storage type: manifest records live in
/// the string-keyed flat KV space, not the h256-keyed node space the MPT templates are built
/// over, and the eventual owner (MultiLayerStorage / ledger) supplies read/delete lambdas.
class PreheatManifest
{
public:
    static constexpr std::string_view keyPrefix = "__mpt_preheat:";

    /// The manifest key for @p addr: keyPrefix + 40 lowercase hex chars.
    static std::string makeKey(bcos::Address const& addr);

    using BackendReader = std::function<bcos::task::Task<std::optional<bcos::bytes>>(std::string)>;
    using BackendDeleter = std::function<bcos::task::Task<void>(std::string)>;

    /// The recorded storage root for @p addr, or nullopt when no manifest record exists.
    /// @throws MPTInvariantViolation when a record is present but not exactly 32 bytes —
    ///         a corrupt manifest must fail loudly, not seed a bogus baseline root.
    static bcos::task::Task<std::optional<bcos::h256>> read(
        BackendReader const& reader, bcos::Address const& addr);

    /// Delete @p addr's manifest record. A consumed record must not seed a second first-touch:
    /// the recorded root goes stale the moment the block's changes commit. Scheduled by the
    /// commit flow (PR-14b) off MPTDeltaLayer::preheatManifestsToDelete; the interface lives
    /// here so the schema has a single owner.
    static bcos::task::Task<void> remove(BackendDeleter const& deleter, bcos::Address const& addr);
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

/// Injected flat-KV access the migration needs (spec §5.3 path 2). All callbacks are optional:
/// default-constructed backends keep MPTBuilder in subsequent-touch-only mode, and each callback
/// is only required by the sub-path that actually consumes it.
struct FlatToMPTBackends
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

/// Resolve a first-touch account's baseline storage-trie root — the migration step proper.
/// Path 2b (manifest hit): adopt the recorded root (its nodes were flushed by the preheat run)
/// and schedule the consumed record for deletion via @p output. Path 2a (miss): bootstrap the
/// trie from a full flat-KV slot scan, building from the empty root; produced nodes are flushed
/// through @p storage and absorbed into @p output. The caller (MPTBuilder) then applies the
/// block's own slot changes over the returned root.
/// @throws MPTInvariantViolation on a manifest miss without a flatSlotScanner.
template <bcos::storage2::ReadWriteStorage<bcos::h256, bcos::bytes> Storage,
    bcos::crypto::hasher::Hasher HasherT>
bcos::task::Task<bcos::h256> resolveFirstTouchBaseline(Storage& storage,
    FlatToMPTBackends const& backends, bcos::Address const& addr, HasherT& hasher,
    MPTDeltaLayer& output)
{
    if (backends.manifestReader)
    {
        auto preheated = co_await PreheatManifest::read(backends.manifestReader, addr);
        if (preheated.has_value())
        {
            // The preheat run already built and flushed this account's storage trie; adopt its
            // root and schedule the consumed record for deletion.
            output.preheatManifestsToDelete.push_back(addr);
            co_return *preheated;
        }
    }

    if (!backends.flatSlotScanner)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                  "FlatToMPT: first-touch needs flatSlotScanner"));
    }
    HashBuilder<Storage, HasherT> bootstrap(storage, emptyRootHash<HasherT>());
    // Bind the awaited result to a local before iterating: iterating a co_await temporary
    // directly is the same GCC template-coroutine hazard class as the member access noted in
    // MPTReadView::hasAccount.
    auto flatSlots = co_await backends.flatSlotScanner(addr);
    for (auto& [slot, value] : flatSlots)
    {
        auto encoded = encodeStorageValue(bcos::ref(value));
        if (encoded.empty())
        {
            continue;  // zero value: never part of the storage trie
        }
        co_await bootstrap.put(slotKeyHash(slot, hasher), std::move(encoded));
    }
    auto baselineRoot = co_await bootstrap.commit();
    absorbNodeDelta(bootstrap, output);
    co_return baselineRoot;
}

}  // namespace bcos::ledger::mpt
