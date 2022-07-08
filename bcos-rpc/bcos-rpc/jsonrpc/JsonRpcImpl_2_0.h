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
#include "bcos-rpc/groupmgr/GroupManager.h"
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-framework/gateway/GatewayInterface.h>
#include <bcos-rpc/jsonrpc/JsonRpcInterface.h>
#include <json/json.h>
#include <tbb/concurrent_hash_map.h>
#include <boost/core/ignore_unused.hpp>
#include <unordered_map>

namespace bcos
{
namespace rpc
{
class JsonRpcImpl_2_0 : public JsonRpcInterface,
                        public std::enable_shared_from_this<JsonRpcImpl_2_0>
{
public:
    using Ptr = std::shared_ptr<JsonRpcImpl_2_0>;
    JsonRpcImpl_2_0(GroupManager::Ptr _groupManager,
        bcos::gateway::GatewayInterface::Ptr _gatewayInterface,
        std::shared_ptr<boostssl::ws::WsService> _wsService);
    ~JsonRpcImpl_2_0() {}


    void setClientID(std::string const& _clientID) { m_clientID = _clientID; }

public:
    void call(std::string const& _groupID, std::string const& _nodeName, const std::string& _to,
        const std::string& _data, RespFunc _respFunc) override;

    void sendTransaction(std::string const& _groupID, std::string const& _nodeName,
        const std::string& _data, bool _requireProof, RespFunc _respFunc) override;

    void getTransaction(std::string const& _groupID, std::string const& _nodeName,
        const std::string& _txHash, bool _requireProof, RespFunc _respFunc) override;

    void getTransactionReceipt(std::string const& _groupID, std::string const& _nodeName,
        const std::string& _txHash, bool _requireProof, RespFunc _respFunc) override;

    void getBlockByHash(std::string const& _groupID, std::string const& _nodeName,
        const std::string& _blockHash, bool _onlyHeader, bool _onlyTxHash,
        RespFunc _respFunc) override;

    void getBlockByNumber(std::string const& _groupID, std::string const& _nodeName,
        int64_t _blockNumber, bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc) override;

    void getBlockHashByNumber(std::string const& _groupID, std::string const& _nodeName,
        int64_t _blockNumber, RespFunc _respFunc) override;

    void getBlockNumber(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) override;

    void getCode(std::string const& _groupID, std::string const& _nodeName,
        const std::string _contractAddress, RespFunc _respFunc) override;

    void getABI(std::string const& _groupID, std::string const& _nodeName,
        const std::string _contractAddress, RespFunc _respFunc) override;

    void getSealerList(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) override;

    void getObserverList(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) override;

    void getPbftView(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) override;

    void getPendingTxSize(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) override;

    void getSyncStatus(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) override;

    void getConsensusStatus(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) override;

    void getSystemConfigByKey(std::string const& _groupID, std::string const& _nodeName,
        const std::string& _keyValue, RespFunc _respFunc) override;

    void getTotalTransactionCount(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) override;

    void getPeers(RespFunc _respFunc) override;

    // get all the groupID list
    void getGroupList(RespFunc _respFunc) override;
    // get the group information of the given group
    void getGroupInfo(std::string const& _groupID, RespFunc _respFunc) override;
    // get all the group info list
    void getGroupInfoList(RespFunc _respFunc) override;
    // get the information of a given node
    void getGroupNodeInfo(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) override;

    void getGroupBlockNumber(RespFunc _respFunc) override;

public:
    void setNodeInfo(const NodeInfo& _nodeInfo) { m_nodeInfo = _nodeInfo; }
    NodeInfo nodeInfo() const { return m_nodeInfo; }
    GroupManager::Ptr groupManager() { return m_groupManager; }

protected:
    static std::shared_ptr<bcos::bytes> decodeData(const std::string& _data);
    
    static void parseRpcResponseJson(const std::string& _responseBody, JsonResponse& _jsonResponse);
    
    static void toJsonResp(
        Json::Value& jResp, bcos::protocol::Transaction::ConstPtr _transactionPtr);

    static void toJsonResp(Json::Value& jResp, bcos::protocol::BlockHeader::Ptr _blockHeaderPtr);
    static void toJsonResp(
        Json::Value& jResp, bcos::protocol::Block::Ptr _blockPtr, bool _onlyTxHash);
    static void toJsonResp(Json::Value& jResp, const std::string& _txHash,
        bcos::protocol::TransactionReceipt::ConstPtr _transactionReceiptPtr);
    static void addProofToResponse(
        Json::Value& jResp, std::string const& _key, ledger::MerkleProofPtr _merkleProofPtr);

    virtual void handleRpcRequest(std::shared_ptr<boostssl::MessageFace> _msg,
        std::shared_ptr<boostssl::ws::WsSession> _session);

    // TODO: check perf influence
    NodeService::Ptr getNodeService(
        std::string const& _groupID, std::string const& _nodeName, std::string const& _command);

    template <typename T>
    void checkService(T _service, std::string _serviceName)
    {
        if (!_service)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(JsonRpcError::ServiceNotInitCompleted,
                "The service " + _serviceName + " has not been inited completed yet!"));
        }
    }

private:
    void gatewayInfoToJson(Json::Value& _response, bcos::gateway::GatewayInfo::Ptr _gatewayInfo);
    void gatewayInfoToJson(Json::Value& _response, bcos::gateway::GatewayInfo::Ptr _localP2pInfo,
        bcos::gateway::GatewayInfosPtr _peersInfo);
    void getGroupPeers(Json::Value& _response, std::string const& _groupID,
        bcos::gateway::GatewayInfo::Ptr _localP2pInfo, bcos::gateway::GatewayInfosPtr _peersInfo);
    void getGroupPeers(std::string const& _groupID, RespFunc _respFunc) override;

private:
    GroupManager::Ptr m_groupManager;
    bcos::gateway::GatewayInterface::Ptr m_gatewayInterface;
    std::shared_ptr<boostssl::ws::WsService> m_wsService;

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

}  // namespace rpc
}  // namespace bcos