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
#include <bcos-framework/engine/DACaps.h>
#include <bcos-framework/ledger/Ledger.h>
#include <bcos-rpc/groupmgr/GroupManager.h>
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>
#include <bcos-rpc/web3jsonrpc/Web3FilterSystem.h>
#include <json/json.h>

#include <atomic>

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
    task::Task<void> feeHistory(const Json::Value&, Json::Value&);
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
    task::Task<void> maxPriorityFeePerGas(const Json::Value&, Json::Value&);
    task::Task<void> getProof(const Json::Value&, Json::Value&);
    task::Task<void> setMaxDASize(const Json::Value&, Json::Value&);

private:
    NodeService::Ptr m_nodeService;
    FilterSystem::Ptr m_filterSystem;
    bool m_syncTransaction;

    /// Shared DA throttling caps with the engine's OP build path (see NodeService::
    /// setDACaps). miner_setMaxDASize writes here; the engine's buildOpPayload filters
    /// oversized sealed txs (maxTxSize) and truncates block assembly (maxBlockSize).
    /// When NodeService carries no shared instance (tars-built nodes, unit fixtures) a
    /// detached local instance is created — recorded and logged, never consumed.
    std::shared_ptr<bcos::engine::DACaps> m_daCaps;
    bcos::engine::DACaps& daCaps();

    task::Task<void> call(const Json::Value&, Json::Value&, u256* gasUsed, bool isEstimate);

    // Runs an eth_call with the gas field pinned to @p limit; true when the
    // simulation succeeds (estimateGas' minimum-viable-limit search).
    task::Task<bool> simulateAtGasLimit(const Json::Value& request, u256 limit);
};

}  // namespace bcos::rpc
