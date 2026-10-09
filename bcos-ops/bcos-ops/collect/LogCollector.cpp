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
 * @file LogCollector.cpp
 */
#include "LogCollector.h"
#include "bcos-ops/log/EventNames.h"

namespace bcos::ops
{
NodeStatus collectFromLog(
    std::vector<Event> const& _events, int64_t _nowMs, int64_t _viewChangeWindowMs)
{
    NodeStatus status;
    status.source = "log";
    status.collectedAt = _nowMs;

    Event const* report = nullptr;
    Event const* versionLine = nullptr;
    Event const* syncFlip = nullptr;
    Event const* syncProgress = nullptr;  // BlockApplied / BlockCommitted on the SYNC channel
    Event const* txpoolStat = nullptr;
    Event const* lastViewChange = nullptr;
    for (auto const& event : _events)
    {
        if (event.name == events::Report && event.hasBadge(events::BadgePbft))
        {
            report = &event;
        }
        else if (event.name == "compatibilityVersion updated" && event.kv.contains("version"))
        {
            versionLine = &event;
        }
        else if (event.name == events::SyncStarted || event.name == events::SyncFinished)
        {
            syncFlip = &event;
        }
        else if ((event.name == events::BlockApplied || event.name == events::BlockCommitted) &&
                 event.hasBadge(events::BadgeBlockSync))
        {
            syncProgress = &event;
        }
        else if (event.name == events::BlockStat && event.hasBadge(events::BadgeTxPool))
        {
            txpoolStat = &event;
        }
        else if (event.name == events::ViewChangeTriggered)
        {
            lastViewChange = &event;
        }
    }

    if (report == nullptr)
    {
        for (auto const* field : {"nodeID", "blockNumber", "latestHash", "latestTimestamp", "view",
                 "changeCycle", "inTimeout", "consensusTimeoutMs", "isConsensusNode"})
        {
            status.missing(field, "no Report line in the window");
        }
    }
    else
    {
        status.nodeId = report->get("nodeId");
        status.blockNumber = report->getInt("committedIndex");
        status.latestHash = report->get("committedHash");
        status.latestTimestamp = report->timeMs;
        status.view = report->getInt("view");
        status.changeCycle = report->getInt("changeCycle");
        status.consensusTimeoutMs = report->getInt("consensusTimeout");
        auto toView = report->getInt("toView");
        bool inTimeout = status.view && toView && *toView > *status.view;
        if (lastViewChange && lastViewChange->timeMs >= _nowMs - _viewChangeWindowMs)
        {
            inTimeout = true;
        }
        status.inTimeout = inTimeout;
        // Idx is printed unsigned: an observer shows 18446744073709551615, which stoll rejects
        auto idxText = report->get("Idx");
        if (!idxText.empty())
        {
            try
            {
                status.isConsensusNode = std::stoull(idxText) < (1ULL << 62);
            }
            catch (std::exception const&)
            {
                status.isConsensusNode = false;
            }
        }
        status.groupId = std::nullopt;
    }
    if (versionLine)
    {
        status.version = versionLine->get("version");
    }
    else
    {
        status.missing("version", "no compatibilityVersion line in the window");
    }
    // SyncStarted/SyncFinished print once per flip, so a node that has been downloading for
    // longer than the window shows only BlockApplied/BlockCommitted lines; those mean "syncing"
    // unless a later SyncFinished closed the segment
    bool progressAfterFlip = syncProgress && (!syncFlip || syncProgress->timeMs > syncFlip->timeMs);
    if (syncFlip && syncFlip->name == events::SyncStarted)
    {
        status.isSyncing = true;
        status.knownHighestNumber = syncFlip->getInt("highest");
        status.lag = syncFlip->getInt("lag");
    }
    else if (progressAfterFlip)
    {
        status.isSyncing = true;
        status.missing(
            "knownHighestNumber", "syncing (BlockApplied in window) without a SyncStarted line");
        status.missing("lag", "syncing (BlockApplied in window) without a SyncStarted line");
    }
    else if (syncFlip)  // SyncFinished is the latest sync event
    {
        status.isSyncing = false;
        status.knownHighestNumber = syncFlip->getInt("number");
        status.lag = 0;
    }
    else
    {
        // no [BLOCK SYNC] event at all: the window cannot tell idle from a long download
        status.missing("isSyncing", "no sync event in window");
        status.missing("knownHighestNumber", "no sync event in window");
        status.missing("lag", "no sync event in window");
    }
    if (txpoolStat)
    {
        status.pendingTxSize = txpoolStat->getInt("pending");
    }
    else
    {
        status.missing("pendingTxSize",
            "BlockStat disabled (log.enable_block_stat) or no block in the window");
    }
    status.missing("connectedGroupNodes", "not in log");
    status.missing("minRequiredQuorum", "not in log");
    status.missing("peerCount", "not in log");
    status.missing("groupId", "not in log");
    status.missing("chainId", "not in log");
    status.missing("txpoolLimit", "not in log");
    return status;
}
}  // namespace bcos::ops
