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
 * @file AMOP.h
 * @author: octopus
 * @date 2021-08-23
 */
#pragma once

#include <bcos-boostssl/interfaces/MessageFace.h>
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-cpp-sdk/amop/AMOPInterface.h>
#include <bcos-cpp-sdk/amop/AMOPRequest.h>
#include <bcos-cpp-sdk/amop/TopicManager.h>
#include <unordered_map>

namespace bcos
{
namespace cppsdk
{
namespace amop
{
class AMOP : public AMOPInterface
{
public:
    using Ptr = std::shared_ptr<AMOP>;
    using UniquePtr = std::unique_ptr<AMOP>;

    virtual ~AMOP() { stop(); }

public:
    virtual void start() override;
    virtual void stop() override;

public:
    // subscribe topics
    virtual void subscribe(const std::set<std::string>& _topics) override;
    // subscribe topics
    virtual void unsubscribe(const std::set<std::string>& _topics) override;
    // subscribe topic with callback
    virtual void subscribe(const std::string& _topic, SubCallback _callback) override;
    // publish message
    virtual void publish(const std::string& _topic, bcos::bytesConstRef _data, uint32_t timeout,
        PubCallback _callback) override;
    // broadcast message
    virtual void broadcast(const std::string& _topic, bcos::bytesConstRef _data) override;
    //
    virtual void sendResponse(
        const std::string& _endPoint, const std::string& _seq, bcos::bytesConstRef _data) override;
    // query all subscribed topics
    virtual void querySubTopics(std::set<std::string>& _topics) override;
    // set default callback
    virtual void setSubCallback(SubCallback _callback) override { m_callback = _callback; }
    virtual SubCallback subCallback() const { return m_callback; }

    void updateTopicsToRemote();
    void updateTopicsToRemote(std::shared_ptr<bcos::boostssl::ws::WsSession> _session);

public:
    void onRecvAMOPRequest(std::shared_ptr<bcos::boostssl::MessageFace> _msg,
        std::shared_ptr<bcos::boostssl::ws::WsSession> _session);
    void onRecvAMOPResponse(std::shared_ptr<bcos::boostssl::MessageFace> _msg,
        std::shared_ptr<bcos::boostssl::ws::WsSession> _session);
    void onRecvAMOPBroadcast(std::shared_ptr<bcos::boostssl::MessageFace> _msg,
        std::shared_ptr<bcos::boostssl::ws::WsSession> _session);

public:
    std::shared_ptr<bcos::boostssl::ws::WsMessageFactory> messageFactory() const
    {
        return m_messageFactory;
    }
    void setMessageFactory(std::shared_ptr<bcos::boostssl::ws::WsMessageFactory> _messageFactory)
    {
        m_messageFactory = _messageFactory;
    }

    std::shared_ptr<bcos::protocol::AMOPRequestFactory> requestFactory() const
    {
        return m_requestFactory;
    }
    void setRequestFactory(std::shared_ptr<bcos::protocol::AMOPRequestFactory> _requestFactory)
    {
        m_requestFactory = _requestFactory;
    }

    std::shared_ptr<TopicManager> topicManager() const { return m_topicManager; }
    void setTopicManager(std::shared_ptr<TopicManager> _topicManager)
    {
        m_topicManager = _topicManager;
    }

    std::shared_ptr<bcos::boostssl::ws::WsService> service() const { return m_service; }
    void setService(std::shared_ptr<bcos::boostssl::ws::WsService> _service)
    {
        m_service = _service;
    }

    void addTopicCallback(const std::string& _topic, SubCallback _callback)
    {
        boost::unique_lock<boost::shared_mutex> lock(x_topic2Callback);
        m_topic2Callback[_topic] = _callback;
    }

    SubCallback getCallbackByTopic(const std::string& _topic)
    {
        boost::shared_lock<boost::shared_mutex> lock(x_topic2Callback);
        auto it = m_topic2Callback.find(_topic);
        if (it == m_topic2Callback.end())
        {
            return nullptr;
        }
        return it->second;
    }

private:
    SubCallback m_callback;
    std::shared_ptr<TopicManager> m_topicManager;
    std::shared_ptr<bcos::boostssl::ws::WsMessageFactory> m_messageFactory;
    std::shared_ptr<bcos::protocol::AMOPRequestFactory> m_requestFactory;

    mutable boost::shared_mutex x_topic2Callback;
    std::unordered_map<std::string, SubCallback> m_topic2Callback;

    std::shared_ptr<bcos::boostssl::ws::WsService> m_service;
};
}  // namespace amop
}  // namespace cppsdk
}  // namespace bcos
