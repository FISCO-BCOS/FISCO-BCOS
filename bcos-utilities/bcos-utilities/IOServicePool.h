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
 *  m_limitations under the License.
 *
 * @file IOServicePool.h
 * @date 2022-06-14
 */

#pragma once
#include "bcos-utilities/Common.h"
#include <boost/asio.hpp>
#include <memory>
namespace bcos
{
class IOServicePool
{
public:
    using Ptr = std::shared_ptr<IOServicePool>;

    using IOService = boost::asio::io_context;
    using ExecutorType = boost::asio::io_context::executor_type;
    using Work = boost::asio::executor_work_guard<ExecutorType>;
    using WorkPtr = std::unique_ptr<Work>;
    explicit IOServicePool(size_t _workerNum = std::thread::hardware_concurrency() + 1);

    IOServicePool(const IOServicePool&) = delete;
    IOServicePool& operator=(const IOServicePool&) = delete;
    virtual ~IOServicePool();

    void start();

    std::shared_ptr<IOService> getIOService();

    void stop();

private:
    std::vector<std::shared_ptr<IOService>> m_ioServices;
    std::vector<WorkPtr> m_works;
    std::vector<std::thread> m_threads;
    std::atomic_size_t m_nextIOService = 0;
    bool m_running = false;
};
}  // namespace bcos
