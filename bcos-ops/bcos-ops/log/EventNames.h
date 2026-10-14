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
 * @brief the event names the `log` commands consume; one constant per handbook row
 *        (tools/.ci/check_log_events.sh requires every handbook name to appear here)
 * @file EventNames.h
 */
#pragma once

#include <string_view>

namespace bcos::ops::events
{
// PBFT round
constexpr std::string_view PrePrepareSent = "PrePrepareSent";
constexpr std::string_view PrePrepareReceived = "PrePrepareReceived";
constexpr std::string_view PrePrepareRejected = "PrePrepareRejected";
constexpr std::string_view PrepareSent = "PrepareSent";
constexpr std::string_view PrepareReceived = "PrepareReceived";
constexpr std::string_view PrepareQuorum = "PrepareQuorum";
constexpr std::string_view CommitSent = "CommitSent";
constexpr std::string_view CommitReceived = "CommitReceived";
constexpr std::string_view CommitQuorum = "CommitQuorum";
constexpr std::string_view ProposalExecuted = "ProposalExecuted";
constexpr std::string_view ProposalExecuteFailed = "ProposalExecuteFailed";
constexpr std::string_view CheckpointSent = "CheckpointSent";
constexpr std::string_view CheckpointReceived = "CheckpointReceived";
constexpr std::string_view CheckpointQuorum = "CheckpointQuorum";
constexpr std::string_view CheckpointResend = "CheckpointResend";
constexpr std::string_view BlockCommitted = "BlockCommitted";  // PBFT (STORAGE) and SYNC
constexpr std::string_view Report = "Report";
// viewchange
constexpr std::string_view ViewChangeTriggered = "ViewChangeTriggered";
constexpr std::string_view ViewChangeSent = "ViewChangeSent";
constexpr std::string_view ViewChangeReceived = "ViewChangeReceived";
constexpr std::string_view ViewChangeRejected = "ViewChangeRejected";
constexpr std::string_view ViewChangeQuorum = "ViewChangeQuorum";
constexpr std::string_view NewViewSent = "NewViewSent";
constexpr std::string_view NewViewReceived = "NewViewReceived";
constexpr std::string_view NewViewRejected = "NewViewRejected";
constexpr std::string_view NewViewReached = "NewViewReached";
// seal stall
constexpr std::string_view SealSkipped = "SealSkipped";
constexpr std::string_view SealResumed = "SealResumed";
// tx lifecycle
constexpr std::string_view TxAdmitted = "TxAdmitted";
constexpr std::string_view TxRejected = "TxRejected";
constexpr std::string_view TxSealed = "TxSealed";
constexpr std::string_view TxSealSkipped = "TxSealSkipped";
constexpr std::string_view TxExecuted = "TxExecuted";
constexpr std::string_view TxRemoved = "TxRemoved";
constexpr std::string_view TxsFetched = "TxsFetched";
constexpr std::string_view TxsRemoved = "TxsRemoved";
constexpr std::string_view TxsExpired = "TxsExpired";
constexpr std::string_view TxPoolFull = "TxPoolFull";
constexpr std::string_view TxPoolRecovered = "TxPoolRecovered";
constexpr std::string_view TxBroadcastFallback = "TxBroadcastFallback";
// tx sync
constexpr std::string_view ProposalVerified = "ProposalVerified";
constexpr std::string_view ProposalTxsMissing = "ProposalTxsMissing";
constexpr std::string_view TxsRequested = "TxsRequested";
constexpr std::string_view TxsReceived = "TxsReceived";
constexpr std::string_view TxsRequestFailed = "TxsRequestFailed";
// block sync
constexpr std::string_view SyncStarted = "SyncStarted";
constexpr std::string_view BlockRequested = "BlockRequested";
constexpr std::string_view BlockApplied = "BlockApplied";
constexpr std::string_view SyncFinished = "SyncFinished";
constexpr std::string_view PeerStatus = "PeerStatus";
// p2p
constexpr std::string_view PeerConnected = "PeerConnected";
constexpr std::string_view PeerDisconnected = "PeerDisconnected";
constexpr std::string_view HandshakeFailed = "HandshakeFailed";
constexpr std::string_view PeerConnectFailed = "PeerConnectFailed";
// ledger
constexpr std::string_view AsyncPrewriteBlock = "asyncPrewriteBlock";
constexpr std::string_view AsyncPreStoreBlockTxs = "asyncPreStoreBlockTxs";
// per-block stats
constexpr std::string_view BlockStat = "BlockStat";

// badges (first badge of the line) the traces filter on
constexpr std::string_view BadgeConsensus = "CONSENSUS";
constexpr std::string_view BadgePbft = "PBFT";
constexpr std::string_view BadgeTxPool = "TXPOOL";
constexpr std::string_view BadgeTxSync = "SYNC";
constexpr std::string_view BadgeBlockSync = "BLOCK SYNC";
constexpr std::string_view BadgeScheduler = "SCHEDULER";
constexpr std::string_view BadgeNetwork = "NETWORK";
constexpr std::string_view BadgeP2P = "P2PService";
}  // namespace bcos::ops::events
