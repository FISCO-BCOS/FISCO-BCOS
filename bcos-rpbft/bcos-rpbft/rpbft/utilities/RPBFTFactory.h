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
 * @file RPBFTFactory.h
 * @author: kyonGuo
 * @date 2023/7/14
 */

#pragma once

#include "bcos-pbft/pbft/config/PBFTConfig.h"
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/storage/KVStorageHelper.h>
#include <bcos-framework/sync/BlockSyncInterface.h>
#include <bcos-pbft/pbft/PBFTFactory.h>
#include <bcos-tool/LedgerConfigFetcher.h>

#include <utility>

namespace bcos::consensus
{
class RPBFTFactory : public PBFTFactory
{
public:
    using Ptr = std::shared_ptr<RPBFTFactory>;
    RPBFTFactory(bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        bcos::crypto::KeyPairInterface::Ptr _keyPair,
        std::shared_ptr<bcos::front::FrontServiceInterface> _frontService,
        std::shared_ptr<bcos::storage::KVStorageHelper> _storage,
        std::shared_ptr<bcos::ledger::LedgerInterface> _ledger,
        bcos::scheduler::SchedulerInterface::Ptr _scheduler,
        bcos::txpool::TxPoolInterface::Ptr _txpool, bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::protocol::TransactionSubmitResultFactory::Ptr _txResultFactory)
      : PBFTFactory(std::move(_cryptoSuite), std::move(_keyPair), std::move(_frontService),
            std::move(_storage), std::move(_ledger), std::move(_scheduler), std::move(_txpool),
            std::move(_blockFactory), std::move(_txResultFactory))
    {}
    RPBFTFactory& operator=(const RPBFTFactory&) = delete;
    RPBFTFactory(const RPBFTFactory&) = delete;
    RPBFTFactory& operator=(RPBFTFactory&&) = delete;
    RPBFTFactory(RPBFTFactory&&) = delete;

    ~RPBFTFactory() override = default;
    PBFTImpl::Ptr createRPBFT();
};
}  // namespace bcos::consensus