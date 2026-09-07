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
#include <bcos-tars-protocol/client/GatewayServiceClient.h>

#include "AMOPClient.h"
#include "fisco-bcos-tars-service/Common/TarsUtils.h"
#include <bcos-framework/protocol/CommonError.h>
#include <bcos-protocol/amop/TopicItem.h>
#include <bcos-rpc/Common.h>
#include <bcos-task/Wait.h>

using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::boostssl::ws;
using namespace bcostars;
using namespace tars;
using namespace bcos::gateway;
using namespace bcos::protocol;

void AMOPClient::initMsgHandler()
{
    m_wsService->registerMsgHandler(AMOPClientMessageType::AMOP_SUBTOPIC,
        [this](boostssl::ws::WsMessage _msg, std::shared_ptr<boostssl::ws::WsSession> _session) {
            onRecvSubTopics(std::move(_msg), std::move(_session));
        });
    m_wsService->registerMsgHandler(AMOPClientMessageType::AMOP_REQUEST,
        [this](boostssl::ws::WsMessage _msg, std::shared_ptr<boostssl::ws::WsSession> _session) {
            onRecvAMOPRequest(std::move(_msg), std::move(_session));
        });
    m_wsService->registerMsgHandler(AMOPClientMessageType::AMOP_BROADCAST,
        [this](boostssl::ws::WsMessage _msg, std::shared_ptr<boostssl::ws::WsSession> _session) {
            onRecvAMOPBroadcast(std::move(_msg), std::move(_session));
        });
    m_wsService->registerDisconnectHandler(
        [this](std::shared_ptr<boostssl::ws::WsSession> _session) {
            onClientDisconnect(std::move(_session));
        });
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
void AMOPClient::onRecvSubTopics(boostssl::ws::WsMessage _msg, std::shared_ptr<WsSession> _session)
{
    auto topicInfo = std::string(_msg.payload().begin(), _msg.payload().end());
    auto seq = _msg.seq();

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
    boostssl::ws::WsMessage _msg, std::shared_ptr<WsSession> _session)
{
    // the message is captured by the gateway async callback below (its payload also
    // keeps the amop request bytesConstRef alive), move it to the heap
    auto msgPtr = std::make_shared<boostssl::ws::WsMessage>(std::move(_msg));
    auto seq = msgPtr->seq();
    auto amopReq = m_requestFactory->buildRequest(
        bytesConstRef(msgPtr->payload().data(), msgPtr->payload().size()));
    AMOP_CLIENT_LOG(DEBUG) << LOG_DESC("onRecvAMOPRequest") << LOG_KV("seq", seq)
                           << LOG_KV("topic", amopReq->topic());

    auto topic = amopReq->topic();
    // gateway inactivated
    if (onGatewayInactivated(*msgPtr, _session))
    {
        // try to send to local node
        if (trySendAMOPRequestToLocalNode(_session, topic, std::move(*msgPtr)))
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
    task::wait([](std::weak_ptr<AMOPClient> self, bcos::gateway::GatewayInterface::Ptr gateway,
                   std::string topic, std::shared_ptr<boostssl::ws::WsMessage> msgPtr,
                   std::shared_ptr<WsSession> session, std::string seq) -> bcos::task::Task<void> {
        try
        {
            auto [error, packetType, responseData] = co_await gateway->sendMessageByTopic(
                topic, bytesConstRef(msgPtr->payload().data(), msgPtr->payload().size()));
            auto amopClient = self.lock();
            if (!amopClient)
            {
                co_return;
            }
            bcos::boostssl::ws::WsMessage responseMsg;
            auto orgSeq = seq;
            if (error && error->errorCode() != bcos::protocol::CommonError::SUCCESS)
            {
                auto ret =
                    amopClient->trySendAMOPRequestToLocalNode(session, topic, std::move(*msgPtr));
                // to local node
                if (ret)
                {
                    co_return;
                }
                // tars timeout
                auto errorCode = error->errorCode();
                auto errorMsg = error->errorMessage();
                if ((error->errorCode() == -7) || (error->errorCode() == -8))
                {
                    errorMsg = "Access to gateway timed out, please check gateway alive";
                }
                responseMsg.setStatus(errorCode);
                // constructor the response
                responseMsg.setPayload(bcos::bytes(errorMsg.begin(), errorMsg.end()));
                // recover the seq
                responseMsg.setSeq(orgSeq);
                AMOP_CLIENT_LOG(ERROR)
                    << LOG_BADGE("onRecvAMOPRequest error")
                    << LOG_DESC("AMOP async send message callback") << LOG_KV("seq", seq)
                    << LOG_KV("code", error->errorCode())
                    << LOG_KV("msg", error->errorMessage());
                session->asyncSendMessage(responseMsg);
                co_return;
            }
            // Note: the decode function will recover m_seq of wsMessage, so it should be
            // better not set orgSeq into the responseMsg before decode
            auto size = responseMsg.decode(bcos::ref(responseData));
            AMOP_CLIENT_LOG(DEBUG)
                << LOG_BADGE("onRecvAMOPRequest")
                << LOG_DESC("AMOP async send message: receive message response for sdk")
                << LOG_KV("size", size) << LOG_KV("seq", seq)
                << LOG_KV("type", responseMsg.packetType());
            // recover the seq
            responseMsg.setSeq(orgSeq);
            session->asyncSendMessage(responseMsg);
        }
        catch (std::exception const& e)
        {
            AMOP_CLIENT_LOG(WARNING) << LOG_DESC("onRecvAMOPRequest exception")
                                     << LOG_KV("message", boost::diagnostic_information(e));
        }
    }(self, m_gateway, topic, msgPtr, _session, seq));
}

bool AMOPClient::trySendAMOPRequestToLocalNode(std::shared_ptr<WsSession> _session,
    std::string const& _topic, boostssl::ws::WsMessage _msg)
{
    // the local node has no client subscribe to the topic
    auto selectedSession = randomChooseSession(_topic);
    if (!selectedSession)
    {
        return false;
    }
    // fire-and-forget: forward the request to the selected local client and relay
    // its response back to the requester session
    task::wait([](std::shared_ptr<AMOPClient> keepAlive, std::shared_ptr<WsSession> session,
                   std::string topic, std::shared_ptr<WsSession> selectedSession,
                   boostssl::ws::WsMessage msg) -> bcos::task::Task<void> {
        try
        {
            auto [error, responseData] = co_await keepAlive->sendMessageToClient(
                topic, std::move(selectedSession), std::move(msg));
            bcos::boostssl::ws::WsMessage responseMsg;
            auto size = responseMsg.decode(ref(*responseData));
            auto seq = responseMsg.seq();
            session->asyncSendMessage(responseMsg);
            AMOP_CLIENT_LOG(DEBUG)
                << LOG_BADGE("trySendAMOPRequestToLocalNode")
                << LOG_DESC("AMOP async send message: receive message response for sdk")
                << LOG_KV("size", size) << LOG_KV("seq", seq)
                << LOG_KV("type", responseMsg.packetType());
        }
        catch (std::exception const& e)
        {
            AMOP_CLIENT_LOG(WARNING) << LOG_DESC("trySendAMOPRequestToLocalNode exception")
                                     << LOG_KV("message", boost::diagnostic_information(e));
        }
    }(shared_from_this(), _session, _topic, selectedSession, std::move(_msg)));
    return true;
}

/**
 * @brief: receive amop broadcast message from sdk
 */
void AMOPClient::onRecvAMOPBroadcast(boostssl::ws::WsMessage _msg, std::shared_ptr<WsSession>)
{
    auto seq = _msg.seq();
    auto amopReq =
        m_requestFactory->buildRequest(bytesConstRef(_msg.payload().data(), _msg.payload().size()));
    // broadcast message to the sdks connected to the local node
    broadcastAMOPMessage(amopReq->topic(), _msg);
    // broadcast messsage to sdks connected to other nodes
    // copy the payload: task::wait is fire-and-forget, the coroutine may outlive this
    // function scope, so the payload must be owned by the coroutine itself
    task::wait([](bcos::gateway::GatewayInterface::Ptr gateway, std::string topic,
                   bcos::bytes payload) -> bcos::task::Task<void> {
        co_await gateway->sendBroadcastMessageByTopic(topic, bcos::ref(payload));
    }(m_gateway, amopReq->topic(),
        bcos::bytes(_msg.payload().begin(), _msg.payload().end())));
    AMOP_CLIENT_LOG(DEBUG) << LOG_BADGE("onRecvAMOPBroadcast") << LOG_KV("seq", seq)
                           << LOG_KV("topic", amopReq->topic());
}

bcos::task::Task<std::tuple<bcos::Error::Ptr, bytesPointer>> AMOPClient::sendMessageToClient(
    std::string const& _topic, std::shared_ptr<WsSession> _selectSession,
    boostssl::ws::WsMessage _msg)
{
    // WsSession::asyncSendMessage is the callback boundary of the ws layer; its callback may fire
    // inline (disconnected / oversize / raw-mismatch) or later on the ws io thread. The bridge
    // below is safe in both cases: the callback never resumes the coroutine directly, it only
    // records the result and marks done; await_suspend runs AFTER asyncSendMessage returns and
    // either resumes the coroutine itself (callback already fired — never from inside
    // await_suspend, FIB-185) or suspends and leaves the resume to the callback.
    struct WsSendAwaitable
    {
        struct State
        {
            // 0 = idle, 1 = suspended (callback resumes), 2 = done (await_suspend resumes itself)
            std::atomic<int> sync{0};
            std::coroutine_handle<> handle;
            Error::Ptr error;
            bytesPointer data;
        };

        std::shared_ptr<WsSession> m_session;
        boostssl::ws::WsMessage m_msg;
        std::string m_topic;
        std::shared_ptr<State> m_state;

        bool await_ready() const noexcept { return false; }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> _handle)
        {
            m_state->handle = _handle;
            auto state = m_state;
            auto seq = m_msg.seq();
            m_session->asyncSendMessage(m_msg, Options(30000),
                [state, seq, topic = m_topic](bcos::Error::Ptr _error,
                    bcos::boostssl::ws::WsMessage _responseMsg, std::shared_ptr<WsSession>) {
                    if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
                    {
                        AMOP_CLIENT_LOG(WARNING)
                            << LOG_BADGE("notifyAMOPMessage")
                            << LOG_DESC("asyncSendMessage callback failed")
                            << LOG_KV("topic", topic) << LOG_KV("seq", seq)
                            << LOG_KV("code", _error->errorCode())
                            << LOG_KV("message", _error->errorMessage());
                    }

                    AMOP_CLIENT_LOG(DEBUG)
                        << LOG_BADGE("notifyAMOPMessage") << LOG_KV("seq", seq)
                        << LOG_KV("data size", _responseMsg.payload().size());
                    auto buffer = std::make_shared<bcos::bytes>();
                    if (!_error)
                    {
                        // Note: on error the response message is a default-constructed WsMessage,
                        // only encode the real response
                        _responseMsg.encode(*buffer);
                    }
                    state->error = _error ? BCOS_ERROR_PTR(_error->errorCode(),
                                               _error->errorMessage()) :
                                            bcos::Error::Ptr();
                    state->data = std::move(buffer);
                    if (state->sync.exchange(2, std::memory_order_acq_rel) == 1)
                    {
                        state->handle.resume();
                    }
                });
            if (state->sync.exchange(1, std::memory_order_acq_rel) == 2)
            {
                // the callback already completed: resume ourselves AFTER await_suspend returns
                return state->handle;
            }
            return std::noop_coroutine();
        }
        std::tuple<Error::Ptr, bytesPointer> await_resume()
        {
            return {std::move(m_state->error), std::move(m_state->data)};
        }
    };
    co_return co_await WsSendAwaitable{std::move(_selectSession), std::move(_msg), _topic,
        std::make_shared<WsSendAwaitable::State>()};
}

bcos::task::Task<std::tuple<bcos::Error::Ptr, bytesPointer>> AMOPClient::notifyAMOPMessage(
    std::string const& _topic, bytesConstRef _amopRequestData)
{
    auto clientSession = randomChooseSession(_topic);

    if (!clientSession)
    {
        bcos::boostssl::ws::WsMessage responseMessage;
        responseMessage.setStatus(bcos::protocol::CommonError::NotFoundClientByTopicDispatchMsg);
        responseMessage.setPacketType(AMOPClientMessageType::AMOP_RESPONSE);
        auto buffer = std::make_shared<bcos::bytes>();
        // Note: encode the message into buffer, response to the request-sdk
        responseMessage.encode(*buffer);
        AMOP_CLIENT_LOG(DEBUG) << LOG_BADGE("notifyAMOPMessage: no client found")
                               << LOG_KV("topic", _topic);
        co_return std::make_tuple(
            BCOS_ERROR_PTR(CommonError::NotFoundClientByTopicDispatchMsg,
                "NotFoundClientByTopicDispatchMsg"),
            buffer);
    }
    AMOP_CLIENT_LOG(DEBUG) << LOG_BADGE("notifyAMOPMessage") << LOG_KV("topic", _topic)
                           << LOG_KV("choosedSession", clientSession->endPoint());
    bcos::boostssl::ws::WsMessage requestMsg;
    // Note: WsMessage won't generate seq automatically, we should setSeq
    // manually when need trigger callback after receive response message from the client
    requestMsg.setSeq(bcos::boostssl::ws::newSeq());
    requestMsg.setPacketType(AMOPClientMessageType::AMOP_REQUEST);
    requestMsg.setPayload(bcos::bytes(_amopRequestData.begin(), _amopRequestData.end()));
    co_return co_await sendMessageToClient(_topic, std::move(clientSession), std::move(requestMsg));
}

bcos::task::Task<std::tuple<bcos::Error::Ptr, bytesPointer>> AMOPClient::notifyAMOPBroadcastMessage(
    std::string const& _topic, bytesConstRef _data)
{
    AMOP_CLIENT_LOG(DEBUG) << LOG_DESC("notifyAMOPBroadcastMessage") << LOG_KV("topic", _topic);
    bcos::boostssl::ws::WsMessage requestMsg;
    requestMsg.setPacketType(AMOPClientMessageType::AMOP_BROADCAST);
    requestMsg.setPayload(bcos::bytes(_data.begin(), _data.end()));
    broadcastAMOPMessage(_topic, requestMsg);
    co_return std::make_tuple(bcos::Error::Ptr(), bytesPointer());
}

void AMOPClient::broadcastAMOPMessage(
    std::string const& _topic, const boostssl::ws::WsMessage& _msg)
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
    auto gatewayClient = std::dynamic_pointer_cast<bcostars::GatewayServiceClient>(m_gateway);

    auto endPoints = tarsProxyAvailableEndPoints(gatewayClient->prx());
    return std::vector<tars::EndpointInfo>(endPoints.begin(), endPoints.end());
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
        auto servicePrx = bcostars::createServantProxy<bcostars::GatewayServicePrx>(
            m_gatewayServiceName, endPoint.getEndpoint());

        auto serviceClient =
            std::make_shared<GatewayServiceClient>(servicePrx, m_gatewayServiceName);
        serviceClient->asyncSubscribeTopic(
            m_clientID, topicInfo, [this, endPoint](Error::Ptr&& _error) {
                if (_error)
                {
                    AMOP_CLIENT_LOG(WARNING) << LOG_DESC("asyncSubScribeTopic failed")
                                             << LOG_KV("gateway", endPoint.getEndpoint().toString())
                                             << LOG_KV("code", _error->errorCode())
                                             << LOG_KV("msg", _error->errorMessage());
                    // set the notify topic flag to false when subscribeTopic failed
                    m_notifyTopicSuccess.store(false);
                    return;
                }
                AMOP_CLIENT_LOG(INFO) << LOG_DESC("asyncSubScribeTopic success")
                                      << LOG_KV("gateway", endPoint.getEndpoint().toString());
            });
    }
}
void AMOPClient::removeTopicFromAllNodes(std::vector<std::string> const& topicsToRemove)
{
    auto activeEndPoints = getActiveGatewayEndPoints();
    for (auto const& endPoint : activeEndPoints)
    {
        auto servicePrx = bcostars::createServantProxy<GatewayServicePrx>(
            m_gatewayServiceName, endPoint.getEndpoint());

        auto serviceClient =
            std::make_shared<GatewayServiceClient>(servicePrx, m_gatewayServiceName);
        serviceClient->asyncRemoveTopic(
            m_clientID, topicsToRemove, [topicsToRemove, endPoint](Error::Ptr&& _error) {
                AMOP_CLIENT_LOG(INFO) << LOG_DESC("asyncRemoveTopic")
                                      << LOG_KV("gateway", endPoint.getEndpoint().toString())
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
    const boostssl::ws::WsMessage& _msg, std::shared_ptr<boostssl::ws::WsSession> _session)
{
    auto activeEndPoints = getActiveGatewayEndPoints();
    // the gateway is in-activated
    if (activeEndPoints.size() > 0)
    {
        return false;
    }
    auto seq = _msg.seq();
    bcos::boostssl::ws::WsMessage responseMsg;
    // set error status
    responseMsg.setStatus(-1);
    std::string errorMsg = "error for the gateway is in-activated";
    // set errorMesg
    responseMsg.setPayload(bcos::bytes(errorMsg.begin(), errorMsg.end()));
    // set seq
    responseMsg.setSeq(seq);
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