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
 * @file TarsCbExecutorManager.h
 * @author: octopuswang
 * @date: 2022-08-01
 */
#pragma once
#include "bcos-scheduler/src/ExecutorManager.h"
#include "bcos-tars-protocol/client/ExecutorServiceClient.h"
#include "bcos-tars-protocol/tars/ExecutorService.h"
#include "bcos-tool/NodeConfig.h"
#include "bcos-utilities/BoostLog.h"
#include "deps/src/wabt_project/src/result.h"
#include "fisco-bcos-tars-service/Common/TarsUtils.h"
#include <bcos-framework/protocol/ServiceDesc.h>
#include <bcos-utilities/Worker.h>
#include <atomic>
#include <memory>
#include <string>

#define CB_EXECUTOR_MANAGER_LOG(LEVEL) \
    BCOS_LOG(LEVEL) << LOG_BADGE("CB_EXECUTOR_MANAGER") << LOG_BADGE("Switch")


namespace bcos::scheduler
{
class TarsCbExecutorManager : public ExecutorManager,
                              public std::enable_shared_from_this<TarsCbExecutorManager>
{
public:
    using Ptr = std::shared_ptr<TarsCbExecutorManager>;
    using EndPointSet = std::shared_ptr<std::set<std::pair<std::string, uint16_t>>>;

    TarsCbExecutorManager(std::string executorServiceName, bcos::tool::NodeConfig::Ptr _nodeConfig)
      : m_nodeConfig(_nodeConfig), m_executorServiceName(executorServiceName)
    {
        CB_EXECUTOR_MANAGER_LOG(INFO)
            << "Initialize " << LOG_KV("executorServiceName", m_executorServiceName);
    }

    TarsCbExecutorManager(TarsCbExecutorManager&&) = delete;
    TarsCbExecutorManager(const TarsCbExecutorManager&) = delete;
    const TarsCbExecutorManager& operator=(const TarsCbExecutorManager&) = delete;
    TarsCbExecutorManager&& operator=(TarsCbExecutorManager&&) = delete;

    virtual ~TarsCbExecutorManager() {}

    void start()
    {
        if (m_running)
        {
            CB_EXECUTOR_MANAGER_LOG(INFO)
                << "Start" << LOG_DESC("cb executor manager is already running")
                << LOG_KV("executorServiceName", m_executorServiceName);
            return;
        }

        m_running = true;

        CB_EXECUTOR_MANAGER_LOG(INFO)
            << "Start" << LOG_KV("executorServiceName", m_executorServiceName);

        auto self = std::weak_ptr<TarsCbExecutorManager>(shared_from_this());

        std::string executorServiceName =
            m_executorServiceName + "." + bcos::protocol::EXECUTOR_SERVANT_NAME;

        if (m_nodeConfig->withoutTarsFramework())
        {
            std::vector<tars::TC_Endpoint> endpoints;
            m_nodeConfig->getTarsClientProxyEndpoints(bcos::protocol::EXECUTOR_NAME, endpoints);

            executorServiceName = bcostars::endPointToString(executorServiceName, endpoints);

            CB_EXECUTOR_MANAGER_LOG(INFO) << "Start" << LOG_DESC("without tars framework")
                                          << LOG_KV("executorServiceName", m_executorServiceName);
        }

        // TODO:

        m_executorServicePrx = bcostars::createServantProxy<bcostars::ExecutorServicePrx>(
            tars::Application::getCommunicator().get(), executorServiceName,
            [self](const tars::TC_Endpoint& ep) {
                auto executorManager = self.lock();
                if (!executorManager)
                {
                    return;
                }

                auto executorServicePrx =
                    bcostars::createServantProxy<bcostars::ExecutorServicePrx>(
                        executorManager->executorServiceName(), ep.getHost(), ep.getPort());

                auto executorName = "executor-" + ep.getHost() + "-" + std::to_string(ep.getPort());
                auto executor =
                    std::make_shared<bcostars::ExecutorServiceClient>(executorServicePrx);

                executorManager->addExecutor(executorName, executor);
            },
            [self](const tars::TC_Endpoint& ep) {
                auto executorManager = self.lock();
                if (!executorManager)
                {
                    return;
                }

                auto executorName = "executor-" + ep.getHost() + "-" + std::to_string(ep.getPort());
                executorManager->removeExecutor(executorName);
                if (executorManager->executorChangeHandler())
                {
                    executorManager->executorChangeHandler()();
                }
            });
    }

    void stop() override { m_running = false; }

    std::string executorServiceName() { return m_executorServiceName; }

private:
    bool m_running = false;
    bcos::tool::NodeConfig::Ptr m_nodeConfig;
    std::string m_executorServiceName;
    bcostars::ExecutorServicePrx m_executorServicePrx;
};
}  // namespace bcos::scheduler
