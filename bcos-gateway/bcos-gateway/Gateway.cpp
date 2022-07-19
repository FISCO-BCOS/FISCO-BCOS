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

#include <bcos-framework/protocol/CommonError.h>
#include <bcos-gateway/Common.h>
#include <bcos-gateway/Gateway.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <json/json.h>
#include <boost/core/ignore_unused.hpp>
#include <algorithm>
#include <random>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::gateway;
using namespace bcos::group;
using namespace bcos::amop;
using namespace bcos::crypto;

void Gateway::start()
{
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

    return;
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
    GATEWAY_LOG(INFO) << LOG_DESC("stop end.");
    return;
}

void Gateway::asyncGetPeers(
    std::function<void(Error::Ptr, GatewayInfo::Ptr, GatewayInfosPtr)> _onGetPeers)
{
    if (!_onGetPeers)
    {
        return;
    }
    auto sessionInfos = m_p2pInterface->sessionInfos();
    GatewayInfosPtr peerGatewayInfos = std::make_shared<GatewayInfos>();
    for (auto const& info : sessionInfos)
    {
        auto gatewayInfo = std::make_shared<GatewayInfo>(info);
        auto nodeIDList = m_gatewayNodeManager->peersNodeIDList(info.p2pID);
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
 * @param _srcNodeID: the sender nodeID
 * @param _dstNodeID: the receiver nodeID
 * @param _payload: message payload
 * @param _errorRespFunc: error func
 * @return void
 */
void Gateway::asyncSendMessageByNodeID(const std::string& _groupID, NodeIDPtr _srcNodeID,
    NodeIDPtr _dstNodeID, bytesConstRef _payload, ErrorRespFunc _errorRespFunc)
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
        GATEWAY_LOG(ERROR) << LOG_DESC("could not find a gateway to send this message")
                           << LOG_KV("groupID", _groupID) << LOG_KV("srcNodeID", _srcNodeID->hex())
                           << LOG_KV("dstNodeID", _dstNodeID->hex());

        auto errorPtr = std::make_shared<Error>(CommonError::NotFoundFrontServiceSendMsg,
            "could not find a gateway to "
            "send this message, groupID:" +
                _groupID + " ,dstNodeID:" + _dstNodeID->hex());
        if (_errorRespFunc)
        {
            _errorRespFunc(errorPtr);
        }
        return;
    }

    class Retry : public std::enable_shared_from_this<Retry>
    {
    public:
        // random choose one p2pID to send message
        P2pID chooseP2pID()
        {
            auto p2pId = P2pID();
            if (!m_p2pIDs.empty())
            {
                p2pId = *m_p2pIDs.begin();
                m_p2pIDs.erase(m_p2pIDs.begin());
            }

            return p2pId;
        }

        // send the message with retry
        void trySendMessage()
        {
            if (m_p2pIDs.empty())
            {
                GATEWAY_LOG(ERROR)
                    << LOG_DESC("[Gateway::Retry]") << LOG_DESC("unable to send the message")
                    << LOG_KV("srcNodeID", m_srcNodeID->hex())
                    << LOG_KV("dstNodeID", m_dstNodeID->hex())
                    << LOG_KV("seq", std::to_string(m_p2pMessage->seq()));

                if (m_respFunc)
                {
                    auto errorPtr = std::make_shared<Error>(
                        CommonError::GatewaySendMsgFailed, "unable to send the message");
                    m_respFunc(errorPtr);
                }
                return;
            }
            auto p2pID = chooseP2pID();
            auto self = shared_from_this();
            auto startT = utcTime();
            auto callback = [self, startT, p2pID](NetworkException e,
                                std::shared_ptr<P2PSession> session,
                                std::shared_ptr<P2PMessage> message) {
                boost::ignore_unused(session);
                // network error
                if (e.errorCode() != P2PExceptionType::Success)
                {
                    GATEWAY_LOG(ERROR)
                        << LOG_BADGE("Retry") << LOG_DESC("network callback")
                        << LOG_KV("dstP2P", p2pID) << LOG_KV("errorCode", e.errorCode())
                        << LOG_KV("errorMessage", e.what())
                        << LOG_KV("timeCost", (utcTime() - startT));
                    // try again
                    self->trySendMessage();
                    return;
                }

                try
                {
                    auto payload = message->payload();
                    int respCode =
                        boost::lexical_cast<int>(std::string(payload->begin(), payload->end()));
                    // the peer gateway not response not ok ,it means the gateway not dispatch the
                    // message successfully,find another gateway and try again
                    if (respCode != CommonError::SUCCESS)
                    {
                        GATEWAY_LOG(WARNING)
                            << LOG_BADGE("Retry") << LOG_KV("p2pid", p2pID)
                            << LOG_KV("errorCode", respCode) << LOG_KV("errorMessage", e.what());
                        // try again
                        self->trySendMessage();
                        return;
                    }
                    GATEWAY_LOG(TRACE)
                        << LOG_BADGE("Retry: asyncSendMessageByNodeID success")
                        << LOG_KV("dstP2P", p2pID) << LOG_KV("srcNodeID", self->m_srcNodeID->hex())
                        << LOG_KV("dstNodeID", self->m_dstNodeID->hex());
                    // send message successfully
                    if (self->m_respFunc)
                    {
                        self->m_respFunc(nullptr);
                    }
                    return;
                }
                catch (const std::exception& e)
                {
                    GATEWAY_LOG(ERROR)
                        << LOG_BADGE("trySendMessage and receive response exception")
                        << LOG_KV("payload",
                               std::string(message->payload()->begin(), message->payload()->end()))
                        << LOG_KV("packetType", message->packetType())
                        << LOG_KV("src", message->options() ?
                                             toHex(*(message->options()->srcNodeID())) :
                                             "unknown")
                        << LOG_KV("size", message->length()) << LOG_KV("error", e.what());

                    self->trySendMessage();
                }
            };
            m_p2pInterface->asyncSendMessageByNodeID(p2pID, m_p2pMessage, callback, Options(10000));
        }

    public:
        std::vector<P2pID> m_p2pIDs;
        NodeIDPtr m_srcNodeID;
        NodeIDPtr m_dstNodeID;
        std::shared_ptr<P2PMessage> m_p2pMessage;
        std::shared_ptr<P2PInterface> m_p2pInterface;
        ErrorRespFunc m_respFunc;
    };

    auto retry = std::make_shared<Retry>();
    auto message =
        std::static_pointer_cast<P2PMessage>(m_p2pInterface->messageFactory()->buildMessage());

    message->setPacketType(GatewayMessageType::PeerToPeerMessage);
    message->setSeq(m_p2pInterface->messageFactory()->newSeq());
    message->options()->setGroupID(_groupID);
    message->options()->setSrcNodeID(_srcNodeID->encode());
    message->options()->dstNodeIDs().push_back(_dstNodeID->encode());
    message->setPayload(std::make_shared<bytes>(_payload.begin(), _payload.end()));

    retry->m_p2pMessage = message;
    retry->m_p2pIDs.insert(retry->m_p2pIDs.begin(), p2pIDs.begin(), p2pIDs.end());
    retry->m_respFunc = _errorRespFunc;
    retry->m_srcNodeID = _srcNodeID;
    retry->m_dstNodeID = _dstNodeID;
    retry->m_p2pInterface = m_p2pInterface;
    retry->trySendMessage();
}

/**
 * @brief: send message to multiple nodes
 * @param _groupID: groupID
 * @param _srcNodeID: the sender nodeID
 * @param _nodeIDs: the receiver nodeIDs
 * @param _payload: message content
 * @return void
 */
void Gateway::asyncSendMessageByNodeIDs(const std::string& _groupID, NodeIDPtr _srcNodeID,
    const NodeIDs& _dstNodeIDs, bytesConstRef _payload)
{
    for (auto dstNodeID : _dstNodeIDs)
    {
        asyncSendMessageByNodeID(_groupID, _srcNodeID, dstNodeID, _payload,
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
 * @param _srcNodeID: the sender nodeID
 * @param _payload: message content
 * @return void
 */
void Gateway::asyncSendBroadcastMessage(
    uint16_t _type, const std::string& _groupID, NodeIDPtr _srcNodeID, bytesConstRef _payload)
{
    // broadcast message to the local nodes
    auto ret = m_gatewayNodeManager->localRouterTable()->asyncBroadcastMsg(
        _type, _groupID, _moduleID, _srcNodeID, _payload);
    auto message =
        std::static_pointer_cast<P2PMessage>(m_p2pInterface->messageFactory()->buildMessage());
    message->setPacketType(GatewayMessageType::BroadcastMessage);
    message->setExt(_type);
    message->setSeq(m_p2pInterface->messageFactory()->newSeq());
    message->options()->setGroupID(_groupID);
    message->options()->setSrcNodeID(_srcNodeID->encode());
    message->setPayload(std::make_shared<bytes>(_payload.begin(), _payload.end()));
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

        auto errorPtr = std::make_shared<Error>(CommonError::NotFoundFrontServiceDispatchMsg,
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
    auto groupID = options->groupID();
    auto srcNodeID = options->srcNodeID();
    const auto& dstNodeIDs = options->dstNodeIDs();
    auto payload = _msg->payload();
    auto bytesConstRefPayload = bytesConstRef(payload->data(), payload->size());
    auto srcNodeIDPtr = m_gatewayNodeManager->keyFactory()->createKey(*srcNodeID.get());
    auto dstNodeIDPtr = m_gatewayNodeManager->keyFactory()->createKey(*dstNodeIDs[0].get());
    auto gateway = std::weak_ptr<Gateway>(shared_from_this());
    onReceiveP2PMessage(groupID, srcNodeIDPtr, dstNodeIDPtr, bytesConstRefPayload,
        [groupID, srcNodeIDPtr, dstNodeIDPtr, _session, _msg, gateway](Error::Ptr _error) {
            auto gatewayPtr = gateway.lock();
            if (!gatewayPtr)
            {
                return;
            }

            auto errorCode =
                std::to_string(_error ? _error->errorCode() : (int)protocol::CommonError::SUCCESS);
            if (_error)
            {
                GATEWAY_LOG(DEBUG)
                    << "onReceiveP2PMessage callback" << LOG_KV("code", _error->errorCode())
                    << LOG_KV("msg", _error->errorMessage()) << LOG_KV("group", groupID)
                    << LOG_KV("src", srcNodeIDPtr->shortHex())
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
        GATEWAY_LOG(WARNING) << LOG_DESC("onReceiveBroadcastMessage error")
                             << LOG_KV("code", _e.errorCode()) << LOG_KV("msg", _e.what());
        return;
    }

    auto options = _msg->options();
    auto msgPayload = _msg->payload();
    auto payload = bytesConstRef(msgPayload->data(), msgPayload->size());

    // groupID
    auto groupID = options->groupID();
    // moduleID
    uint16_t moduleID = options->moduleID();

    /*
    // TODO: if outgoing bandwidth exceeds the upper limit
    // the request of the module that restricts network traffic should not be forwarded to front
    // other modules, discard the request directly ???

    bool isOutGoingBWOverflow = false;
    if (isOutGoingBWOverflow)
    {
        GATEWAY_LOG(WARNING)
            << LOG_BADGE("onReceiveBroadcastMessage")
            << LOG_DESC("stop forward the message to the front for outgoing bandwidth overflow")
            << LOG_KV("group", groupID) << LOG_KV("moduleID", moduleID);

        // TODO: Add statistics about discarded messages

        return;
    }
    */

    auto srcNodeIDPtr =
        m_gatewayNodeManager->keyFactory()->createKey(*(_msg->options()->srcNodeID()));
    auto groupID = _msg->options()->groupID();
    auto type = _msg->ext();
    GATEWAY_LOG(TRACE) << LOG_DESC("onReceiveBroadcastMessage")
                       << LOG_KV("src", _msg->srcP2PNodeID())
                       << LOG_KV("dst", _msg->dstP2PNodeID());
    m_gatewayNodeManager->localRouterTable()->asyncBroadcastMsg(type, groupID, moduleID,
        srcNodeIDPtr, bytesConstRef(_msg->payload()->data(), _msg->payload()->size()));
}
