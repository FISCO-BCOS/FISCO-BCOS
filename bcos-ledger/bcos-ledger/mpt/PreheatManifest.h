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
 * @file PreheatManifest.h
 * @brief Preheat-manifest KV schema and read/remove interface (spec §5.11)
 */
#pragma once

#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::ledger::mpt
{

/// The preheat manifest is one flat-KV record per pre-built account storage trie:
///   key   = "__mpt_preheat:" + lowercase-hex address (40 chars, no 0x)
///   value = the 32-byte storage root the preheat run committed for that account
/// A hit lets MPTBuilder's first-touch path adopt the recorded root as its baseline instead of
/// scanning every flat slot (spec §5.3 path 2b). This class owns the schema plus the read/remove
/// side; the manifest-writing CLI is Plan C scope and does not exist yet.
///
/// Preheating is a scenario-A device (legacy chain activating feature_mpt_state_root mid-flight,
/// spec §5.10/§5.11): it moves a large account's one-off bootstrap cost off the consensus commit
/// path into an operator-chosen offline window. It is purely an accelerator — correctness never
/// depends on a manifest being present. Scenario-B chains (L2, MPT from genesis) have no pre-MPT
/// flat state and no use for it.
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
    /// commit flow (PR-14b) off MPTBuildOutput::preheatManifestsToDelete; the interface lives
    /// here so the schema has a single owner.
    static bcos::task::Task<void> remove(BackendDeleter const& deleter, bcos::Address const& addr);
};

}  // namespace bcos::ledger::mpt
