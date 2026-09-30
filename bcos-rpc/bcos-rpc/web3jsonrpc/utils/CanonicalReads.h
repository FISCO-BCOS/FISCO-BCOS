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
 * @file CanonicalReads.h
 * @brief Block reads that follow the canonical chain on the OP Engine lane (D1 §10.2).
 */
#pragma once

#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-rpc/groupmgr/NodeService.h>
#include <bcos-task/Task.h>
#include <cstdint>
#include <optional>

namespace bcos::rpc
{

/// One routing rule for every block read EthEndpoint and the filter system make: without an
/// OP facade (NodeService::opCanonicalReader), the ledger exactly as before; with one, the
/// ledger at or below the finalized tip and the facade above it. The ledger's own error
/// contract is kept — a height the canonical chain does not have throws
/// LedgerError::GetStorageError like a missing ledger row, so callers that already map "not
/// found" (catch → JSON null) keep working unchanged.

/// `latest`: the facade's head (the Engine tracker's head, the finalized tip after a restart),
/// else the ledger's current number.
task::Task<protocol::BlockNumber> canonicalLatestNumber(NodeService& nodeService);

/// The block at @p blockNumber on the canonical chain, with @p blockFlag rows.
task::Task<protocol::Block::Ptr> canonicalBlockByNumber(
    NodeService& nodeService, protocol::BlockNumber blockNumber, int32_t blockFlag);

/// The block with @p blockHash: an unfinalized block (ANY branch — op-node's backup-unsafe
/// restore reads a replaced sibling by hash) from its own window layer, else the ledger.
task::Task<protocol::Block::Ptr> canonicalBlockByHash(
    NodeService& nodeService, crypto::HashType const& blockHash, int32_t blockFlag);

/// The canonical hash at @p blockNumber: the ledger's SYS_NUMBER_2_HASH at or below the
/// finalized tip, the head chain's block above it; throws (GetStorageError) when there is none.
task::Task<crypto::HashType> canonicalBlockHashAt(
    NodeService& nodeService, protocol::BlockNumber blockNumber);

}  // namespace bcos::rpc
