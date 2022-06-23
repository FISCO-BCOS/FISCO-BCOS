/*
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief Manager remote executor
 * @file TarsRemoteExecutorManager.h
 * @author: jimmyshi
 * @date: 2022-05-25
 */
#pragma once
#include "bcos-scheduler/src/ExecutorManager.h"
#include <bcos-framework/protocol/ServiceDesc.h>
#include <bcos-utilities/Worker.h>

#define EXECUTOR_MANAGER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("EXECUTOR_MANAGER")


namespace bcos::scheduler
{
class TarsRemoteExecutorManager : public ExecutorManager, Worker
{
public:
    using Ptr = std::shared_ptr<TarsRemoteExecutorManager>;
    using EndPointSet = std::shared_ptr<std::set<std::pair<std::string, uint16_t>>>;

    TarsRemoteExecutorManager(std::string executorServiceName)
      : Worker("TarsRemoteExecutorManager", 1000)
    {
        if (executorServiceName.empty())
        {
            return;
        }

        m_executorServiceName = executorServiceName + "." + bcos::protocol::EXECUTOR_SERVANT_NAME;

        EXECUTOR_MANAGER_LOG(INFO) << "Initialize " << threadName() << " "
                                   << LOG_KV("executorServiceName", m_executorServiceName);
    }

    void start()
    {
        EXECUTOR_MANAGER_LOG(INFO) << "Start" << threadName() << " "
                                   << LOG_KV("executorServiceName", m_executorServiceName);
        startWorking();
    }

    virtual ~TarsRemoteExecutorManager() { stopWorking(); };

    void setRemoteExecutorChangeHandler(std::function<void()> handler)
    {
        m_onRemoteExecutorChange = std::move(handler);
    }

    void executeWorker() override;

    void update(EndPointSet endPointMap);

    bool empty() { return size() == 0; }


private:
    std::string buildEndPointUrl(std::string host, uint16_t port)
    {
        auto endPointStr = m_executorServiceName + "@tcp -h " + host + " -p " +
                           boost::lexical_cast<std::string>(port);
        return endPointStr;
    }

private:
    std::function<void()> m_onRemoteExecutorChange;
    std::string m_executorServiceName;

    boost::condition_variable m_signalled;
    boost::mutex x_signalled;

    EndPointSet m_endPointSet = std::make_shared<std::set<std::pair<std::string, uint16_t>>>();
};
}  // namespace bcos::scheduler
