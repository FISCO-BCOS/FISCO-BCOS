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
 * @file AMOPImpl.cpp
 * @author: octopus
 * @date 2021-10-26
 */
#include "AMOPImpl.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-framework/protocol/CommonError.h"
#include "bcos-gateway/libamop/AMOPMessage.h"
#include "bcos-gateway/libnetwork/Common.h"
#include <bcos-task/Wait.h>
#include <algorithm>
#include <chrono>
#include <random>
using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::amop;
using namespace bcos::protocol;

AMOPImpl::~AMOPImpl() = default;

TopicManager::Ptr AMOPImpl::topicManager()
{
    return m_topicManager;
}

AMOPImpl::AMOPImpl(TopicManager::Ptr _topicManager,
    bcos::amop::AMOPMessageFactory::Ptr _messageFactory, AMOPRequestFactory::Ptr _requestFactory,
    P2PInterface::Ptr _network, P2pID const& _p2pNodeID,
    boost::asio::io_context& _ioContext,
    bcos::IOServicePool::Ptr _ioServicePool)
  : m_topicManager(_topicManager),
    m_messageFactory(_messageFactory),
    m_requestFactory(_requestFactory),
    m_network(_network),
    m_p2pNodeID(_p2pNodeID),
    m_strand(std::move(_ioServicePool))
{
    m_timer = std::make_shared<Timer>(_ioContext, TOPIC_SYNC_PERIOD, "topicSync");
    m_timer->registerTimeoutHandler([this]() { broadcastTopicSeq(); });

    m_network->registerHandlerByMsgType(GatewayMessageType::AMOPMessageType,
        [this](bcos::gateway::NetworkException const& _e, bcos::gateway::P2PSession::Ptr _session,
            std::shared_ptr<bcos::gateway::P2PMessage> _message) {
            onAMOPMessage(_e, std::move(_session), std::move(_message));
        });
}

void AMOPImpl::start()
{
    m_timer->start();
    m_topicManager->start();
}

void AMOPImpl::stop()
{
    m_timer->stop();
    m_topicManager->stop();
}

void AMOPImpl::broadcastTopicSeq()
{
    auto topicSeq = std::to_string(m_topicManager->topicSeq());
    auto buffer = buildAndEncodeMessage(
        AMOPMessage::Type::TopicSeq, bytesConstRef((byte*)topicSeq.data(), topicSeq.size()));
    auto network = m_network;
    // value message held by shared_ptr: broadcastMessageToAll fans out one coroutine per peer and
    // each task keeps the message alive (zero-copy: the payload rides as a view). All state is
    // passed as coroutine parameters so it is copied into the frame and stays alive.
    task::wait([](P2PInterface::Ptr _network, bcos::bytes _payload) -> task::Task<void> {
        auto message = std::static_pointer_cast<P2PMessage>(
            _network->messageFactory()->buildMessage());
        message->setPacketType(GatewayMessageType::AMOPMessageType);
        message->setSeq(_network->messageFactory()->newSeq());
        message->setPayload(std::move(_payload));
        co_await _network->broadcastMessageToAll(
            message, ::ranges::views::single(message->payload()), Options(0));
    }(network, std::move(buffer)));
    AMOP_LOG(TRACE) << LOG_BADGE("broadcastTopicSeq") << LOG_KV("topicSeq", topicSeq);
    m_timer->restart();
}

// receive the topic seq of other nodes, and try to request the latest topic when seq falling behind
void AMOPImpl::onReceiveTopicSeqMessage(P2pID const& _nodeID, AMOPMessage::Ptr _msg)
{
    try
    {
        uint32_t topicSeq =
            boost::lexical_cast<uint32_t>(std::string(_msg->data().begin(), _msg->data().end()));
        if (!m_topicManager->checkTopicSeq(_nodeID, topicSeq))
        {
            return;
        }
        AMOP_LOG(INFO) << LOG_BADGE("onReceiveTopicSeqMessage")
                       << LOG_BADGE("try to request latest AMOP information")
                       << LOG_KV("nodeID", printShortP2pID(_nodeID))
                       << LOG_KV("topicSeq", topicSeq);

        auto buffer = buildAndEncodeMessage(AMOPMessage::Type::RequestTopic, bytesConstRef());
        auto network = m_network;
        // fire-and-forget through the coroutine fast path: the message is built in the frame and
        // the payload is moved into it (the caller's buffer does not outlive the deferred send);
        // an unreachable peer is an expected, recoverable state.
        task::wait([](P2PInterface::Ptr _network, uint16_t _type, P2pID _nodeID,
                       bcos::bytes _payload) -> task::Task<void> {
            auto message = std::static_pointer_cast<P2PMessage>(
                _network->messageFactory()->buildMessage());
            message->setPacketType(_type);
            message->setSeq(_network->messageFactory()->newSeq());
            message->setPayload(std::move(_payload));
            try
            {
                co_await _network->sendMessageByNodeID(_nodeID, *message,
                    ::ranges::views::single(message->payload()), Options(0));
            }
            catch (NetworkException const& e)
            {
                AMOP_LOG(WARNING) << LOG_BADGE("onReceiveTopicSeqMessage")
                                  << LOG_DESC("send RequestTopic failed")
                                  << LOG_KV("nodeID", printShortP2pID(_nodeID))
                                  << LOG_KV("code", e.errorCode()) << LOG_KV("msg", e.what());
            }
        }(network, GatewayMessageType::AMOPMessageType, _nodeID, std::move(buffer)));
    }
    catch (const std::exception& e)
    {
        AMOP_LOG(ERROR) << LOG_DESC("onReceiveTopicSeqMessage")
                        << LOG_KV("nodeID", printShortP2pID(_nodeID))
                        << LOG_KV("message", boost::diagnostic_information(e));
    }
}

/**
 * @brief: create message and encode the message to bytes
 * @param _type: message type
 * @param _data: message data
 * @return bcos::bytes (moved into the caller's frame, no extra copy)
 */
bcos::bytes AMOPImpl::buildAndEncodeMessage(uint32_t _type, bcos::bytesConstRef _data)
{
    auto message = m_messageFactory->buildMessage();
    message->setType(_type);
    message->setData(_data);
    bcos::bytes buffer;
    message->encode(buffer);
    return buffer;
}

// receive topic response and update the local topicManager
void AMOPImpl::onReceiveResponseTopicMessage(P2pID const& _nodeID, AMOPMessage::Ptr _msg)
{
    try
    {
        uint32_t topicSeq;
        TopicItems topicItems;
        std::string topicJson = std::string(_msg->data().begin(), _msg->data().end());
        if (m_topicManager->parseTopicItemsJson(topicSeq, topicItems, topicJson))
        {
            m_topicManager->updateSeqAndTopicsByNodeID(_nodeID, topicSeq, topicItems);
        }
    }
    catch (const std::exception& e)
    {
        AMOP_LOG(ERROR) << LOG_BADGE("onReceiveResponseTopicMessage")
                        << LOG_KV("nodeID", printShortP2pID(_nodeID))
                        << LOG_KV("message", boost::diagnostic_information(e));
    }
}

// response topic message to the given node
void AMOPImpl::onReceiveRequestTopicMessage(P2pID const& _nodeID, AMOPMessage::Ptr _msg)
{
    (void)_msg;
    try
    {
        // the current node subscribed topic info
        std::string topicJson = m_topicManager->queryTopicsSubByClient();

        AMOP_LOG(INFO) << LOG_BADGE("onReceiveRequestTopicMessage")
                       << LOG_KV("nodeID", printShortP2pID(_nodeID))
                       << LOG_KV("topicJson", topicJson);

        auto buffer = buildAndEncodeMessage(AMOPMessage::Type::ResponseTopic,
            bytesConstRef((byte*)topicJson.data(), topicJson.size()));
        auto network = m_network;
        // fire-and-forget through the coroutine fast path: the message is built in the frame and
        // the payload is moved into it (the caller's buffer does not outlive the deferred send);
        // a send failure is logged here (the old async callback only logged errors too).
        task::wait([](P2PInterface::Ptr _network, uint16_t _type, P2pID _nodeID,
                       bcos::bytes _payload) -> task::Task<void> {
            auto message = std::static_pointer_cast<P2PMessage>(
                _network->messageFactory()->buildMessage());
            message->setPacketType(_type);
            message->setSeq(_network->messageFactory()->newSeq());
            message->setPayload(std::move(_payload));
            try
            {
                co_await _network->sendMessageByNodeID(_nodeID, *message,
                    ::ranges::views::single(message->payload()), Options(0));
            }
            catch (NetworkException const& e)
            {
                AMOP_LOG(WARNING) << LOG_BADGE("onReceiveRequestTopicMessage")
                                  << LOG_DESC("send ResponseTopic failed")
                                  << LOG_KV("dstNode", printShortP2pID(_nodeID))
                                  << LOG_KV("code", e.errorCode()) << LOG_KV("msg", e.what());
            }
        }(network, GatewayMessageType::AMOPMessageType, _nodeID, std::move(buffer)));
    }
    catch (const std::exception& e)
    {
        AMOP_LOG(ERROR) << LOG_BADGE("onReceiveRequestTopicMessage")
                        << LOG_KV("nodeID", printShortP2pID(_nodeID))
                        << LOG_KV("message", boost::diagnostic_information(e));
    }
}

// receive AMOP request message from the given node
void AMOPImpl::onReceiveAMOPMessage(P2pID const& _nodeID, AMOPMessage::Ptr _msg,
    std::function<void(bytesPointer, int16_t)> const& _responseCallback)
{
    // AMOPRequest
    auto request = m_requestFactory->buildRequest(_msg->data());
    // message seq
    std::string topic = request->topic();
    onReceiveAMOPMessage(_nodeID, topic, _msg->data(), _responseCallback);
}

void AMOPImpl::onReceiveAMOPMessage(P2pID const& _nodeID, std::string const& _topic,
    bytesConstRef _data, std::function<void(bytesPointer, int16_t)> const& _responseCallback)
{
    std::vector<std::string> clients;
    m_topicManager->queryClientsByTopic(_topic, clients);
    bcos::rpc::RPCInterface::Ptr clientService = nullptr;
    std::string choosedClient;
    if (!clients.empty())
    {
        choosedClient = randomChoose(clients);
        clientService = m_topicManager->createAndGetServiceByClient(choosedClient);
    }
    if (!clientService)
    {
        auto amopMsg = m_messageFactory->buildMessage();
        auto buffer = std::make_shared<bcos::bytes>();
        amopMsg->setStatus(CommonError::NotFoundClientByTopicDispatchMsg);
        amopMsg->setType(AMOPMessage::Type::AMOPResponse);
        std::string errorMessage = "NotFoundClientByTopicDispatchMsg";
        amopMsg->setData(bytesConstRef((bcos::byte*)errorMessage.c_str(), errorMessage.size()));
        amopMsg->encode(*buffer);
        m_strand.post([buffer, _responseCallback]() {
            _responseCallback(buffer, GatewayMessageType::AMOPMessageType);
        });
        AMOP_LOG(WARNING) << LOG_BADGE("onRecvAMOPMessage")
                          << LOG_DESC("no client subscribe the topic") << LOG_KV("topic", _topic)
                          << LOG_KV("nodeID", printShortP2pID(_nodeID));
        return;
    }

    AMOP_LOG(INFO) << LOG_DESC("onRecvAMOPMessage") << LOG_KV("topic", _topic)
                   << LOG_KV("from", printShortP2pID(_nodeID))
                   << LOG_KV("choosedClient", choosedClient);
    clientService->asyncNotifyAMOPMessage(bcos::rpc::AMOPNotifyMessageType::Unicast, _topic, _data,
        [this, _responseCallback](Error::Ptr&& _error, bytesPointer _responseData) {
            if (!_error || _error->errorCode() == CommonError::SUCCESS)
            {
                _responseCallback(_responseData, GatewayMessageType::WSMessageType);
                return;
            }
            auto amopMsg = m_messageFactory->buildMessage();
            amopMsg->setStatus(_error->errorCode());
            amopMsg->setType(AMOPMessage::Type::AMOPResponse);
            auto const& errorMessage = _error->errorMessage();
            amopMsg->setData(bytesConstRef((bcos::byte*)errorMessage.c_str(), errorMessage.size()));
            auto buffer = std::make_shared<bcos::bytes>();
            amopMsg->encode(*buffer);
            _responseCallback(buffer, GatewayMessageType::AMOPMessageType);
            AMOP_LOG(WARNING) << LOG_DESC("asyncNotifyAMOPMessage failed")
                              << LOG_KV("code", _error->errorCode())
                              << LOG_KV("msg", _error->errorMessage());
        });
}

// receive the AMOP broadcast message from given node
void AMOPImpl::onReceiveAMOPBroadcastMessage(P2pID const& _nodeID, AMOPMessage::Ptr _msg)
{
    // AMOPRequest
    auto request = m_requestFactory->buildRequest(_msg->data());
    // message seq
    std::string topic = request->topic();
    std::vector<std::string> clients;
    m_topicManager->queryClientsByTopic(topic, clients);
    if (clients.empty())
    {
        AMOP_LOG(WARNING) << LOG_BADGE("onRecvAMOPBroadcastMessage")
                          << LOG_DESC("no client subscribe the topic") << LOG_KV("topic", topic)
                          << LOG_KV("from", printShortP2pID(_nodeID));
        return;
    }
    for (const auto& client : clients)
    {
        auto clientService = m_topicManager->createAndGetServiceByClient(client);
        if (!clientService)
        {
            continue;
        }
        AMOP_LOG(DEBUG) << LOG_BADGE("onRecvAMOPBroadcastMessage")
                        << LOG_DESC("push message to client") << LOG_KV("topic", topic)
                        << LOG_KV("client", client);
        clientService->asyncNotifyAMOPMessage(bcos::rpc::AMOPNotifyMessageType::Broadcast, topic,
            _msg->data(), [client](Error::Ptr&& _error, bytesPointer) {
                if (_error)
                {
                    AMOP_LOG(WARNING)
                        << LOG_BADGE("onRecvAMOPBroadcastMessage")
                        << LOG_DESC("asyncNotifyAMOPMessage failed") << LOG_KV("client", client)
                        << LOG_KV("code", _error->errorCode())
                        << LOG_KV("msg", _error->errorMessage());
                }
            });
    }
    AMOP_LOG(DEBUG) << LOG_DESC("onReceiveAMOPBroadcastMessage")
                    << LOG_KV("nodeID", printShortP2pID(_nodeID));
}

bool AMOPImpl::trySendTopicMessageToLocalClient(const std::string& _topic,
    bcos::bytesConstRef _data,
    std::function<void(bcos::Error::Ptr&&, int16_t, bytesConstRef)> _respFunc)
{
    std::vector<std::string> clients;
    m_topicManager->queryClientsByTopic(_topic, clients);
    if (clients.empty())
    {
        AMOP_LOG(INFO) << LOG_DESC("trySendTopicMessageToLocalClient failed for empty client")
                       << LOG_KV("topic", _topic);
        return false;
    }
    AMOP_LOG(INFO) << LOG_DESC("trySendTopicMessageToLocalClient") << LOG_KV("topic", _topic)
                   << LOG_KV("clientsSubscribeTopic", clients.size());
    auto self = shared_from_this();
    onReceiveAMOPMessage(m_p2pNodeID, _topic, _data,
        [self, _topic, _respFunc](bytesPointer _response, int16_t _type) {
            self->onRecvAMOPResponse(_type, _response, _respFunc);
            AMOP_LOG(INFO) << LOG_DESC("trySendTopicMessageToLocalClient: receive response")
                           << LOG_KV("topic", _topic);
        });

    return true;
}

// asyncSendMessage to the given topic
void AMOPImpl::asyncSendMessageByTopic(const std::string& _topic, bcos::bytesConstRef _data,
    std::function<void(bcos::Error::Ptr&&, int16_t, bytesConstRef)> _respFunc)
{
    std::vector<P2pID> nodeIDs;
    m_topicManager->queryNodeIDsByTopic(_topic, nodeIDs);
    if (nodeIDs.empty())
    {
        if (trySendTopicMessageToLocalClient(_topic, _data, _respFunc))
        {
            return;
        }
        auto errorPtr = BCOS_ERROR_PTR(CommonError::NotFoundPeerByTopicSendMsg,
            "there has no node subscribe this topic, topic: " + _topic);
        if (_respFunc)
        {
            _respFunc(std::move(errorPtr), 0, {});
        }

        AMOP_LOG(WARNING) << LOG_BADGE("asyncSendMessage")
                          << LOG_DESC("there has no node subscribe the topic")
                          << LOG_KV("topic", _topic);
        return;
    }
    AMOP_LOG(INFO) << LOG_DESC("asyncSendMessageByTopic") << LOG_KV("topic", _topic)
                   << LOG_KV("nodeIDsSize", nodeIDs.size());
    auto buffer = buildAndEncodeMessage(AMOPMessage::Type::AMOPRequest, _data);

    auto self = shared_from_this();
    // try to send the message to a random node and retry with the remaining nodes on failure.
    // The retry loop (a coroutine launched fire-and-forget) replaces the old recursive
    // RetrySender callback chain. All state is passed as coroutine parameters so it is copied
    // into the frame and stays alive for the whole (possibly deferred) send.
    task::wait([](std::shared_ptr<AMOPImpl> _self, bcos::bytes _payload,
                   std::vector<P2pID> _nodeIDs,
                   std::function<void(bcos::Error::Ptr&&, int16_t, bytesConstRef)> _callback)
                   -> task::Task<void> {
        auto network = _self->m_network;
        auto messageFactory = _self->m_messageFactory;
        // finite response timeout: Options{0, true} waits forever (Session registers no timer
        // for a zero timeout), so a peer that accepts the request but never answers would suspend
        // this detached coroutine indefinitely — the remaining candidates would never be tried
        // and the caller would never see the terminal AMOPSendMsgFailed. Tradeoff: a subscriber
        // that answers slower than this timeout makes the gateway retry the NEXT subscriber while
        // the first may still be processing, so one request can be delivered to (and handled by)
        // two subscribers — delivery is possibly-duplicated, not at-most-once. If deployments
        // need a different value this should become a gateway config option.
        constexpr uint32_t c_amopResponseTimeoutMs = 30000;
        // shuffle the candidate list once, then take-and-erase the front per attempt: the node
        // attempted is always the one removed, so a dead node is never retried while a live one
        // is dropped untried (randomChoose+erase(begin()) removed a different node than tried).
        auto seed = std::chrono::system_clock::now().time_since_epoch().count();
        std::shuffle(_nodeIDs.begin(), _nodeIDs.end(), std::default_random_engine(seed));
        // build the message once and re-stamp only the seq per attempt (each attempt is a fresh
        // request on another session); the payload is moved in once instead of being lvalue-copied
        // into a rebuilt message every iteration.
        auto message = std::static_pointer_cast<P2PMessage>(
            network->messageFactory()->buildMessage());
        message->setPacketType(GatewayMessageType::AMOPMessageType);
        message->setPayload(std::move(_payload));
        while (!_nodeIDs.empty())
        {
            auto choosedNodeID = _nodeIDs.front();
            _nodeIDs.erase(_nodeIDs.begin());
            AMOP_LOG(INFO) << LOG_DESC("asyncSendMessageByTopic")
                           << LOG_KV("choosedNodeID", printShortP2pID(choosedNodeID));
            message->setSeq(network->messageFactory()->newSeq());
            try
            {
                auto resp = co_await network->sendMessageByNodeID(choosedNodeID, *message,
                    ::ranges::views::single(message->payload()),
                    Options{c_amopResponseTimeoutMs, true});
                auto respMessage = std::dynamic_pointer_cast<P2PMessage>(resp);
                if (!respMessage)
                {
                    // self-id sends and sessions expiring before the write co_return a null
                    // response: treat it as a (retryable) send failure, not a silent success
                    AMOP_LOG(INFO)
                        << LOG_DESC("asyncSendMessageByTopic: no response, retry next node")
                        << LOG_KV("nodeID", printShortP2pID(choosedNodeID));
                    continue;
                }
                auto packetType = respMessage->packetType();
                auto responseData = respMessage->payload();
                bcos::Error::Ptr error = nullptr;
                if (packetType == bcos::gateway::GatewayMessageType::AMOPMessageType)
                {
                    // zero copy overhead
                    auto amopMsg = messageFactory->buildMessage();
                    if (amopMsg->decode(responseData) < 0)
                    {
                        // the peer answered, so the request was DELIVERED and its handler ran —
                        // retrying the next subscriber would replay the same payload (instant
                        // at-least-once, worse than the documented 30s-timeout duplicate). Fail
                        // the caller instead; retry is reserved for sends that never reached a
                        // peer (null response / send exception).
                        AMOP_LOG(WARNING)
                            << LOG_DESC("asyncSendMessageByTopic: decode response failed")
                            << LOG_KV("nodeID", printShortP2pID(choosedNodeID))
                            << LOG_KV("size", responseData.size());
                        error = BCOS_ERROR_PTR(CommonError::AMOPSendMsgFailed,
                            "unable to decode the AMOP response from the peer");
                        responseData = bytesConstRef();
                    }
                    else
                    {
                        auto errorMessage =
                            std::string(amopMsg->data().begin(), amopMsg->data().end());
                        auto errorCode = amopMsg->status();
                        // tars error
                        if (amopMsg->status() == (uint16_t)(-8) ||
                            amopMsg->status() == (uint16_t)(-7))
                        {
                            errorMessage =
                                "Access to the remote RPC service timed out, please make sure it "
                                "is online";
                            errorCode = -1;
                        }
                        error = BCOS_ERROR_PTR(errorCode, errorMessage);

                        AMOP_LOG(INFO)
                            << LOG_DESC("asyncSendMessageByTopic error: receive responseData")
                            << LOG_KV("status", amopMsg->status()) << LOG_KV("msg", errorMessage);
                    }
                }
                if (_callback)
                {
                    AMOP_LOG(INFO)
                        << LOG_DESC("asyncSendMessageByTopic: receive responseData")
                        << LOG_KV("size", responseData.size()) << LOG_KV("type", packetType);
                    try
                    {
                        _callback(std::move(error), packetType, responseData);
                    }
                    catch (std::exception const& e)
                    {
                        AMOP_LOG(WARNING)
                            << LOG_DESC("asyncSendMessageByTopic: response callback exception")
                            << LOG_KV("msg", boost::diagnostic_information(e));
                    }
                }
                co_return;
            }
            catch (NetworkException const& e)
            {
                AMOP_LOG(DEBUG) << LOG_BADGE("asyncSendMessageByTopic")
                                << LOG_DESC("send failed, retry next node")
                                << LOG_KV("nodeID", printShortP2pID(choosedNodeID))
                                << LOG_KV("code", e.errorCode()) << LOG_KV("msg", e.what());
            }
            catch (std::exception const& e)
            {
                // a non-network throw must not escape onto the io thread that resumed this
                // detached coroutine (aborting the process and losing the terminal callback):
                // log it and fall through to the next candidate
                AMOP_LOG(WARNING) << LOG_BADGE("asyncSendMessageByTopic")
                                  << LOG_DESC("unexpected exception, retry next node")
                                  << LOG_KV("nodeID", printShortP2pID(choosedNodeID))
                                  << LOG_KV("msg", boost::diagnostic_information(e));
            }
        }
        // all candidate nodes failed
        auto errorPtr = BCOS_ERROR_PTR(
            CommonError::AMOPSendMsgFailed, "unable to send message to peer by topic");
        if (_callback)
        {
            try
            {
                _callback(std::move(errorPtr), 0, {});
            }
            catch (std::exception const& e)
            {
                AMOP_LOG(WARNING)
                    << LOG_DESC("asyncSendMessageByTopic: failure callback exception")
                    << LOG_KV("msg", boost::diagnostic_information(e));
            }
        }
    }(self, std::move(buffer), nodeIDs, _respFunc));
}

void AMOPImpl::onRecvAMOPResponse(int16_t _type, bytesPointer _responseData,
    std::function<void(bcos::Error::Ptr&&, int16_t, bytesConstRef)> _callback)
{
    bcos::Error::Ptr error = nullptr;
    if (_type == bcos::gateway::GatewayMessageType::AMOPMessageType)
    {
        // zero copy overhead
        auto amopMsg = m_messageFactory->buildMessage(ref(*_responseData));
        auto errorMessage = std::string(amopMsg->data().begin(), amopMsg->data().end());
        auto errorCode = amopMsg->status();
        // tars error
        if (amopMsg->status() == (uint16_t)(-8) || amopMsg->status() == (uint16_t)(-7))
        {
            errorMessage =
                "Access to the remote RPC service timed out, please make sure it "
                "is online";
            errorCode = -1;
        }
        error = BCOS_ERROR_PTR(errorCode, errorMessage);

        AMOP_LOG(INFO) << LOG_DESC("asyncSendMessageByTopic error: receive responseData")
                       << LOG_KV("status", amopMsg->status()) << LOG_KV("msg", errorMessage);
    }
    if (_callback)
    {
        AMOP_LOG(INFO) << LOG_DESC("asyncSendMessageByTopic: receive responseData")
                       << LOG_KV("size", _responseData->size()) << LOG_KV("type", _type);
        _callback(std::move(error), _type, bcos::ref(*_responseData));
    }
}

void AMOPImpl::asyncSendBroadcastMessageByTopic(
    const std::string& _topic, bcos::bytesConstRef _data)
{
    std::vector<std::string> nodeIDs;
    m_topicManager->queryNodeIDsByTopic(_topic, nodeIDs);
    if (nodeIDs.empty())
    {
        AMOP_LOG(WARNING) << LOG_BADGE("asyncSendBroadbastMessage")
                          << LOG_DESC("there no node subscribe this topic")
                          << LOG_KV("topic", _topic);
        return;
    }
    auto buffer = buildAndEncodeMessage(AMOPMessage::Type::AMOPBroadcast, _data);
    auto network = m_network;
    // fire-and-forget through the coroutine fast path: the message is built in the coroutine and
    // the payload is moved into it (the caller's buffer does not outlive the deferred send); a
    // failed/unreachable node is logged and skipped by sendMessageByNodeIDs.
    task::wait([](P2PInterface::Ptr _network, uint16_t _type, std::vector<P2pID> _nodeIDs,
                   bcos::bytes _payload) -> task::Task<void> {
        co_await _network->sendMessageByNodeIDs(_type, _nodeIDs, std::move(_payload), Options(0));
    }(network, GatewayMessageType::AMOPMessageType, nodeIDs, std::move(buffer)));
    AMOP_LOG(DEBUG) << LOG_BADGE("asyncSendBroadbastMessage") << LOG_DESC("send broadcast message")
                    << LOG_KV("topic", _topic) << LOG_KV("data size", _data.size());
}

void AMOPImpl::onAMOPMessage(
    NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _message)
{
    auto self = std::weak_ptr<AMOPImpl>(shared_from_this());
    m_strand.post([self, _e, _session, _message]() {
        auto amop = self.lock();
        if (!amop)
        {
            return;
        }
        try
        {
            amop->dispatcherAMOPMessage(_e, _session, _message);
        }
        catch (std::exception const& e)
        {
            AMOP_LOG(WARNING) << LOG_DESC("dispatcher AMOPMessage exception")
                              << LOG_KV("message", boost::diagnostic_information(e));
        }
    });
}

void AMOPImpl::dispatcherAMOPMessage(
    NetworkException const& _e, P2PSession::Ptr _session, std::shared_ptr<P2PMessage> _message)
{
    if (_e.errorCode() != 0 || !_message)
    {
        AMOP_LOG(WARNING) << LOG_DESC("onAMOPMessage error for NetworkException")
                          << LOG_KV("message", _e.what()) << LOG_KV("code", _e.errorCode());
        return;
    }
    if (_message->packetType() != GatewayMessageType::AMOPMessageType)
    {
        return;
    }
    // zero copy overhead
    auto amopMessage = m_messageFactory->buildMessage(_message->payload());
    auto amopMsgType = amopMessage->type();
    auto fromNodeID =
        _message->srcP2PNodeID().empty() ? _session->p2pID() : _message->srcP2PNodeID();
    switch (amopMsgType)
    {
    case AMOPMessage::Type::TopicSeq:
        onReceiveTopicSeqMessage(fromNodeID, amopMessage);
        break;
    case AMOPMessage::Type::RequestTopic:
        onReceiveRequestTopicMessage(fromNodeID, amopMessage);
        break;
    case AMOPMessage::Type::ResponseTopic:
        onReceiveResponseTopicMessage(fromNodeID, amopMessage);
        break;
    case AMOPMessage::Type::AMOPRequest:
        onReceiveAMOPMessage(fromNodeID, amopMessage,
            [this, _session, _message](bytesPointer _responseData, int16_t _type) {
                auto responseP2PMsg = std::dynamic_pointer_cast<P2PMessage>(
                    m_network->messageFactory()->buildMessage());
                AMOP_LOG(DEBUG) << LOG_BADGE("onReceiveAMOPMessage") << LOG_DESC("send response")
                                << LOG_KV("type", _type) << LOG_KV("data", _responseData->size());
                responseP2PMsg->setDstP2PNodeID(_message->srcP2PNodeID());
                responseP2PMsg->setSrcP2PNodeID(_message->dstP2PNodeID());
                responseP2PMsg->setSeq(_message->seq());
                responseP2PMsg->setRespPacket();
                responseP2PMsg->setPayload(*_responseData);
                responseP2PMsg->setPacketType(_type);
                // send the AMOP response through the coroutine fast path: the message is passed as
                // a coroutine parameter so it is copied into the frame and stays alive for the
                // (possibly deferred) send; its payload rides as a view (zero-copy).
                auto network = m_network;
                task::wait([](P2PInterface::Ptr _network,
                               std::shared_ptr<P2PMessage> _responseP2PMsg) -> task::Task<void> {
                    try
                    {
                        co_await _network->sendMessageByNodeID(_responseP2PMsg->dstP2PNodeID(),
                            *_responseP2PMsg,
                            ::ranges::views::single(_responseP2PMsg->payload()), Options{});
                    }
                    catch (std::exception const& e)
                    {
                        // A synchronous pre-send rejection (rate limit / max size) or an async
                        // write failure on the AMOP response path is expected during bandwidth
                        // saturation — log the dst and seq so the response-loss investigation
                        // keeps the routing dimension.
                        AMOP_LOG(WARNING) << LOG_BADGE("onReceiveAMOPMessage")
                                          << LOG_DESC("send response failed")
                                          << LOG_KV("seq", _responseP2PMsg->seq())
                                          << LOG_KV("dst", _responseP2PMsg->dstP2PNodeID())
                                          << LOG_KV("what", boost::diagnostic_information(e));
                    }
                }(network, responseP2PMsg));
            });
        break;
    case AMOPMessage::Type::AMOPBroadcast:
        onReceiveAMOPBroadcastMessage(fromNodeID, amopMessage);
        break;
    default:
        AMOP_LOG(WARNING) << LOG_DESC("unknown AMOP message type") << LOG_KV("type", amopMsgType);
    }
}

void AMOPImpl::asyncSubscribeTopic(std::string const& _clientID, std::string const& _topicInfo,
    std::function<void(Error::Ptr&&)> _callback)
{
    m_topicManager->subTopic(_clientID, _topicInfo);
    if (!_callback)
    {
        return;
    }
    _callback(nullptr);
}

void AMOPImpl::asyncRemoveTopic(std::string const& _clientID,
    std::vector<std::string> const& _topicList, std::function<void(Error::Ptr&&)> _callback)
{
    m_topicManager->removeTopics(_clientID, _topicList);
    if (!_callback)
    {
        return;
    }
    _callback(nullptr);
}