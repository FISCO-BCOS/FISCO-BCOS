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
 * @file EvenPush.h
 * @author: octopus
 * @date 2021-09-01
 */

#pragma once
#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-cpp-sdk/event/EventSubInterface.h>
#include <bcos-cpp-sdk/event/EventSubTask.h>
#include <bcos-cpp-sdk/ws/Service.h>
#include <boost/thread/thread.hpp>
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

namespace bcos
{
namespace cppsdk
{
namespace event
{
class EventSub : public EventSubInterface
{
public:
    using Ptr = std::shared_ptr<EventSub>;
    using UniquePtr = std::unique_ptr<EventSub>;

    EventSub() {}
    virtual ~EventSub() { stop(); }

public:
    virtual void start() override;
    virtual void stop() override;

    void doLoop();

    virtual std::string subscribeEvent(
        const std::string& _group, const std::string& _params, Callback _callback) override;
    virtual std::string subscribeEvent(
        const std::string& _group, EventSubParams::Ptr _params, Callback _callback) override;
    virtual void unsubscribeEvent(const std::string& _id) override;

public:
    void subscribeEvent(EventSubTask::Ptr _task, Callback _callback);

public:
    void onRecvEventSubMessage(std::shared_ptr<bcos::boostssl::MessageFace> _msg,
        std::shared_ptr<bcos::boostssl::ws::WsSession> _session);

public:
    bool addTask(EventSubTask::Ptr _task);
    EventSubTask::Ptr getTask(const std::string& _id, bool includeSuspendTask = true);
    EventSubTask::Ptr getTaskAndRemove(const std::string& _id, bool includeSuspendTask = true);

    bool addSuspendTask(EventSubTask::Ptr _task);
    bool removeSuspendTask(const std::string& _id);

    bool removeWaitResp(const std::string& _id)
    {
        boost::lock_guard<boost::mutex> lock(x_waitRespTasks);
        return 0 != m_waitRespTasks.erase(_id);
    }

    bool addWaitResp(const std::string& _id)
    {
        boost::lock_guard<boost::mutex> lock(x_waitRespTasks);
        auto r = m_waitRespTasks.insert(_id);
        return r.second;
    }

    std::size_t suspendTasks(std::shared_ptr<bcos::boostssl::ws::WsSession> _session);

    void setService(bcos::cppsdk::service::Service::Ptr _service) { m_service = _service; }
    bcos::cppsdk::service::Service::Ptr service() const { return m_service; }
    void setMessageFactory(std::shared_ptr<bcos::boostssl::ws::WsMessageFactory> _messageFactory)
    {
        m_messagefactory = _messageFactory;
    }
    std::shared_ptr<bcos::boostssl::ws::WsMessageFactory> messageFactory() const
    {
        return m_messagefactory;
    }

    boostssl::ws::WsConfig::ConstPtr config() const { return m_config; }
    void setConfig(boostssl::ws::WsConfig::ConstPtr _config) { m_config = _config; }

    uint32_t suspendTasksCount() const { return m_suspendTasksCount.load(); }
    const std::unordered_map<std::string, EventSubTask::Ptr>& suspendTasks() const
    {
        return m_suspendTasks;
    }
    const std::unordered_map<std::string, EventSubTask::Ptr>& workingtasks() const
    {
        return m_workingTasks;
    }

private:
    bool m_running = false;

    std::atomic<uint32_t> m_suspendTasksCount{0};

    mutable boost::shared_mutex x_tasks;
    std::unordered_map<std::string, EventSubTask::Ptr> m_workingTasks;
    std::unordered_map<std::string, EventSubTask::Ptr> m_suspendTasks;

    mutable boost::mutex x_waitRespTasks;
    std::set<std::string> m_waitRespTasks;

    // timer
    std::shared_ptr<bcos::Timer> m_timer;
    // message factory
    std::shared_ptr<bcos::boostssl::ws::WsMessageFactory> m_messagefactory;
    // websocket service
    bcos::cppsdk::service::Service::Ptr m_service;
    //
    boostssl::ws::WsConfig::ConstPtr m_config;
};
}  // namespace event
}  // namespace cppsdk
}  // namespace bcos
