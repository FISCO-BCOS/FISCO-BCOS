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
 * @brief: threadpool that can execute tasks asyncly
 *
 * @file ThreadPool.h
 * @author: yujiechen
 * @date 2021-02-26
 */

#pragma once
#include "Common.h"
#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>
#include <iosfwd>
#include <memory>

namespace bcos
{
class ThreadPool
{
public:
    typedef std::shared_ptr<ThreadPool> Ptr;

    explicit ThreadPool(const std::string& threadName, size_t size) : m_work(_ioService)
    {
        _threadName = threadName;

        for (size_t i = 0; i < size; ++i)
        {
            _workers.create_thread([this] {
                bcos::pthread_setThreadName(_threadName);
                _ioService.run();
            });
        }
    }
    void stop()
    {
        _ioService.stop();
        if (!_workers.is_this_thread_in())
        {
            _workers.join_all();
        }
    }

    ~ThreadPool() { stop(); }

    // Add new work item to the pool.
    template <class F>
    void enqueue(F f)
    {
        _ioService.post(f);
    }

private:
    std::string _threadName;
    boost::thread_group _workers;
    boost::asio::io_service _ioService;
    // m_work ensures that io_service's run() function will not exit while work is underway
    boost::asio::io_service::work m_work;
};

}  // namespace bcos
