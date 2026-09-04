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
 * @file LedgerConfigState.h
 * @brief Node-wide ledger configuration, republished once per committed block.
 * @date 2026/8/31
 */
#pragma once
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-utilities/Common.h"
#include <memory>
#include <utility>

namespace bcos::ledger
{
/// The ledger configuration as of the last committed block, shared by every component that needs
/// it. Read on nearly every transaction; written once per block.
///
/// The contract is a SNAPSHOT, not a lock:
///
///   - get() returns the LedgerConfig that was current when it was called, and nothing mutates
///     that object afterwards. The caller may hold it for as long as it likes.
///   - set() publishes a NEW object. It does not edit the one readers are holding.
///
/// So a reader racing a commit sees either the previous block's configuration or the new one,
/// complete either way. A reader can still be held behind the writer, but only for the writer's
/// critical section: a pointer move-assign, plus at most dropping the last reference to the
/// previous config. Never for the time it takes to BUILD a configuration, which is where the
/// cost actually is. That is the property being bought: the config is assembled outside the
/// lock and published as a finished object.
///
/// That property is the whole point, because the two holders this replaces do not have it:
///
///   - executor::LedgerCache mutates *m_ledgerConfig in place (via getLedgerConfig writing into
///     the existing object) while ledgerConfig() hands out a reference to that same object, with
///     no lock on the config at all. A concurrent reader can observe a half-updated config --
///     some fields from block N, some from block N+1.
///   - scheduler::SchedulerImpl assigns the pointer, which is the right shape, but a plain
///     shared_ptr assignment concurrent with a read is a data race.
///
/// Callers must pass a freshly built config to set(), never a mutated copy of what get()
/// returned: readers may still be holding that object.
///
/// One node process serves one group (NodeConfig::groupId is singular), so a single instance per
/// process is per-group by construction.
class LedgerConfigState
{
public:
    using Ptr = std::shared_ptr<LedgerConfigState>;

    /// Starts with an empty configuration so readers never see null before the first publish.
    /// Empty is not the chain's configuration, and it is not uniformly looser either: no EVM
    /// revision is declared, which admission reads as "stand down", and gasLimit() is
    /// LedgerConfig's compile-time default rather than the chain's tx_gas_limit -- but chainId()
    /// is unset, which the chain-id check refuses. So the owner of a holder publishes a real
    /// configuration at startup and then after every commit; the empty state is for
    /// construction, not for serving traffic.
    LedgerConfigState() : m_config(std::make_shared<LedgerConfig>()) {}
    /// A null argument is replaced by an empty configuration rather than stored: the "never null"
    /// guarantee below has to hold at every entrance, and set() already enforces it. Storing null
    /// here would make every revision-dependent check throw on every transaction.
    explicit LedgerConfigState(std::shared_ptr<const LedgerConfig> config)
      : m_config(config ? std::move(config) : std::make_shared<LedgerConfig>())
    {}

    /// The configuration as of the last committed block. Never null.
    ///
    /// Returns a pointer to CONST. The snapshot contract -- "nothing mutates this object after
    /// publication" -- is the whole value of this class, and a mutable pointer would leave it as
    /// a comment that every caller is free to ignore. That is precisely how executor::LedgerCache
    /// ended up mutating its config in place.
    std::shared_ptr<const LedgerConfig> get() const
    {
        ReadGuard guard(x_config);
        return m_config;
    }

    /// Publish the configuration for a newly committed block. A null argument is ignored rather
    /// than published: losing the configuration entirely would turn every revision-dependent
    /// check into a stand-down, which is a worse failure than serving one stale block.
    ///
    /// Takes a pointer to const. The caller may keep a mutable handle of its own, but what is
    /// published is read-only from here on: the snapshot contract in the signature, not in a
    /// comment.
    void set(std::shared_ptr<const LedgerConfig> config)
    {
        if (!config)
        {
            return;
        }
        WriteGuard guard(x_config);
        m_config = std::move(config);
    }

private:
    mutable SharedMutex x_config;
    std::shared_ptr<const LedgerConfig> m_config;
};
}  // namespace bcos::ledger
