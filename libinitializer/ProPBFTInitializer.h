/**
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
 * @brief initializer for the PBFT module
 * @file ProPBFTInitializer.h
 * @author: yujiechen
 * @date 2021-06-10
 */
#pragma once
#include "Common/TarsUtils.h"
#include "libinitializer/PBFTInitializer.h"
#include <bcos-framework/interfaces/front/FrontServiceInterface.h>
#include <bcos-framework/interfaces/gateway/GatewayInterface.h>
#include <bcos-framework/interfaces/rpc/RPCInterface.h>
#include <bcos-utilities/Timer.h>

namespace bcos
{
namespace initializer
{
class ProPBFTInitializer : public PBFTInitializer
{
public:
    using Ptr = std::shared_ptr<ProPBFTInitializer>;
    ProPBFTInitializer(bcos::initializer::NodeArchitectureType _nodeArchType,
        bcos::tool::NodeConfig::Ptr _nodeConfig, ProtocolInitializer::Ptr _protocolInitializer,
        bcos::txpool::TxPoolInterface::Ptr _txpool, std::shared_ptr<bcos::ledger::Ledger> _ledger,
        bcos::scheduler::SchedulerInterface::Ptr _scheduler,
        bcos::storage::StorageInterface::Ptr _storage,
        std::shared_ptr<bcos::front::FrontServiceInterface> _frontService);

    virtual ~ProPBFTInitializer() { stop(); }

    void init() override;

    void start() override;
    void stop() override;

protected:
    // the task triggered by the timer periodically
    virtual void scheduledTask();
    virtual void reportNodeInfo();

    void onGroupInfoChanged() override;

private:
    std::shared_ptr<bcos::Timer> m_timer;
    uint64_t m_timerSchedulerInterval = 3000;

    bcos::gateway::GatewayInterface::Ptr m_gateway;
    bcos::rpc::RPCInterface::Ptr m_rpc;
};
}  // namespace initializer
}  // namespace bcos