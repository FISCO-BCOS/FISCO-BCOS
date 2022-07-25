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
#include <bcos-framework/front/FrontServiceInterface.h>
#include <bcos-tool/NodeConfig.h>
#include <memory>

#ifdef WITH_LIGHTNODE
#include <bcos-lightnode/ledger/LedgerServerImpl.h>
#include <bcos-lightnode/syncer/BlockSyncerServerImpl.h>
#endif

namespace bcos
{
namespace consensus
{
class ConsensusInterface;
}
namespace sync
{
class BlockSyncInterface;
}
namespace txpool
{
class TxPoolInterface;
}
namespace gateway
{
class GatewayInterface;
}
namespace front
{
class FrontService;
}

namespace initializer
{
class ProtocolInitializer;

class FrontServiceInitializer
{
public:
    using Ptr = std::shared_ptr<FrontServiceInitializer>;
    FrontServiceInitializer(bcos::tool::NodeConfig::Ptr _nodeConfig,
        std::shared_ptr<bcos::initializer::ProtocolInitializer> _protocolInitializer,
        std::shared_ptr<bcos::gateway::GatewayInterface> _gateWay);
    virtual ~FrontServiceInitializer() { stop(); }

    virtual void init(std::shared_ptr<bcos::consensus::ConsensusInterface> _pbft,
        std::shared_ptr<bcos::sync::BlockSyncInterface> _blockSync,
        std::shared_ptr<bcos::txpool::TxPoolInterface> _txpool);
    virtual void start();
    virtual void stop();

    bcos::front::FrontServiceInterface::Ptr front();
    bcos::crypto::KeyFactory::Ptr keyFactory();

protected:
    virtual void initMsgHandlers(std::shared_ptr<bcos::consensus::ConsensusInterface> _pbft,
        std::shared_ptr<bcos::sync::BlockSyncInterface> _blockSync,
        std::shared_ptr<bcos::txpool::TxPoolInterface> _txpool);

private:
    bcos::tool::NodeConfig::Ptr m_nodeConfig;
    std::shared_ptr<bcos::initializer::ProtocolInitializer> m_protocolInitializer;
    std::shared_ptr<bcos::gateway::GatewayInterface> m_gateWay;

    std::shared_ptr<bcos::front::FrontService> m_front;
    std::atomic_bool m_running = {false};
};
}  // namespace initializer
}  // namespace bcos