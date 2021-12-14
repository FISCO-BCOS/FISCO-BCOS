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
 * @brief factory to create BlockSync
 * @file BlockSyncFactory.cpp
 * @author: yujiechen
 * @date 2021-05-28
 */
#include "BlockSyncFactory.h"
#include "protocol/PB/BlockSyncMsgFactoryImpl.h"

using namespace bcos;
using namespace bcos::sync;

BlockSyncFactory::BlockSyncFactory(bcos::crypto::PublicPtr _nodeId,
    bcos::protocol::BlockFactory::Ptr _blockFactory,
    bcos::protocol::TransactionSubmitResultFactory::Ptr _txResultFactory,
    bcos::ledger::LedgerInterface::Ptr _ledger, bcos::txpool::TxPoolInterface::Ptr _txpool,
    bcos::front::FrontServiceInterface::Ptr _frontService,
    bcos::scheduler::SchedulerInterface::Ptr _scheduler,
    bcos::consensus::ConsensusInterface::Ptr _consensus)
  : m_nodeId(_nodeId),
    m_blockFactory(_blockFactory),
    m_txResultFactory(_txResultFactory),
    m_ledger(_ledger),
    m_txpool(_txpool),
    m_frontService(_frontService),
    m_scheduler(_scheduler),
    m_consensus(_consensus)
{}

BlockSync::Ptr BlockSyncFactory::createBlockSync()
{
    auto msgFactory = std::make_shared<BlockSyncMsgFactoryImpl>();
    auto syncConfig = std::make_shared<BlockSyncConfig>(m_nodeId, m_ledger, m_txpool,
        m_blockFactory, m_txResultFactory, m_frontService, m_scheduler, m_consensus, msgFactory);
    return std::make_shared<BlockSync>(syncConfig);
}
