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
 * @brief: in-process RPC bridge implementation
 * @file: LocalRpcConnection.cpp
 */

#include "LocalRpcConnection.h"

using namespace bcos::console;

LocalRpcConnection::LocalRpcConnection(bcos::rpc::JsonRpcInterface::Ptr jsonRpc,
    std::string defaultGroup, std::string defaultNodeName)
  : m_jsonRpc(std::move(jsonRpc)),
    m_defaultGroup(std::move(defaultGroup))
{
    if (!defaultNodeName.empty())
    {
        m_defaultNodeName = std::move(defaultNodeName);
    }
}

void LocalRpcConnection::call(std::string_view groupID, std::string_view nodeName,
    std::string_view to, std::string_view data, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->call(gid, nname, to, data,
        [respFunc = std::move(respFunc)](bcos::Error::Ptr error, Json::Value& result) {
            respFunc(std::move(error), result);
        });
}

void LocalRpcConnection::call(std::string_view groupID, std::string_view nodeName,
    std::string_view to, std::string_view data, std::string_view sign, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->call(gid, nname, to, data, sign,
        [respFunc = std::move(respFunc)](bcos::Error::Ptr error, Json::Value& result) {
            respFunc(std::move(error), result);
        });
}

void LocalRpcConnection::sendTransaction(std::string_view groupID, std::string_view nodeName,
    std::string_view data, bool requireProof, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->sendTransaction(gid, nname, data, requireProof,
        [respFunc = std::move(respFunc)](bcos::Error::Ptr error, Json::Value& result) {
            respFunc(std::move(error), result);
        });
}

void LocalRpcConnection::getTransaction(std::string_view groupID, std::string_view nodeName,
    std::string_view txHash, bool requireProof, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getTransaction(gid, nname, txHash, requireProof, std::move(respFunc));
}

void LocalRpcConnection::getTransactionReceipt(std::string_view groupID, std::string_view nodeName,
    std::string_view txHash, bool requireProof, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getTransactionReceipt(gid, nname, txHash, requireProof, std::move(respFunc));
}

void LocalRpcConnection::getBlockByHash(std::string_view groupID, std::string_view nodeName,
    std::string_view blockHash, bool onlyHeader, bool onlyTxHash, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getBlockByHash(gid, nname, blockHash, onlyHeader, onlyTxHash, std::move(respFunc));
}

void LocalRpcConnection::getBlockByNumber(std::string_view groupID, std::string_view nodeName,
    int64_t blockNumber, bool onlyHeader, bool onlyTxHash, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getBlockByNumber(gid, nname, blockNumber, onlyHeader, onlyTxHash, std::move(respFunc));
}

void LocalRpcConnection::getBlockHashByNumber(std::string_view groupID,
    std::string_view nodeName, int64_t blockNumber, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getBlockHashByNumber(gid, nname, blockNumber, std::move(respFunc));
}

void LocalRpcConnection::getBlockNumber(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getBlockNumber(gid, nname, std::move(respFunc));
}

void LocalRpcConnection::getCode(std::string_view groupID, std::string_view nodeName,
    std::string_view contractAddress, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getCode(gid, nname, contractAddress, std::move(respFunc));
}

void LocalRpcConnection::getABI(std::string_view groupID, std::string_view nodeName,
    std::string_view contractAddress, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getABI(gid, nname, contractAddress, std::move(respFunc));
}

void LocalRpcConnection::getSealerList(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getSealerList(gid, nname, std::move(respFunc));
}

void LocalRpcConnection::getObserverList(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getObserverList(gid, nname, std::move(respFunc));
}

void LocalRpcConnection::getNodeListByType(std::string_view groupID, std::string_view nodeName,
    std::string_view nodeType, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getNodeListByType(gid, nname, nodeType, std::move(respFunc));
}

void LocalRpcConnection::getPbftView(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getPbftView(gid, nname, std::move(respFunc));
}

void LocalRpcConnection::getConsensusStatus(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getConsensusStatus(gid, nname, std::move(respFunc));
}

void LocalRpcConnection::getSyncStatus(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getSyncStatus(gid, nname, std::move(respFunc));
}

void LocalRpcConnection::getSystemConfigByKey(std::string_view groupID, std::string_view nodeName,
    std::string_view key, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getSystemConfigByKey(gid, nname, key, std::move(respFunc));
}

void LocalRpcConnection::getPendingTxSize(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getPendingTxSize(gid, nname, std::move(respFunc));
}

void LocalRpcConnection::getTotalTransactionCount(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getTotalTransactionCount(gid, nname, std::move(respFunc));
}

void LocalRpcConnection::getPeers(RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    m_jsonRpc->getPeers(std::move(respFunc));
}

void LocalRpcConnection::getGroupList(RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    m_jsonRpc->getGroupList(std::move(respFunc));
}

void LocalRpcConnection::getGroupInfo(std::string_view groupID, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    m_jsonRpc->getGroupInfo(gid, std::move(respFunc));
}

void LocalRpcConnection::getGroupInfoList(RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    m_jsonRpc->getGroupInfoList(std::move(respFunc));
}

void LocalRpcConnection::getGroupNodeInfo(
    std::string_view groupID, std::string_view nodeName, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    auto nname = nodeName.empty() ? m_defaultNodeName : nodeName;
    m_jsonRpc->getGroupNodeInfo(gid, nname, std::move(respFunc));
}

void LocalRpcConnection::getGroupPeers(std::string_view groupID, RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    auto gid = groupID.empty() ? m_defaultGroup : groupID;
    m_jsonRpc->getGroupPeers(gid, std::move(respFunc));
}

void LocalRpcConnection::getGroupBlockNumber(RpcRespFunc respFunc)
{
    std::lock_guard lock(m_mutex);
    m_jsonRpc->getGroupBlockNumber(std::move(respFunc));
}
