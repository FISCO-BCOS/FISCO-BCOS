/*
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
 * @file JsonRpcImpl.cpp
 * @author: octopus
 * @date 2021-08-10
 */

#include <bcos-boostssl/websocket/WsError.h>
#include <bcos-cpp-sdk/rpc/Common.h>
#include <bcos-cpp-sdk/rpc/JsonRpcImpl.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/value.h>
#include <fstream>

using namespace bcos;
using namespace cppsdk;
using namespace jsonrpc;
using namespace bcos;

void JsonRpcImpl::start()
{
    if (m_service)
    {  // start websocket service
        m_service->start();
    }
    else
    {
        RPCIMPL_LOG(WARNING) << LOG_BADGE("start")
                             << LOG_DESC("websocket service is not uninitialized");
    }
    RPCIMPL_LOG(INFO) << LOG_BADGE("start") << LOG_DESC("start rpc");
}

void JsonRpcImpl::stop()
{
    if (m_service)
    {
        // RPCIMPL_LOG(INFO) << LOG_BADGE("stop") << LOG_DESC("stop rpc");
        m_service->stop();
    }
}

void JsonRpcImpl::genericMethod(const std::string& _data, RespFunc _respFunc)
{
    m_sender("", "", _data, _respFunc);
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        RPCIMPL_LOG(TRACE) << LOG_BADGE("genericMethod") << LOG_KV("request", _data);
    }
}

void JsonRpcImpl::genericMethod(
    const std::string& _groupID, const std::string& _data, RespFunc _respFunc)
{
    std::string name;
    if (m_sendRequestToHighestBlockNode)
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }
    m_sender(_groupID, name, _data, _respFunc);
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        RPCIMPL_LOG(TRACE) << LOG_BADGE("genericMethod") << LOG_KV("group", _groupID)
                           << LOG_KV("request", _data);
    }
}

void JsonRpcImpl::genericMethod(const std::string& _groupID, const std::string& _nodeName,
    const std::string& _data, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    m_sender(_groupID, name, _data, _respFunc);
    if (c_fileLogLevel == TRACE) [[unlikely]]
    {
        RPCIMPL_LOG(TRACE) << LOG_BADGE("genericMethod") << LOG_KV("group", _groupID)
                           << LOG_KV("nodeName", name) << LOG_KV("request", _data);
    }
}

void JsonRpcImpl::call(const std::string& _groupID, const std::string& _nodeName,
    const std::string& _to, const std::string& _data, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);
    params.append(_to);
    params.append(_data);

    auto request = m_factory->buildRequest("call", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("call") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::sendTransaction(const std::string& _groupID, const std::string& _nodeName,
    const std::string& _data, bool _requireProof, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    std::ostringstream oss;
    oss << "{\"id\":" << m_factory->nextId()
        << R"(,"jsonrpc":"2.0","method":"sendTransaction","params":[")" << _groupID << "\",\""
        << name << "\",\"" << _data << "\"," << (_requireProof ? "true" : "false") << "]}";

    std::string json = oss.str();

    /*
    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);
    params.append(_data);
    params.append(_requireProof);

    auto request = m_factory->buildRequest("sendTransaction", params);
    auto s = request->toJson();
    */

    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("sendTransaction")
                           << LOG_KV(
                                  "sendRequestToHighestBlockNode", m_sendRequestToHighestBlockNode)
                           << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getTransaction(const std::string& _groupID, const std::string& _nodeName,
    const std::string& _txHash, bool _requireProof, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);
    params.append(_txHash);
    params.append(_requireProof);

    auto request = m_factory->buildRequest("getTransaction", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getTransaction") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getTransactionReceipt(const std::string& _groupID, const std::string& _nodeName,
    const std::string& _txHash, bool _requireProof, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);
    params.append(_txHash);
    params.append(_requireProof);

    auto request = m_factory->buildRequest("getTransactionReceipt", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getTransactionReceipt") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getBlockByHash(const std::string& _groupID, const std::string& _nodeName,
    const std::string& _blockHash, bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);
    params.append(_blockHash);
    params.append(_onlyHeader);
    params.append(_onlyTxHash);

    auto request = m_factory->buildRequest("getBlockByHash", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getBlockByHash") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getBlockByNumber(const std::string& _groupID, const std::string& _nodeName,
    int64_t _blockNumber, bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);
    params.append(_blockNumber);
    params.append(_onlyHeader);
    params.append(_onlyTxHash);

    auto request = m_factory->buildRequest("getBlockByNumber", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getBlockByNumber") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getBlockHashByNumber(const std::string& _groupID, const std::string& _nodeName,
    int64_t _blockNumber, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);
    params.append(_blockNumber);

    auto request = m_factory->buildRequest("getBlockHashByNumber", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getBlockHashByNumber") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getBlockNumber(
    const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);

    auto request = m_factory->buildRequest("getBlockNumber", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getBlockNumber") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getCode(const std::string& _groupID, const std::string& _nodeName,
    const std::string& _contractAddress, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);
    params.append(_contractAddress);

    auto request = m_factory->buildRequest("getCode", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getCode") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getSealerList(
    const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);

    auto request = m_factory->buildRequest("getSealerList", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getSealerList") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getObserverList(
    const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);

    auto request = m_factory->buildRequest("getObserverList", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getObserverList") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getPbftView(
    const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);

    auto request = m_factory->buildRequest("getPbftView", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getPbftView") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getPendingTxSize(
    const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);

    auto request = m_factory->buildRequest("getPendingTxSize", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getPendingTxSize") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getSyncStatus(
    const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);

    auto request = m_factory->buildRequest("getSyncStatus", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getSyncStatus") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getConsensusStatus(
    const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);

    auto request = m_factory->buildRequest("getConsensusStatus", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getConsensusStatus") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getSystemConfigByKey(const std::string& _groupID, const std::string& _nodeName,
    const std::string& _keyValue, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);
    params.append(_keyValue);

    auto request = m_factory->buildRequest("getSystemConfigByKey", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getSystemConfigByKey") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getTotalTransactionCount(
    const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc)
{
    std::string name = _nodeName;
    if (m_sendRequestToHighestBlockNode && name.empty())
    {
        m_service->randomGetHighestBlockNumberNode(_groupID, name);
    }

    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(name);

    auto request = m_factory->buildRequest("getTotalTransactionCount", params);
    auto json = request->toJson();
    m_sender(_groupID, name, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getTotalTransactionCount") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getPeers(RespFunc _respFunc)
{
    Json::Value params = Json::Value(Json::arrayValue);
    auto request = m_factory->buildRequest("getPeers", params);
    auto json = request->toJson();
    m_sender("", "", json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getPeers") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getGroupList(RespFunc _respFunc)
{
    Json::Value params = Json::Value(Json::arrayValue);
    auto request = m_factory->buildRequest("getGroupList", params);
    auto json = request->toJson();
    m_sender("", "", json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getGroupList") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getGroupInfo(const std::string& _groupID, RespFunc _respFunc)
{
    JsonResponse jsonResp;
    jsonResp.jsonrpc = "2.0";
    jsonResp.id = m_factory->nextId();
    bool hitCache = false;

    auto groupInfo = m_service->getGroupInfo(_groupID);
    if (groupInfo)
    {
        jsonResp.result = m_groupInfoCodec->serialize(groupInfo);
        hitCache = true;
    }
    else
    {
        jsonResp.result = Json::Value(Json::nullValue);
        hitCache = false;
    }

    auto jsonString = jsonResp.toJsonString();
    auto jsonData = std::make_shared<bytes>(jsonString.begin(), jsonString.end());
    _respFunc(nullptr, jsonData);

    RPCIMPL_LOG(INFO) << LOG_BADGE("getGroupInfo") << LOG_BADGE("get group info from cache")
                      << LOG_KV("hitCache", hitCache) << LOG_KV("response", jsonString);
}

void JsonRpcImpl::getGroupInfoList(RespFunc _respFunc)
{
    Json::Value params = Json::Value(Json::arrayValue);

    auto request = m_factory->buildRequest("getGroupInfoList", params);
    auto json = request->toJson();
    m_sender("", "", json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getGroupNodeInfo") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getGroupNodeInfo(
    const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc)
{
    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);
    params.append(_nodeName);

    auto request = m_factory->buildRequest("getGroupNodeInfo", params);
    auto json = request->toJson();
    m_sender(_groupID, _nodeName, json, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getGroupNodeInfo") << LOG_KV("request", json);
    }
}

void JsonRpcImpl::getGroupPeers(std::string const& _groupID, RespFunc _respFunc)
{
    Json::Value params = Json::Value(Json::arrayValue);
    params.append(_groupID);

    auto request = m_factory->buildRequest("getGroupPeers", params);
    auto requestStr = request->toJson();
    m_sender("", "", requestStr, _respFunc);
    if (c_fileLogLevel <= DEBUG) [[unlikely]]
    {
        RPCIMPL_LOG(DEBUG) << LOG_BADGE("getGroupPeers") << LOG_KV("request", requestStr);
    }
}