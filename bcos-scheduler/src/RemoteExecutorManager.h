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
 * @file RemoteExecutorManager.h
 * @author: jimmyshi
 * @date: 2022-05-25
 */
#pragma once
#include "bcos-scheduler/src/ExecutorManager.h"
#include <bcos-framework/interfaces/protocol/ServiceDesc.h>
#include <bcos-utilities/Worker.h>

#define EXECUTOR_MANAGER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("EXECUTOR_MANAGER")


namespace bcos::scheduler
{
class RemoteExecutorManager : public ExecutorManager, Worker
{
public:
    using Ptr = std::shared_ptr<RemoteExecutorManager>;
    using EndPointMap = std::shared_ptr<std::map<std::string, uint16_t>>;

    RemoteExecutorManager(std::string executorServiceName) : Worker("RemoteExecutorManager", 1000)
    {
        if (executorServiceName.empty())
        {
            return;
        }

        m_executorServiceName = executorServiceName + "." + bcos::protocol::EXECUTOR_SERVANT_NAME;

        EXECUTOR_MANAGER_LOG(INFO) << "Initialize " << threadName() << " "
                                   << LOG_KV("executorServiceName", m_executorServiceName);
        startWorking();
    }

    virtual ~RemoteExecutorManager() { stopWorking(); };

    void setRemoteExecutorChangeHandler(std::function<void()> handler)
    {
        m_onRemoteExecutorChange = std::move(handler);
    }

    void executeWorker() override;

    void update(EndPointMap endPointMap);


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

    EndPointMap m_endPointMap = std::make_shared<std::map<std::string, uint16_t>>();
};
}  // namespace bcos::scheduler
