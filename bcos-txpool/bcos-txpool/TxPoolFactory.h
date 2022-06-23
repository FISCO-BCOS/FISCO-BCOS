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
 * @brief factory to create txpool
 * @file TxPoolFactory.h
 * @author: yujiechen
 * @date 2021-05-19
 */
#pragma once
#include "bcos-txpool/TxPool.h"
#include "bcos-txpool/TxPoolConfig.h"
#include "bcos-txpool/sync/TransactionSyncConfig.h"
#include <bcos-framework//txpool/TxPoolInterface.h>
namespace bcos
{
namespace txpool
{
class TxPoolFactory
{
public:
    using Ptr = std::shared_ptr<TxPoolFactory>;
    TxPoolFactory(bcos::crypto::NodeIDPtr _nodeId, bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        bcos::protocol::TransactionSubmitResultFactory::Ptr _txResultFactory,
        bcos::protocol::BlockFactory::Ptr _blockFactory,
        bcos::front::FrontServiceInterface::Ptr _frontService,
        std::shared_ptr<bcos::ledger::LedgerInterface> _ledger, std::string const& _groupId,
        std::string const& _chainId, int64_t _blockLimit);

    virtual ~TxPoolFactory() {}
    TxPool::Ptr createTxPool(size_t _notifyWorkerNum = 2, size_t _verifierWorkerNum = 1,
        int64_t _txsExpirationTime = 10 * 60 * 1000);

private:
    bcos::crypto::NodeIDPtr m_nodeId;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    bcos::protocol::TransactionSubmitResultFactory::Ptr m_txResultFactory;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::front::FrontServiceInterface::Ptr m_frontService;
    std::shared_ptr<bcos::ledger::LedgerInterface> m_ledger;
    std::string m_groupId;
    std::string m_chainId;
    int64_t m_blockLimit = 1000;
};
}  // namespace txpool
}  // namespace bcos