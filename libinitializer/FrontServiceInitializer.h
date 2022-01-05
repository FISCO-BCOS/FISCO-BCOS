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
 * @brief initializer for the the front module
 * @file FrontServiceInitializer.h
 * @author: yujiechen
 * @date 2021-06-10
 */
#pragma once
#include "Common/TarsUtils.h"
#include "libinitializer/ProtocolInitializer.h"
#include <bcos-framework/interfaces/consensus/ConsensusInterface.h>
#include <bcos-framework/interfaces/gateway/GatewayInterface.h>
#include <bcos-framework/interfaces/sync/BlockSyncInterface.h>
#include <bcos-framework/interfaces/txpool/TxPoolInterface.h>
#include <bcos-front/FrontServiceFactory.h>
#include <bcos-tool/NodeConfig.h>

namespace bcos
{
namespace initializer
{
class FrontServiceInitializer
{
public:
    using Ptr = std::shared_ptr<FrontServiceInitializer>;
    FrontServiceInitializer(bcos::tool::NodeConfig::Ptr _nodeConfig,
        bcos::initializer::ProtocolInitializer::Ptr _protocolInitializer,
        bcos::gateway::GatewayInterface::Ptr _gateWay);
    virtual ~FrontServiceInitializer() { stop(); }

    virtual void init(bcos::consensus::ConsensusInterface::Ptr _pbft,
        bcos::sync::BlockSyncInterface::Ptr _blockSync, bcos::txpool::TxPoolInterface::Ptr _txpool);
    virtual void start();
    virtual void stop();

    bcos::front::FrontService::Ptr front() { return m_front; }
    bcos::crypto::KeyFactory::Ptr keyFactory() { return m_protocolInitializer->keyFactory(); }

protected:
    virtual void initMsgHandlers(bcos::consensus::ConsensusInterface::Ptr _pbft,
        bcos::sync::BlockSyncInterface::Ptr _blockSync, bcos::txpool::TxPoolInterface::Ptr _txpool);

private:
    bcos::tool::NodeConfig::Ptr m_nodeConfig;
    bcos::initializer::ProtocolInitializer::Ptr m_protocolInitializer;
    bcos::gateway::GatewayInterface::Ptr m_gateWay;

    bcos::front::FrontService::Ptr m_front;
    std::atomic_bool m_running = {false};
};
}  // namespace initializer
}  // namespace bcos