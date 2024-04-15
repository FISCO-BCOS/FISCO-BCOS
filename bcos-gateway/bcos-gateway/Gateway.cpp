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
 * @file Gateway.cpp
 * @author: octopus
 * @date 2021-04-19
 */

#include "bcos-front/FrontMessage.h"
#include "bcos-gateway/GatewayConfig.h"
#include "bcos-gateway/GatewayFactory.h"
#include "bcos-gateway/gateway/FrontServiceInfo.h"
#include "bcos-gateway/gateway/GatewayStatus.h"
#include "bcos-gateway/libnetwork/Session.h"
#include "bcos-gateway/libnetwork/Socket.h"
#include "bcos-gateway/libp2p/P2PMessageV2.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-gateway/libp2p/ServiceV2.h"
#include "bcos-gateway/protocol/GatewayNodeStatus.h"
#include "bcos-utilities/BoostLog.h"
#include "filter/Filter.h"
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-gateway/Common.h>
#include <bcos-gateway/Gateway.h>
#include <bcos-gateway/gateway/GatewayMessageExtAttributes.h>
#include <bcos-gateway/libp2p/P2PMessage.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <json/json.h>
#include <algorithm>
#include <random>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::gateway;
using namespace bcos::group;
using namespace bcos::amop;
using namespace bcos::crypto;

void Gateway::start()
{
    if (m_gatewayRateLimiter)
    {
        m_gatewayRateLimiter->start();
    }
    if (m_p2pInterface)
    {
        m_p2pInterface->start();
    }
    if (m_amop)
    {
        m_amop->start();
    }
    if (m_gatewayNodeManager)
    {
        m_gatewayNodeManager->start();
    }

    GATEWAY_LOG(INFO) << LOG_DESC("start end.");
}

void Gateway::stop()
{
    // erase the registered handler
    if (m_p2pInterface)
    {
        m_p2pInterface->eraseHandlerByMsgType(GatewayMessageType::PeerToPeerMessage);
        m_p2pInterface->eraseHandlerByMsgType(GatewayMessageType::BroadcastMessage);
        m_p2pInterface->stop();
    }
    if (m_amop)
    {
        m_amop->stop();
    }
    if (m_gatewayNodeManager)
    {
        m_gatewayNodeManager->stop();
    }
    if (m_gatewayRateLimiter)
    {
        m_gatewayRateLimiter->stop();
    }

    GATEWAY_LOG(INFO) << LOG_DESC("stop end.");
}

void Gateway::asyncGetPeers(
    std::function<void(Error::Ptr, GatewayInfo::Ptr, GatewayInfosPtr)> _onGetPeers)
{
    if (!_onGetPeers)
    {
        return;
    }
    auto sessionInfos = m_p2pInterface->sessionInfos();
    auto peersNodeIDList = m_gatewayNodeManager->peersRouterTable()->getAllPeers();
    GatewayInfosPtr peerGatewayInfos = std::make_shared<GatewayInfos>();
    // append the peers sessionInfos
    for (auto const& info : sessionInfos)
    {
        auto gatewayInfo = std::make_shared<GatewayInfo>(info);
        auto nodeIDList = m_gatewayNodeManager->peersNodeIDList(info.p2pID);
        gatewayInfo->setNodeIDInfo(std::move(nodeIDList));
        peerGatewayInfos->emplace_back(gatewayInfo);
        peersNodeIDList.erase(info.p2pID);
    }
    // append peers that are not directly connected to nodeSelf
    for (auto const& peer : peersNodeIDList)
    {
        P2PInfo p2pInfo;
        p2pInfo.p2pID = peer;
        auto gatewayInfo = std::make_shared<GatewayInfo>(p2pInfo);
        auto nodeIDList = m_gatewayNodeManager->peersNodeIDList(peer);
        gatewayInfo->setNodeIDInfo(std::move(nodeIDList));
        peerGatewayInfos->emplace_back(gatewayInfo);
    }
    auto localP2pInfo = m_p2pInterface->localP2pInfo();
    auto localGatewayInfo = std::make_shared<GatewayInfo>(localP2pInfo);
    auto localNodeInfo = m_gatewayNodeManager->localRouterTable()->nodeListInfo();
    localGatewayInfo->setNodeIDInfo(std::move(localNodeInfo));
    _onGetPeers(nullptr, localGatewayInfo, peerGatewayInfos);
}

/**
 * @brief: get nodeIDs from gateway
 * @param _groupID:
 * @param _onGetGroupNodeInfo: get nodeIDs callback
 * @return void
 */
void Gateway::asyncGetGroupNodeInfo(
    const std::string& _groupID, GetGroupNodeInfoFunc _onGetGroupNodeInfo)
{
    auto groupNodeInfo = m_gatewayNodeManager->getGroupNodeInfoList(_groupID);
    _onGetGroupNodeInfo(nullptr, groupNodeInfo);
}


/**
 * @brief: send message
 * @param _groupID: groupID
 * @param _moduleID: moduleID
 * @param _srcNodeID: the sender nodeID
 * @param _dstNodeID: the receiver nodeID
 * @param _payload: message payload
 * @param _errorRespFunc: error func
 * @return void
 */
void Gateway::asyncSendMessageByNodeID(const std::string& _groupID, int _moduleID,
    NodeIDPtr _srcNodeID, NodeIDPtr _dstNodeID, bytesConstRef _payload,
    ErrorRespFunc _errorRespFunc)
{
    auto p2pIDs =
        m_gatewayNodeManager->peersRouterTable()->queryP2pIDs(_groupID, _dstNodeID->hex());
    if (p2pIDs.empty())
    {
        if (m_gatewayNodeManager->localRouterTable()->sendMessage(
                _groupID, _srcNodeID, _dstNodeID, _payload, _errorRespFunc))
        {
            return;
        }
        GATEWAY_LOG(DEBUG) << LOG_DESC("could not find a gateway to send this message")
                           << LOG_KV("groupID", _groupID) << LOG_KV("srcNodeID", _srcNodeID->hex())
                           << LOG_KV("dstNodeID", _dstNodeID->hex());

        auto errorPtr = BCOS_ERROR_PTR(CommonError::NotFoundFrontServiceSendMsg,
            "could not find a gateway to "
            "send this message, groupID:" +
                _groupID + " ,dstNodeID:" + _dstNodeID->hex());
        if (_errorRespFunc)
        {
            _errorRespFunc(errorPtr);
        }
        return;
    }

    auto message =
        std::static_pointer_cast<P2PMessage>(m_p2pInterface->messageFactory()->buildMessage());

    auto msgExtAttr = std::make_shared<GatewayMessageExtAttributes>();
    msgExtAttr->setGroupID(_groupID);
    msgExtAttr->setModuleID(_moduleID);

    message->setPacketType(GatewayMessageType::PeerToPeerMessage);
    message->setSeq(m_p2pInterface->messageFactory()->newSeq());
    message->setPayload(std::make_shared<bytes>(_payload.begin(), _payload.end()));
    message->setExtAttributes(msgExtAttr);

    auto options = message->options();
    options->setGroupID(_groupID);
    options->setModuleID(_moduleID);
    options->setSrcNodeID(_srcNodeID->encode());
    options->dstNodeIDs().push_back(_dstNodeID->encode());

    auto retry = std::make_shared<Retry>(std::move(_srcNodeID), std::move(_dstNodeID),
        std::move(message), m_p2pInterface, std::move(_errorRespFunc), _moduleID);
    retry->insertP2pIDs(p2pIDs);
    retry->trySendMessage();
}

/**
 * @brief: send message to multiple nodes
 * @param _groupID: groupID
 * @param _moduleID: moduleID
 * @param _srcNodeID: the sender nodeID
 * @param _nodeIDs: the receiver nodeIDs
 * @param _payload: message content
 * @return void
 */
void Gateway::asyncSendMessageByNodeIDs(const std::string& _groupID, int _moduleID,
    NodeIDPtr _srcNodeID, const NodeIDs& _dstNodeIDs, bytesConstRef _payload)
{
    for (auto dstNodeID : _dstNodeIDs)
    {
        asyncSendMessageByNodeID(_groupID, _moduleID, _srcNodeID, dstNodeID, _payload,
            [_groupID, _srcNodeID, dstNodeID](Error::Ptr _error) {
                if (!_error)
                {
                    return;
                }
                GATEWAY_LOG(TRACE)
                    << LOG_DESC("asyncSendMessageByNodeIDs callback") << LOG_KV("groupID", _groupID)
                    << LOG_KV("srcNodeID", _srcNodeID->hex())
                    << LOG_KV("dstNodeID", dstNodeID->hex()) << LOG_KV("code", _error->errorCode());
            });
    }
}

/**
 * @brief: send message to all nodes
 * @param _groupID: groupID
 * @param _moduleID: moduleID
 * @param _srcNodeID: the sender nodeID
 * @param _payload: message content
 * @return void
 */
void Gateway::asyncSendBroadcastMessage(uint16_t _type, const std::string& _groupID, int _moduleID,
    NodeIDPtr _srcNodeID, bytesConstRef _payload)
{
    // broadcast message to the local nodes
    auto ret = m_gatewayNodeManager->localRouterTable()->asyncBroadcastMsg(
        _type, _groupID, _moduleID, _srcNodeID, _payload);
    auto message =
        std::static_pointer_cast<P2PMessage>(m_p2pInterface->messageFactory()->buildMessage());
    message->setPacketType(GatewayMessageType::BroadcastMessage);
    message->setExt(_type);
    message->setSeq(m_p2pInterface->messageFactory()->newSeq());
    message->setPayload(std::make_shared<bytes>(_payload.begin(), _payload.end()));

    auto options = message->options();
    options->setGroupID(_groupID);
    options->setSrcNodeID(_srcNodeID->encode());
    options->setModuleID(_moduleID);

    auto msgExtAttr = std::make_shared<GatewayMessageExtAttributes>();
    msgExtAttr->setGroupID(_groupID);
    msgExtAttr->setModuleID(_moduleID);
    message->setExtAttributes(msgExtAttr);

    // broadcast message to the peers
    m_gatewayNodeManager->peersRouterTable()->asyncBroadcastMsg(
        _type, _groupID, _moduleID, message);
}

/**
 * @brief: receive p2p message from p2p network module
 * @param _groupID: groupID
 * @param _srcNodeID: the sender nodeID
 * @param _dstNodeID: the receiver nodeID
 * @param _payload: message content
 * @param _callback: callback
 * @return void
 */
void Gateway::onReceiveP2PMessage(const std::string& _groupID, NodeIDPtr _srcNodeID,
    NodeIDPtr _dstNodeID, bytesConstRef _payload, ErrorRespFunc _errorRespFunc)
{
    auto frontService =
        m_gatewayNodeManager->localRouterTable()->getFrontService(_groupID, _dstNodeID);
    if (!frontService)
    {
        GATEWAY_LOG(ERROR) << LOG_DESC(
                                  "onReceiveP2PMessage unable to find front "
                                  "service to dispatch this message")
                           << LOG_KV("groupID", _groupID) << LOG_KV("srcNodeID", _srcNodeID->hex())
                           << LOG_KV("dstNodeID", _dstNodeID->hex());

        auto errorPtr = BCOS_ERROR_PTR(CommonError::NotFoundFrontServiceDispatchMsg,
            "unable to find front service dispatch message to "
            "groupID:" +
                _groupID + " ,nodeID:" + _dstNodeID->hex());

        if (_errorRespFunc)
        {
            _errorRespFunc(errorPtr);
        }
        return;
    }

    frontService->frontService()->onReceiveMessage(_groupID, _srcNodeID, _payload,
        [_groupID, _srcNodeID, _dstNodeID, _errorRespFunc](Error::Ptr _error) {
            if (_errorRespFunc)
            {
                _errorRespFunc(_error);
            }
            GATEWAY_LOG(TRACE) << LOG_DESC("onReceiveP2PMessage callback")
                               << LOG_KV("groupID", _groupID)
                               << LOG_KV("srcNodeID", _srcNodeID->hex())
                               << LOG_KV("dstNodeID", _dstNodeID->hex())
                               << LOG_KV("code", (_error ? _error->errorCode() : 0))
                               << LOG_KV("msg", (_error ? _error->errorMessage() : ""));
        });
}

bool Gateway::checkGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo)
{
    // check the serviceName
    auto nodeList = _groupInfo->nodeInfos();
    for (auto const& node : nodeList)
    {
        auto const& expectedGatewayService =
            node.second->serviceName(bcos::protocol::ServiceType::GATEWAY);
        if (expectedGatewayService != m_gatewayServiceName)
        {
            GATEWAY_LOG(INFO) << LOG_DESC(
                                     "unfollowed groupInfo for inconsistent gateway service name")
                              << LOG_KV("expected", expectedGatewayService)
                              << LOG_KV("selfName", m_gatewayServiceName);
            return false;
        }
    }
    return true;
}

void Gateway::asyncNotifyGroupInfo(
    bcos::group::GroupInfo::Ptr _groupInfo, std::function<void(Error::Ptr&&)> _callback)
{
    if (!checkGroupInfo(_groupInfo))
    {
        if (_callback)
        {
            _callback(nullptr);
        }
        return;
    }
    m_gatewayNodeManager->updateFrontServiceInfo(_groupInfo);
    if (_callback)
    {
        _callback(nullptr);
    }
}

void Gateway::onReceiveP2PMessage(
    NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg)
{
    if (_e.errorCode())
    {
        GATEWAY_LOG(WARNING) << LOG_DESC("onReceiveP2PMessage error")
                             << LOG_KV("code", _e.errorCode()) << LOG_KV("msg", _e.what());
        return;
    }

    auto options = _msg->options();
    auto msgPayload = _msg->payload();
    auto payload = bytesConstRef(msgPayload->data(), msgPayload->size());
    // groupID
    auto groupID = options->groupID();
    // moduleID
    auto moduleID = options->moduleID();


    // Notice: moduleID not set the previous version, try to decode from front message
    if (moduleID == 0)
    {
        moduleID = front::FrontMessage::tryDecodeModuleID(payload);
    }

    if (moduleID == 0)
    {
        GATEWAY_LOG(TRACE) << LOG_BADGE("onReceiveP2PMessage")
                           << LOG_DESC("front message module id not found")
                           << LOG_KV("groupID", groupID) << LOG_KV("moduleID", moduleID)
                           << LOG_KV("seq", _msg->seq()) << LOG_KV("payload size", payload.size());
    }

    // Readonly filter
    if (m_readonlyFilter && !filter(*m_readonlyFilter, groupID, moduleID, {}))
    {
        GATEWAY_LOG(WARNING) << "P2PMessage moduleID: " << moduleID << " filter by readOnlyFilter";

        // Drop the message
        return;
    }

    if (m_gatewayRateLimiter)
    {
        // The moduleID is not obtained,
        auto result = ((moduleID == 0) ?
                           std::nullopt :
                           m_gatewayRateLimiter->checkInComing(groupID, moduleID, _msg->length()));
        if (result.has_value())
        {
            auto errorCode = std::to_string((int)protocol::CommonError::GatewayQPSOverFlow);
            m_p2pInterface->sendRespMessageBySession(
                bytesConstRef((byte*)errorCode.data(), errorCode.size()), _msg, _session);
            return;
        }
    }

    auto srcNodeID = options->srcNodeID();
    const auto& dstNodeIDs = options->dstNodeIDs();
    auto srcNodeIDPtr = m_gatewayNodeManager->keyFactory()->createKey(*srcNodeID);
    auto dstNodeIDPtr = m_gatewayNodeManager->keyFactory()->createKey(*dstNodeIDs[0]);
    auto gateway = std::weak_ptr<Gateway>(shared_from_this());
    onReceiveP2PMessage(groupID, srcNodeIDPtr, dstNodeIDPtr, payload,
        [groupID, moduleID, srcNodeIDPtr, dstNodeIDPtr, _session, _msg, gateway](
            Error::Ptr _error) {
            auto gatewayPtr = gateway.lock();
            if (!gatewayPtr)
            {
                return;
            }

            auto errorCode =
                std::to_string(_error ? _error->errorCode() : (int)protocol::CommonError::SUCCESS);
            if (_error)
            {
                GATEWAY_LOG(TRACE)
                    << LOG_BADGE("onReceiveP2PMessage") << "callback failed"
                    << LOG_KV("code", _error->errorCode())
                    << LOG_KV("message", _error->errorMessage()) << LOG_KV("group", groupID)
                    << LOG_KV("moduleID", moduleID) << LOG_KV("src", srcNodeIDPtr->shortHex())
                    << LOG_KV("dst", dstNodeIDPtr->shortHex());
            }
            gatewayPtr->m_p2pInterface->sendRespMessageBySession(
                bytesConstRef((byte*)errorCode.data(), errorCode.size()), _msg, _session);
        });
}

void Gateway::onReceiveBroadcastMessage(
    NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _msg)
{
    if (_e.errorCode() != 0)
    {
        GATEWAY_LOG(WARNING) << LOG_DESC("onReceiveBroadcastMessage failed")
                             << LOG_KV("code", _e.errorCode()) << LOG_KV("msg", _e.what());
        return;
    }

    auto options = _msg->options();
    auto payload = _msg->payload();

    // groupID
    auto groupID = options->groupID();
    // moduleID
    uint16_t moduleID = options->moduleID();


    // Notice: moduleID not set the previous version, try to decode from front message
    if (moduleID == 0)
    {
        moduleID =
            front::FrontMessage::tryDecodeModuleID(bytesConstRef(payload->data(), payload->size()));
    }

    if (moduleID == 0)
    {
        GATEWAY_LOG(TRACE) << LOG_BADGE("onReceiveBroadcastMessage")
                           << LOG_DESC("front message module id not found")
                           << LOG_KV("groupID", groupID) << LOG_KV("moduleID", moduleID)
                           << LOG_KV("seq", _msg->seq()) << LOG_KV("payload size", payload->size());
    }

    // Readonly filter
    if (m_readonlyFilter && !filter(*m_readonlyFilter, groupID, moduleID, bytesConstRef{}))
    {
        GATEWAY_LOG(INFO) << "BroadcastMessage moduleID: " << moduleID
                          << " filter by readOnlyFilter";
        return;
    }

    if (m_gatewayRateLimiter)
    {
        auto result = ((moduleID == 0) ?
                           std::nullopt :
                           m_gatewayRateLimiter->checkInComing(groupID, moduleID, _msg->length()));
        if (result.has_value())
        {
            auto result = m_gatewayRateLimiter->checkInComing(groupID, moduleID, _msg->length());

            if (result)
            {
                // For broadcast message, ratelimit check failed, do nothing.
                return;
            }
        }
    }

    auto srcNodeIDPtr =
        m_gatewayNodeManager->keyFactory()->createKey(*(_msg->options()->srcNodeID()));

    auto type = _msg->ext();
    m_gatewayNodeManager->localRouterTable()->asyncBroadcastMsg(type, groupID, moduleID,
        srcNodeIDPtr, bytesConstRef(_msg->payload()->data(), _msg->payload()->size()));
}

void bcos::gateway::Gateway::enableReadOnlyMode()
{
    m_readonlyFilter.emplace();
}
