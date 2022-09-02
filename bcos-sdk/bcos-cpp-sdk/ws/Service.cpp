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
 * @file Service.cpp
 * @author: octopus
 * @date 2021-10-22
 */
#include <bcos-boostssl/websocket/WsError.h>
#include <bcos-cpp-sdk/ws/Common.h>
#include <bcos-cpp-sdk/ws/HandshakeResponse.h>
#include <bcos-cpp-sdk/ws/Service.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/rpc/HandshakeRequest.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <boost/thread/thread.hpp>
#include <algorithm>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::service;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;
using namespace bcos;

static const int32_t BLOCK_LIMIT_RANGE = 500;

Service::Service(bcos::group::GroupInfoCodec::Ptr _groupInfoCodec,
    bcos::group::GroupInfoFactory::Ptr _groupInfoFactory, std::string _moduleName)
  : WsService(_moduleName), m_groupInfoCodec(_groupInfoCodec), m_groupInfoFactory(_groupInfoFactory)
{
    m_localProtocol = g_BCOSConfig.protocolInfo(bcos::protocol::ProtocolModuleID::RpcService);
    RPC_WS_LOG(INFO) << LOG_DESC("init the local protocol")
                     << LOG_KV("minVersion", m_localProtocol->minVersion())
                     << LOG_KV("maxVersion", m_localProtocol->maxVersion())
                     << LOG_KV("module", m_localProtocol->protocolModuleID());
}

void Service::start()
{
    bcos::boostssl::ws::WsService::start();

    waitForConnectionEstablish();
}

void Service::stop()
{
    bcos::boostssl::ws::WsService::stop();
}

void Service::waitForConnectionEstablish()
{
    auto start = std::chrono::high_resolution_clock::now();
    auto end = start + std::chrono::milliseconds(waitConnectFinishTimeout());

    while (true)
    {
        // sleep for connection establish
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (handshakeSucCount())
        {
            RPC_WS_LOG(INFO) << LOG_BADGE("waitForConnectionEstablish")
                             << LOG_DESC("wait for websocket connection handshake success")
                             << LOG_KV("suc count", handshakeSucCount());
            break;
        }

        if (std::chrono::high_resolution_clock::now() < end)
        {
            continue;
        }
        else
        {
            stop();
            RPC_WS_LOG(WARNING) << LOG_BADGE("waitForConnectionEstablish")
                                << LOG_DESC("wait for websocket connection handshake timeout")
                                << LOG_KV("timeout", waitConnectFinishTimeout());

            BOOST_THROW_EXCEPTION(std::runtime_error("The websocket connection handshake timeout"));
            return;
        }
    }
}

void Service::onConnect(Error::Ptr _error, std::shared_ptr<WsSession> _session)
{
    bcos::boostssl::ws::WsService::onConnect(_error, _session);

    startHandshake(_session);
}

void Service::onDisconnect(Error::Ptr _error, std::shared_ptr<WsSession> _session)
{
    bcos::boostssl::ws::WsService::onDisconnect(_error, _session);

    std::string endPoint = _session ? _session->endPoint() : std::string();
    if (!endPoint.empty())
    {
        clearGroupInfoByEp(endPoint);
    }
}

void Service::onRecvMessage(std::shared_ptr<MessageFace> _msg, std::shared_ptr<WsSession> _session)
{
    auto seq = _msg->seq();
    if (!checkHandshakeDone(_session))
    {
        // Note: The message is received before the handshake with the node is complete
        RPC_WS_LOG(WARNING) << LOG_BADGE("onRecvMessage")
                            << LOG_DESC(
                                   "websocket service unable to handler message before handshake"
                                   "with the node successfully")
                            << LOG_KV("endpoint", _session ? _session->endPoint() : std::string(""))
                            << LOG_KV("seq", seq);

        _session->drop(bcos::boostssl::ws::WsError::UserDisconnect);
        return;
    }

    bcos::boostssl::ws::WsService::onRecvMessage(_msg, _session);
}

// ---------------------overide end ---------------------------------------------------------------

// ---------------------send message begin---------------------------------------------------------
void Service::asyncSendMessageByGroupAndNode(const std::string& _group, const std::string& _node,
    std::shared_ptr<bcos::boostssl::MessageFace> _msg, bcos::boostssl::ws::Options _options,
    bcos::boostssl::ws::RespCallBack _respFunc)
{
    std::set<std::string> endPoints;
    if (_group.empty())
    {
        // asyncSendMessage(_msg, _options, _respFunc);
        auto ss = sessions();
        for (const auto& session : ss)
        {
            if (session->isConnected())
            {
                endPoints.insert(session->endPoint());
            }
        }

        if (endPoints.empty())
        {
            auto error = std::make_shared<Error>(WsError::EndPointNotExist,
                "there has no connection available, maybe all connections disconnected");
            _respFunc(error, nullptr, nullptr);
            return;
        }
    }
    else if (_node.empty())
    {
        // all connections available for the group
        getEndPointsByGroup(_group, endPoints);
        if (endPoints.empty())
        {
            auto error = std::make_shared<Error>(WsError::EndPointNotExist,
                "there has no connection available for the group, maybe all connections "
                "disconnected or "
                "the group does not exist, group: " +
                    _group);
            _respFunc(error, nullptr, nullptr);
            return;
        }
    }
    else
    {
        // all connections available for the node
        getEndPointsByGroupAndNode(_group, _node, endPoints);
        if (endPoints.empty())
        {
            auto error = std::make_shared<Error>(WsError::EndPointNotExist,
                "there has no connection available for the node of the group, maybe all "
                "connections "
                "disconnected or the node does not exist, group: " +
                    _group + " ,node: " + _node);
            _respFunc(error, nullptr, nullptr);
            return;
        }
    }

    std::vector<std::string> vecEndPoints(endPoints.begin(), endPoints.end());

    auto seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::default_random_engine e(seed);
    std::shuffle(vecEndPoints.begin(), vecEndPoints.end(), e);

    asyncSendMessageByEndPoint(*vecEndPoints.begin(), _msg, _options, _respFunc);
}
// ---------------------send message end---------------------------------------------------------


bool Service::checkHandshakeDone(std::shared_ptr<bcos::boostssl::ws::WsSession> _session)
{
    return _session && _session->version();
}

void Service::startHandshake(std::shared_ptr<bcos::boostssl::ws::WsSession> _session)
{
    auto message = messageFactory()->buildMessage();
    message->setSeq(messageFactory()->newSeq());
    message->setPacketType(bcos::protocol::MessageType::HANDESHAKE);
    bcos::rpc::HandshakeRequest request(m_localProtocol);
    auto requestData = request.encode();
    message->setPayload(requestData);

    RPC_WS_LOG(INFO) << LOG_BADGE("startHandshake")
                     << LOG_KV("endpoint", _session ? _session->endPoint() : std::string(""));

    auto session = _session;
    auto service = std::dynamic_pointer_cast<Service>(shared_from_this());
    _session->asyncSendMessage(message, Options(m_wsHandshakeTimeout),
        [message, session, service](Error::Ptr _error, std::shared_ptr<MessageFace> _msg,
            std::shared_ptr<WsSession> _session) {
            if (_error && _error->errorCode() != 0)
            {
                RPC_WS_LOG(WARNING)
                    << LOG_BADGE("startHandshake") << LOG_DESC("callback response error")
                    << LOG_KV("endpoint", session ? session->endPoint() : std::string(""))
                    << LOG_KV("errorCode", _error ? _error->errorCode() : -1)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : std::string(""));
                session->drop(bcos::boostssl::ws::WsError::UserDisconnect);
                return;
            }

            auto endPoint = session ? session->endPoint() : std::string("");
            auto response = std::string(_msg->payload()->begin(), _msg->payload()->end());
            auto handshakeResponse = std::make_shared<HandshakeResponse>(service->m_groupInfoCodec);
            if (!handshakeResponse->decode(response))
            {
                _session->drop(bcos::boostssl::ws::WsError::UserDisconnect);

                RPC_WS_LOG(WARNING) << LOG_BADGE("startHandshake")
                                    << LOG_DESC("invalid protocol version json string")
                                    << LOG_KV("endpoint", endPoint);
                return;
            }

            // set protocol version
            session->setVersion(handshakeResponse->protocolVersion());
            auto groupInfoList = handshakeResponse->groupInfoList();
            for (auto& groupInfo : groupInfoList)
            {
                service->updateGroupInfoByEp(endPoint, groupInfo);

                RPC_WS_LOG(DEBUG) << LOG_BADGE("startHandshake") << LOG_DESC("group info")
                                  << LOG_KV("endPoint", endPoint)
                                  << LOG_KV("smCrypto", groupInfo->smCryptoType())
                                  << LOG_KV("wasm", groupInfo->wasm());
            }

            auto groupBlockNumber = handshakeResponse->groupBlockNumber();
            for (auto entry : groupBlockNumber)
            {
                service->updateGroupBlockNumber(entry.first, entry.second);
            }

            service->increaseHandshakeSucCount();
            service->callWsHandshakeSucHandlers(_session);

            RPC_WS_LOG(INFO) << LOG_BADGE("startHandshake") << LOG_DESC("handshake successfully")
                             << LOG_KV("endPoint", endPoint)
                             << LOG_KV("handshake version", _session->version())
                             << LOG_KV("groupInfoList size", groupInfoList.size())
                             << LOG_KV("groupBlockNumber size", groupBlockNumber.size())
                             << LOG_KV("handshake string", response);
        });
}


void Service::onNotifyGroupInfo(
    const std::string& _groupInfoJson, std::shared_ptr<bcos::boostssl::ws::WsSession> _session)
{
    std::string endPoint = _session->endPoint();
    RPC_WS_LOG(TRACE) << LOG_BADGE("onNotifyGroupInfo") << LOG_KV("endPoint", endPoint)
                      << LOG_KV("groupInfoJson", _groupInfoJson);

    try
    {
        auto groupInfo = m_groupInfoCodec->deserialize(_groupInfoJson);
        updateGroupInfoByEp(endPoint, groupInfo);
    }
    catch (const std::exception& e)
    {
        RPC_WS_LOG(WARNING) << LOG_BADGE("onNotifyGroupInfo") << LOG_KV("endPoint", endPoint)
                            << LOG_KV("e", boost::diagnostic_information(e))
                            << LOG_KV("groupInfoJson", _groupInfoJson);
    }
}

void Service::onNotifyGroupInfo(std::shared_ptr<bcos::boostssl::ws::WsMessage> _msg,
    std::shared_ptr<bcos::boostssl::ws::WsSession> _session)
{
    std::string groupInfo = std::string(_msg->payload()->begin(), _msg->payload()->end());

    RPC_WS_LOG(INFO) << LOG_BADGE("onNotifyGroupInfo") << LOG_KV("groupInfo", groupInfo);

    return onNotifyGroupInfo(groupInfo, _session);
}

void Service::clearGroupInfoByEp(const std::string& _endPoint)
{
    RPC_WS_LOG(INFO) << LOG_BADGE("clearGroupInfoByEp") << LOG_KV("endPoint", _endPoint);
    {
        boost::unique_lock<boost::shared_mutex> lock(x_endPointLock);
        {
            for (auto it = m_group2Node2Endpoints.begin(); it != m_group2Node2Endpoints.end();)
            {
                for (auto innerIt = it->second.begin(); innerIt != it->second.end();)
                {
                    innerIt->second.erase(_endPoint);

                    if (innerIt->second.empty())
                    {
                        RPC_WS_LOG(INFO)
                            << LOG_BADGE("clearGroupInfoByEp")
                            << LOG_DESC("group2Node2Endpoints, clear node")
                            << LOG_KV("group", it->first) << LOG_KV("node", innerIt->first);
                        innerIt = it->second.erase(innerIt);
                    }
                    else
                    {
                        innerIt++;
                    }
                }

                if (it->second.empty())
                {
                    RPC_WS_LOG(INFO) << LOG_BADGE("clearGroupInfoByEp")
                                     << LOG_DESC("group2Node2Endpoints, clear group")
                                     << LOG_KV("group", it->first);
                    it = m_group2Node2Endpoints.erase(it);
                }
                else
                {
                    it++;
                }
            }
        }

        {
            auto it = m_endPoint2GroupId2GroupInfo.find(_endPoint);
            if (it != m_endPoint2GroupId2GroupInfo.end())
            {
                m_endPoint2GroupId2GroupInfo.erase(it);

                RPC_WS_LOG(INFO) << LOG_BADGE("clearGroupInfoByEp") << LOG_DESC("clear endPoint")
                                 << LOG_KV("endPoint", _endPoint);
            }
        }
    }

    // Note: for debug
    printGroupInfo();
}

void Service::clearGroupInfoByEp(const std::string& _endPoint, const std::string& _groupID)
{
    RPC_WS_LOG(INFO) << LOG_BADGE("clearGroupInfoByEp") << LOG_KV("endPoint", _endPoint)
                     << LOG_KV("group", _groupID);

    {
        boost::unique_lock<boost::shared_mutex> lock(x_endPointLock);
        auto it = m_group2Node2Endpoints.find(_groupID);
        if (it == m_group2Node2Endpoints.end())
        {
            return;
        }

        auto& groupMapper = it->second;
        for (auto innerIt = groupMapper.begin(); innerIt != groupMapper.end();)
        {
            innerIt->second.erase(_endPoint);
            if (innerIt->second.empty())
            {
                RPC_WS_LOG(INFO) << LOG_BADGE("clearGroupInfoByEp") << LOG_DESC("clear node")
                                 << LOG_KV("group", it->first) << LOG_KV("endPoint", _endPoint)
                                 << LOG_KV("node", innerIt->first);
                innerIt = it->second.erase(innerIt);
            }
            else
            {
                innerIt++;
            }
        }
    }

    // Note: for debug
    // printGroupInfo();
}

void Service::updateGroupInfoByEp(
    const std::string& _endPoint, bcos::group::GroupInfo::Ptr _groupInfo)
{
    RPC_WS_LOG(INFO) << LOG_BADGE("updateGroupInfoByEp") << LOG_KV("endPoint", _endPoint)
                     << LOG_KV("group", _groupInfo->groupID())
                     << LOG_KV("chainID", _groupInfo->chainID())
                     << LOG_KV("genesisConfig", _groupInfo->genesisConfig())
                     << LOG_KV("iniConfig", _groupInfo->iniConfig())
                     << LOG_KV("nodesNum", _groupInfo->nodesNum());
    const auto& group = _groupInfo->groupID();
    const auto& nodes = _groupInfo->nodeInfos();

    {
        updateGroupInfo(_endPoint, _groupInfo);
    }

    {
        // remove first
        clearGroupInfoByEp(_endPoint, group);
    }

    {
        // update
        boost::unique_lock<boost::shared_mutex> lock(x_endPointLock);
        auto& groupMapper = m_group2Node2Endpoints[group];
        for (const auto& node : nodes)
        {
            auto& nodeMapper = groupMapper[node.first];
            nodeMapper.insert(_endPoint);
        }
    }

    // Note: for debug
    printGroupInfo();
}

bool Service::hasEndPointOfNodeAvailable(const std::string& _group, const std::string& _node)
{
    boost::shared_lock<boost::shared_mutex> lock(x_endPointLock);
    auto it = m_group2Node2Endpoints.find(_group);
    if (it == m_group2Node2Endpoints.end())
    {
        return false;
    }

    auto& nodes = it->second;

    return nodes.find(_node) != nodes.end();
}

bool Service::getEndPointsByGroup(const std::string& _group, std::set<std::string>& _endPoints)
{
    boost::shared_lock<boost::shared_mutex> lock(x_endPointLock);
    auto it = m_group2Node2Endpoints.find(_group);
    if (it == m_group2Node2Endpoints.end())
    {
        RPC_WS_LOG(WARNING) << LOG_BADGE("getEndPointsByGroup") << LOG_DESC("group not exist")
                            << LOG_KV("group", _group);
        return false;
    }

    for (auto& nodeMapper : it->second)
    {
        _endPoints.insert(nodeMapper.second.begin(), nodeMapper.second.end());
    }

    RPC_WS_LOG(TRACE) << LOG_BADGE("getEndPointsByGroup") << LOG_KV("group", _group)
                      << LOG_KV("endPoints", _endPoints.size());
    return true;
}

bool Service::getEndPointsByGroupAndNode(
    const std::string& _group, const std::string& _node, std::set<std::string>& _endPoints)
{
    boost::shared_lock<boost::shared_mutex> lock(x_endPointLock);
    auto it = m_group2Node2Endpoints.find(_group);
    if (it == m_group2Node2Endpoints.end())
    {
        RPC_WS_LOG(WARNING) << LOG_BADGE("getEndPointsByGroupAndNode")
                            << LOG_DESC("group not exist") << LOG_KV("group", _group)
                            << LOG_KV("node", _node);
        return false;
    }

    auto innerIt = it->second.find(_node);
    if (innerIt == it->second.end())
    {
        RPC_WS_LOG(WARNING) << LOG_BADGE("getEndPointsByGroupAndNode") << LOG_DESC("node not exist")
                            << LOG_KV("group", _group) << LOG_KV("node", _node);
        return false;
    }

    _endPoints = innerIt->second;

    RPC_WS_LOG(TRACE) << LOG_BADGE("getEndPointsByGroupAndNode") << LOG_KV("group", _group)
                      << LOG_KV("node", _node) << LOG_KV("endPoints", _endPoints.size());
    return true;
}

void Service::printGroupInfo()
{
    boost::shared_lock<boost::shared_mutex> lock(x_endPointLock);

    RPC_WS_LOG(INFO) << LOG_BADGE("printGroupInfo")
                     << LOG_KV("total count", m_group2Node2Endpoints.size());

    for (const auto& groupMapper : m_group2Node2Endpoints)
    {
        RPC_WS_LOG(INFO) << LOG_BADGE("printGroupInfo") << LOG_DESC("group list")
                         << LOG_KV("group", groupMapper.first)
                         << LOG_KV("count", groupMapper.second.size());
        for (const auto& nodeMapper : groupMapper.second)
        {
            RPC_WS_LOG(INFO) << LOG_BADGE("printGroupInfo") << LOG_DESC("node list")
                             << LOG_KV("group", groupMapper.first)
                             << LOG_KV("node", nodeMapper.first)
                             << LOG_KV("count", nodeMapper.second.size());
        }
    }
}

bcos::group::GroupInfo::Ptr Service::getGroupInfo(const std::string& _groupID)
{
    std::vector<bcos::group::GroupInfo::Ptr> groupInfos;
    {
        boost::shared_lock<boost::shared_mutex> lock(x_endPointLock);
        for (const auto& group2GroupInfoMapper : m_endPoint2GroupId2GroupInfo)
        {
            auto& group2GroupInfo = group2GroupInfoMapper.second;
            auto it = group2GroupInfo.find(_groupID);
            if (it == group2GroupInfo.end())
            {
                continue;
            }

            groupInfos.push_back(it->second);
        }
    }

    if (groupInfos.empty())
    {
        RPC_WS_LOG(INFO) << LOG_BADGE("getGroupInfo") << LOG_DESC("group not exist")
                         << LOG_KV("group", _groupID);
        return nullptr;
    }

    RPC_WS_LOG(INFO) << LOG_BADGE("getGroupInfo") << LOG_KV("count", groupInfos.size());

    auto firstGroupInfo = *groupInfos.begin();
    auto groupInfo =
        m_groupInfoFactory->createGroupInfo(firstGroupInfo->chainID(), firstGroupInfo->groupID());
    groupInfo->setSmCryptoType(firstGroupInfo->smCryptoType());
    groupInfo->setWasm(firstGroupInfo->wasm());
    groupInfo->setGenesisConfig(firstGroupInfo->genesisConfig());
    groupInfo->setIniConfig(firstGroupInfo->iniConfig());

    for (const auto& g : groupInfos)
    {
        for (const auto& node : g->nodeInfos())
        {
            if (groupInfo->nodeInfo(node.second->nodeName()))
            {
                continue;
            }

            groupInfo->appendNodeInfo(node.second);
        }
    }

    return groupInfo;
}

void Service::updateGroupInfo(const std::string& _endPoint, bcos::group::GroupInfo::Ptr _groupInfo)
{
    RPC_WS_LOG(INFO) << LOG_BADGE("updateGroupInfo") << LOG_KV("endPoint", _endPoint)
                     << LOG_KV("group", _groupInfo->groupID())
                     << LOG_KV("chainID", _groupInfo->chainID())
                     << LOG_KV("nodesNum", _groupInfo->nodesNum());
    {
        boost::unique_lock<boost::shared_mutex> lock(x_endPointLock);
        m_endPoint2GroupId2GroupInfo[_endPoint][_groupInfo->groupID()] = _groupInfo;
    }
}

//------------------------------ Block Notifier Begin --------------------------
bool Service::getBlockNumber(const std::string& _group, int64_t& _blockNumber)
{
    {
        boost::shared_lock<boost::shared_mutex> lock(x_blockNotifierLock);
        auto it = m_group2BlockNumber.find(_group);
        if (it == m_group2BlockNumber.end())
        {
            return false;
        }

        _blockNumber = it->second;
    }
    /*
    RPC_WS_LOG(TRACE) << LOG_BADGE("getBlockNumber") << LOG_KV("group", _group)
                      << LOG_KV("blockNumber", _blockNumber);
    */
    return true;
}

bool Service::getBlockLimit(const std::string& _groupID, int64_t& _blockLimit)
{
    int64_t blockNumber = -1;
    auto r = getBlockNumber(_groupID, blockNumber);
    if (r)
    {
        _blockLimit = blockNumber + BLOCK_LIMIT_RANGE;
        return true;
    }
    return false;
}

std::pair<bool, bool> Service::updateGroupBlockNumber(
    const std::string& _groupID, int64_t _blockNumber)
{
    bool newBlockNumber = false;
    bool highestBlockNumber = false;
    {
        boost::unique_lock<boost::shared_mutex> lock(x_blockNotifierLock);
        auto it = m_group2BlockNumber.find(_groupID);
        if (it != m_group2BlockNumber.end())
        {
            if (_blockNumber > it->second)
            {
                it->second = _blockNumber;
                newBlockNumber = true;
                highestBlockNumber = true;
            }
            else if (_blockNumber == it->second)
            {
                highestBlockNumber = true;
            }
        }
        else
        {
            m_group2BlockNumber[_groupID] = _blockNumber;
            newBlockNumber = true;
            highestBlockNumber = true;
        }
    }

    if (newBlockNumber)
    {
        RPC_WS_LOG(INFO) << LOG_BADGE("updateGroupBlockNumber") << LOG_KV("groupID", _groupID)
                         << LOG_KV("_blockNumber", _blockNumber);
    }

    return std::make_pair(newBlockNumber, highestBlockNumber);
}

bool Service::randomGetHighestBlockNumberNode(const std::string& _group, std::string& _node)
{
    std::set<std::string> setNodes;
    if (!getHighestBlockNumberNodes(_group, setNodes))
    {
        return false;
    }

    std::vector<std::string> vectorNodes(setNodes.begin(), setNodes.end());
    std::default_random_engine e(std::chrono::system_clock::now().time_since_epoch().count());
    std::shuffle(vectorNodes.begin(), vectorNodes.end(), e);

    _node = *vectorNodes.begin();
    return true;
}

bool Service::getHighestBlockNumberNodes(const std::string& _group, std::set<std::string>& _nodes)
{
    std::set<std::string> tempNodes;

    {
        boost::shared_lock<boost::shared_mutex> lock(x_blockNotifierLock);
        auto it = m_group2LatestBlockNumberNodes.find(_group);
        if (it == m_group2LatestBlockNumberNodes.end())
        {
            return false;
        }

        tempNodes = it->second;
    }

    for (auto it = tempNodes.begin(); it != tempNodes.end(); ++it)
    {
        auto& node = *it;
        if (!hasEndPointOfNodeAvailable(_group, node))
        {
            RPC_WS_LOG(WARNING) << LOG_BADGE("getHighestBlockNumberNodes")
                                << LOG_DESC("node has no endpoint available")
                                << LOG_KV("group", _group) << LOG_KV("nodes", node);

            continue;
        }

        _nodes.insert(*it);
    }

    RPC_WS_LOG(TRACE) << LOG_BADGE("getHighestBlockNumberNodes") << LOG_KV("group", _group)
                      << LOG_KV("nodes size", _nodes.size());
    return !_nodes.empty();
}

void Service::removeBlockNumberInfo(const std::string& _group)
{
    RPC_WS_LOG(INFO) << LOG_BADGE("removeBlockNumberInfo") << LOG_KV("group", _group);
    boost::unique_lock<boost::shared_mutex> lock(x_blockNotifierLock);
    m_group2callbacks.erase(_group);
    m_group2BlockNumber.erase(_group);
    m_group2LatestBlockNumberNodes.erase(_group);
}

void Service::onRecvBlockNotifier(const std::string& _msg)
{
    auto blockNumberInfo = std::make_shared<BlockNumberInfo>();
    if (blockNumberInfo->fromJson(_msg))
    {
        onRecvBlockNotifier(blockNumberInfo);
    }
}

void Service::onRecvBlockNotifier(BlockNumberInfo::Ptr _blockNumber)
{
    RPC_WS_LOG(INFO) << LOG_BADGE("onRecvBlockNotifier")
                     << LOG_DESC("receive block number notifier")
                     << LOG_KV("group", _blockNumber->group())
                     << LOG_KV("node", _blockNumber->node())
                     << LOG_KV("blockNumber", _blockNumber->blockNumber());

    auto r = updateGroupBlockNumber(_blockNumber->group(), _blockNumber->blockNumber());
    bool isNewBlock = r.first;
    bool isHighestBlock = r.second;
    if (isNewBlock || isHighestBlock)
    {
        boost::unique_lock<boost::shared_mutex> lock(x_blockNotifierLock);
        if (isNewBlock)
        {
            m_group2LatestBlockNumberNodes[_blockNumber->group()].clear();
        }

        if (isHighestBlock)
        {
            m_group2LatestBlockNumberNodes[_blockNumber->group()].insert(_blockNumber->node());
        }
    }

    if (isNewBlock)
    {
        RPC_WS_LOG(INFO) << LOG_BADGE("onRecvBlockNotifier") << LOG_DESC("block notifier callback")
                         << LOG_KV("group", _blockNumber->group())
                         << LOG_KV("node", _blockNumber->node())
                         << LOG_KV("blockNumber", _blockNumber->blockNumber());

        boost::shared_lock<boost::shared_mutex> lock(x_blockNotifierLock);
        auto it = m_group2callbacks.find(_blockNumber->group());
        if (it != m_group2callbacks.end())
        {
            for (auto& callback : it->second)
            {
                callback(_blockNumber->group(), _blockNumber->blockNumber());
            }
        }
    }
}

void Service::registerBlockNumberNotifier(
    const std::string& _group, BlockNotifierCallback _callback)
{
    RPC_WS_LOG(INFO) << LOG_BADGE("registerBlockNumberNotifier") << LOG_KV("group", _group);
    boost::unique_lock<boost::shared_mutex> lock(x_blockNotifierLock);
    m_group2callbacks[_group].push_back(_callback);
}
//------------------------------ Block Notifier End --------------------------
