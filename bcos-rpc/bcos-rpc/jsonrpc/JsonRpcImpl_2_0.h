/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief: implement for the RPC
 * @file: JsonRpcImpl_2_0.h
 * @author: octopus
 * @date: 2021-07-09
 */

#pragma once
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-rpc/groupmgr/GroupManager.h"
#include "bcos-rpc/validator/CallValidator.h"
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-rpc/filter/FilterSystem.h>
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>
#include <json/json.h>
#include <tbb/concurrent_hash_map.h>
#include <boost/core/ignore_unused.hpp>
#include <unordered_map>

namespace bcos::rpc
{
class JsonRpcImpl_2_0 : public JsonRpcInterface,
                        public std::enable_shared_from_this<JsonRpcImpl_2_0>
{
public:
    using Ptr = std::shared_ptr<JsonRpcImpl_2_0>;
    JsonRpcImpl_2_0(GroupManager::Ptr _groupManager,
        bcos::gateway::GatewayInterface::Ptr _gatewayInterface,
        std::shared_ptr<boostssl::ws::WsService> _wsService, FilterSystem::Ptr filterSystem);
    ~JsonRpcImpl_2_0() override = default;

    void setClientID(std::string_view _clientID) { m_clientID = _clientID; }

    void call(std::string_view _groupID, std::string_view _nodeName, std::string_view _to,
        std::string_view _data, RespFunc _respFunc) override;

    void call(std::string_view _groupID, std::string_view _nodeName, std::string_view _to,
        std::string_view _data, std::string_view sign, RespFunc _respFunc) override;

    void sendTransaction(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _data, bool _requireProof, RespFunc _respFunc) override;

    void getTransaction(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _txHash, bool _requireProof, RespFunc _respFunc) override;

    void getTransactionReceipt(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _txHash, bool _requireProof, RespFunc _respFunc) override;

    void getBlockByHash(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _blockHash, bool _onlyHeader, bool _onlyTxHash,
        RespFunc _respFunc) override;

    void getBlockByNumber(std::string_view _groupID, std::string_view _nodeName,
        int64_t _blockNumber, bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc) override;

    void getBlockHashByNumber(std::string_view _groupID, std::string_view _nodeName,
        int64_t _blockNumber, RespFunc _respFunc) override;

    void getBlockNumber(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getCode(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _contractAddress, RespFunc _respFunc) override;

    void getABI(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _contractAddress, RespFunc _respFunc) override;

    void getSealerList(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getObserverList(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getNodeListByType(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _nodeType, RespFunc _respFunc) override;

    void getPbftView(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getPendingTxSize(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getSyncStatus(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getConsensusStatus(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getSystemConfigByKey(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _keyValue, RespFunc _respFunc) override;

    void getTotalTransactionCount(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    void getPeers(RespFunc _respFunc) override;

    // get all the groupID list
    void getGroupList(RespFunc _respFunc) override;
    // get the group information of the given group
    void getGroupInfo(std::string_view _groupID, RespFunc _respFunc) override;
    // get all the group info list
    void getGroupInfoList(RespFunc _respFunc) override;
    // get the information of a given node
    void getGroupNodeInfo(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) override;

    // filter interface
    void newBlockFilter(std::string_view _groupID, RespFunc _respFunc) override;
    void newPendingTransactionFilter(std::string_view _groupID, RespFunc _respFunc) override;
    void newFilter(
        std::string_view _groupID, const Json::Value& params, RespFunc _respFunc) override;
    void uninstallFilter(
        std::string_view _groupID, std::string_view filterID, RespFunc _respFunc) override;
    void getFilterChanges(
        std::string_view _groupID, std::string_view filterID, RespFunc _respFunc) override;
    void getFilterLogs(
        std::string_view _groupID, std::string_view filterID, RespFunc _respFunc) override;
    void getLogs(std::string_view _groupID, const Json::Value& params, RespFunc _respFunc) override;

    void getGroupBlockNumber(RespFunc _respFunc) override;

    void setNodeInfo(const NodeInfo& _nodeInfo) { m_nodeInfo = _nodeInfo; }
    NodeInfo nodeInfo() const { return m_nodeInfo; }
    GroupManager::Ptr groupManager() { return m_groupManager; }

    int sendTxTimeout() const { return m_sendTxTimeout; }
    void setSendTxTimeout(int _sendTxTimeout) { m_sendTxTimeout = _sendTxTimeout; }

protected:
    static bcos::bytes decodeData(std::string_view _data);

    static void parseRpcResponseJson(std::string_view _responseBody, JsonResponse& _jsonResponse);

    static void addProofToResponse(
        Json::Value& jResp, const std::string& _key, ledger::MerkleProofPtr _merkleProofPtr);

    virtual void handleRpcRequest(std::shared_ptr<boostssl::MessageFace> _msg,
        std::shared_ptr<boostssl::ws::WsSession> _session);

    // TODO: check perf influence
    NodeService::Ptr getNodeService(
        std::string_view _groupID, std::string_view _nodeName, std::string_view _command);

    template <typename T>
    void checkService(T _service, std::string_view _serviceName)
    {
        if (!_service)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(
                JsonRpcError::ServiceNotInitCompleted, "The service " + std::string(_serviceName) +
                                                           " has not been initted completed yet!"));
        }
    }

    FilterSystem& filterSystem() { return *m_filterSystem; }

protected:
    void gatewayInfoToJson(Json::Value& _response, bcos::gateway::GatewayInfo::Ptr _gatewayInfo);
    void gatewayInfoToJson(Json::Value& _response, bcos::gateway::GatewayInfo::Ptr _localP2pInfo,
        bcos::gateway::GatewayInfosPtr _peersInfo);
    void getGroupPeers(Json::Value& _response, std::string_view _groupID,
        bcos::gateway::GatewayInfo::Ptr _localP2pInfo, bcos::gateway::GatewayInfosPtr _peersInfo);
    void getGroupPeers(std::string_view _groupID, RespFunc _respFunc) override;

    static void execCall(NodeService::Ptr nodeService, protocol::Transaction::Ptr _tx,
        bcos::rpc::RespFunc _respFunc);

    // ms
    int m_sendTxTimeout = -1;

    GroupManager::Ptr m_groupManager;
    bcos::gateway::GatewayInterface::Ptr m_gatewayInterface;
    std::shared_ptr<boostssl::ws::WsService> m_wsService;
    FilterSystem::Ptr m_filterSystem;

    NodeInfo m_nodeInfo;
    // Note: here clientID must non-empty for the rpc will set clientID as source for the tx for
    // tx-notify and the scheduler will not notify the tx-result if the tx source is empty
    std::string m_clientID = "localRpc";

    struct TxHasher
    {
        size_t hash(const bcos::crypto::HashType& hash) const { return hasher(hash); }

        bool equal(const bcos::crypto::HashType& lhs, const bcos::crypto::HashType& rhs) const
        {
            return lhs == rhs;
        }

        std::hash<bcos::crypto::HashType> hasher;
    };
};

void toJsonResp(Json::Value& jResp, bcos::protocol::Transaction const& transactionPtr);
void toJsonResp(Json::Value& jResp, bcos::protocol::BlockHeader::Ptr _blockHeaderPtr);
void toJsonResp(Json::Value& jResp, bcos::protocol::Block& block, bool _onlyTxHash);
void toJsonResp(Json::Value& jResp, std::string_view _txHash, protocol::TransactionStatus status,
    bcos::protocol::TransactionReceipt const& transactionReceiptPtr, bool _isWasm,
    crypto::Hash& hashImpl);

}  // namespace bcos::rpc