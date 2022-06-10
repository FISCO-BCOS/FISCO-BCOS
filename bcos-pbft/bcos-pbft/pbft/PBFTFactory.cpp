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
 * @file PBFTFactory.cpp
 * @author: yujiechen
 * @date 2021-05-19
 */
#include "PBFTFactory.h"
#include "bcos-pbft/core/StateMachine.h"
#include "engine/Validator.h"
#include "protocol/PB/PBFTCodec.h"
#include "protocol/PB/PBFTMessageFactoryImpl.h"
#include "storage/LedgerStorage.h"
#include "utilities/Common.h"
#include <bcos-scheduler/src/SchedulerManager.h>
#include <memory>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::protocol;

PBFTFactory::PBFTFactory(bcos::protocol::NodeArchitectureType _nodeArchType,
    bcos::crypto::CryptoSuite::Ptr _cryptoSuite, bcos::crypto::KeyPairInterface::Ptr _keyPair,
    std::shared_ptr<bcos::front::FrontServiceInterface> _frontService,
    std::shared_ptr<bcos::storage::KVStorageHelper> _storage,
    std::shared_ptr<bcos::ledger::LedgerInterface> _ledger,
    bcos::scheduler::SchedulerInterface::Ptr _scheduler, bcos::txpool::TxPoolInterface::Ptr _txpool,
    bcos::protocol::BlockFactory::Ptr _blockFactory,
    bcos::protocol::TransactionSubmitResultFactory::Ptr _txResultFactory)
  : m_nodeArchType(_nodeArchType),
    m_cryptoSuite(_cryptoSuite),
    m_keyPair(_keyPair),
    m_frontService(_frontService),
    m_storage(_storage),
    m_ledger(_ledger),
    m_scheduler(_scheduler),
    m_txpool(_txpool),
    m_blockFactory(_blockFactory),
    m_txResultFactory(_txResultFactory)
{}

PBFTImpl::Ptr PBFTFactory::createPBFT()
{
    auto pbftMessageFactory = std::make_shared<PBFTMessageFactoryImpl>();
    PBFT_LOG(INFO) << LOG_DESC("create PBFTCodec");
    auto pbftCodec = std::make_shared<PBFTCodec>(m_keyPair, m_cryptoSuite, pbftMessageFactory);

    PBFT_LOG(INFO) << LOG_DESC("create PBFT validator");
    auto validator = std::make_shared<TxsValidator>(m_txpool, m_blockFactory, m_txResultFactory);

    PBFT_LOG(DEBUG) << LOG_DESC("create StateMachine");
    auto stateMachine = std::make_shared<StateMachine>(m_scheduler, m_blockFactory);

    PBFT_LOG(INFO) << LOG_DESC("create pbftStorage");
    auto pbftStorage =
        std::make_shared<LedgerStorage>(m_scheduler, m_storage, m_blockFactory, pbftMessageFactory);

    PBFT_LOG(INFO) << LOG_DESC("create pbftConfig");
    auto pbftConfig = std::make_shared<PBFTConfig>(m_cryptoSuite, m_keyPair, pbftMessageFactory,
        pbftCodec, validator, m_frontService, stateMachine, pbftStorage);

    PBFT_LOG(INFO) << LOG_DESC("create PBFTEngine");
    auto pbftEngine = std::make_shared<PBFTEngine>(pbftConfig);

    PBFT_LOG(INFO) << LOG_DESC("create PBFT");
    auto ledgerFetcher = std::make_shared<bcos::tool::LedgerConfigFetcher>(m_ledger);
    auto pbft = std::make_shared<PBFTImpl>(pbftEngine);
    pbft->setLedgerFetcher(ledgerFetcher);

    if (m_nodeArchType == bcos::protocol::NodeArchitectureType::MAX)
    {
        PBFT_LOG(INFO) << LOG_DESC("Register switch handler in scheduler manager");
        // PBFT and scheduler are in the same process here, we just cast m_scheduler to
        // SchedulerService
        auto schedulerServer =
            std::dynamic_pointer_cast<bcos::scheduler::SchedulerManager>(m_scheduler);
        schedulerServer->registerOnSwitchTermHandler(
            [pbftEngine](bcos::protocol::BlockNumber blockNumber) {
                PBFT_LOG(DEBUG) << LOG_BADGE("Switch")
                                << "Receive scheduler switch term notify of number " +
                                       std::to_string(blockNumber);
                pbftEngine->clearExceptionProposalState(blockNumber);
            });
    }

    return pbft;
}