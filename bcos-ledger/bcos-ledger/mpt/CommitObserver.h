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
 * @file CommitObserver.h
 * @brief Post-commit hook over the block's MPTDeltaLayer — the pathdb pruning seam (spec §4.8)
 */
#pragma once

#include "MPTDeltaLayer.h"
#include <bcos-framework/protocol/ProtocolTypeDef.h>

namespace bcos::ledger::mpt
{

/// The seam reserved for the future pathdb pruning spec (§4.8): pruning subscribes to every
/// block's node delta without the commit flow knowing pruning exists. Interface lands ahead of
/// any real implementation so the two efforts stay decoupled.
class CommitObserver
{
public:
    virtual ~CommitObserver() = default;
    CommitObserver() = default;
    CommitObserver(CommitObserver const&) = default;
    CommitObserver(CommitObserver&&) = default;
    CommitObserver& operator=(CommitObserver const&) = default;
    CommitObserver& operator=(CommitObserver&&) = default;

    /// Timing contract (spec §5.6): the commit flow calls this AFTER the block's WriteBatch
    /// has landed on disk and BEFORE lastCommittedBlockNumber advances, so the delta the
    /// observer sees is exactly the persisted state. Runs on the commit path — implementations
    /// must not throw and must not block on slow work (hand off to their own executor instead).
    virtual void onCommit(bcos::protocol::BlockNumber blockNumber, MPTDeltaLayer const& delta) = 0;
};

/// The default observer until the pruning spec lands: receives and ignores every delta.
class NoopCommitObserver : public CommitObserver
{
public:
    void onCommit(
        bcos::protocol::BlockNumber /*blockNumber*/, MPTDeltaLayer const& /*delta*/) override
    {}
};

}  // namespace bcos::ledger::mpt
