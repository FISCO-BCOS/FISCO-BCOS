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
 * @file PBFTInitializer.h
 * @author: yujiechen
 * @date 2021-06-10
 */
#pragma once
#include "Common/TarsUtils.h"
#include "libinitializer/ProtocolInitializer.h"
#include <bcos-framework/interfaces/front/FrontServiceInterface.h>
#include <bcos-framework/interfaces/gateway/GatewayInterface.h>
#include <bcos-framework/interfaces/multigroup/GroupInfo.h>
#include <bcos-framework/interfaces/rpc/RPCInterface.h>
#include <bcos-ledger/src/libledger/Ledger.h>
#include <bcos-pbft/pbft/PBFTFactory.h>
#include <bcos-sealer/SealerFactory.h>
#include <bcos-sync/BlockSyncFactory.h>
#include <bcos-tars-protocol/client/RpcServiceClient.h>
#include <bcos-txpool/TxPoolFactory.h>
#include <bcos-utilities/Timer.h>

namespace bcos
{
namespace initializer
{
class PBFTInitializer
{
public:
    using Ptr = std::shared_ptr<PBFTInitializer>;
    PBFTInitializer(bcos::initializer::NodeArchitectureType _nodeArchType,
        bcos::tool::NodeConfig::Ptr _nodeConfig, ProtocolInitializer::Ptr _protocolInitializer,
        bcos::txpool::TxPoolInterface::Ptr _txpool, std::shared_ptr<bcos::ledger::Ledger> _ledger,
        bcos::scheduler::SchedulerInterface::Ptr _scheduler,
        bcos::storage::StorageInterface::Ptr _storage,
        std::shared_ptr<bcos::front::FrontServiceInterface> _frontService);

    virtual ~PBFTInitializer() { stop(); }

    virtual void init();

    virtual void start();
    virtual void stop();

    bcos::txpool::TxPoolInterface::Ptr txpool() { return m_txpool; }
    bcos::sync::BlockSync::Ptr blockSync() { return m_blockSync; }
    bcos::consensus::PBFTImpl::Ptr pbft() { return m_pbft; }
    bcos::sealer::Sealer::Ptr sealer() { return m_sealer; }

    bcos::protocol::BlockFactory::Ptr blockFactory()
    {
        return m_protocolInitializer->blockFactory();
    }
    bcos::crypto::KeyFactory::Ptr keyFactory() { return m_protocolInitializer->keyFactory(); }

    bcos::group::GroupInfo::Ptr groupInfo() { return m_groupInfo; }

protected:
    virtual void initChainNodeInfo(bcos::initializer::NodeArchitectureType _nodeArchType,
        bcos::tool::NodeConfig::Ptr _nodeConfig);
    virtual void createSealer();
    virtual void createPBFT();
    virtual void createSync();
    virtual void registerHandlers();
    std::string generateGenesisConfig(bcos::tool::NodeConfig::Ptr _nodeConfig);
    std::string generateIniConfig(bcos::tool::NodeConfig::Ptr _nodeConfig);

protected:
    bcos::initializer::NodeArchitectureType m_nodeArchType;
    bcos::tool::NodeConfig::Ptr m_nodeConfig;
    ProtocolInitializer::Ptr m_protocolInitializer;

    bcos::txpool::TxPoolInterface::Ptr m_txpool;
    // Note: PBFT and other modules (except rpc and gateway) access ledger with bcos-ledger SDK
    std::shared_ptr<bcos::ledger::Ledger> m_ledger;
    bcos::scheduler::SchedulerInterface::Ptr m_scheduler;
    bcos::storage::StorageInterface::Ptr m_storage;
    std::shared_ptr<bcos::front::FrontServiceInterface> m_frontService;

    bcos::sealer::Sealer::Ptr m_sealer;
    bcos::sync::BlockSync::Ptr m_blockSync;
    bcos::consensus::PBFTImpl::Ptr m_pbft;

    bcos::group::GroupInfo::Ptr m_groupInfo;
};
}  // namespace initializer
}  // namespace bcos