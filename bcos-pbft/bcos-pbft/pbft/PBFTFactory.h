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
 * @brief factory to create the PBFTEngine
 * @file PBFTFactory.h
 * @author: yujiechen
 * @date 2021-05-19
 */
#pragma once
#include "PBFTImpl.h"
#include "config/PBFTConfig.h"
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/storage/KVStorageHelper.h>
#include <bcos-framework/sync/BlockSyncInterface.h>
#include <bcos-tool/LedgerConfigFetcher.h>

namespace bcos
{
namespace consensus
{
class PBFTFactory : public std::enable_shared_from_this<PBFTFactory>
{
public:
    using Ptr = std::shared_ptr<PBFTFactory>;
    PBFTFactory(bcos::protocol::NodeArchitectureType _nodeArchType,
        bcos::crypto::CryptoSuite::Ptr _cryptoSuite, bcos::crypto::KeyPairInterface::Ptr _keyPair,
        std::shared_ptr<bcos::front::FrontServiceInterface> _frontService,
        std::shared_ptr<bcos::storage::KVStorageHelper> _storage,
        std::shared_ptr<bcos::ledger::LedgerInterface> _ledger,
        bcos::scheduler::SchedulerInterface::Ptr _scheduler,
        bcos::txpool::TxPoolInterface::Ptr _txpool, bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::protocol::TransactionSubmitResultFactory::Ptr _txResultFactory);

    virtual ~PBFTFactory() {}
    virtual PBFTImpl::Ptr createPBFT();

protected:
    bcos::protocol::NodeArchitectureType m_nodeArchType;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    bcos::crypto::KeyPairInterface::Ptr m_keyPair;
    std::shared_ptr<bcos::front::FrontServiceInterface> m_frontService;
    std::shared_ptr<bcos::storage::KVStorageHelper> m_storage;
    std::shared_ptr<bcos::ledger::LedgerInterface> m_ledger;
    bcos::scheduler::SchedulerInterface::Ptr m_scheduler;
    bcos::txpool::TxPoolInterface::Ptr m_txpool;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::protocol::TransactionSubmitResultFactory::Ptr m_txResultFactory;
};
}  // namespace consensus
}  // namespace bcos