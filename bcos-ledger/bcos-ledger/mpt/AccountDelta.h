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
 * @file AccountDelta.h
 * @brief Per-account field changes for one block, grouped by address (spec §5.2)
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <optional>
#include <unordered_map>

namespace bcos::ledger::mpt
{

/// All changes a single block makes to a single account. Produced by classify()
/// from the flat key→Entry delta; consumed by the MPT builder (PR-10).
///
/// A field's *State* records whether the block touched it. nonce/balance/codeHash
/// carry both a State and the new value (only meaningful when State==Updated).
/// storageChanges holds per-slot new values, where nullopt means the slot was
/// deleted (zeroed).
struct AccountDelta
{
    enum class FieldState
    {
        Unchanged,  ///< the block did not write this field
        Updated,    ///< the block wrote a new value (held in the sibling member)
        Deleted     ///< the block deleted this field (Entry status DELETED)
    };

    FieldState nonceState = FieldState::Unchanged;
    bcos::u256 nonce{};

    FieldState balanceState = FieldState::Unchanged;
    bcos::u256 balance{};

    FieldState codeHashState = FieldState::Unchanged;
    bcos::h256 codeHash{};

    /// slot-key → new value, or nullopt to delete (zero) the slot.
    std::unordered_map<bcos::h256, std::optional<bcos::bytes>> storageChanges;

    /// All three core fields (nonce, balance, codeHash) deleted in this block →
    /// the account is destroyed (SELFDESTRUCT). The builder removes its leaf.
    bool tombstone = false;

    /// The account did not exist in the parent state (readView.hasAccount==false).
    /// The builder inserts a brand-new leaf rather than merging onto an old one.
    bool firstTouch = false;
};

/// One block's changes to every touched account, keyed by 20-byte address.
struct MPTBuildInput
{
    std::unordered_map<bcos::Address, AccountDelta> perAccount;
};

}  // namespace bcos::ledger::mpt
