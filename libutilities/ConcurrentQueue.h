/*
 *  Copyright (C) 2020 FISCO BCOS.
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
 *  @brief: Implement a queue with notification/consumption mechanism
 *  @file ConcurrentQueue.h
 *  @date 2020-11-20
 */
#pragma once
#include <condition_variable>
#include <mutex>
#include <queue>
#include <utility>


namespace bcos
{
/// Concurrent queue.
/// You can push and pop elements to/from the queue. Pop will block until the queue is not empty.
/// The default backend (QueueT) is std::queue. It can be changed to any type that has
/// proper push(), pop(), empty() and front() methods.
template <typename _T, typename QueueT = std::queue<_T>>
class ConcurrentQueue
{
public:
    template <typename _U>
    void push(_U&& _elem)
    {
        {
            std::lock_guard<decltype(x_mutex)> guard{x_mutex};
            m_queue.push(std::forward<_U>(_elem));
        }
        m_cv.notify_one();
    }

    _T pop()
    {
        std::unique_lock<std::mutex> lock{x_mutex};
        m_cv.wait(lock, [this] { return !m_queue.empty(); });
        auto item = std::move(m_queue.front());
        m_queue.pop();
        return item;
    }

    std::pair<bool, _T> tryPop(int milliseconds)
    {
        boost::unique_lock<boost::mutex> lock{x_mutex};
        // if g_BCOSConfig.shouldExit is true, should return directly
        // in consideration that when the system time has been changed,
        // the process maybe stucked in 'wait_for'
        auto ret = m_cv.wait_for(lock, boost::chrono::milliseconds(milliseconds),
            [this] { return !m_queue.empty() || g_BCOSConfig.shouldExit; });
        if (!ret || g_BCOSConfig.shouldExit)
        {
            return std::make_pair(false, _T());
        }
        auto item = std::move(m_queue.front());
        m_queue.pop();
        return std::make_pair(ret, item);
    }

private:
    QueueT m_queue;
    boost::mutex x_mutex;
    boost::condition_variable m_cv;
};
}  // namespace bcos
