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
 * @file RPBFTFactory.cpp
 * @author: kyonGuo
 * @date 2023/7/14
 */

#include "RPBFTFactory.h"
#include <bcos-pbft/core/StateMachine.h>
#include <bcos-pbft/pbft/engine/Validator.h>
#include <bcos-pbft/pbft/protocol/PB/PBFTCodec.h>
#include <bcos-pbft/pbft/protocol/PB/PBFTMessageFactoryImpl.h>
#include <bcos-pbft/pbft/storage/LedgerStorage.h>
#include <bcos-rpbft/bcos-rpbft/rpbft/config/RPBFTConfig.h>
#include <memory>

using namespace bcos;
using namespace bcos::consensus;
using namespace bcos::protocol;

PBFTImpl::Ptr RPBFTFactory::createRPBFT()
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

    PBFT_LOG(INFO) << LOG_DESC("create rPBFTConfig");
    PBFTConfig::Ptr rpbftConfig =
        std::make_shared<RPBFTConfig>(m_cryptoSuite, m_keyPair, pbftMessageFactory, pbftCodec,
            validator, m_frontService, stateMachine, pbftStorage, m_blockFactory);

    PBFT_LOG(INFO) << LOG_DESC("create rPBFTEngine");
    auto pbftEngine = std::make_shared<PBFTEngine>(rpbftConfig);

    PBFT_LOG(INFO) << LOG_DESC("create rPBFT");
    auto ledgerFetcher = std::make_shared<bcos::tool::LedgerConfigFetcher>(m_ledger);
    auto pbft = std::make_shared<PBFTImpl>(pbftEngine);
    pbft->setLedgerFetcher(ledgerFetcher);
    return pbft;
}