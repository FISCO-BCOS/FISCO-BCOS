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
 * @file AMOP.cpp
 * @author: octopus
 * @date 2021-08-23
 */
#include <bcos-boostssl/interfaces/MessageFace.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-cpp-sdk/amop/AMOP.h>
#include <bcos-cpp-sdk/amop/Common.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <json/json.h>
#include <memory>


using namespace bcos;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;
using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::amop;

void AMOP::start()
{
    if (m_service)
    {  // start websocket service
        m_service->start();
    }
    else
    {
        AMOP_CLIENT(WARNING) << LOG_BADGE("start")
                             << LOG_DESC("websocket service is not uninitialized");
    }

    AMOP_CLIENT(INFO) << LOG_BADGE("start") << LOG_DESC("start amop");
}
void AMOP::stop()
{
    AMOP_CLIENT(INFO) << LOG_BADGE("stop") << LOG_DESC("stop amop");
}

// subscribe topics
void AMOP::subscribe(const std::set<std::string>& _topics)
{
    // add topics to manager and update topics to server
    auto result = m_topicManager->addTopics(_topics);
    if (result)
    {
        updateTopicsToRemote();
    }
}

// subscribe topics
void AMOP::unsubscribe(const std::set<std::string>& _topics)
{
    // add topics to manager
    auto result = m_topicManager->removeTopics(_topics);
    if (result)
    {
        updateTopicsToRemote();
    }
}

// query all subscribed topics
void AMOP::querySubTopics(std::set<std::string>& _topics)
{
    _topics = m_topicManager->topics();
    AMOP_CLIENT(INFO) << LOG_BADGE("querySubTopics") << LOG_KV("topics size", _topics.size());
}

// subscribe topic with callback
void AMOP::subscribe(const std::string& _topic, SubCallback _callback)
{
    auto r = m_topicManager->addTopic(_topic);
    addTopicCallback(_topic, _callback);
    if (r)
    {
        updateTopicsToRemote();
    }

    AMOP_CLIENT(INFO) << LOG_BADGE("subscribe") << LOG_DESC("subscribe topic with callback")
                      << LOG_KV("topic", _topic) << LOG_KV("r", r);
}

//
void AMOP::sendResponse(
    const std::string& _endPoint, const std::string& _seq, bcos::bytesConstRef _data)
{
    auto msg = m_messageFactory->buildMessage();
    msg->setSeq(_seq);
    msg->setPayload(std::make_shared<bytes>(_data.begin(), _data.end()));
    msg->setPacketType(bcos::cppsdk::amop::MessageType::AMOP_RESPONSE);

    m_service->asyncSendMessageByEndPoint(_endPoint, msg);
}

// publish message
void AMOP::publish(
    const std::string& _topic, bcos::bytesConstRef _data, uint32_t _timeout, PubCallback _callback)
{
    auto request = m_requestFactory->buildRequest();
    request->setTopic(_topic);
    request->setData(_data);

    auto buffer = std::make_shared<bytes>();
    request->encode(*buffer);

    auto sendMsg = m_messageFactory->buildMessage();
    sendMsg->setSeq(m_messageFactory->newSeq());
    sendMsg->setPacketType(bcos::cppsdk::amop::MessageType::AMOP_REQUEST);
    sendMsg->setPayload(buffer);

    auto sendBuffer = std::make_shared<bytes>();
    sendMsg->encode(*sendBuffer);

    AMOP_CLIENT(TRACE) << LOG_BADGE("publish") << LOG_DESC("publish message")
                       << LOG_KV("topic", _topic);
    m_service->asyncSendMessage(sendMsg, bcos::boostssl::ws::Options(_timeout),
        [_callback](Error::Ptr _error, std::shared_ptr<bcos::boostssl::MessageFace> _msg,
            std::shared_ptr<bcos::boostssl::ws::WsSession> _session) {
            auto wsMessage = std::dynamic_pointer_cast<WsMessage>(_msg);
            if (!_error && wsMessage && wsMessage->status() != 0)
            {
                auto errorNew = std::make_shared<Error>();
                errorNew->setErrorCode(wsMessage->status());
                errorNew->setErrorMessage(
                    std::string(wsMessage->payload()->begin(), wsMessage->payload()->end()));

                AMOP_CLIENT(WARNING) << LOG_BADGE("publish") << LOG_DESC("publish response error")
                                     << LOG_KV("errorCode", errorNew->errorCode())
                                     << LOG_KV("errorMessage", errorNew->errorMessage());

                _error = errorNew;
            }

            _callback(_error, wsMessage, _session);
        });
}

// broadcast message
void AMOP::broadcast(const std::string& _topic, bcos::bytesConstRef _data)
{
    auto request = m_requestFactory->buildRequest();
    request->setTopic(_topic);
    request->setData(_data);

    auto buffer = std::make_shared<bytes>();
    request->encode(*buffer);

    auto sendMsg = m_messageFactory->buildMessage();
    sendMsg->setSeq(m_messageFactory->newSeq());
    sendMsg->setPacketType(bcos::cppsdk::amop::MessageType::AMOP_BROADCAST);
    sendMsg->setPayload(buffer);

    auto sendBuffer = std::make_shared<bytes>();
    sendMsg->encode(*sendBuffer);

    AMOP_CLIENT(TRACE) << LOG_BADGE("broadcast") << LOG_DESC("broadcast message")
                       << LOG_KV("topic", _topic);
    m_service->broadcastMessage(sendMsg);
}


void AMOP::updateTopicsToRemote()
{
    auto ss = m_service->sessions();
    for (auto session : ss)
    {
        updateTopicsToRemote(session);
    }
}

void AMOP::updateTopicsToRemote(std::shared_ptr<bcos::boostssl::ws::WsSession> _session)
{
    std::string request = m_topicManager->toJson();
    auto msg = m_messageFactory->buildMessage();
    msg->setSeq(m_messageFactory->newSeq());
    msg->setPacketType(bcos::cppsdk::amop::MessageType::AMOP_SUBTOPIC);
    msg->setPayload(std::make_shared<bytes>(request.begin(), request.end()));

    _session->asyncSendMessage(msg);

    AMOP_CLIENT(INFO) << LOG_BADGE("updateTopicsToRemote")
                      << LOG_DESC("send subscribe message to server")
                      << LOG_KV("endpoint", _session->endPoint()) << LOG_KV("topics", request);
}

void AMOP::onRecvAMOPRequest(std::shared_ptr<boostssl::MessageFace> _msg,
    std::shared_ptr<bcos::boostssl::ws::WsSession> _session)
{
    auto seq = _msg->seq();
    auto request = m_requestFactory->buildRequest();
    auto ret =
        request->decode(bcos::bytesConstRef(_msg->payload()->data(), _msg->payload()->size()));
    if (ret < 0)
    {
        AMOP_CLIENT(WARNING) << LOG_BADGE("onRecvAMOPRequest")
                             << LOG_DESC("decode amop request message error")
                             << LOG_KV("endpoint", _session->endPoint()) << LOG_KV("seq", seq);
        return;
    }
    auto topic = request->topic();
    // AMOP_CLIENT(INFO) << LOG_DESC("onRecvAMOPRequest")
    //                         << LOG_KV("endpoint", _session->endPoint())
    //                         << LOG_KV("data size", data->size());

    auto callback = getCallbackByTopic(topic);
    if (!callback && m_callback)
    {
        callback = m_callback;
    }

    if (callback)
    {
        callback(nullptr, _session->connectedEndPoint(), seq, request->data(), _session);
    }
    else
    {
        AMOP_CLIENT(WARNING) << LOG_BADGE("onRecvAMOPRequest")
                             << LOG_DESC("there has no callback register for the topic")
                             << LOG_KV("topic", topic);
    }
}

void AMOP::onRecvAMOPResponse(std::shared_ptr<boostssl::MessageFace> _msg,
    std::shared_ptr<bcos::boostssl::ws::WsSession> _session)
{
    auto seq = _msg->seq();
    AMOP_CLIENT(WARNING) << LOG_BADGE("onRecvAMOPResponse")
                         << LOG_DESC("maybe the amop request callback timeout")
                         << LOG_KV("seq", seq) << LOG_KV("endpoint", _session->endPoint());
}

void AMOP::onRecvAMOPBroadcast(std::shared_ptr<boostssl::MessageFace> _msg,
    std::shared_ptr<bcos::boostssl::ws::WsSession> _session)
{
    auto seq = _msg->seq();
    auto request = m_requestFactory->buildRequest();
    auto ret =
        request->decode(bcos::bytesConstRef(_msg->payload()->data(), _msg->payload()->size()));
    if (ret < 0)
    {
        AMOP_CLIENT(WARNING) << LOG_BADGE("onRecvAMOPBroadcast")
                             << LOG_DESC("decode amop request message error")
                             << LOG_KV("endpoint", _session->endPoint()) << LOG_KV("seq", seq);
        return;
    }

    auto topic = request->topic();

    AMOP_CLIENT(DEBUG) << LOG_BADGE("onRecvAMOPBroadcast")
                       << LOG_KV("endpoint", _session->endPoint()) << LOG_KV("seq", seq)
                       << LOG_KV("data size", request->data().size());

    auto callback = getCallbackByTopic(topic);
    if (!callback && m_callback)
    {
        callback = m_callback;
    }

    if (callback)
    {
        callback(nullptr, _session->connectedEndPoint(), seq, request->data(), _session);
    }
    else
    {
        AMOP_CLIENT(WARNING) << LOG_BADGE("onRecvAMOPBroadcast")
                             << LOG_DESC("there has no callback register for the topic")
                             << LOG_KV("endpoint", _session->endPoint()) << LOG_KV("seq", seq)
                             << LOG_KV("topic", topic);
    }
}