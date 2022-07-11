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
 * @brief interface for JsonRPC
 * @file JsonRpcInterface.h
 * @author: octopus
 * @date 2021-07-09
 */

#pragma once

#include <bcos-framework/multigroup/GroupInfo.h>
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-utilities/Error.h>
#include <json/json.h>
#include <util/tc_json.h>
#include <functional>

namespace bcos::rpc
{
using Sender = std::function<void(bcos::bytes)>;
using RespFunc = std::function<void(bcos::Error::Ptr, Json::Value&)>;

class JsonRpcInterface
{
public:
    using Ptr = std::shared_ptr<JsonRpcInterface>;
    JsonRpcInterface() { initMethod(); }
    JsonRpcInterface(const JsonRpcInterface&) = default;
    JsonRpcInterface(JsonRpcInterface&&) = default;
    JsonRpcInterface& operator=(const JsonRpcInterface&) = default;
    JsonRpcInterface& operator=(JsonRpcInterface&&) = default;
    virtual ~JsonRpcInterface() {}

public:
    virtual void call(std::string_view _groupID, std::string_view _nodeName, std::string_view _to,
        std::string_view _data, RespFunc _respFunc) = 0;

    virtual void sendTransaction(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _data, bool _requireProof, RespFunc _respFunc) = 0;

    virtual void getTransaction(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _txHash, bool _requireProof, RespFunc _respFunc) = 0;

    virtual void getTransactionReceipt(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _txHash, bool _requireProof, RespFunc _respFunc) = 0;

    virtual void getBlockByHash(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _blockHash, bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc) = 0;

    virtual void getBlockByNumber(std::string_view _groupID, std::string_view _nodeName,
        int64_t _blockNumber, bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc) = 0;

    virtual void getBlockHashByNumber(std::string_view _groupID, std::string_view _nodeName,
        int64_t _blockNumber, RespFunc _respFunc) = 0;

    virtual void getBlockNumber(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) = 0;

    virtual void getCode(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _contractAddress, RespFunc _respFunc) = 0;

    virtual void getABI(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _contractAddress, RespFunc _respFunc) = 0;

    virtual void getSealerList(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) = 0;

    virtual void getObserverList(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) = 0;

    virtual void getPbftView(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) = 0;

    virtual void getPendingTxSize(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) = 0;

    virtual void getSyncStatus(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) = 0;
    virtual void getConsensusStatus(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) = 0;

    virtual void getSystemConfigByKey(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _keyValue, RespFunc _respFunc) = 0;

    virtual void getTotalTransactionCount(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) = 0;

    virtual void getGroupPeers(std::string_view _groupID, RespFunc _respFunc) = 0;
    virtual void getPeers(RespFunc _respFunc) = 0;
    // get all the groupID list
    virtual void getGroupList(RespFunc _respFunc) = 0;
    // get the group information of the given group
    virtual void getGroupInfo(std::string_view _groupID, RespFunc _respFunc) = 0;
    // get all the group info list
    virtual void getGroupInfoList(RespFunc _respFunc) = 0;
    // get the information of a given node
    virtual void getGroupNodeInfo(
        std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc) = 0;

    virtual void getGroupBlockNumber(RespFunc _respFunc) = 0;

public:
    void onRPCRequest(std::string_view _requestBody, Sender _sender);

private:
    void initMethod();

    std::unordered_map<std::string, std::function<void(Json::Value, RespFunc)>> m_methodToFunc;

    static void parseRpcRequestJson(std::string_view _requestBody, JsonRequest& _jsonRequest);
    static bcos::bytes toStringResponse(JsonResponse _jsonResponse);
    static Json::Value toJsonResponse(JsonResponse _jsonResponse);

    std::string_view toView(const Json::Value& value)
    {
        const char* begin;
        const char* end;
        value.getString(&begin, &end);

        std::string_view view{begin, end};
        return view;
    }

    void callI(const Json::Value& req, RespFunc _respFunc)
    {
        call(toView(req[0u]), toView(req[1u]), toView(req[2u]), toView(req[3u]),
            std::move(_respFunc));
    }

    void sendTransactionI(const Json::Value& req, RespFunc _respFunc)
    {
        sendTransaction(toView(req[0u]), toView(req[1u]), toView(req[2u]), req[3u].asBool(),
            std::move(_respFunc));
    }

    void getTransactionI(const Json::Value& req, RespFunc _respFunc)
    {
        getTransaction(toView(req[0u]), toView(req[1u]), toView(req[2u]), req[3u].asBool(),
            std::move(_respFunc));
    }

    void getTransactionReceiptI(const Json::Value& req, RespFunc _respFunc)
    {
        getTransactionReceipt(toView(req[0u]), toView(req[1u]), toView(req[2u]), req[3u].asBool(),
            std::move(_respFunc));
    }

    void getBlockByHashI(const Json::Value& req, RespFunc _respFunc)
    {
        getBlockByHash(toView(req[0u]), toView(req[1u]), toView(req[2u]),
            (req.size() > 3 ? req[3u].asBool() : true), (req.size() > 4 ? req[4u].asBool() : true),
            std::move(_respFunc));
    }

    void getBlockByNumberI(const Json::Value& req, RespFunc _respFunc)
    {
        getBlockByNumber(toView(req[0u]), toView(req[1u]), req[2u].asInt64(),
            (req.size() > 3 ? req[3u].asBool() : true), (req.size() > 4 ? req[4u].asBool() : true),
            std::move(_respFunc));
    }

    void getBlockHashByNumberI(const Json::Value& req, RespFunc _respFunc)
    {
        getBlockHashByNumber(
            toView(req[0u]), toView(req[1u]), req[2u].asInt64(), std::move(_respFunc));
    }

    void getBlockNumberI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getBlockNumber(toView(req[0u]), toView(req[1u]), std::move(_respFunc));
    }

    void getCodeI(const Json::Value& req, RespFunc _respFunc)
    {
        getCode(toView(req[0u]), toView(req[1u]), toView(req[2u]), std::move(_respFunc));
    }

    void getABII(const Json::Value& req, RespFunc _respFunc)
    {
        getABI(toView(req[0u]), toView(req[1u]), toView(req[2u]), std::move(_respFunc));
    }

    void getSealerListI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getSealerList(toView(req[0u]), toView(req[1u]), std::move(_respFunc));
    }

    void getObserverListI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getObserverList(toView(req[0u]), toView(req[1u]), std::move(_respFunc));
    }

    void getPbftViewI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getPbftView(toView(req[0u]), toView(req[1u]), std::move(_respFunc));
    }

    void getPendingTxSizeI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getPendingTxSize(toView(req[0u]), toView(req[1u]), std::move(_respFunc));
    }

    void getSyncStatusI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getSyncStatus(toView(req[0u]), toView(req[1u]), std::move(_respFunc));
    }

    void getConsensusStatusI(const Json::Value& _req, RespFunc _respFunc)
    {
        getConsensusStatus(toView(_req[0u]), toView(_req[1u]), std::move(_respFunc));
    }

    void getSystemConfigByKeyI(const Json::Value& req, RespFunc _respFunc)
    {
        getSystemConfigByKey(
            toView(req[0u]), toView(req[1u]), toView(req[2u]), std::move(_respFunc));
    }

    void getTotalTransactionCountI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getTotalTransactionCount(toView(req[0u]), toView(req[1u]), std::move(_respFunc));
    }

    void getPeersI(const Json::Value& req, RespFunc _respFunc)
    {
        boost::ignore_unused(req);
        getPeers(std::move(_respFunc));
    }

    void getGroupPeersI(const Json::Value& req, RespFunc _respFunc)
    {
        getGroupPeers(toView(req[0u]), std::move(_respFunc));
    }

    // get all the groupID list
    void getGroupListI(const Json::Value& _req, RespFunc _respFunc)
    {
        (void)_req;
        getGroupList(std::move(_respFunc));
    }
    // get the group information of the given group
    void getGroupInfoI(const Json::Value& _req, RespFunc _respFunc)
    {
        (void)_req;
        getGroupInfo(toView(_req[0u]), std::move(_respFunc));
    }
    // get the group information of the given group
    void getGroupInfoListI(const Json::Value& _req, RespFunc _respFunc)
    {
        (void)_req;
        getGroupInfoList(std::move(_respFunc));
    }
    // get the information of a given node
    void getGroupNodeInfoI(const Json::Value& _req, RespFunc _respFunc)
    {
        getGroupNodeInfo(toView(_req[0u]), toView(_req[1u]), std::move(_respFunc));
    }
};

}  // namespace bcos::rpc
