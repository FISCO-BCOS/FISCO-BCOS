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
 * @file AMOPImpl.h
 * @author: octopus
 * @date 2021-10-26
 */
#pragma once
#include "bcos-framework/protocol/AMOPRequest.h"
#include "bcos-gateway/libamop/AMOPMessage.h"
#include "bcos-gateway/libamop/TopicManager.h"
#include "bcos-gateway/libp2p/P2PInterface.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-utilities/IOServicePool.h"
#include "bcos-utilities/Timer.h"
#include <bcos-task/Task.h>
#include <boost/asio/io_context.hpp>
#include <optional>
#include <tuple>
namespace bcos
{
namespace amop
{
class AMOPImpl : public std::enable_shared_from_this<AMOPImpl>
{
public:
    using Ptr = std::shared_ptr<AMOPImpl>;
    AMOPImpl(TopicManager::Ptr _topicManager, AMOPMessageFactory::Ptr _messageFactory,
        bcos::protocol::AMOPRequestFactory::Ptr _requestFactory,
        bcos::gateway::P2PInterface::Ptr _network, bcos::gateway::P2pID const& _p2pNodeID,
        boost::asio::io_context& _ioContext,
        bcos::IOServicePool::Ptr _ioServicePool);
    virtual ~AMOPImpl();

    virtual void start();
    virtual void stop();
    virtual void asyncSubscribeTopic(std::string const& _clientID, std::string const& _topicInfo,
        std::function<void(Error::Ptr&&)> _callback);
    virtual void asyncRemoveTopic(std::string const& _clientID,
        std::vector<std::string> const& _topicList, std::function<void(Error::Ptr&&)> _callback);

    /**
     * @brief: send message to a random node subscribed to _topic
     * @param _topic: topic
     * @param _data: message data
     * @return {error, responseType, responseData}: error is nullptr on success
     */
    virtual task::Task<std::tuple<Error::Ptr, int16_t, bcos::bytes>> sendMessageByTopic(
        const std::string& _topic, bcos::bytesConstRef _data);

    /**
     * @brief: broadcast message to all nodes subscribed to _topic
     * @param _topic: topic
     * @param _data: message data
     * @return void
     */
    virtual task::Task<void> sendBroadcastMessageByTopic(
        const std::string& _topic, bcos::bytesConstRef _data);

    virtual void onAMOPMessage(bcos::gateway::NetworkException const& _e,
        bcos::gateway::P2PSession::Ptr _session,
        std::shared_ptr<bcos::gateway::P2PMessage> _message);

    virtual TopicManager::Ptr topicManager();

protected:
    virtual void dispatcherAMOPMessage(bcos::gateway::NetworkException const& _e,
        bcos::gateway::P2PSession::Ptr _session,
        std::shared_ptr<bcos::gateway::P2PMessage> _message);
    /**
     * @brief: periodically send topicSeq to all other nodes
     * @return void
     */
    virtual void broadcastTopicSeq();

    /**
     * @brief: receive topicSeq from other nodes
     * @param _nodeID: the sender nodeID
     * @param _id: the message id
     * @param _msg: message
     * @return void
     */
    virtual void onReceiveTopicSeqMessage(
        bcos::gateway::P2pID const& _nodeID, AMOPMessage::Ptr _msg);

    /**
     * @brief: receive request topic message from other nodes
     * @param _nodeID: the sender nodeID
     * @param _id: the message id
     * @param _msg: message
     * @return void
     */
    void onReceiveRequestTopicMessage(bcos::gateway::P2pID const& _nodeID, AMOPMessage::Ptr _msg);

    /**
     * @brief: receive topic response message from other nodes
     * @param _nodeID: the sender nodeID
     * @param _id: the message id
     * @param _msg: message
     * @return void
     */
    virtual void onReceiveResponseTopicMessage(
        bcos::gateway::P2pID const& _nodeID, AMOPMessage::Ptr _msg);

    /**
     * @brief: receive amop request message from the given node and dispatch it to the local
     *         client subscribed to the topic
     * @param _nodeID: the sender nodeID
     * @param _msg: message
     * @return {responseData, responseType}: the response to send back to the sender
     */
    virtual task::Task<std::tuple<bytesPointer, int16_t>> onReceiveAMOPMessage(
        bcos::gateway::P2pID const& _nodeID, AMOPMessage::Ptr _msg);

    /**
     * @brief: receive broadcast message
     * @param _nodeID: the sender nodeID
     * @param _id: the message id
     * @param _msg: message
     * @return void
     */
    virtual void onReceiveAMOPBroadcastMessage(
        bcos::gateway::P2pID const& _nodeID, AMOPMessage::Ptr _msg);

private:
    bcos::bytes buildAndEncodeMessage(uint32_t _type, bcos::bytesConstRef _data);
    virtual task::Task<std::tuple<bytesPointer, int16_t>> onReceiveAMOPMessage(
        bcos::gateway::P2pID const& _nodeID, std::string const& _topic, bytesConstRef _data);
    /**
     * @brief: send the topic message to a local client subscribed to _topic
     * @return {error, responseType, responseData}, or nullopt when no local client subscribes
     *         the topic
     */
    task::Task<std::optional<std::tuple<Error::Ptr, int16_t, bcos::bytes>>>
        trySendTopicMessageToLocalClient(const std::string& _topic, bcos::bytesConstRef _data);

    std::shared_ptr<TopicManager> m_topicManager;
    std::shared_ptr<AMOPMessageFactory> m_messageFactory;
    std::shared_ptr<bcos::protocol::AMOPRequestFactory> m_requestFactory;
    std::shared_ptr<Timer> m_timer;
    bcos::gateway::P2PInterface::Ptr m_network;
    bcos::gateway::P2pID m_p2pNodeID;
    bcos::Strand m_strand;

    unsigned const TOPIC_SYNC_PERIOD = 2000;
};
}  // namespace amop
}  // namespace bcos