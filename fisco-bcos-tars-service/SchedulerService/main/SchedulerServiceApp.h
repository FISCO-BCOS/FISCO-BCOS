/**
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
 * @brief Application for the SchedulerService
 * @file SchedulerServiceApp.h
 * @author: yujiechen
 * @date 2022-5-10
 */
#pragma once

#include "bcos-framework/rpc/RPCInterface.h"
#include "libinitializer/ProtocolInitializer.h"
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-utilities/BoostLogInitializer.h>
#include <bcos-utilities/Log.h>
#include <servant/Application.h>

#define SCHEDULER_SERVICE_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_DESC("SchedulerServiceApp")

namespace bcostars
{
class SchedulerServiceApp : public tars::Application
{
public:
    SchedulerServiceApp() = default;
    ~SchedulerServiceApp() override {}
    void initialize() override;
    void destroyApp() override {}

protected:
    virtual void createAndInitSchedulerService();
    virtual void createScheduler();
    virtual void fetchConfig();
    virtual void initConfig();

private:
    std::string m_iniConfigPath;
    std::string m_genesisConfigPath;
    bcos::BoostLogInitializer::Ptr m_logInitializer;
    bcos::tool::NodeConfig::Ptr m_nodeConfig;
    bcos::initializer::ProtocolInitializer::Ptr m_protocolInitializer;

    bcos::rpc::RPCInterface::Ptr m_rpc;
    bcos::txpool::TxPoolInterface::Ptr m_txpool;

    bcos::scheduler::SchedulerInterface::Ptr m_scheduler;
};
}  // namespace bcostars