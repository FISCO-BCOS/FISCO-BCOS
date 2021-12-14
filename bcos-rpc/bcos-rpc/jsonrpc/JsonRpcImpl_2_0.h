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
#include "groupmgr/GroupManager.h"
#include <bcos-framework/interfaces/gateway/GatewayInterface.h>
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
    JsonRpcImpl_2_0(
        GroupManager::Ptr _groupManager, bcos::gateway::GatewayInterface::Ptr _gatewayInterface)
      : m_groupManager(_groupManager), m_gatewayInterface(_gatewayInterface)
    {
        initMethod();
    }
    ~JsonRpcImpl_2_0() {}

    void initMethod();
    void setClientID(std::string const& _clientID) { m_clientID = _clientID; }

public:
    static std::string encodeData(bcos::bytesConstRef _data);
    static std::shared_ptr<bcos::bytes> decodeData(const std::string& _data);
    static void parseRpcRequestJson(const std::string& _requestBody, JsonRequest& _jsonRequest);
    static void parseRpcResponseJson(const std::string& _responseBody, JsonResponse& _jsonResponse);
    static Json::Value toJsonResponse(const JsonResponse& _jsonResponse);
    static std::string toStringResponse(const JsonResponse& _jsonResponse);
    static void toJsonResp(
        Json::Value& jResp, bcos::protocol::Transaction::ConstPtr _transactionPtr);

    static void toJsonResp(Json::Value& jResp, bcos::protocol::BlockHeader::Ptr _blockHeaderPtr);
    static void toJsonResp(
        Json::Value& jResp, bcos::protocol::Block::Ptr _blockPtr, bool _onlyTxHash);
    static void toJsonResp(Json::Value& jResp, const std::string& _txHash,
        bcos::protocol::TransactionReceipt::ConstPtr _transactionReceiptPtr);
    static void addProofToResponse(
        Json::Value& jResp, std::string const& _key, ledger::MerkleProofPtr _merkleProofPtr);

    void onRPCRequest(const std::string& _requestBody, Sender _sender) override;

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

public:
    void callI(const Json::Value& req, RespFunc _respFunc)
    {
        call(req[0u].asString(), req[1u].asString(), req[2u].asString(), req[3u].asString(),
            _respFunc);
    }

    void sendTransactionI(const Json::Value& req, RespFunc _respFunc)
    {
        sendTransaction(req[0u].asString(), req[1u].asString(), req[2u].asString(),
            req[3u].asBool(), _respFunc);
    }

    void getTransactionI(const Json::Value& req, RespFunc _respFunc)
    {
        getTransaction(req[0u].asString(), req[1u].asString(), req[2u].asString(), req[3u].asBool(),
            _respFunc);
    }

    void getTransactionReceiptI(const Json::Value& req, RespFunc _respFunc)
    {
        getTransactionReceipt(req[0u].asString(), req[1u].asString(), req[2u].asString(),
            req[3u].asBool(), _respFunc);
    }

    void getBlockByHashI(const Json::Value& req, RespFunc _respFunc)
    {
        getBlockByHash(req[0u].asString(), req[1u].asString(), req[2u].asString(),
            (req.size() > 3 ? req[3u].asBool() : true), (req.size() > 4 ? req[4u].asBool() : true),
            _respFunc);
    }

    void getBlockByNumberI(const Json::Value& req, RespFunc _respFunc)
    {
        getBlockByNumber(req[0u].asString(), req[1u].asString(), req[2u].asInt64(),
            (req.size() > 3 ? req[3u].asBool() : true), (req.size() > 4 ? req[4u].asBool() : true),
            _respFunc);
    }

    void getBlockHashByNumberI(const Json::Value& req, RespFunc _respFunc)
    {
        getBlockHashByNumber(req[0u].asString(), req[1u].asString(), req[2u].asInt64(), _respFunc);
    }

    void getBlockNumberI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getBlockNumber(req[0u].asString(), req[1u].asString(), _respFunc);
    }

    void getCodeI(const Json::Value& req, RespFunc _respFunc)
    {
        getCode(req[0u].asString(), req[1u].asString(), req[2u].asString(), _respFunc);
    }

    void getSealerListI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getSealerList(req[0u].asString(), req[1u].asString(), _respFunc);
    }

    void getObserverListI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getObserverList(req[0u].asString(), req[1u].asString(), _respFunc);
    }

    void getPbftViewI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getPbftView(req[0u].asString(), req[1u].asString(), _respFunc);
    }

    void getPendingTxSizeI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getPendingTxSize(req[0u].asString(), req[1u].asString(), _respFunc);
    }

    void getSyncStatusI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getSyncStatus(req[0u].asString(), req[1u].asString(), _respFunc);
    }

    void getConsensusStatusI(const Json::Value& _req, RespFunc _respFunc)
    {
        getConsensusStatus(_req[0u].asString(), _req[1u].asString(), _respFunc);
    }

    void getSystemConfigByKeyI(const Json::Value& req, RespFunc _respFunc)
    {
        getSystemConfigByKey(req[0u].asString(), req[1u].asString(), req[2u].asString(), _respFunc);
    }

    void getTotalTransactionCountI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getTotalTransactionCount(req[0u].asString(), req[1u].asString(), _respFunc);
    }

    void getPeersI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getPeers(_respFunc);
    }

    void getGroupPeersI(const Json::Value& req, RespFunc _respFunc)
    {
        getGroupPeers(req[0u].asString(), _respFunc);
    }

    // get all the groupID list
    void getGroupListI(const Json::Value& _req, RespFunc _respFunc)
    {
        (void)_req;
        getGroupList(_respFunc);
    }
    // get the group information of the given group
    void getGroupInfoI(const Json::Value& _req, RespFunc _respFunc)
    {
        (void)_req;
        getGroupInfo(_req[0u].asString(), _respFunc);
    }
    // get the group information of the given group
    void getGroupInfoListI(const Json::Value& _req, RespFunc _respFunc)
    {
        (void)_req;
        getGroupInfoList(_respFunc);
    }
    // get the information of a given node
    void getGroupNodeInfoI(const Json::Value& _req, RespFunc _respFunc)
    {
        getGroupNodeInfo(_req[0u].asString(), _req[1u].asString(), _respFunc);
    }

public:
    const std::unordered_map<std::string, std::function<void(Json::Value, RespFunc _respFunc)>>&
    methodToFunc() const
    {
        return m_methodToFunc;
    }

    void registerMethod(
        const std::string& _method, std::function<void(Json::Value, RespFunc _respFunc)> _callback)
    {
        m_methodToFunc[_method] = _callback;
    }
    void setNodeInfo(const NodeInfo& _nodeInfo) { m_nodeInfo = _nodeInfo; }
    NodeInfo nodeInfo() const { return m_nodeInfo; }
    GroupManager::Ptr groupManager() { return m_groupManager; }

private:
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

    void gatewayInfoToJson(Json::Value& _response, bcos::gateway::GatewayInfo::Ptr _gatewayInfo);
    void gatewayInfoToJson(Json::Value& _response, bcos::gateway::GatewayInfo::Ptr _localP2pInfo,
        bcos::gateway::GatewayInfosPtr _peersInfo);
    void getGroupPeers(Json::Value& _response, std::string const& _groupID,
        bcos::gateway::GatewayInfo::Ptr _localP2pInfo, bcos::gateway::GatewayInfosPtr _peersInfo);
    void getGroupPeers(std::string const& _groupID, RespFunc _respFunc) override;

private:
    std::unordered_map<std::string, std::function<void(Json::Value, RespFunc _respFunc)>>
        m_methodToFunc;

    GroupManager::Ptr m_groupManager;
    bcos::gateway::GatewayInterface::Ptr m_gatewayInterface;
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