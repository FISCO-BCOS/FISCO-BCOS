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
bcos::task::Task<std::tuple<bytesPointer, int16_t>> AMOPImpl::onReceiveAMOPMessage(
    P2pID const& _nodeID, AMOPMessage::Ptr _msg)
{
    // AMOPRequest
    auto request = m_requestFactory->buildRequest(_msg->data());
    // message topic
    std::string topic = request->topic();
    co_return co_await onReceiveAMOPMessage(_nodeID, topic, _msg->data());
}

bcos::task::Task<std::tuple<bytesPointer, int16_t>> AMOPImpl::onReceiveAMOPMessage(
    P2pID const& _nodeID, std::string const& _topic, bytesConstRef _data)
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
        AMOP_LOG(WARNING) << LOG_BADGE("onRecvAMOPMessage")
                          << LOG_DESC("no client subscribe the topic") << LOG_KV("topic", _topic)
                          << LOG_KV("nodeID", printShortP2pID(_nodeID));
        co_return std::make_tuple(buffer, (int16_t)GatewayMessageType::AMOPMessageType);
    }

    AMOP_LOG(INFO) << LOG_DESC("onRecvAMOPMessage") << LOG_KV("topic", _topic)
                   << LOG_KV("from", printShortP2pID(_nodeID))
                   << LOG_KV("choosedClient", choosedClient);
    auto [error, responseData] = co_await clientService->notifyAMOPMessage(
        bcos::rpc::AMOPNotifyMessageType::Unicast, _topic, _data);
    if (!error || error->errorCode() == CommonError::SUCCESS)
    {
        co_return std::make_tuple(responseData, (int16_t)GatewayMessageType::WSMessageType);
    }
    auto amopMsg = m_messageFactory->buildMessage();
    amopMsg->setStatus(error->errorCode());
    amopMsg->setType(AMOPMessage::Type::AMOPResponse);
    auto const& errorMessage = error->errorMessage();
    amopMsg->setData(bytesConstRef((bcos::byte*)errorMessage.c_str(), errorMessage.size()));
    auto buffer = std::make_shared<bcos::bytes>();
    amopMsg->encode(*buffer);
    AMOP_LOG(WARNING) << LOG_DESC("notifyAMOPMessage failed")
                      << LOG_KV("code", error->errorCode()) << LOG_KV("msg", error->errorMessage());
    co_return std::make_tuple(buffer, (int16_t)GatewayMessageType::AMOPMessageType);
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
        // one detached coroutine per client: the notifies run concurrently and each only logs
        // its own failure. The payload is copied into the coroutine frame (task::wait is
        // fire-and-forget, _msg does not outlive this function).
        task::wait([](bcos::rpc::RPCInterface::Ptr _clientService, std::string _topic,
                       bcos::bytes _data, std::string _client) -> task::Task<void> {
            auto [error, responseData] = co_await _clientService->notifyAMOPMessage(
                bcos::rpc::AMOPNotifyMessageType::Broadcast, _topic, bcos::ref(_data));
            if (error)
            {
                AMOP_LOG(WARNING) << LOG_BADGE("onRecvAMOPBroadcastMessage")
                                  << LOG_DESC("notifyAMOPMessage failed")
                                  << LOG_KV("client", _client) << LOG_KV("code", error->errorCode())
                                  << LOG_KV("msg", error->errorMessage());
            }
        }(clientService, topic, bcos::bytes(_msg->data().begin(), _msg->data().end()), client));
    }
    AMOP_LOG(DEBUG) << LOG_DESC("onReceiveAMOPBroadcastMessage")
                    << LOG_KV("nodeID", printShortP2pID(_nodeID));
}

bcos::task::Task<std::optional<std::tuple<bcos::Error::Ptr, int16_t, bcos::bytes>>>
AMOPImpl::trySendTopicMessageToLocalClient(const std::string& _topic, bcos::bytesConstRef _data)
{
    std::vector<std::string> clients;
    m_topicManager->queryClientsByTopic(_topic, clients);
    if (clients.empty())
    {
        AMOP_LOG(INFO) << LOG_DESC("trySendTopicMessageToLocalClient failed for empty client")
                       << LOG_KV("topic", _topic);
        co_return std::nullopt;
    }
    AMOP_LOG(INFO) << LOG_DESC("trySendTopicMessageToLocalClient") << LOG_KV("topic", _topic)
                   << LOG_KV("clientsSubscribeTopic", clients.size());
    auto [response, type] = co_await onReceiveAMOPMessage(m_p2pNodeID, _topic, _data);
    AMOP_LOG(INFO) << LOG_DESC("trySendTopicMessageToLocalClient: receive response")
                   << LOG_KV("topic", _topic);
    // decode the AMOP response (the former onRecvAMOPResponse): an AMOPMessageType response
    // carries an encoded AMOPMessage whose status is the real error code
    bcos::Error::Ptr error = nullptr;
    if (type == bcos::gateway::GatewayMessageType::AMOPMessageType)
    {
        // zero copy overhead
        auto amopMsg = m_messageFactory->buildMessage(ref(*response));
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

        AMOP_LOG(INFO) << LOG_DESC("sendMessageByTopic error: receive responseData")
                       << LOG_KV("status", amopMsg->status()) << LOG_KV("msg", errorMessage);
    }
    AMOP_LOG(INFO) << LOG_DESC("sendMessageByTopic: receive responseData")
                   << LOG_KV("size", response->size()) << LOG_KV("type", type);
    co_return std::make_optional(std::make_tuple(std::move(error), type, *response));
}

// send message to the given topic
bcos::task::Task<std::tuple<bcos::Error::Ptr, int16_t, bcos::bytes>> AMOPImpl::sendMessageByTopic(
    const std::string& _topic, bcos::bytesConstRef _data)
{
    // keep this alive for the whole (possibly deferred) send — callers may launch the coroutine
    // detached via task::wait
    auto self = shared_from_this();
    std::vector<P2pID> nodeIDs;
    m_topicManager->queryNodeIDsByTopic(_topic, nodeIDs);
    if (nodeIDs.empty())
    {
        // no remote subscriber: try the local clients
        auto result = co_await trySendTopicMessageToLocalClient(_topic, _data);
        if (result)
        {
            co_return std::move(*result);
        }
        AMOP_LOG(WARNING) << LOG_BADGE("asyncSendMessage")
                          << LOG_DESC("there has no node subscribe the topic")
                          << LOG_KV("topic", _topic);
        co_return std::make_tuple(BCOS_ERROR_PTR(CommonError::NotFoundPeerByTopicSendMsg,
                                      "there has no node subscribe this topic, topic: " + _topic),
            (int16_t)0, bcos::bytes{});
    }
    AMOP_LOG(INFO) << LOG_DESC("sendMessageByTopic") << LOG_KV("topic", _topic)
                   << LOG_KV("nodeIDsSize", nodeIDs.size());
    auto buffer = buildAndEncodeMessage(AMOPMessage::Type::AMOPRequest, _data);

    // try to send the message to a random node and retry with the remaining nodes on failure.
    // All state lives in this coroutine frame so it stays alive for the whole (possibly
    // deferred) send.
    auto network = m_network;
    auto messageFactory = m_messageFactory;
    // finite response timeout: Options{0, true} waits forever (Session registers no timer
    // for a zero timeout), so a peer that accepts the request but never answers would suspend
    // this coroutine indefinitely — the remaining candidates would never be tried
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
    std::shuffle(nodeIDs.begin(), nodeIDs.end(), std::default_random_engine(seed));
    // build the message once and re-stamp only the seq per attempt (each attempt is a fresh
    // request on another session); the payload is moved in once instead of being lvalue-copied
    // into a rebuilt message every iteration.
    auto message =
        std::static_pointer_cast<P2PMessage>(network->messageFactory()->buildMessage());
    message->setPacketType(GatewayMessageType::AMOPMessageType);
    message->setPayload(std::move(buffer));
    while (!nodeIDs.empty())
    {
        auto choosedNodeID = nodeIDs.front();
        nodeIDs.erase(nodeIDs.begin());
        AMOP_LOG(INFO) << LOG_DESC("sendMessageByTopic")
                       << LOG_KV("choosedNodeID", printShortP2pID(choosedNodeID));
        message->setSeq(network->messageFactory()->newSeq());
        try
        {
            auto resp = co_await network->sendMessageByNodeID(choosedNodeID, *message,
                ::ranges::views::single(message->payload()), Options{c_amopResponseTimeoutMs, true});
            auto respMessage = std::dynamic_pointer_cast<P2PMessage>(resp);
            if (!respMessage)
            {
                // self-id sends and sessions expiring before the write co_return a null
                // response: treat it as a (retryable) send failure, not a silent success
                AMOP_LOG(INFO) << LOG_DESC("sendMessageByTopic: no response, retry next node")
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
                        << LOG_DESC("sendMessageByTopic: decode response failed")
                        << LOG_KV("nodeID", printShortP2pID(choosedNodeID))
                        << LOG_KV("size", responseData.size());
                    error = BCOS_ERROR_PTR(CommonError::AMOPSendMsgFailed,
                        "unable to decode the AMOP response from the peer");
                    responseData = bytesConstRef();
                }
                else
                {
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

                    AMOP_LOG(INFO) << LOG_DESC("sendMessageByTopic error: receive responseData")
                                   << LOG_KV("status", amopMsg->status())
                                   << LOG_KV("msg", errorMessage);
                }
            }
            AMOP_LOG(INFO) << LOG_DESC("sendMessageByTopic: receive responseData")
                           << LOG_KV("size", responseData.size()) << LOG_KV("type", packetType);
            co_return std::make_tuple(
                std::move(error), packetType, responseData.toBytes());
        }
        catch (NetworkException const& e)
        {
            AMOP_LOG(DEBUG) << LOG_BADGE("sendMessageByTopic")
                            << LOG_DESC("send failed, retry next node")
                            << LOG_KV("nodeID", printShortP2pID(choosedNodeID))
                            << LOG_KV("code", e.errorCode()) << LOG_KV("msg", e.what());
        }
        catch (std::exception const& e)
        {
            // a non-network throw must not escape onto the io thread that resumed this
            // coroutine: log it and fall through to the next candidate
            AMOP_LOG(WARNING) << LOG_BADGE("sendMessageByTopic")
                              << LOG_DESC("unexpected exception, retry next node")
                              << LOG_KV("nodeID", printShortP2pID(choosedNodeID))
                              << LOG_KV("msg", boost::diagnostic_information(e));
        }
    }
    // all candidate nodes failed
    co_return std::make_tuple(
        BCOS_ERROR_PTR(CommonError::AMOPSendMsgFailed, "unable to send message to peer by topic"),
        (int16_t)0, bcos::bytes{});
}

bcos::task::Task<void> AMOPImpl::sendBroadcastMessageByTopic(
    const std::string& _topic, bcos::bytesConstRef _data)
{
    // keep this alive for the whole (possibly deferred) send — callers may launch the coroutine
    // detached via task::wait
    auto self = shared_from_this();
    std::vector<std::string> nodeIDs;
    m_topicManager->queryNodeIDsByTopic(_topic, nodeIDs);
    if (nodeIDs.empty())
    {
        AMOP_LOG(WARNING) << LOG_BADGE("asyncSendBroadbastMessage")
                          << LOG_DESC("there no node subscribe this topic")
                          << LOG_KV("topic", _topic);
        co_return;
    }
    auto buffer = buildAndEncodeMessage(AMOPMessage::Type::AMOPBroadcast, _data);
    auto dataSize = _data.size();
    // a failed/unreachable node is logged and skipped by sendMessageByNodeIDs
    co_await m_network->sendMessageByNodeIDs(
        GatewayMessageType::AMOPMessageType, nodeIDs, std::move(buffer), Options(0));
    AMOP_LOG(DEBUG) << LOG_BADGE("asyncSendBroadbastMessage") << LOG_DESC("send broadcast message")
                    << LOG_KV("topic", _topic) << LOG_KV("data size", dataSize);
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
        // dispatch the request to the local client, then send the response back to the peer;
        // all state is passed as coroutine parameters so it is copied into the frame and stays
        // alive for the whole (possibly deferred) round trip
        task::wait([](std::shared_ptr<AMOPImpl> _self, P2pID _fromNodeID,
                       AMOPMessage::Ptr _amopMessage, std::shared_ptr<P2PMessage> _message)
                       -> task::Task<void> {
            auto [responseData, type] =
                co_await _self->onReceiveAMOPMessage(_fromNodeID, _amopMessage);
            auto responseP2PMsg = std::dynamic_pointer_cast<P2PMessage>(
                _self->m_network->messageFactory()->buildMessage());
            AMOP_LOG(DEBUG) << LOG_BADGE("onReceiveAMOPMessage") << LOG_DESC("send response")
                            << LOG_KV("type", type) << LOG_KV("data", responseData->size());
            responseP2PMsg->setDstP2PNodeID(_message->srcP2PNodeID());
            responseP2PMsg->setSrcP2PNodeID(_message->dstP2PNodeID());
            responseP2PMsg->setSeq(_message->seq());
            responseP2PMsg->setRespPacket();
            responseP2PMsg->setPayload(*responseData);
            responseP2PMsg->setPacketType(type);
            try
            {
                // the response payload rides as a view (zero-copy): responseP2PMsg lives in this
                // frame for the duration of the co_await
                co_await _self->m_network->sendMessageByNodeID(responseP2PMsg->dstP2PNodeID(),
                    *responseP2PMsg, ::ranges::views::single(responseP2PMsg->payload()),
                    Options{});
            }
            catch (std::exception const& e)
            {
                // A synchronous pre-send rejection (rate limit / max size) or an async
                // write failure on the AMOP response path is expected during bandwidth
                // saturation — log the dst and seq so the response-loss investigation
                // keeps the routing dimension.
                AMOP_LOG(WARNING) << LOG_BADGE("onReceiveAMOPMessage")
                                  << LOG_DESC("send response failed")
                                  << LOG_KV("seq", responseP2PMsg->seq())
                                  << LOG_KV("dst", responseP2PMsg->dstP2PNodeID())
                                  << LOG_KV("what", boost::diagnostic_information(e));
            }
        }(shared_from_this(), fromNodeID, amopMessage, _message));
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