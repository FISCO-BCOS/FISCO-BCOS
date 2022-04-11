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
#include "Common.h"
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/interfaces/protocol/AMOPRequest.h>
#include <bcos-gateway/libamop/AMOPMessage.h>
#include <bcos-gateway/libamop/TopicManager.h>
#include <bcos-gateway/libp2p/P2PInterface.h>
#include <bcos-gateway/libp2p/P2PMessage.h>
#include <bcos-gateway/libp2p/P2PSession.h>
#include <bcos-utilities/ThreadPool.h>
#include <bcos-utilities/Timer.h>
#include <boost/asio.hpp>
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
        bcos::gateway::P2PInterface::Ptr _network, bcos::gateway::P2pID const& _p2pNodeID);
    virtual ~AMOPImpl() {}

    virtual void start();
    virtual void stop();
    virtual void asyncSubscribeTopic(std::string const& _clientID, std::string const& _topicInfo,
        std::function<void(Error::Ptr&&)> _callback);
    virtual void asyncRemoveTopic(std::string const& _clientID,
        std::vector<std::string> const& _topicList, std::function<void(Error::Ptr&&)> _callback);

    /**
     * @brief: async send message to random node subscribe _topic
     * @param _topic: topic
     * @param _data: message data
     * @param _respFunc: callback
     * @return void
     */
    virtual void asyncSendMessageByTopic(const std::string& _topic, bcos::bytesConstRef _data,
        std::function<void(bcos::Error::Ptr&&, int16_t, bytesPointer)> _respFunc);

    /**
     * @brief: async send message to all nodes subscribe _topic
     * @param _topic: topic
     * @param _data: message data
     * @return void
     */
    virtual void asyncSendBroadbastMessageByTopic(
        const std::string& _topic, bcos::bytesConstRef _data);

    virtual void onAMOPMessage(
        boostssl::MessageFace::Ptr _message, gateway::P2PSession::Ptr _p2pSession);

    virtual TopicManager::Ptr topicManager() { return m_topicManager; }

protected:
    virtual void dispatcherAMOPMessage(
        boostssl::MessageFace::Ptr _message, gateway::P2PSession::Ptr _p2pSession);
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
     * @brief: receive amop message
     * @param _nodeID: the sender nodeID
     * @param _id: the message id
     * @param _msg: message
     * @return void
     */
    virtual void onReceiveAMOPMessage(bcos::gateway::P2pID const& _nodeID, AMOPMessage::Ptr _msg,
        std::function<void(bytesPointer, int16_t)> const& _responseCallback);

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
    std::shared_ptr<bytes> buildAndEncodeMessage(uint32_t _type, bcos::bytesConstRef _data);
    virtual void onReceiveAMOPMessage(bcos::gateway::P2pID const& _nodeID,
        std::string const& _topic, bytesConstRef _data,
        std::function<void(bytesPointer, int16_t)> const& _responseCallback);
    void onRecvAMOPResponse(int16_t _type, bytesPointer _responseData,
        std::function<void(bcos::Error::Ptr&&, int16_t, bytesPointer)> _callback);
    bool trySendTopicMessageToLocalClient(const std::string& _topic, bcos::bytesConstRef _data,
        std::function<void(bcos::Error::Ptr&&, int16_t, bytesPointer)> _respFunc);

private:
    std::shared_ptr<TopicManager> m_topicManager;
    std::shared_ptr<AMOPMessageFactory> m_messageFactory;
    std::shared_ptr<bcos::protocol::AMOPRequestFactory> m_requestFactory;
    std::shared_ptr<Timer> m_timer;
    bcos::gateway::P2PInterface::Ptr m_network;
    bcos::gateway::P2pID m_p2pNodeID;
    ThreadPool::Ptr m_threadPool;

    unsigned const TOPIC_SYNC_PERIOD = 2000;
};
}  // namespace amop
}  // namespace bcos