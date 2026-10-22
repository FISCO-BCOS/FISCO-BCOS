/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file EthEndpoint.h
 * @author: kyonGuo
 * @date 2024/3/21
 */

#pragma once
#include <bcos-framework/ledger/Ledger.h>
#include <bcos-mempool/MemPoolImpl.h>
#include <bcos-rpc/groupmgr/GroupManager.h>
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>
#include <bcos-rpc/web3jsonrpc/Web3FilterSystem.h>
#include <json/json.h>

namespace bcos::rpc
{

/**
 * eth entry point to match 'eth_' methods
 */
class EthEndpoint
{
public:
    EthEndpoint(NodeService::Ptr nodeService, FilterSystem::Ptr filterSystem, bool syncTransaction);
    virtual ~EthEndpoint() = default;

    task::Task<void> protocolVersion(const Json::Value&, Json::Value&);
    task::Task<void> syncing(const Json::Value&, Json::Value&);
    task::Task<void> coinbase(const Json::Value&, Json::Value&);
    task::Task<void> chainId(const Json::Value&, Json::Value&);
    task::Task<void> mining(const Json::Value&, Json::Value&);
    task::Task<void> hashrate(const Json::Value&, Json::Value&);
    task::Task<void> gasPrice(const Json::Value&, Json::Value&);
    task::Task<void> accounts(const Json::Value&, Json::Value&);
    task::Task<void> blockNumber(const Json::Value&, Json::Value&);
    task::Task<void> getBalance(const Json::Value&, Json::Value&);
    task::Task<void> getStorageAt(const Json::Value&, Json::Value&);
    task::Task<void> getTransactionCount(const Json::Value&, Json::Value&);
    task::Task<void> getBlockTxCountByHash(const Json::Value&, Json::Value&);
    task::Task<void> getBlockTxCountByNumber(const Json::Value&, Json::Value&);
    task::Task<void> getUncleCountByBlockHash(const Json::Value&, Json::Value&);
    task::Task<void> getUncleCountByBlockNumber(const Json::Value&, Json::Value&);
    task::Task<void> getCode(const Json::Value&, Json::Value&);
    task::Task<void> sign(const Json::Value&, Json::Value&);
    task::Task<void> signTransaction(const Json::Value&, Json::Value&);
    task::Task<void> sendTransaction(const Json::Value&, Json::Value&);
    task::Task<void> sendRawTransaction(const Json::Value&, Json::Value&);
    task::Task<void> call(const Json::Value&, Json::Value&);
    task::Task<void> estimateGas(const Json::Value&, Json::Value&);
    task::Task<void> getBlockByHash(const Json::Value&, Json::Value&);
    task::Task<void> getBlockByNumber(const Json::Value&, Json::Value&);
    task::Task<void> getTransactionByHash(const Json::Value&, Json::Value&);
    task::Task<void> getTransactionByBlockHashAndIndex(const Json::Value&, Json::Value&);
    task::Task<void> getTransactionByBlockNumberAndIndex(const Json::Value&, Json::Value&);
    task::Task<void> getTransactionReceipt(const Json::Value&, Json::Value&);
    task::Task<void> getUncleByBlockHashAndIndex(const Json::Value&, Json::Value&);
    task::Task<void> getUncleByBlockNumberAndIndex(const Json::Value&, Json::Value&);
    task::Task<void> newFilter(const Json::Value&, Json::Value&);
    task::Task<void> newBlockFilter(const Json::Value&, Json::Value&);
    task::Task<void> newPendingTransactionFilter(const Json::Value&, Json::Value&);
    task::Task<void> uninstallFilter(const Json::Value&, Json::Value&);
    task::Task<void> getFilterChanges(const Json::Value&, Json::Value&);
    task::Task<void> getFilterLogs(const Json::Value&, Json::Value&);
    task::Task<void> getLogs(const Json::Value&, Json::Value&);
    task::Task<std::tuple<protocol::BlockNumber, bool>> getBlockNumberByTag(
        std::string_view blockTag);
    task::Task<std::tuple<protocol::BlockNumber, protocol::BlockNumber>> getBlockNumberAndHeadByTag(
        std::string_view blockTag);
    task::Task<void> maxPriorityFeePerGas(const Json::Value&, Json::Value&);
    // OP lane only (requireOpStackLane): the Web3 surface op-batcher / op-proposer call.
    task::Task<void> feeHistory(const Json::Value&, Json::Value&);
    task::Task<void> getBlockReceipts(const Json::Value&, Json::Value&);
    task::Task<void> txpoolStatus(const Json::Value&, Json::Value&);
    task::Task<void> txpoolContent(const Json::Value&, Json::Value&);
    task::Task<void> getProof(const Json::Value&, Json::Value&);

private:
    NodeService::Ptr m_nodeService;
    FilterSystem::Ptr m_filterSystem;
    bool m_syncTransaction;

    /// eth_getTransactionReceipt's result for @p hash; throws when the tx or receipt is absent.
    task::Task<Json::Value> receiptJson(crypto::HashType const& hash);
    /// The OP mempool split by sealability (empty when this node has no mempool).
    task::Task<std::vector<txpool::PooledTransaction>> pooledTransactions();

    /// Everything one block-tag resolution needs, gathered once so eth_getBlockByNumber,
    /// eth_getLogs / eth_newFilter and the state endpoints cannot diverge: `latest`, the
    /// static [web3_rpc] depths, the engine lane's forkchoice safe/finalized and whether an
    /// unset forkchoice value fails closed. On the OP lane (NodeService::opCanonicalReader,
    /// D1 §10.2) latest is the tracker head, safe/finalized are the tracker's with the
    /// finalized (backend) tip as their fallback and the depths are ignored; on the Eth
    /// engine lane an unset value fails closed (-32000 header not found); off the engine
    /// lanes the static depths apply.
    struct TagContext
    {
        protocol::BlockNumber latest = 0;
        protocol::BlockNumber safeDepth = 0;
        protocol::BlockNumber finalizedDepth = 0;
        std::optional<protocol::BlockNumber> safe;
        std::optional<protocol::BlockNumber> finalized;
        bool failClosedOnMissingForkchoice = false;
    };
    task::Task<TagContext> tagContext();

    /// A transaction with its receipt and block hash: the ledger first (finalized blocks, as
    /// before), then the OP head chain's unfinalized part. nullopt when neither carries it —
    /// including a transaction that exists only on a side branch, which is null like geth's
    /// answer for a non-canonical inclusion.
    task::Task<std::optional<bcos::engine::OpCanonicalReader::ChainTransaction>> lookupTransaction(
        crypto::HashType const& hash);

    /// The (header block, MPT node reader) pair a state read at @p blockNumber resolves its
    /// root and trie from: the ledger row and the committed-plane reader at or below the
    /// finalized tip (or off the OP lane), the head chain's window block and a reader over
    /// its chain view above it. @p chainHash names a specific chain (eth_getProof by block
    /// hash, which may be a replaced sibling) instead of the head chain.
    struct StateReadContext
    {
        protocol::Block::Ptr block;
        std::shared_ptr<NodeService::MPTNodeReader> mptReader;
    };
    task::Task<StateReadContext> stateReadContext(protocol::BlockNumber blockNumber,
        std::optional<crypto::HashType> chainHash = std::nullopt);

    /// The flat state plane behind `latest`: the OP head chain's view, else the node's
    /// committed-plane provider (NodeService::stateStorageProvider); null when neither is
    /// wired (tars-built NodeService).
    task::Task<std::shared_ptr<NodeService::StateStorage>> latestStateStorage();

    /// True when @p blockNumber is above the OP facade's finalized tip (an unfinalized height
    /// served from the window); false off the OP lane.
    task::Task<bool> isUnfinalizedHeight(protocol::BlockNumber blockNumber);

    /// An OP window block by hash (any branch) / the head chain's block at an unfinalized
    /// height, with @p blockFlag rows; null off the OP lane, for a finalized block or a height
    /// the head chain does not reach — the caller then takes its ledger path unchanged.
    task::Task<protocol::Block::Ptr> unfinalizedBlockByHash(
        crypto::HashType const& blockHash, int32_t blockFlag);
    task::Task<protocol::Block::Ptr> unfinalizedBlockByNumber(
        protocol::BlockNumber blockNumber, int32_t blockFlag);
    /// eth_getTransactionByBlock*AndIndex's result from a block's own transaction and
    /// receipt rows; null for an index out of range.
    static Json::Value transactionAtIndex(
        protocol::Block& block, uint64_t transactionIndex, crypto::HashType const& blockHash);

    task::Task<void> call(const Json::Value&, Json::Value&, u256* gasUsed, bool isEstimate);
};

}  // namespace bcos::rpc
