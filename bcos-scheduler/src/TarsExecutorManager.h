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
 * @file TarsExecutorManager.h
 * @author: octopuswang
 * @date: 2022-08-01
 */
#pragma once
#include "bcos-scheduler/src/ExecutorManager.h"
#include "bcos-tars-protocol/client/ExecutorServiceClient.h"
#include "bcos-tars-protocol/tars/ExecutorService.h"
#include "bcos-tool/NodeConfig.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Timer.h"

#include "fisco-bcos-tars-service/Common/TarsUtils.h"
#include <bcos-framework/protocol/ServiceDesc.h>
#include <bcos-utilities/Worker.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#define TARS_EXECUTOR_MANAGER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TARS_EXECUTOR_MANAGER")

namespace bcos::scheduler
{
class TarsExecutorManager : public ExecutorManager,
                            public std::enable_shared_from_this<TarsExecutorManager>
{
public:
    using Ptr = std::shared_ptr<TarsExecutorManager>;
    using EndPointSet = std::shared_ptr<std::set<std::pair<std::string, uint16_t>>>;

    TarsExecutorManager(
        const std::string& _executorServiceName, bcos::tool::NodeConfig::Ptr& _nodeConfig)
      : m_nodeConfig(_nodeConfig)
    {
        m_executorServiceName = _executorServiceName + "." + bcos::protocol::EXECUTOR_SERVANT_NAME;

        TARS_EXECUTOR_MANAGER_LOG(INFO)
            << "Initialize " << LOG_KV("executorServiceName", m_executorServiceName);
    }

    TarsExecutorManager(TarsExecutorManager&&) = delete;
    TarsExecutorManager(const TarsExecutorManager&) = delete;
    const TarsExecutorManager& operator=(const TarsExecutorManager&) = delete;
    TarsExecutorManager&& operator=(TarsExecutorManager&&) = delete;

    ~TarsExecutorManager() override = default;

    void start();
    void stop() override { m_running = false; }

    void waitForExecutorConnection();

    std::string executorServiceName() { return m_executorServiceName; }

private:
    bool m_running = false;

    bcos::tool::NodeConfig::Ptr m_nodeConfig;
    std::string m_executorServiceName;
    bcostars::ExecutorServicePrx m_executorServicePrx;
    int m_waitingExecutorMaxRetryTimes = 10;
};
}  // namespace bcos::scheduler
