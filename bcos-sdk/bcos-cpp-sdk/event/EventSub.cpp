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
 * @file EvenPush.cpp
 * @author: octopus
 * @date 2021-09-01
 */

#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-cpp-sdk/event/Common.h>
#include <bcos-cpp-sdk/event/EventSub.h>
#include <bcos-cpp-sdk/event/EventSubRequest.h>
#include <bcos-cpp-sdk/event/EventSubResponse.h>
#include <bcos-cpp-sdk/event/EventSubStatus.h>
#include <bcos-utilities/Common.h>
#include <json/reader.h>
#include <boost/thread/thread.hpp>
#include <memory>
#include <mutex>

using namespace bcos;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;
using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::event;

void EventSub::start()
{
    if (m_running)
    {
        EVENT_SUB(INFO) << LOG_BADGE("start") << LOG_DESC("event sub is running");
        return;
    }
    m_running = true;

    // start websocket service
    m_service->start();

    m_timer = std::make_shared<bcos::Timer>(m_config->reconnectPeriod(), "doLoop");
    m_timer->registerTimeoutHandler([this]() { doLoop(); });
    m_timer->start();
    EVENT_SUB(INFO) << LOG_BADGE("start") << LOG_DESC("start event sub successfully")
                    << LOG_KV("sendMsgTimeout", m_config->sendMsgTimeout())
                    << LOG_KV("reconnectPeriod", m_config->reconnectPeriod());
}

void EventSub::stop()
{
    if (!m_running)
    {
        EVENT_SUB(INFO) << LOG_BADGE("stop") << LOG_DESC("event sub is not running");
        return;
    }

    m_running = false;
    if (m_timer)
    {
        m_timer->stop();
    }

    EVENT_SUB(INFO) << LOG_BADGE("stop") << LOG_DESC("stop event sub successfully");
}

void EventSub::doLoop()
{
    m_timer->restart();
    {
        boost::shared_lock<boost::shared_mutex> lock(x_tasks);
        EVENT_SUB(INFO) << LOG_BADGE("doLoop") << LOG_DESC("event sub tasks report")
                        << LOG_KV("working event sub count", m_workingTasks.size())
                        << LOG_KV("suspend event sub count", m_suspendTasks.size());
    }

    if (m_suspendTasksCount.load())
    {
        auto ss = m_service->sessions();
        if (ss.empty())
        {
            EVENT_SUB(INFO)
                << LOG_BADGE("doLoop")
                << LOG_DESC(
                       "no active sessions available and discard resend suspend event sub tasks");
        }
        else
        {
            boost::shared_lock<boost::shared_mutex> lock(x_tasks);
            for (const auto& taskEntry : m_suspendTasks)
            {
                auto task = taskEntry.second;
                std::string id = task->id();
                if (!this->addWaitResp(id))
                {
                    continue;
                }

                subscribeEvent(
                    task, [id, this](Error::Ptr, const std::string&) { this->removeWaitResp(id); });
            }
        }
    }
}

bool EventSub::addTask(EventSubTask::Ptr _task)
{
    boost::unique_lock<boost::shared_mutex> lock(x_tasks);
    removeSuspendTask(_task->id());
    if (m_workingTasks.find(_task->id()) == m_workingTasks.end())
    {
        m_workingTasks[_task->id()] = _task;
        return true;
    }

    return false;
}

EventSubTask::Ptr EventSub::getTask(const std::string& _id, bool includeSuspendTask)
{
    EventSubTask::Ptr task = nullptr;

    boost::shared_lock<boost::shared_mutex> lock(x_tasks);
    auto it = m_workingTasks.find(_id);
    if (it != m_workingTasks.end())
    {
        task = it->second;

        EVENT_SUB(TRACE) << LOG_BADGE("getTask") << LOG_DESC("event sub task is working")
                         << LOG_KV("id", _id);
    }
    else if (includeSuspendTask)
    {
        auto innerIt = m_suspendTasks.find(_id);
        if (innerIt != m_suspendTasks.end())
        {
            task = innerIt->second;

            EVENT_SUB(TRACE) << LOG_BADGE("getTask") << LOG_DESC("event sub task suspend")
                             << LOG_KV("id", _id);
        }
        else
        {
            EVENT_SUB(DEBUG) << LOG_BADGE("getTask") << LOG_DESC("cannot found event sub task")
                             << LOG_KV("id", _id);
        }
    }

    return task;
}

EventSubTask::Ptr EventSub::getTaskAndRemove(const std::string& _id, bool includeSuspendTask)
{
    EventSubTask::Ptr task = nullptr;

    boost::unique_lock<boost::shared_mutex> lock(x_tasks);
    auto it = m_workingTasks.find(_id);
    if (it != m_workingTasks.end())
    {  // remove from m_workingTasks
        task = it->second;
        m_workingTasks.erase(it);

        EVENT_SUB(TRACE) << LOG_BADGE("getTaskAndRemove") << LOG_DESC("event sub task is working")
                         << LOG_KV("id", _id);
    }
    else if (includeSuspendTask)
    {
        // remove from m_suspendTasks
        auto innerIt = m_suspendTasks.find(_id);
        if (innerIt != m_suspendTasks.end())
        {
            task = innerIt->second;
            m_suspendTasksCount--;
            m_suspendTasks.erase(innerIt);

            EVENT_SUB(TRACE) << LOG_BADGE("getTaskAndRemove") << LOG_DESC("event sub task suspend")
                             << LOG_KV("id", _id);
        }
    }

    return task;
}

bool EventSub::addSuspendTask(EventSubTask::Ptr _task)
{
    if (m_suspendTasks.find(_task->id()) == m_suspendTasks.end())
    {
        m_suspendTasksCount++;
        m_suspendTasks[_task->id()] = _task;
        return true;
    }

    return false;
}

bool EventSub::removeSuspendTask(const std::string& _id)
{
    // remove from suspendTasks
    auto it = m_suspendTasks.find(_id);
    if (it != m_suspendTasks.end())
    {
        m_suspendTasksCount--;
        m_suspendTasks.erase(it);
        return true;
    }
    return false;
}

std::size_t EventSub::suspendTasks(std::shared_ptr<WsSession> _session)
{
    if (!_session)
    {
        return 0;
    }

    std::size_t count = 0;
    {
        boost::unique_lock<boost::shared_mutex> lock(x_tasks);
        for (auto it = m_workingTasks.begin(); it != m_workingTasks.end();)
        {
            auto task = it->second;
            auto s = task->session();
            if (s && s->endPoint() != _session->endPoint())
            {
                ++it;
                continue;
            }

            EVENT_SUB(INFO) << LOG_BADGE("suspendTasks")
                            << LOG_DESC("suspend event sub task for disconnection")
                            << LOG_KV("id", task->id()) << LOG_KV("endPoint", _session->endPoint());
            it = m_workingTasks.erase(it);
            task->setSession(nullptr);
            addSuspendTask(task);
            count++;
        }
    }

    EVENT_SUB(INFO) << LOG_BADGE("suspendTasks")
                    << LOG_DESC("suspend event sub tasks for disconnection")
                    << LOG_KV("endPoint", _session->endPoint()) << LOG_KV("count", count);

    return count;
}

void EventSub::onRecvEventSubMessage(
    std::shared_ptr<boostssl::MessageFace> _msg, std::shared_ptr<WsSession> _session)
{
    /*
    {
        "id": "",
        "status": 0,
        "result": {
            [
                {},
                {},
                {}
            ]
        }
    }
    */
    auto strResp = std::string(_msg->payload()->begin(), _msg->payload()->end());

    EVENT_SUB(TRACE) << LOG_BADGE("onRecvEventSubMessage") << LOG_DESC("receive event sub message")
                     << LOG_KV("endpoint", _session->endPoint()) << LOG_KV("response", strResp);

    auto resp = std::make_shared<EventSubResponse>();
    if (!resp->fromJson(strResp))
    {
        EVENT_SUB(WARNING) << LOG_BADGE("onRecvEventSubMessage")
                           << LOG_DESC("recv invalid event sub message")
                           << LOG_KV("endpoint", _session->endPoint())
                           << LOG_KV("response", strResp);
        return;
    }

    auto task = getTask(resp->id());
    if (task == nullptr)
    {
        EVENT_SUB(WARNING) << LOG_BADGE("onRecvEventSubMessage")
                           << LOG_DESC("event sub task not exist") << LOG_KV("id", resp->id())
                           << LOG_KV("endpoint", _session->endPoint())
                           << LOG_KV("response", strResp);
        return;
    }

    if (resp->status() == StatusCode::EndOfPush)
    {  // event sub end
        getTaskAndRemove(resp->id());
        task->callback()(nullptr, strResp);

        EVENT_SUB(INFO) << LOG_BADGE("onRecvEventSubMessage") << LOG_DESC("end of push")
                        << LOG_KV("id", task->id()) << LOG_KV("endpoint", _session->endPoint())
                        << LOG_KV("response", strResp);
    }
    else if (resp->status() != StatusCode::Success)
    {  // event sub error
        getTaskAndRemove(resp->id());
        task->callback()(nullptr, strResp);

        EVENT_SUB(INFO) << LOG_BADGE("onRecvEventSubMessage") << LOG_DESC("event sub error")
                        << LOG_KV("id", task->id()) << LOG_KV("endpoint", _session->endPoint())
                        << LOG_KV("response", strResp);
    }
    else
    {
        // NOTE: update the latest blocknumber of event sub for network disconnect continue
        auto jResp = resp->jResp();
        try
        {
            int64_t blockNumber = -1;
            if (jResp["result"][0]["blockNumber"].isInt64())
            {
                blockNumber = jResp["result"][0]["blockNumber"].asInt64();
                task->state()->setCurrentBlockNumber(blockNumber);
            }
            task->callback()(nullptr, strResp);

            EVENT_SUB(TRACE) << LOG_BADGE("onRecvEventSubMessage") << LOG_DESC("event sub")
                             << LOG_KV("id", task->id()) << LOG_KV("endpoint", _session->endPoint())
                             << LOG_KV("blockNumber", blockNumber) << LOG_KV("response", strResp);
        }
        catch (const std::exception& e)
        {
            EVENT_SUB(WARNING) << LOG_BADGE("onRecvEventSubMessage")
                               << LOG_DESC("unrecognized event sub response")
                               << LOG_KV("id", task->id())
                               << LOG_KV("endpoint", _session->endPoint())
                               << LOG_KV("resp", strResp);
        }
    }
}

void EventSub::subscribeEvent(EventSubTask::Ptr _task, Callback _callback)
{
    auto id = _task->id();
    auto group = _task->group();

    auto request = std::make_shared<EventSubSubRequest>();
    request->setId(id);
    request->setParams(_task->params());
    request->setGroup(_task->group());
    request->setState(_task->state());

    auto jsonReq = request->generateJson();

    auto message = m_messagefactory->buildMessage();
    message->setSeq(m_messagefactory->newSeq());
    message->setPacketType(bcos::cppsdk::event::MessageType::EVENT_SUBSCRIBE);
    message->setPayload(std::make_shared<bytes>(jsonReq.begin(), jsonReq.end()));

    EVENT_SUB(INFO) << LOG_BADGE("subscribeEvent") << LOG_DESC("subscribe event")
                    << LOG_KV("id", id) << LOG_KV("group", group) << LOG_KV("request", jsonReq);

    m_service->asyncSendMessageByGroupAndNode(_task->group(), "", message, Options(),
        [id, _task, _callback, this](Error::Ptr _error, std::shared_ptr<boostssl::MessageFace> _msg,
            std::shared_ptr<WsSession> _session) {
            if (_error && _error->errorCode() != 0)
            {
                EVENT_SUB(WARNING)
                    << LOG_BADGE("subscribeEvent") << LOG_DESC("callback response error")
                    << LOG_KV("id", id) << LOG_KV("errorCode", _error->errorCode())
                    << LOG_KV("errorMessage", _error->errorMessage());

                _callback(_error, "");
                return;
            }

            auto strResp = std::string(_msg->payload()->begin(), _msg->payload()->end());
            auto resp = std::make_shared<EventSubResponse>();
            if (!resp->fromJson(strResp))
            {
                EVENT_SUB(WARNING)
                    << LOG_BADGE("subscribeEvent") << LOG_DESC("invalid subscribe event response")
                    << LOG_KV("id", id) << LOG_KV("response", strResp);
                _callback(nullptr, strResp);
            }
            else if (resp->status() != StatusCode::Success)
            {
                _callback(nullptr, strResp);
                EVENT_SUB(WARNING)
                    << LOG_BADGE("subscribeEvent") << LOG_DESC("callback response error")
                    << LOG_KV("id", id) << LOG_KV("response", strResp);
            }
            else
            {
                // subscribe event successfully, set network session for unsubscribe
                _task->setSession(_session);

                this->addTask(_task);

                _callback(nullptr, strResp);
                EVENT_SUB(INFO) << LOG_BADGE("subscribeEvent")
                                << LOG_DESC("callback response success") << LOG_KV("id", id)
                                << LOG_KV("response", strResp);
            }
        });
}

std::string EventSub::subscribeEvent(
    const std::string& _group, const std::string& _params, Callback _callback)
{
    EventSubParams::Ptr params = std::make_shared<EventSubParams>();
    if (!params->fromJsonString(_params))
    {
        // invalid request params string format
        auto error = std::make_shared<Error>(-1, "invalid request JSON string");
        _callback(error, "");
        return "";
    }

    return subscribeEvent(_group, params, _callback);
}

std::string EventSub::subscribeEvent(
    const std::string& _group, EventSubParams::Ptr _params, Callback _callback)
{
    // invalid request params string format
    if (!_params->verifyParams())
    {
        auto error = std::make_shared<Error>(-1, "params verification failure");
        _callback(error, "");
        return "";
    }

    auto taskId = m_messagefactory->newSeq();
    auto task = std::make_shared<EventSubTask>();

    task->setId(taskId);
    task->setGroup(_group);
    task->setParams(_params);
    task->setCallback(_callback);
    task->setState(std::make_shared<EventSubTaskState>());

    subscribeEvent(task, _callback);
    return taskId;
}

void EventSub::unsubscribeEvent(const std::string& _id)
{
    auto task = getTaskAndRemove(_id);
    if (task == nullptr)
    {
        EVENT_SUB(WARNING) << LOG_BADGE("unsubscribeEvent") << LOG_DESC("event sub not found")
                           << LOG_KV("id", _id);
        return;
    }

    auto session = task->session();
    if (!session)
    {
        EVENT_SUB(INFO) << LOG_BADGE("unsubscribeEvent") << LOG_DESC("task is suspend")
                        << LOG_KV("id", _id);
        return;
    }

    auto request = std::make_shared<EventSubUnsubRequest>();
    request->setId(_id);
    request->setGroup(task->group());
    auto strReq = request->generateJson();

    auto message = m_messagefactory->buildMessage();
    message->setSeq(m_messagefactory->newSeq());
    message->setPacketType(bcos::cppsdk::event::MessageType::EVENT_UNSUBSCRIBE);
    message->setPayload(std::make_shared<bytes>(strReq.begin(), strReq.end()));

    session->asyncSendMessage(message, Options(),
        [_id](Error::Ptr _error, std::shared_ptr<boostssl::MessageFace> _msg,
            std::shared_ptr<WsSession>) {
            if (_error && _error->errorCode() != 0)
            {
                EVENT_SUB(WARNING)
                    << LOG_BADGE("unsubscribeEvent") << LOG_DESC("callback response error")
                    << LOG_KV("id", _id) << LOG_KV("errorCode", _error->errorCode())
                    << LOG_KV("errorMessage", _error->errorMessage());
                return;
            }

            auto strResp = std::string(_msg->payload()->begin(), _msg->payload()->end());
            auto resp = std::make_shared<EventSubResponse>();
            if (!resp->fromJson(strResp))
            {
                EVENT_SUB(WARNING)
                    << LOG_BADGE("unsubscribeEvent") << LOG_DESC("callback invalid response")
                    << LOG_KV("id", _id) << LOG_KV("response", strResp);
                return;
            }

            if (resp->status() != StatusCode::Success)
            {
                EVENT_SUB(WARNING)
                    << LOG_BADGE("unsubscribeEvent") << LOG_DESC("callback response error")
                    << LOG_KV("id", _id) << LOG_KV("status", resp->status())
                    << LOG_KV("response", strResp);
            }
            else
            {
                EVENT_SUB(INFO) << LOG_BADGE("unsubscribeEvent")
                                << LOG_DESC("callback response success") << LOG_KV("id", _id)
                                << LOG_KV("status", resp->status()) << LOG_KV("response", strResp);
            }
        });
}