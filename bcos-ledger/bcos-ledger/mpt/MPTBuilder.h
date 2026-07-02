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
 * @brief Commit-time MPT entry point — subsequent-touch path (spec §5.3 path 1, §5.4)
 */
#pragma once

#include "Account.h"
#include "AccountDelta.h"
#include "Errors.h"
#include "HashBuilder.h"
#include "MPTReadView.h"
#include "StorageValueCodec.h"
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/throw_exception.hpp>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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
};

/// Builds the post-block MPT from a classify()-produced MPTBuildInput on top of the parent
/// block's state root. Templated on the same storage2 ReadWriteStorage the trie primitives use:
/// baseline reads (parent account leaves, prior storage-trie nodes) and node flushes all go
/// through @p Storage.
///
/// SCOPE (M4.3): only the subsequent-touch path — the account already exists in the parent MPT.
/// firstTouch and tombstone deltas throw MPTInvariantViolation until PR-11/PR-12 land.
template <bcos::storage2::ReadWriteStorage<bcos::h256, bcos::bytes> Storage>
class MPTBuilder
{
public:
    /// @param storage         node storage shared by reads and writes.
    /// @param parentStateRoot the parent block's MPT state root (emptyRootHash() = empty state).
    MPTBuilder(Storage& storage, bcos::h256 parentStateRoot)
      : m_storage(storage), m_parentStateRoot(parentStateRoot)
    {}

    /// Apply every AccountDelta in @p input and return the new state root plus the node delta.
    /// @throws MPTInvariantViolation on firstTouch/tombstone deltas (PR-11/12 scope), on a
    ///         subsequent-touch account missing from the parent state, and on a Deleted field
    ///         state outside a tombstone (spec §5.4 treats that as an error).
    bcos::task::Task<MPTBuildOutput> buildAndCollect(MPTBuildInput const& input)
    {
        MPTBuildOutput output;
        MPTReadView<Storage> view(m_storage.get(), m_parentStateRoot);
        HashBuilder accountBuilder(m_storage.get(), m_parentStateRoot);

        for (auto const& [addr, delta] : input.perAccount)
        {
            if (delta.firstTouch)
            {
                BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                          "MPTBuilder: first-touch path is PR-11 (M4.4) scope"));
            }
            if (delta.tombstone)
            {
                BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                          "MPTBuilder: tombstone path is PR-12 (M4.5) scope"));
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
        co_return output;
    }

private:
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
        HashBuilder storageBuilder(m_storage.get(), priorStorageRoot);
        for (auto const& [slot, valueOpt] : delta.storageChanges)
        {
            auto const keyHash = slotKeyHash(slot);
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

    static void mergeNodeDelta(HashBuilder<Storage>& builder, MPTBuildOutput& output)
    {
        for (auto& [hash, raw] : builder.drainNewNodes())
        {
            output.newNodes.insert_or_assign(hash, std::move(raw));
        }
        output.obsoletedNodes.merge(builder.drainObsoletedNodes());
    }

    std::reference_wrapper<Storage> m_storage;
    bcos::h256 m_parentStateRoot;
};

}  // namespace bcos::ledger::mpt
