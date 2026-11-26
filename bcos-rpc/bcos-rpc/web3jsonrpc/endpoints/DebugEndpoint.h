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
 * @file DebugEndpoint.h
 * @brief Geth debug namespace consumed by the fault-proof preimage server (kona-host):
 *        debug_dbGet answers a raw preimage (L2 state node RLP or contract code) for a
 *        content-address key, and debug_executePayload is the high-level execution-witness
 *        hint the host probes before falling back to the fine-grained dbGet path.
 */

#pragma once

#include <bcos-rpc/groupmgr/GroupManager.h>
#include <bcos-task/Task.h>
#include <json/json.h>

namespace bcos::rpc
{

/// Geth debug namespace consumed by the fault-proof preimage server (kona-host).
class DebugEndpoint
{
public:
    explicit DebugEndpoint(NodeService::Ptr nodeService) : m_nodeService(std::move(nodeService)) {}
    virtual ~DebugEndpoint() = default;

    /// debug_dbGet: return the raw preimage stored under a content-address key. The key is
    /// either the 33-byte "c" + codeHash form of the geth hashdb scheme, or a bare 32-byte
    /// keccak hash; the shape only picks which keccak-addressed store to try first (code store
    /// vs MPT node rows). A miss answers -32000 "not found", geth's server-error code.
    task::Task<void> dbGet(const Json::Value&, Json::Value&);

    /// debug_getRawHeader: return the RLP encoding of a block header, resolved from a block
    /// number, tag, or 32-byte block hash. kona-host expects the exact bytes whose keccak256
    /// equals the published block hash (canonicalBlockHash); the bytes come from
    /// EthBlockHeader::encodeHeader, the same encoding computeHash hashes. Mirrors geth
    /// DebugAPI.GetRawHeader.
    task::Task<void> getRawHeader(const Json::Value&, Json::Value&);

    /// debug_getRawReceipts: the RLP encoding of every receipt in a block, in block order, as
    /// kona-host's L1 data plane reads them (geth's debug_getRawReceipts). Each element is the
    /// same byte string the block's receipts trie leaf holds, i.e. type-prefix || rlp([status,
    /// cumulativeGasUsed, logsBloom, logs]).
    task::Task<void> getRawReceipts(const Json::Value&, Json::Value&);

    /// debug_getRawBlock: the RLP encoding of a whole block, i.e. geth's `types.Block`
    /// rlp([header, transactions, ommers, withdrawals?]). op-succinct's witness generator reads
    /// L2 blocks through this method (`get_l2_block_by_number` in utils/host/src/fetcher.rs) and
    /// has no fallback, so without it proof generation cannot start at all. The optional fourth
    /// element is fork-significant (EIP-4895): present iff the header carries a withdrawalsRoot,
    /// which is what tells the decoder the block is Shanghai+.
    task::Task<void> getRawBlock(const Json::Value&, Json::Value&);

    /// debug_getRawTransaction: the raw EIP-2718 envelope of one transaction by hash — the same
    /// bytes its transactions-trie leaf holds, so a client can rebuild the trie and get the
    /// header's txsRoot back. Mirrors geth's DebugAPI.GetRawTransaction.
    task::Task<void> getRawTransaction(const Json::Value&, Json::Value&);

    /// debug_executePayload: kona-host's high-level "execute a payload and return the whole
    /// stateless execution witness" hint. See the .cpp for why it answers MethodNotFound.
    task::Task<void> executePayload(const Json::Value&, Json::Value&);

    /// debug_traceTransaction: read-only L1 replay from the retained parent state.
    task::Task<void> traceTransaction(const Json::Value&, Json::Value&);

private:
    /// Read the single block parameter the raw-data endpoints take (a block number, a tag other
    /// than safe/finalized/pending, or a 32-byte hash) on the CANONICAL plane and return the
    /// block with @p blockFlag rows. Shared so the header, receipts and block paths cannot
    /// disagree about which block a request names -- on the fault-proof data plane two answers
    /// for one input is a divergence, not a cosmetic difference.
    ///
    /// Canonical, not raw-ledger, is the point: on the OP lane the backend holds the finalized
    /// chain only and the blocks op-node is currently serving live in the OpScheduler window, so
    /// a direct ledger read would answer "not found" for every unfinalized block -- including
    /// "latest" -- and disagree with eth_getBlockByNumber on the same node.
    /// When the parameter is a hash and @p requestedHash is non-null, the requested hash is
    /// returned through it so the caller can bind the answer to it (a window entry read by hash
    /// is unambiguous, a hash resolved to a height may be replaced by a reorg underneath).
    task::Task<protocol::Block::Ptr> rawBlockByParameter(const Json::Value& request,
        std::string_view method, int32_t blockFlag,
        std::optional<crypto::HashType>* requestedHash = nullptr);

    NodeService::Ptr m_nodeService;
};

}  // namespace bcos::rpc
