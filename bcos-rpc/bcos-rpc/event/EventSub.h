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
 * @file EventSub.h
 * @author: octopus
 * @date 2021-09-07
 */

#pragma once

#include <bcos-framework/interfaces/ledger/LedgerInterface.h>
#include <bcos-framework/interfaces/protocol/ProtocolTypeDef.h>
#include <bcos-framework/libutilities/Worker.h>
#include <bcos-rpc/event/EventSubTask.h>
#include <bcos-rpc/jsonrpc/groupmgr/GroupManager.h>
#include <atomic>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bcos
{
namespace ws
{
class WsSession;
class WsMessage;
}  // namespace ws

namespace event
{
class EventSubMatcher;
class EventSub : bcos::Worker, public std::enable_shared_from_this<EventSub>
{
public:
    using Ptr = std::shared_ptr<EventSub>;
    using ConstPtr = std::shared_ptr<const EventSub>;
    EventSub() : bcos::Worker("t_event_sub") {}
    virtual ~EventSub() { stop(); }

public:
    virtual void start();
    virtual void stop();

    void executeWorker() override;

public:
    virtual void onRecvSubscribeEvent(std::shared_ptr<bcos::boostssl::ws::WsMessage> _msg,
        std::shared_ptr<bcos::boostssl::ws::WsSession> _session);
    virtual void onRecvUnsubscribeEvent(std::shared_ptr<bcos::boostssl::ws::WsMessage> _msg,
        std::shared_ptr<bcos::boostssl::ws::WsSession> _session);

public:
    /**
     * @brief: send response
     * @param _session: the peer
     * @param _msg: the msg
     * @param _id: the EventSub id
     * @param _status: the response status
     * @return bool: if _session is inactive, false will be return
     */
    bool sendResponse(std::shared_ptr<bcos::boostssl::ws::WsSession> _session,
        std::shared_ptr<bcos::boostssl::ws::WsMessage> _msg, const std::string& _id,
        int32_t _status);

    /**
     * @brief: send event log list to client
     * @param _session: the peer
     * @param _complete: if task _completed
     * @param _id: the event sub id
     * @param _result:
     * @return bool: if _session is inactive, false will be return
     */
    bool sendEvents(std::shared_ptr<bcos::boostssl::ws::WsSession> _session, bool _complete,
        const std::string& _id, const Json::Value& _result);

public:
    void executeAddTasks();
    void executeCancelTasks();
    void executeEventSubTasks();
    void reportEventSubTasks();

public:
    int64_t executeEventSubTask(EventSubTask::Ptr _task);
    void subscribeEventSub(EventSubTask::Ptr _task);
    void unsubscribeEventSub(const std::string& _id);

public:
    int64_t executeEventSubTask(EventSubTask::Ptr _task, int64_t _currentBlockNumber);
    void onTaskComplete(bcos::event::EventSubTask::Ptr _task);
    bool checkConnAvailable(bcos::event::EventSubTask::Ptr _task);
    void processNextBlock(int64_t _blockNumber, bcos::event::EventSubTask::Ptr _task,
        std::function<void(Error::Ptr _error)> _callback);

public:
    std::shared_ptr<EventSubMatcher> matcher() const { return m_matcher; }
    void setMatcher(std::shared_ptr<EventSubMatcher> _matcher) { m_matcher = _matcher; }

    void setIoc(std::shared_ptr<boost::asio::io_context> _ioc) { m_ioc = _ioc; }
    std::shared_ptr<boost::asio::io_context> ioc() const { return m_ioc; }

    int64_t maxBlockProcessPerLoop() const { return m_maxBlockProcessPerLoop; }
    void setMaxBlockProcessPerLoop(int64_t _maxBlockProcessPerLoop)
    {
        m_maxBlockProcessPerLoop = _maxBlockProcessPerLoop;
    }

    bcos::rpc::GroupManager::Ptr groupManager() { return m_groupManager; }
    void setGroupManager(bcos::rpc::GroupManager::Ptr _groupManager)
    {
        m_groupManager = _groupManager;
    }

    std::shared_ptr<bcos::boostssl::ws::WsMessageFactory> messageFactory() const
    {
        return m_messageFactory;
    }
    void setMessageFactory(std::shared_ptr<bcos::boostssl::ws::WsMessageFactory> _messageFactory)
    {
        m_messageFactory = _messageFactory;
    }

private:
    // group manager
    bcos::rpc::GroupManager::Ptr m_groupManager;
    // io context
    std::shared_ptr<boost::asio::io_context> m_ioc;
    // match for event log compare
    std::shared_ptr<EventSubMatcher> m_matcher;
    // message factory
    std::shared_ptr<bcos::boostssl::ws::WsMessageFactory> m_messageFactory;

private:
    std::atomic<bool> m_running{false};

    // lock for m_addTasks
    mutable std::shared_mutex x_addTasks;
    // tasks to be add
    std::vector<bcos::event::EventSubTask::Ptr> m_addTasks;
    // the number of tasks to be add
    std::atomic<uint32_t> m_addTaskCount{0};

    // lock for m_cancelTasks
    mutable std::shared_mutex x_cancelTasks;
    // tasks to be cancel
    std::vector<std::string> m_cancelTasks;
    // the number of tasks to be cancel
    std::atomic<uint32_t> m_cancelTaskCount{0};

    // all subscribe event tasks
    std::unordered_map<std::string, EventSubTask::Ptr> m_tasks;

    //
    int64_t m_maxBlockProcessPerLoop = 10;
};

class EventSubFactory : public std::enable_shared_from_this<EventSubFactory>
{
public:
    using Ptr = std::shared_ptr<EventSubFactory>;

public:
    EventSub::Ptr buildEventSub()
    {
        auto es = std::make_shared<EventSub>();
        return es;
    }
};

}  // namespace event
}  // namespace bcos