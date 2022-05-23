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
 * @brief AMOP client
 * @file AMOPClient.cpp
 * @author: yujiechen
 * @date 2021-10-28
 */
#include "AMOPClient.h"
#include <bcos-boostssl/interfaces/MessageFace.h>
#include <bcos-framework/interfaces/gateway/GatewayTypeDef.h>
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-protocol/amop/TopicItem.h>
#include <bcos-rpc/Common.h>
#include <bcos-tars-protocol/client/GatewayServiceClient.h>
using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;
using namespace bcostars;
using namespace tars;
using namespace bcos::gateway;
using namespace bcos::protocol;

void AMOPClient::initMsgHandler()
{
    m_wsService->registerMsgHandler(AMOPClientMessageType::AMOP_SUBTOPIC,
        boost::bind(
            &AMOPClient::onRecvSubTopics, this, boost::placeholders::_1, boost::placeholders::_2));
    m_wsService->registerMsgHandler(
        AMOPClientMessageType::AMOP_REQUEST, boost::bind(&AMOPClient::onRecvAMOPRequest, this,
                                                 boost::placeholders::_1, boost::placeholders::_2));
    m_wsService->registerMsgHandler(AMOPClientMessageType::AMOP_BROADCAST,
        boost::bind(&AMOPClient::onRecvAMOPBroadcast, this, boost::placeholders::_1,
            boost::placeholders::_2));
    m_wsService->registerDisconnectHandler(
        boost::bind(&AMOPClient::onClientDisconnect, this, boost::placeholders::_1));
}

bool AMOPClient::updateTopicInfos(
    std::string const& _topicInfo, std::shared_ptr<WsSession> _session)
{
    TopicItems topicItems;
    auto ret = parseSubTopicsJson(_topicInfo, topicItems);
    if (!ret)
    {
        return false;
    }
    {
        WriteGuard l(x_topicToSessions);
        for (auto const& item : topicItems)
        {
            m_topicToSessions[item.topicName()][_session->endPoint()] = _session;
        }
    }
    return true;
}
/**
 * @brief: receive sub topic message from sdk
 */
void AMOPClient::onRecvSubTopics(
    std::shared_ptr<MessageFace> _msg, std::shared_ptr<WsSession> _session)
{
    auto topicInfo = std::string(_msg->payload()->begin(), _msg->payload()->end());
    auto seq = _msg->seq();
    if (gatewayInactivated())
    {
        AMOP_CLIENT_LOG(WARNING) << LOG_BADGE("onRecvSubTopics: the gateway in-activated")
                                 << LOG_KV("topicInfo", topicInfo)
                                 << LOG_KV("endpoint", _session->endPoint()) << LOG_KV("seq", seq);
    }
    auto ret = updateTopicInfos(topicInfo, _session);
    if (!ret)
    {
        AMOP_CLIENT_LOG(WARNING) << LOG_BADGE("onRecvSubTopics: invalid topic info")
                                 << LOG_KV("topicInfo", topicInfo)
                                 << LOG_KV("endpoint", _session->endPoint()) << LOG_KV("seq", seq);
        return;
    }
    subscribeTopicToAllNodes();
    AMOP_CLIENT_LOG(INFO) << LOG_BADGE("onRecvSubTopics") << LOG_KV("topicInfo", topicInfo)
                          << LOG_KV("endpoint", _session->endPoint()) << LOG_KV("seq", seq);
}

/**
 * @brief: receive amop request message from sdk
 */
void AMOPClient::onRecvAMOPRequest(
    std::shared_ptr<MessageFace> _msg, std::shared_ptr<WsSession> _session)
{
    auto seq = _msg->seq();
    auto amopReq = m_requestFactory->buildRequest(
        bytesConstRef(_msg->payload()->data(), _msg->payload()->size()));
    AMOP_CLIENT_LOG(DEBUG) << LOG_DESC("onRecvAMOPRequest") << LOG_KV("seq", seq)
                           << LOG_KV("topic", amopReq->topic());

    auto topic = amopReq->topic();
    // gateway inactivated
    if (onGatewayInactivated(_msg, _session))
    {
        // try to send to local node
        if (trySendAMOPRequestToLocalNode(_session, topic, _msg))
        {
            return;
        }
        AMOP_CLIENT_LOG(WARNING) << LOG_BADGE(
                                        "onRecvAMOPRequest: the gateway in-activated and try to "
                                        "request to local nodes failed")
                                 << LOG_KV("endpoint", _session->endPoint()) << LOG_KV("seq", seq);
        return;
    }
    auto self = std::weak_ptr<AMOPClient>(shared_from_this());
    m_gateway->asyncSendMessageByTopic(amopReq->topic(),
        bytesConstRef(_msg->payload()->data(), _msg->payload()->size()),
        [self, seq, _msg, topic, _session](
            bcos::Error::Ptr&& _error, int16_t, bytesPointer _responseData) {
            try
            {
                auto amopClient = self.lock();
                if (!amopClient)
                {
                    return;
                }
                auto responseMsg = amopClient->m_wsMessageFactory->buildMessage();
                auto orgSeq = seq;
                if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
                {
                    auto ret = amopClient->trySendAMOPRequestToLocalNode(_session, topic, _msg);
                    // to local node
                    if (ret)
                    {
                        return;
                    }
                    // tars timeout
                    auto errorCode = _error->errorCode();
                    auto errorMsg = _error->errorMessage();
                    if ((_error->errorCode() == -7) || (_error->errorCode() == -8))
                    {
                        errorMsg = "Access to gateway timed out, please check gateway alive";
                    }
                    std::dynamic_pointer_cast<bcos::boostssl::ws::WsMessage>(responseMsg)
                        ->setStatus(errorCode);
                    // constructor the response
                    responseMsg->setPayload(
                        std::make_shared<bytes>(errorMsg.begin(), errorMsg.end()));
                    // recover the seq
                    responseMsg->setSeq(orgSeq);
                    AMOP_CLIENT_LOG(ERROR)
                        << LOG_BADGE("onRecvAMOPRequest error")
                        << LOG_DESC("AMOP async send message callback") << LOG_KV("seq", seq)
                        << LOG_KV("code", _error->errorCode())
                        << LOG_KV("msg", _error->errorMessage());
                    _session->asyncSendMessage(responseMsg);
                    return;
                }
                // Note: the decode function will recover m_seq of wsMessage, so it should be
                // better not set orgSeq into the responseMsg before decode
                auto size = responseMsg->decode(ref(*_responseData));
                AMOP_CLIENT_LOG(DEBUG)
                    << LOG_BADGE("onRecvAMOPRequest")
                    << LOG_DESC("AMOP async send message: receive message response for sdk")
                    << LOG_KV("size", size) << LOG_KV("seq", seq)
                    << LOG_KV("type", responseMsg->packetType());
                // recover the seq
                responseMsg->setSeq(orgSeq);
                _session->asyncSendMessage(responseMsg);
            }
            catch (std::exception const& e)
            {
                AMOP_CLIENT_LOG(WARNING) << LOG_DESC("onRecvAMOPRequest exception")
                                         << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
}

bool AMOPClient::trySendAMOPRequestToLocalNode(std::shared_ptr<WsSession> _session,
    std::string const& _topic, std::shared_ptr<MessageFace> _msg)
{
    // the local node has no client subscribe to the topic
    auto selectedSession = randomChooseSession(_topic);
    if (!selectedSession)
    {
        return false;
    }
    auto self = std::weak_ptr<AMOPClient>(shared_from_this());
    sendMessageToClient(
        _topic, selectedSession, _msg, [self, _session](Error::Ptr&&, bytesPointer _responseData) {
            try
            {
                auto amopClient = self.lock();
                if (!amopClient)
                {
                    return;
                }
                auto responseMsg = amopClient->m_wsMessageFactory->buildMessage();
                auto size = responseMsg->decode(ref(*_responseData));
                _session->asyncSendMessage(responseMsg);
                AMOP_CLIENT_LOG(DEBUG)
                    << LOG_BADGE("trySendAMOPRequestToLocalNode")
                    << LOG_DESC("AMOP async send message: receive message response for sdk")
                    << LOG_KV("size", size) << LOG_KV("seq", responseMsg->seq())
                    << LOG_KV("type", responseMsg->packetType());
            }
            catch (std::exception const& e)
            {
                AMOP_CLIENT_LOG(WARNING) << LOG_DESC("trySendAMOPRequestToLocalNode exception")
                                         << LOG_KV("error", boost::diagnostic_information(e));
            }
        });
    return true;
}

/**
 * @brief: receive amop broadcast message from sdk
 */
void AMOPClient::onRecvAMOPBroadcast(std::shared_ptr<MessageFace> _msg, std::shared_ptr<WsSession>)
{
    auto seq = _msg->seq();
    auto amopReq = m_requestFactory->buildRequest(
        bytesConstRef(_msg->payload()->data(), _msg->payload()->size()));
    // broadcast message to the sdks connected to the local node
    broadcastAMOPMessage(amopReq->topic(), _msg);
    // broadcast messsage to sdks connected to other nodes
    m_gateway->asyncSendBroadbastMessageByTopic(
        amopReq->topic(), bytesConstRef(_msg->payload()->data(), _msg->payload()->size()));
    AMOP_CLIENT_LOG(DEBUG) << LOG_BADGE("onRecvAMOPBroadcast") << LOG_KV("seq", seq)
                           << LOG_KV("topic", amopReq->topic());
}

void AMOPClient::sendMessageToClient(std::string const& _topic,
    std::shared_ptr<WsSession> _selectSession, std::shared_ptr<MessageFace> _msg,
    std::function<void(Error::Ptr&&, bytesPointer)> _callback)
{
    _selectSession->asyncSendMessage(_msg, Options(30000),
        [_msg, _topic, _callback](bcos::Error::Ptr _error,
            std::shared_ptr<boostssl::MessageFace> _responseMsg,
            std::shared_ptr<WsSession> _session) {
            auto seq = _msg->seq();
            if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
            {
                AMOP_CLIENT_LOG(WARNING)
                    << LOG_BADGE("asyncNotifyAMOPMessage")
                    << LOG_DESC("asyncSendMessage callback error")
                    << LOG_KV("endpoint", (_session ? _session->endPoint() : std::string("")))
                    << LOG_KV("topic", _topic) << LOG_KV("seq", seq)
                    << LOG_KV("errorCode", _error ? _error->errorCode() : -1)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }

            AMOP_CLIENT_LOG(DEBUG)
                << LOG_BADGE("asyncNotifyAMOPMessage")
                << LOG_DESC("asyncSendMessage callback response") << LOG_KV("seq", seq)
                << LOG_KV("data size", _responseMsg ? _responseMsg->payload()->size() : 0);
            auto buffer = std::make_shared<bcos::bytes>();
            if (_responseMsg)
            {
                _responseMsg->encode(*buffer);
            }

            if (_error)
            {
                _callback(
                    std::make_shared<bcos::Error>(_error->errorCode(), _error->errorMessage()),
                    std::move(buffer));
            }
            else
            {
                _callback(nullptr, std::move(buffer));
            }
        });
}

void AMOPClient::asyncNotifyAMOPMessage(std::string const& _topic, bytesConstRef _amopRequestData,
    std::function<void(Error::Ptr&&, bytesPointer)> _callback)
{
    auto clientSession = randomChooseSession(_topic);

    if (!clientSession)
    {
        auto responseMessage =
            std::dynamic_pointer_cast<WsMessage>(m_wsMessageFactory->buildMessage());
        responseMessage->setStatus(bcos::protocol::CommonError::NotFoundClientByTopicDispatchMsg);
        responseMessage->setPacketType(AMOPClientMessageType::AMOP_RESPONSE);
        auto buffer = std::make_shared<bcos::bytes>();
        responseMessage->encode(*buffer);
        _callback(std::make_shared<Error>(CommonError::NotFoundClientByTopicDispatchMsg,
                      "NotFoundClientByTopicDispatchMsg"),
            buffer);
        AMOP_CLIENT_LOG(DEBUG) << LOG_BADGE("asyncNotifyAMOPMessage: no client found")
                               << LOG_KV("topic", _topic);
        return;
    }
    AMOP_CLIENT_LOG(DEBUG) << LOG_BADGE("asyncNotifyAMOPMessage") << LOG_KV("topic", _topic)
                           << LOG_KV("choosedSession", clientSession->endPoint());
    auto requestMsg = std::dynamic_pointer_cast<WsMessage>(m_wsMessageFactory->buildMessage());
    requestMsg->setSeq(m_wsMessageFactory->newSeq());
    requestMsg->setPacketType(AMOPClientMessageType::AMOP_REQUEST);
    auto requestPayLoad = std::make_shared<bytes>(_amopRequestData.begin(), _amopRequestData.end());
    requestMsg->setPayload(requestPayLoad);
    sendMessageToClient(_topic, clientSession, requestMsg, _callback);
}

void AMOPClient::asyncNotifyAMOPBroadcastMessage(std::string const& _topic, bytesConstRef _data,
    std::function<void(Error::Ptr&&, bytesPointer)> _callback)
{
    AMOP_CLIENT_LOG(DEBUG) << LOG_DESC("asyncNotifyAMOPBroadcastMessage")
                           << LOG_KV("topic", _topic);
    auto requestMsg = std::dynamic_pointer_cast<WsMessage>(m_wsMessageFactory->buildMessage());
    requestMsg->setPacketType(AMOPClientMessageType::AMOP_BROADCAST);
    requestMsg->setPayload(std::make_shared<bytes>(_data.begin(), _data.end()));
    broadcastAMOPMessage(_topic, requestMsg);
    if (_callback)
    {
        _callback(nullptr, nullptr);
    }
}

void AMOPClient::broadcastAMOPMessage(std::string const& _topic, std::shared_ptr<MessageFace> _msg)
{
    AMOP_CLIENT_LOG(DEBUG) << LOG_DESC("broadcastAMOPMessage") << LOG_KV("topic", _topic);
    auto sessions = querySessionsByTopic(_topic);
    for (auto const& session : sessions)
    {
        session.second->asyncSendMessage(_msg, Options(30000));
    }
}
std::shared_ptr<WsSession> AMOPClient::randomChooseSession(std::string const& _topic)
{
    ReadGuard l(x_topicToSessions);
    AMOP_CLIENT_LOG(DEBUG) << LOG_DESC("randomChooseSession:")
                           << LOG_KV("sessionSize", m_topicToSessions.size())
                           << LOG_KV("topic", _topic);
    if (!m_topicToSessions.count(_topic))
    {
        return nullptr;
    }
    std::shared_ptr<WsSession> selectedSession = nullptr;
    auto const& sessions = m_topicToSessions[_topic];
    // no client subscribe the topic
    if (sessions.size() == 0)
    {
        return selectedSession;
    }
    size_t retryTime = 0;
    do
    {
        srand(utcTime());
        auto selectedClient = rand() % sessions.size();
        auto it = sessions.begin();
        if (selectedClient > 0)
        {
            std::advance(it, selectedClient);
        }
        selectedSession = it->second;
        retryTime++;
    } while (
        (!selectedSession || !(selectedSession->isConnected())) && (retryTime <= sessions.size()));
    return selectedSession;
}

void AMOPClient::onClientDisconnect(std::shared_ptr<WsSession> _session)
{
    std::vector<std::string> topicsToRemove;
    {
        WriteGuard l(x_topicToSessions);
        for (auto it = m_topicToSessions.begin(); it != m_topicToSessions.end();)
        {
            auto& sessions = it->second;
            if (sessions.count(_session->endPoint()))
            {
                sessions.erase(_session->endPoint());
            }
            if (sessions.size() == 0)
            {
                topicsToRemove.emplace_back(it->first);
                it = m_topicToSessions.erase(it);
                continue;
            }
            it++;
        }
    }
    if (topicsToRemove.size() == 0)
    {
        return;
    }
    removeTopicFromAllNodes(topicsToRemove);
}

std::vector<tars::EndpointInfo> AMOPClient::getActiveGatewayEndPoints()
{
    vector<EndpointInfo> activeEndPoints;
    vector<EndpointInfo> nactiveEndPoints;
    auto gatewayClient = std::dynamic_pointer_cast<bcostars::GatewayServiceClient>(m_gateway);
    gatewayClient->prx()->tars_endpointsAll(activeEndPoints, nactiveEndPoints);
    return activeEndPoints;
}

void AMOPClient::subscribeTopicToAllNodes()
{
    auto activeEndPoints = getActiveGatewayEndPoints();
    auto topicInfo = generateTopicInfo();
    // set the notify topic flag to true when subscribeTopicToAllNodes
    m_notifyTopicSuccess.store(true);
    AMOP_CLIENT_LOG(INFO) << LOG_DESC("subscribeTopicToAllNodes") << LOG_KV("topicInfo", topicInfo)
                          << LOG_KV("activeEndPoints", activeEndPoints.size());
    for (auto const& endPoint : activeEndPoints)
    {
        auto endPointStr = endPointToString(m_gatewayServiceName, endPoint.getEndpoint());
        auto servicePrx =
            Application::getCommunicator()->stringToProxy<GatewayServicePrx>(endPointStr);
        auto serviceClient =
            std::make_shared<GatewayServiceClient>(servicePrx, m_gatewayServiceName);
        serviceClient->asyncSubscribeTopic(
            m_clientID, topicInfo, [this, endPointStr](Error::Ptr&& _error) {
                if (_error)
                {
                    AMOP_CLIENT_LOG(WARNING)
                        << LOG_DESC("asyncSubScribeTopic error") << LOG_KV("gateway", endPointStr)
                        << LOG_KV("code", _error->errorCode())
                        << LOG_KV("msg", _error->errorMessage());
                    // set the notify topic flag to false when subscribeTopic failed
                    m_notifyTopicSuccess.store(false);
                    return;
                }
                AMOP_CLIENT_LOG(INFO)
                    << LOG_DESC("asyncSubScribeTopic success") << LOG_KV("gateway", endPointStr);
            });
    }
}
void AMOPClient::removeTopicFromAllNodes(std::vector<std::string> const& topicsToRemove)
{
    auto activeEndPoints = getActiveGatewayEndPoints();
    for (auto const& endPoint : activeEndPoints)
    {
        auto endPointStr = endPointToString(m_gatewayServiceName, endPoint.getEndpoint());
        auto servicePrx =
            Application::getCommunicator()->stringToProxy<GatewayServicePrx>(endPointStr);
        auto serviceClient =
            std::make_shared<GatewayServiceClient>(servicePrx, m_gatewayServiceName);
        serviceClient->asyncRemoveTopic(
            m_clientID, topicsToRemove, [topicsToRemove, endPointStr](Error::Ptr&& _error) {
                AMOP_CLIENT_LOG(INFO)
                    << LOG_DESC("asyncRemoveTopic") << LOG_KV("gateway", endPointStr)
                    << LOG_KV("removedSize", topicsToRemove.size())
                    << LOG_KV("code", _error ? _error->errorCode() : 0)
                    << LOG_KV("msg", _error ? _error->errorMessage() : "success");
            });
    }
}

void AMOPClient::pingGatewayAndNotifyTopics()
{
    m_gatewayStatusDetector->restart();
    auto activeEndPoints = getActiveGatewayEndPoints();
    // the gateway become inactived from active status
    if (activeEndPoints.size() == 0)
    {
        if (m_gatewayActivated.load() == true)
        {
            AMOP_CLIENT_LOG(INFO) << LOG_DESC(
                "pingGatewayAndNotifyTopics: gateway inactived, reset the status");
            m_gatewayActivated.store(false);
        }
        return;
    }
    // the gateway in active status, return directly
    if (m_gatewayActivated.load() == true && m_notifyTopicSuccess)
    {
        return;
    }
    // if gateway become activated or notify topic failed before, should subscribeTopicToAllNodes
    subscribeTopicToAllNodes();

    AMOP_CLIENT_LOG(INFO) << LOG_DESC(
                                 "pingGatewayAndNotifyTopics: the gateway become activated from "
                                 "in-active status, re-subscribe the topics")
                          << LOG_KV("gatewayNodesSize", activeEndPoints.size())
                          << LOG_KV("topicsSize", m_topicToSessions.size());
    m_gatewayActivated.store(true);
}

bool AMOPClient::onGatewayInactivated(
    std::shared_ptr<MessageFace> _msg, std::shared_ptr<boostssl::ws::WsSession> _session)
{
    auto activeEndPoints = getActiveGatewayEndPoints();
    // the gateway is in-activated
    if (activeEndPoints.size() > 0)
    {
        return false;
    }
    auto seq = _msg->seq();
    auto responseMsg = m_wsMessageFactory->buildMessage();
    // set error status
    std::dynamic_pointer_cast<bcos::boostssl::ws::WsMessage>(responseMsg)->setStatus(-1);
    std::string errorMsg = "error for the gateway is in-activated";
    // set errorMesg
    responseMsg->setPayload(std::make_shared<bytes>(errorMsg.begin(), errorMsg.end()));
    // set seq
    responseMsg->setSeq(seq);
    _session->asyncSendMessage(responseMsg);

    AMOP_CLIENT_LOG(INFO) << LOG_DESC(
                                 "Gateway inactivated, notify error message to the client directly")
                          << LOG_KV("endPoint", _session->endPoint()) << LOG_KV("seq", seq);

    return true;
}

bool AMOPClient::gatewayInactivated()
{
    auto activeEndPoints = getActiveGatewayEndPoints();
    return (activeEndPoints.size() == 0);
}

std::string AMOPClient::generateTopicInfo()
{
    Json::Value topicInfo;
    Json::Value topicItems(Json::arrayValue);
    std::set<std::string> topicList;
    ReadGuard l(x_topicToSessions);
    for (auto const& it : m_topicToSessions)
    {
        if (topicList.count(it.first))
        {
            continue;
        }
        topicList.insert(it.first);
    }
    for (auto const& topicName : topicList)
    {
        topicItems.append(topicName);
    }
    topicInfo["topics"] = topicItems;
    return topicInfo.toStyledString();
}

void AMOPClient::asyncNotifySubscribeTopic(
    std::function<void(Error::Ptr&& _error, std::string)> _callback)
{
    auto topicInfo = generateTopicInfo();
    AMOP_CLIENT_LOG(INFO) << LOG_DESC(
                                 "Receive asyncNotifySubscribeTopic request from the gateway, "
                                 "re-subscribe topics now")
                          << LOG_KV("topic", topicInfo);
    if (_callback)
    {
        _callback(nullptr, topicInfo);
    }
}