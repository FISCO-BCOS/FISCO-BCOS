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
 * @brief config for transaction sync
 * @file TransactionSyncConfig.h
 * @author: yujiechen
 * @date 2021-05-11
 */
#pragma once
#include "bcos-txpool/sync/interfaces/TxsSyncMsgFactory.h"
#include "bcos-txpool/txpool/interfaces/TxPoolStorageInterface.h"
#include <bcos-framework//front/FrontServiceInterface.h>
#include <bcos-framework//ledger/LedgerInterface.h>
#include <bcos-framework//protocol/BlockFactory.h>
#include <bcos-framework//sync/SyncConfig.h>
namespace bcos
{
namespace sync
{
class TransactionSyncConfig : public SyncConfig
{
public:
    using Ptr = std::shared_ptr<TransactionSyncConfig>;
    TransactionSyncConfig(bcos::crypto::NodeIDPtr _nodeId,
        bcos::front::FrontServiceInterface::Ptr _frontService,
        bcos::txpool::TxPoolStorageInterface::Ptr _txpoolStorage,
        bcos::sync::TxsSyncMsgFactory::Ptr _msgFactory,
        bcos::protocol::BlockFactory::Ptr _blockFactory,
        std::shared_ptr<bcos::ledger::LedgerInterface> _ledger)
      : SyncConfig(_nodeId),
        m_frontService(_frontService),
        m_txpoolStorage(_txpoolStorage),
        m_msgFactory(_msgFactory),
        m_blockFactory(_blockFactory),
        m_ledger(_ledger)
    {}

    virtual ~TransactionSyncConfig() {}

    bcos::front::FrontServiceInterface::Ptr frontService() { return m_frontService; }
    bcos::txpool::TxPoolStorageInterface::Ptr txpoolStorage() { return m_txpoolStorage; }
    bcos::sync::TxsSyncMsgFactory::Ptr msgFactory() { return m_msgFactory; }

    bcos::protocol::BlockFactory::Ptr blockFactory() { return m_blockFactory; }

    unsigned networkTimeout() const { return m_networkTimeout; }
    void setNetworkTimeout(unsigned _networkTimeout) { m_networkTimeout = _networkTimeout; }
    unsigned forwardPercent() const { return m_forwardPercent; }
    void setForwardPercent(unsigned _forwardPercent) { m_forwardPercent = _forwardPercent; }
    std::shared_ptr<bcos::ledger::LedgerInterface> ledger() { return m_ledger; }

    // for ut
    void setTxPoolStorage(bcos::txpool::TxPoolStorageInterface::Ptr _txpoolStorage)
    {
        m_txpoolStorage = _txpoolStorage;
    }

private:
    bcos::front::FrontServiceInterface::Ptr m_frontService;
    bcos::txpool::TxPoolStorageInterface::Ptr m_txpoolStorage;
    bcos::sync::TxsSyncMsgFactory::Ptr m_msgFactory;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    std::shared_ptr<bcos::ledger::LedgerInterface> m_ledger;

    // set networkTimeout to 500ms
    unsigned m_networkTimeout = 500;

    unsigned m_forwardPercent = 25;
};
}  // namespace sync
}  // namespace bcos