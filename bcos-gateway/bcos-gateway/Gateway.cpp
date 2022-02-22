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

#include <bcos-framework/interfaces/protocol/CommonError.h>
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

    const auto& frontServiceInfos = m_gatewayNodeManager->frontServiceInfos();
    if (frontServiceInfos.empty())
    {
        GATEWAY_LOG(WARNING) << LOG_DESC("gateway has no registered front service");
    }
    else
    {
        for (const auto& group : frontServiceInfos)
        {
            for (const auto& node : group.second)
            {
                GATEWAY_LOG(INFO) << LOG_DESC("start") << LOG_KV("groupID", group.first)
                                  << LOG_KV("nodeID", node.first);
            }
        }
    }

    GATEWAY_LOG(INFO) << LOG_DESC("start end.");

    return;
}

void Gateway::stop()
{
    if (m_p2pInterface)
    {
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

std::shared_ptr<P2PMessage> Gateway::newP2PMessage(const std::string& _groupID,
    bcos::crypto::NodeIDPtr _srcNodeID, bcos::crypto::NodeIDPtr _dstNodeID, bytesConstRef _payload)
{
    auto message =
        std::static_pointer_cast<P2PMessage>(m_p2pInterface->messageFactory()->buildMessage());

    message->setPacketType(MessageType::PeerToPeerMessage);
    message->setSeq(m_p2pInterface->messageFactory()->newSeq());
    message->options()->setGroupID(_groupID);
    message->options()->setSrcNodeID(_srcNodeID->encode());
    message->options()->dstNodeIDs().push_back(_dstNodeID->encode());
    message->setPayload(std::make_shared<bytes>(_payload.begin(), _payload.end()));

    return message;
}

std::shared_ptr<P2PMessage> Gateway::newP2PMessage(
    const std::string& _groupID, bcos::crypto::NodeIDPtr _srcNodeID, bytesConstRef _payload)
{
    auto message =
        std::static_pointer_cast<P2PMessage>(m_p2pInterface->messageFactory()->buildMessage());

    message->setPacketType(MessageType::BroadcastMessage);
    message->setSeq(m_p2pInterface->messageFactory()->newSeq());
    message->options()->setGroupID(_groupID);
    message->options()->setSrcNodeID(_srcNodeID->encode());
    message->setPayload(std::make_shared<bytes>(_payload.begin(), _payload.end()));

    return message;
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
        auto nodeIDInfo = m_gatewayNodeManager->nodeIDInfo(info.p2pID);
        gatewayInfo->setNodeIDInfo(std::move(nodeIDInfo));
        peerGatewayInfos->emplace_back(gatewayInfo);
    }
    auto localP2pInfo = m_p2pInterface->localP2pInfo();
    auto localGatewayInfo = std::make_shared<GatewayInfo>(localP2pInfo);
    auto loaclNodeIDInfo = m_gatewayNodeManager->getLocalNodeIDInfo();
    localGatewayInfo->setNodeIDInfo(std::move(loaclNodeIDInfo));
    _onGetPeers(nullptr, localGatewayInfo, peerGatewayInfos);
}

/**
 * @brief: get nodeIDs from gateway
 * @param _groupID:
 * @param _getNodeIDsFunc: get nodeIDs callback
 * @return void
 */
void Gateway::asyncGetNodeIDs(const std::string& _groupID, GetNodeIDsFunc _getNodeIDsFunc)
{
    std::shared_ptr<crypto::NodeIDs> nodeIDs = std::make_shared<crypto::NodeIDs>();
    m_gatewayNodeManager->queryNodeIDsByGroupID(_groupID, *nodeIDs);
    _getNodeIDsFunc(nullptr, nodeIDs);
}

/**
 * @brief: register FrontService
 * @param _groupID: groupID
 * @param _nodeID: nodeID
 * @param _frontServiceInterface: FrontService
 * @return bool
 */
bool Gateway::registerFrontService(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
    bcos::front::FrontServiceInterface::Ptr _frontServiceInterface)
{
    return m_gatewayNodeManager->registerFrontService(_groupID, _nodeID, _frontServiceInterface);
}

/**
 * @brief: unregister FrontService
 * @param _groupID: groupID
 * @param _nodeID: nodeID
 * @return bool
 */
bool Gateway::unregisterFrontService(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID)
{
    return m_gatewayNodeManager->unregisterFrontService(_groupID, _nodeID);
}


// send message to the local nodes
bool Gateway::trySendLocalMessage(const std::string& _groupID, bcos::crypto::NodeIDPtr _srcNodeID,
    bcos::crypto::NodeIDPtr _dstNodeID, bytesConstRef _payload, ErrorRespFunc _errorRespFunc)
{
    auto frontServiceInfo = m_gatewayNodeManager->queryLocalNodes(_groupID, _dstNodeID->hex());
    if (!frontServiceInfo)
    {
        return false;
    }
    frontServiceInfo->frontService()->onReceiveMessage(
        _groupID, _srcNodeID, _payload, [_errorRespFunc](Error::Ptr _error) {
            if (_errorRespFunc)
            {
                _errorRespFunc(_error);
            }
        });
    return true;
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
void Gateway::asyncSendMessageByNodeID(const std::string& _groupID,
    bcos::crypto::NodeIDPtr _srcNodeID, bcos::crypto::NodeIDPtr _dstNodeID, bytesConstRef _payload,
    ErrorRespFunc _errorRespFunc)
{
    std::set<P2pID> p2pIDs;
    if (!m_gatewayNodeManager->queryP2pIDs(_groupID, _dstNodeID->hex(), p2pIDs))
    {
        if (trySendLocalMessage(_groupID, _srcNodeID, _dstNodeID, _payload, _errorRespFunc))
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
        P2pID randomChooseP2pID()
        {
            auto p2pId = P2pID();
            if (!m_p2pIDs.empty())
            {
                // shuffle
                std::random_device rd;
                std::default_random_engine rng(rd());
                std::shuffle(m_p2pIDs.begin(), m_p2pIDs.end(), rng);
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
            auto p2pID = randomChooseP2pID();
            auto self = shared_from_this();
            auto callback = [self, p2pID](NetworkException e, std::shared_ptr<P2PSession> session,
                                std::shared_ptr<P2PMessage> message) {
                boost::ignore_unused(session);
                // network error
                if (e.errorCode() != P2PExceptionType::Success)
                {
                    GATEWAY_LOG(DEBUG)
                        << LOG_BADGE("Retry") << LOG_DESC("network callback")
                        << LOG_KV("p2pid", p2pID) << LOG_KV("errorCode", e.errorCode())
                        << LOG_KV("errorMessage", e.what());
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
                        GATEWAY_LOG(DEBUG)
                            << LOG_BADGE("Retry") << LOG_KV("p2pid", p2pID)
                            << LOG_KV("errorCode", respCode) << LOG_KV("errorMessage", e.what());
                        // try again
                        self->trySendMessage();
                        return;
                    }

                    GATEWAY_LOG(TRACE) << LOG_BADGE("Retry") << LOG_KV("p2pid", p2pID)
                                       << LOG_KV("srcNodeID", self->m_srcNodeID->hex())
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
                    GATEWAY_LOG(ERROR) << LOG_BADGE("Retry") << LOG_KV("error", e.what());
                    self->trySendMessage();
                }
            };

            m_p2pInterface->asyncSendMessageByNodeID(p2pID, m_p2pMessage, callback, Options(10000));
        }

    public:
        std::vector<P2pID> m_p2pIDs;
        bcos::crypto::NodeIDPtr m_srcNodeID;
        bcos::crypto::NodeIDPtr m_dstNodeID;
        std::shared_ptr<P2PMessage> m_p2pMessage;
        std::shared_ptr<P2PInterface> m_p2pInterface;
        ErrorRespFunc m_respFunc;
    };

    auto retry = std::make_shared<Retry>();
    retry->m_p2pMessage = newP2PMessage(_groupID, _srcNodeID, _dstNodeID, _payload);
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
void Gateway::asyncSendMessageByNodeIDs(const std::string& _groupID,
    bcos::crypto::NodeIDPtr _srcNodeID, const bcos::crypto::NodeIDs& _dstNodeIDs,
    bytesConstRef _payload)
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


bool Gateway::asyncBroadcastMessageToLocalNodes(
    const std::string& _groupID, bcos::crypto::NodeIDPtr _srcNodeID, bytesConstRef _payload)
{
    auto frontServiceInfos = m_gatewayNodeManager->groupFrontServices(_groupID);
    if (frontServiceInfos.size() == 0)
    {
        return false;
    }
    for (auto const& it : frontServiceInfos)
    {
        auto frontService = it.second->frontService();
        auto dstNodeID = it.first;
        frontService->onReceiveMessage(
            _groupID, _srcNodeID, _payload, [_srcNodeID, dstNodeID](Error::Ptr _error) {
                if (_error)
                {
                    GATEWAY_LOG(ERROR)
                        << LOG_DESC("asyncBroadcastMessageToLocalNodes error")
                        << LOG_KV("src", _srcNodeID->hex()) << LOG_KV("dst", dstNodeID)
                        << LOG_KV("code", _error->errorCode())
                        << LOG_KV("msg", _error->errorMessage());
                }
            });
    }
    return true;
}
/**
 * @brief: send message to all nodes
 * @param _groupID: groupID
 * @param _srcNodeID: the sender nodeID
 * @param _payload: message content
 * @return void
 */
void Gateway::asyncSendBroadcastMessage(
    const std::string& _groupID, bcos::crypto::NodeIDPtr _srcNodeID, bytesConstRef _payload)
{
    std::set<P2pID> p2pIDs;
    auto ret = asyncBroadcastMessageToLocalNodes(_groupID, _srcNodeID, _payload);
    if (!m_gatewayNodeManager->queryP2pIDsByGroupID(_groupID, p2pIDs))
    {
        if (!ret)
        {
            GATEWAY_LOG(ERROR) << LOG_DESC(
                                      "could not find a gateway "
                                      "to send this broadcast message")
                               << LOG_KV("groupID", _groupID)
                               << LOG_KV("srcNodeID", _srcNodeID->hex());
        }
        return;
    }

    auto p2pMessage = newP2PMessage(_groupID, _srcNodeID, _payload);
    for (const P2pID& p2pID : p2pIDs)
    {
        m_p2pInterface->asyncSendMessageByNodeID(p2pID, p2pMessage, CallbackFuncWithSession());
    }

    GATEWAY_LOG(TRACE) << "asyncSendBroadcastMessage send message" << LOG_KV("groupID", _groupID);
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
void Gateway::onReceiveP2PMessage(const std::string& _groupID, bcos::crypto::NodeIDPtr _srcNodeID,
    bcos::crypto::NodeIDPtr _dstNodeID, bytesConstRef _payload, ErrorRespFunc _errorRespFunc)
{
    bcos::front::FrontServiceInterface::Ptr frontServiceInterface =
        m_gatewayNodeManager->queryFrontServiceInterfaceByGroupIDAndNodeID(_groupID, _dstNodeID);
    if (!frontServiceInterface)
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

    frontServiceInterface->onReceiveMessage(_groupID, _srcNodeID, _payload,
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

/**
 * @brief: receive group broadcast message
 * @param _groupID: groupID
 * @param _srcNodeID: the sender nodeID
 * @param _payload: message payload
 * @return void
 */
void Gateway::onReceiveBroadcastMessage(
    const std::string& _groupID, bcos::crypto::NodeIDPtr _srcNodeID, bytesConstRef _payload)
{
    auto frontServiceInterfaces =
        m_gatewayNodeManager->queryFrontServiceInterfaceByGroupID(_groupID);
    for (const auto& frontServiceInterface : frontServiceInterfaces)
    {
        frontServiceInterface->onReceiveMessage(
            _groupID, _srcNodeID, _payload, [_groupID, _srcNodeID](Error::Ptr _error) {
                GATEWAY_LOG(TRACE)
                    << LOG_DESC("onReceiveBroadcastMessage callback") << LOG_KV("groupID", _groupID)
                    << LOG_KV("srcNodeID", _srcNodeID->hex())
                    << LOG_KV("code", (_error ? _error->errorCode() : 0))
                    << LOG_KV("msg", (_error ? _error->errorMessage() : ""));
            });
    }
}

void Gateway::asyncNotifyGroupInfo(
    bcos::group::GroupInfo::Ptr _groupInfo, std::function<void(Error::Ptr&&)> _callback)
{
    m_gatewayNodeManager->updateFrontServiceInfo(_groupInfo);
    if (_callback)
    {
        _callback(nullptr);
    }
}
