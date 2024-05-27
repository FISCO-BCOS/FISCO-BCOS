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
 * @brief Tars manager executor
 * @file TarsExecutorManager.cpp
 * @author: octopuswang
 * @date: 2022-08-01
 */

#include "bcos-scheduler/src/TarsExecutorManager.h"

using namespace bcos;
using namespace bcos::scheduler;

void TarsExecutorManager::start()
{
    if (m_running)
    {
        TARS_EXECUTOR_MANAGER_LOG(INFO)
            << LOG_BADGE("start") << LOG_DESC("tars executor manager is already running")
            << LOG_KV("executorServiceName", m_executorServiceName);
        return;
    }

    m_running = true;

    TARS_EXECUTOR_MANAGER_LOG(INFO)
        << LOG_BADGE("start") << LOG_KV("executorServiceName", m_executorServiceName);

    auto self = std::weak_ptr<TarsExecutorManager>(shared_from_this());

    std::string executorServiceName = m_executorServiceName;

    if (m_nodeConfig->withoutTarsFramework())
    {
        std::vector<tars::TC_Endpoint> endpoints;
        m_nodeConfig->getTarsClientProxyEndpoints(bcos::protocol::EXECUTOR_NAME, endpoints);

        executorServiceName = bcostars::endPointToString(executorServiceName, endpoints);

        TARS_EXECUTOR_MANAGER_LOG(INFO) << LOG_BADGE("start") << LOG_DESC("without tars framework")
                                        << LOG_KV("executorServiceName", m_executorServiceName);
    }

    m_executorServicePrx = bcostars::createServantProxy<bcostars::ExecutorServicePrx>(
        tars::Application::getCommunicator().get(), executorServiceName,
        [self, executorServiceName](const tars::TC_Endpoint& _ep) {
            auto executorManager = self.lock();
            if (!executorManager)
            {
                return;
            }

            auto executorServicePrx = bcostars::createServantProxy<bcostars::ExecutorServicePrx>(
                executorManager->executorServiceName(), _ep.getHost(), _ep.getPort());

            auto executorName = "executor-" + _ep.getHost() + "-" + std::to_string(_ep.getPort());
            auto executor = std::make_shared<bcostars::ExecutorServiceClient>(executorServicePrx);

            try
            {
                executor->status([executorName, executor, self](bcos::Error::UniquePtr error,
                                     bcos::protocol::ExecutorStatus::UniquePtr status) {
                    auto executorManager = self.lock();
                    if (!executorManager)
                    {
                        return;
                    }

                    if (error)
                    {
                        TARS_EXECUTOR_MANAGER_LOG(WARNING)
                            << LOG_BADGE("start") << LOG_DESC("getExecutorStatus failed")
                            << LOG_KV("executorName", executorName);
                    }
                    else
                    {
                        bool result = executorManager->addExecutor(executorName, executor);
                        TARS_EXECUTOR_MANAGER_LOG(INFO)
                            << LOG_BADGE("start") << LOG_DESC("addExecutor")
                            << LOG_KV("executorName", executorName) << LOG_KV("seq", status->seq())
                            << LOG_KV("suc", result);
                    }
                });
            }
            catch (...)
            {}
        },
        [self](const tars::TC_Endpoint& _ep) {
            auto executorManager = self.lock();
            if (!executorManager)
            {
                return;
            }

            auto executorName = "executor-" + _ep.getHost() + "-" + std::to_string(_ep.getPort());

            TARS_EXECUTOR_MANAGER_LOG(INFO) << LOG_BADGE("start") << LOG_DESC("removeExecutor")
                                            << LOG_KV("executorName", executorName);
            try
            {
                executorManager->removeExecutor(executorName);
            }
            catch (...)
            {}
        });

    waitForExecutorConnection();

    startTimer();
}

void TarsExecutorManager::waitForExecutorConnection()
{
    int retryTimes = 1;
    do
    {
        auto s = size();
        if (s > 0)
        {
            TARS_EXECUTOR_MANAGER_LOG(INFO)
                << LOG_BADGE("waitForExecutorConnection") << LOG_DESC("executor exist")
                << LOG_KV("executor size", s);
            break;
        }

        std::string message =
            "Waiting for connecting some executors, try times: " + std::to_string(retryTimes) +
            ", max retry times: " + std::to_string(m_waitingExecutorMaxRetryTimes);

        std::cout << message << std::endl;
        EXECUTOR_MANAGER_LOG(INFO) << message;
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // wait for 1s

    } while (size() == 0 && ++retryTimes <= m_waitingExecutorMaxRetryTimes);

    if (retryTimes > m_waitingExecutorMaxRetryTimes)
    {
        // throw error
        throw std::runtime_error("Error: Could not connect any executor after " +
                                 std::to_string(m_waitingExecutorMaxRetryTimes) + " times retry");
    }
}
