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
 * @file EventSub.cpp
 * @author: octopus
 * @date 2021-09-07
 */

#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-framework/interfaces/protocol/ProtocolTypeDef.h>
#include <bcos-framework/libutilities/Log.h>
#include <bcos-rpc/event/EventSub.h>
#include <bcos-rpc/event/EventSubMatcher.h>
#include <bcos-rpc/event/EventSubRequest.h>
#include <bcos-rpc/event/EventSubResponse.h>
#include <bcos-rpc/event/EventSubTask.h>
#include <chrono>
#include <cstddef>
#include <memory>
#include <thread>

using namespace bcos;
using namespace bcos::event;

void EventSub::start()
{
    if (m_running.load())
    {
        EVENT_SUB(INFO) << LOG_BADGE("start") << LOG_DESC("event sub is running");
        return;
    }
    m_running.store(true);
    startWorking();

    EVENT_SUB(INFO) << LOG_BADGE("start") << LOG_DESC("start event sub successfully");
}

void EventSub::stop()
{
    if (!m_running.load())
    {
        EVENT_SUB(INFO) << LOG_BADGE("stop") << LOG_DESC("event sub is not running");
        return;
    }
    m_running.store(false);

    finishWorker();
    stopWorking();
    // will not restart worker, so terminate it
    terminate();

    EVENT_SUB(INFO) << LOG_BADGE("stop") << LOG_DESC("stop event sub successfully");
}

void EventSub::onRecvSubscribeEvent(std::shared_ptr<bcos::boostssl::ws::WsMessage> _msg,
    std::shared_ptr<bcos::boostssl::ws::WsSession> _session)
{
    std::string seq = std::string(_msg->seq()->begin(), _msg->seq()->end());
    std::string request = std::string(_msg->data()->begin(), _msg->data()->end());

    EVENT_SUB(INFO) << LOG_BADGE("onRecvSubscribeEvent") << LOG_KV("endpoint", _session->endPoint())
                    << LOG_KV("seq", seq) << LOG_KV("request", request);

    auto eventSubRequest = std::make_shared<EventSubRequest>();
    if (!eventSubRequest->fromJson(request))
    {
        sendResponse(_session, _msg, eventSubRequest->id(), EP_STATUS_CODE::INVALID_PARAMS);
        return;
    }

    auto nodeService = m_groupManager->getNodeService(eventSubRequest->group(), "");
    if (!nodeService)
    {
        sendResponse(_session, _msg, eventSubRequest->id(), EP_STATUS_CODE::GROUP_NOT_EXIST);
        EVENT_SUB(ERROR) << LOG_BADGE("onRecvSubscribeEvent") << LOG_DESC("group not exist")
                         << LOG_KV("group", eventSubRequest->group());
        return;
    }

    auto state = std::make_shared<EventSubTaskState>();

    // TODO: check request parameters
    auto task = std::make_shared<EventSubTask>();
    task->setGroup(eventSubRequest->group());
    task->setId(eventSubRequest->id());
    task->setParams(eventSubRequest->params());
    task->setState(state);

    auto eventSubWeakPtr = std::weak_ptr<EventSub>(shared_from_this());
    task->setCallback([eventSubWeakPtr, _session](const std::string& _id, bool _complete,
                          const Json::Value& _result) -> bool {
        auto eventSub = eventSubWeakPtr.lock();
        if (eventSub)
        {
            return eventSub->sendEvents(_session, _complete, _id, _result);
        }
        return false;
    });

    subscribeEventSub(task);
    sendResponse(_session, _msg, eventSubRequest->id(), EP_STATUS_CODE::SUCCESS);
    return;
}

void EventSub::onRecvUnsubscribeEvent(std::shared_ptr<bcos::boostssl::ws::WsMessage> _msg,
    std::shared_ptr<bcos::boostssl::ws::WsSession> _session)
{
    std::string seq = std::string(_msg->seq()->begin(), _msg->seq()->end());
    std::string request = std::string(_msg->data()->begin(), _msg->data()->end());

    EVENT_SUB(INFO) << LOG_BADGE("onRecvUnsubscribeEvent") << LOG_KV("seq", seq)
                    << LOG_KV("endpoint", _session->endPoint()) << LOG_KV("request", request);

    auto esRes = std::make_shared<EventSubUnsubRequest>();
    if (!esRes->fromJson(request))
    {
        sendResponse(_session, _msg, esRes->id(), EP_STATUS_CODE::INVALID_PARAMS);
        return;
    }

    unsubscribeEventSub(esRes->id());
    sendResponse(_session, _msg, esRes->id(), EP_STATUS_CODE::SUCCESS);
    return;
}


/**
 * @brief: send response
 * @param _session: the peer session
 * @param _msg: the msg
 * @param _status: the response status
 * @return bool: if _session is inactive, false will be return
 */
bool EventSub::sendResponse(std::shared_ptr<bcos::boostssl::ws::WsSession> _session,
    std::shared_ptr<bcos::boostssl::ws::WsMessage> _msg, const std::string& _id, int32_t _status)
{
    if (!_session->isConnected())
    {
        EVENT_SUB(WARNING) << LOG_BADGE("sendResponse") << LOG_DESC("session has been inactive")
                           << LOG_KV("id", _id) << LOG_KV("status", _status)
                           << LOG_KV("endpoint", _session->endPoint());
        return false;
    }

    auto esResp = std::make_shared<EventSubResponse>();
    esResp->setId(_id);
    esResp->setStatus(_status);
    auto result = esResp->generateJson();

    auto data = std::make_shared<bcos::bytes>(result.begin(), result.end());
    _msg->setData(data);

    _session->asyncSendMessage(_msg);
    return true;
}

/**
 * @brief: send event log list to client
 * @param _session: the peer
 * @param _complete: if task _completed
 * @param _id: the EventSub id
 * @param _result:
 * @return bool: if _session is inactive, false will be return
 */
bool EventSub::sendEvents(std::shared_ptr<bcos::boostssl::ws::WsSession> _session, bool _complete,
    const std::string& _id, const Json::Value& _result)
{
    // session disconnected
    if (!_session->isConnected())
    {
        EVENT_SUB(WARNING) << LOG_BADGE("sendEvents") << LOG_DESC("session has been inactive")
                           << LOG_KV("id", _id) << LOG_KV("endpoint", _session->endPoint());
        return false;
    }

    // task completed
    if (_complete)
    {
        auto msg = m_messageFactory->buildMessage();
        msg->setType(bcos::event::MessageType::EVENT_LOG_PUSH);
        sendResponse(_session, msg, _id, EP_STATUS_CODE::PUSH_COMPLETED);
        return true;
    }

    // null
    if (0 == _result.size())
    {
        return true;
    }

    auto esResp = std::make_shared<EventSubResponse>();
    esResp->setId(_id);
    esResp->setStatus(EP_STATUS_CODE::SUCCESS);
    esResp->generateJson();

    auto jResp = esResp->jResp();
    jResp["result"] = _result;

    Json::FastWriter writer;
    std::string strEventInfo = writer.write(jResp);
    auto data = std::make_shared<bcos::bytes>(strEventInfo.begin(), strEventInfo.end());

    auto msg = m_messageFactory->buildMessage();
    msg->setType(bcos::event::MessageType::EVENT_LOG_PUSH);
    msg->setData(data);
    _session->asyncSendMessage(msg);

    EVENT_SUB(TRACE) << LOG_BADGE("sendEvents") << LOG_DESC("send events to client")
                     << LOG_KV("endpoint", _session->endPoint()) << LOG_KV("id", _id)
                     << LOG_KV("events", strEventInfo);

    return true;
}

void EventSub::subscribeEventSub(EventSubTask::Ptr _task)
{
    EVENT_SUB(INFO) << LOG_BADGE("subscribeEventSub") << LOG_KV("id", _task->id())
                    << LOG_KV("startBlk", _task->state()->currentBlockNumber());
    std::unique_lock lock(x_addTasks);
    m_addTasks.push_back(_task);
    m_addTaskCount++;
}

void EventSub::unsubscribeEventSub(const std::string& _id)
{
    EVENT_SUB(INFO) << LOG_BADGE("unsubscribeEventSub") << LOG_KV("id", _id);
    std::unique_lock lock(x_cancelTasks);
    m_cancelTasks.push_back(_id);
    m_cancelTaskCount++;
}

void EventSub::executeWorker()
{
    executeCancelTasks();
    executeAddTasks();
    executeEventSubTasks();
    reportEventSubTasks();
}

void EventSub::reportEventSubTasks()
{
    static auto start = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    //
    if (elapsedMs > 10 * 1000)
    {
        EVENT_SUB(INFO) << LOG_BADGE("reportEventSubTasks")
                        << LOG_DESC("all event sub tasks subscribed by client")
                        << LOG_KV("count", m_tasks.size());
        start = std::chrono::high_resolution_clock::now();
    }
}

void EventSub::executeAddTasks()
{
    if (m_addTaskCount.load() == 0)
    {
        return;
    }

    std::unique_lock lock(x_addTasks);
    for (auto& task : m_addTasks)
    {
        auto id = task->id();
        if (m_tasks.find(id) == m_tasks.end())
        {
            m_tasks[id] = task;
            EVENT_SUB(INFO) << LOG_BADGE("executeAddTasks") << LOG_KV("id", task->id());
        }
        else
        {
            EVENT_SUB(ERROR) << LOG_BADGE("executeAddTasks")
                             << LOG_DESC("event sub task already exist")
                             << LOG_KV("id", task->id());
        }
    }
    m_addTaskCount.store(0);
    m_addTasks.clear();

    auto taskCount = m_tasks.size();
    EVENT_SUB(INFO) << LOG_BADGE("executeAddTasks") << LOG_DESC("report event subscribe tasks ")
                    << LOG_KV("count", taskCount);
}

void EventSub::executeCancelTasks()
{
    if (m_cancelTaskCount.load() == 0)
    {
        return;
    }

    std::unique_lock lock(x_cancelTasks);
    for (const auto& id : m_cancelTasks)
    {
        auto r = m_tasks.erase(id);
        if (r)
        {
            EVENT_SUB(INFO) << LOG_BADGE("executeCancelTasks") << LOG_KV("id", id);
        }
        else
        {
            EVENT_SUB(WARNING) << LOG_BADGE("executeCancelTasks")
                               << LOG_DESC("event sub task not exist") << LOG_KV("id", id);
        }
    }
    m_cancelTaskCount.store(0);
    m_cancelTasks.clear();

    auto taskCount = m_tasks.size();
    EVENT_SUB(INFO) << LOG_BADGE("executeCancelTasks") << LOG_DESC("report event subscribe tasks ")
                    << LOG_KV("count", taskCount);
}

bool EventSub::checkConnAvailable(EventSubTask::Ptr _task)
{
    Json::Value jResp(Json::arrayValue);
    return _task->callback()(_task->id(), false, jResp);
}

void EventSub::onTaskComplete(EventSubTask::Ptr _task)
{
    Json::Value jResp(Json::arrayValue);
    _task->callback()(_task->id(), true, jResp);

    EVENT_SUB(INFO) << LOG_BADGE("onTaskComplete") << LOG_DESC("event sub completed")
                    << LOG_KV("id", _task->id())
                    << LOG_KV("fromBlock", _task->params()->fromBlock())
                    << LOG_KV("toBlock", _task->params()->toBlock())
                    << LOG_KV("currentBlock", _task->state()->currentBlockNumber());
}

int64_t EventSub::executeEventSubTask(EventSubTask::Ptr _task, int64_t _blockNumber)
{
    bcos::protocol::BlockNumber currentBlockNumber = _task->state()->currentBlockNumber();
    if (currentBlockNumber < 0)
    {
        currentBlockNumber =
            _task->params()->fromBlock() > 0 ? _task->params()->fromBlock() : _blockNumber;
    }

    if (_blockNumber < currentBlockNumber)
    {
        // waiting for block to be sealed
        return 0;
    }

    /*
    EVENT_SUB(TRACE) << LOG_BADGE("executeEventSubTask") << LOG_DESC("running")
                     << LOG_KV("id", _task->id())
                     << LOG_KV("fromBlock", _task->params()->fromBlock())
                     << LOG_KV("toBlock", _task->params()->toBlock())
                     << LOG_KV("blockNumber", _blockNumber)
                     << LOG_KV("currentBlockNumber", _task->state()->currentBlockNumber());
     */

    int64_t toBlockNumber = _task->params()->toBlock();
    if (toBlockNumber > 0 && toBlockNumber < _blockNumber)
    {
        _blockNumber = toBlockNumber;
    }

    int64_t blockCanProcess = _blockNumber - currentBlockNumber + 1;
    int64_t maxBlockProcessPerLoop = m_maxBlockProcessPerLoop;
    blockCanProcess =
        (blockCanProcess > maxBlockProcessPerLoop ? maxBlockProcessPerLoop : blockCanProcess);

    _task->setWork(true);
    class RecursiveProcess : public std::enable_shared_from_this<RecursiveProcess>
    {
    public:
        void process(int64_t _blockNumber)
        {
            if (_blockNumber > m_endBlockNumber)
            {  // all block has been proccessed
                m_task->setWork(false);
                return;
            }

            EVENT_SUB(TRACE) << LOG_BADGE("executeEventSubTask:process")
                             << LOG_KV("id", m_task->id())
                             << LOG_KV("fromBlock", m_task->params()->fromBlock())
                             << LOG_KV("toBlock", m_task->params()->toBlock())
                             << LOG_KV("blockNumber", _blockNumber);

            auto eventSub = m_eventSub;
            auto task = m_task;
            auto p = shared_from_this();
            eventSub->processNextBlock(
                _blockNumber, task, [task, _blockNumber, p](Error::Ptr _error) {
                    if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
                    {
                        task->setWork(false);
                        return;
                    }
                    // next block
                    task->state()->setCurrentBlockNumber(_blockNumber + 1);
                    p->process(_blockNumber + 1);
                });
        }

    public:
        bcos::protocol::BlockNumber m_endBlockNumber;
        std::shared_ptr<EventSub> m_eventSub;
        EventSubTask::Ptr m_task;
    };

    auto p = std::make_shared<RecursiveProcess>();
    p->m_endBlockNumber = currentBlockNumber + blockCanProcess - 1;
    p->m_eventSub = shared_from_this();
    p->m_task = _task;
    p->process(currentBlockNumber);

    return blockCanProcess;
}

int64_t EventSub::executeEventSubTask(EventSubTask::Ptr _task)
{
    // tests whether the connection of the session is available first
    auto connAvailable = checkConnAvailable(_task);
    if (!connAvailable)
    {
        unsubscribeEventSub(_task->id());
        return -1;
    }

    if (_task->isCompleted())
    {
        unsubscribeEventSub(_task->id());
        onTaskComplete(_task);
        return 0;
    }

    // task is working, waiting for done
    if (_task->work())
    {
        return 0;
    }

    std::string group = _task->group();
    auto nodeService = m_groupManager->getNodeService(group, "");
    if (!nodeService)
    {
        // group not exist???
        EVENT_SUB(ERROR) << LOG_BADGE("executeEventSubTask")
                         << LOG_DESC("cannot get node service maybe the group has been removed")
                         << LOG_KV("id", _task->id()) << LOG_KV("group", _task->group());
        unsubscribeEventSub(_task->id());
        return -1;
    }

    // TODO: optimize getBlockNumberByGroup instead of asyncGetBlockNumber
    auto self = std::weak_ptr<EventSub>(shared_from_this());
    auto ledger = nodeService->ledger();
    ledger->asyncGetBlockNumber(
        [group, self, _task](Error::Ptr _error, protocol::BlockNumber _blockNumber) {
            if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
            {
                EVENT_SUB(ERROR) << LOG_BADGE("executeEventSubTask")
                                 << LOG_DESC("asyncGetBlockNumber error") << LOG_KV("group", group)
                                 << LOG_KV("errorCode", _error->errorCode())
                                 << LOG_KV("errorMessage", _error->errorMessage());

                // error should be occur
                return;
            }

            auto eventSub = self.lock();
            if (eventSub)
            {
                eventSub->executeEventSubTask(_task, _blockNumber);
            }
        });

    return 0;
}

void EventSub::processNextBlock(
    int64_t _blockNumber, EventSubTask::Ptr _task, std::function<void(Error::Ptr _error)> _callback)
{
    auto self = std::weak_ptr<EventSub>(shared_from_this());
    auto matcher = m_matcher;

    std::string group = _task->group();
    auto nodeService = m_groupManager->getNodeService(group, "");
    if (!nodeService)
    {
        // group not exist???
        EVENT_SUB(ERROR)
            << LOG_BADGE("processNextBlock")
            << LOG_DESC("cannot get node service of the group maybe the group has been removed")
            << LOG_KV("id", _task->id()) << LOG_KV("group", _task->group());
        unsubscribeEventSub(_task->id());
        return;
    }

    auto ledger = nodeService->ledger();
    ledger->asyncGetBlockDataByNumber(_blockNumber,
        bcos::ledger::RECEIPTS | bcos::ledger::TRANSACTIONS,
        [matcher, _task, _blockNumber, _callback, self](
            Error::Ptr _error, protocol::Block::Ptr _block) {
            if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
            {
                // Note: wait for next time
                EVENT_SUB(ERROR) << LOG_BADGE("processNextBlock")
                                 << LOG_DESC("asyncGetBlockDataByNumber")
                                 << LOG_KV("id", _task->id()) << LOG_KV("blockNumber", _blockNumber)
                                 << LOG_KV("errorCode", _error->errorCode())
                                 << LOG_KV("errorMessage", _error->errorMessage());
                _callback(_error);
                return;
            }

            Json::Value jResp(Json::arrayValue);
            auto count = matcher->matches(_task->params(), _block, jResp);
            if (count)
            {
                EVENT_SUB(TRACE) << LOG_BADGE("processNextBlock")
                                 << LOG_DESC("asyncGetBlockDataByNumber")
                                 << LOG_KV("blockNumber", _blockNumber) << LOG_KV("id", _task->id())
                                 << LOG_KV("count", count);

                _task->callback()(_task->id(), false, jResp);
            }

            _callback(nullptr);
        });
}

void EventSub::executeEventSubTasks()
{
    for (auto& task : m_tasks)
    {
        executeEventSubTask(task.second);
    }

    // limiting speed
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
