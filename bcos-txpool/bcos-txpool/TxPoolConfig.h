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
 * @brief Transaction pool configuration module,
 *        including transaction pool dependent modules and related configuration information
 * @file TxPoolConfig.h
 * @author: yujiechen
 * @date 2021-05-08
 */
#pragma once
#include "txpool/interfaces/NonceCheckerInterface.h"
#include "txpool/interfaces/TxPoolStorageInterface.h"
#include "txpool/interfaces/TxValidatorInterface.h"
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/TransactionMetaData.h>
#include <bcos-framework/protocol/TransactionSubmitResultFactory.h>
namespace bcos
{
namespace txpool
{
class TxPoolConfig
{
public:
    using Ptr = std::shared_ptr<TxPoolConfig>;
    TxPoolConfig(TxValidatorInterface::Ptr _txValidator,
        bcos::protocol::TransactionSubmitResultFactory::Ptr _txResultFactory,
        bcos::protocol::BlockFactory::Ptr _blockFactory,
        std::shared_ptr<bcos::ledger::LedgerInterface> _ledger,
        NonceCheckerInterface::Ptr _txpoolNonceChecker, int64_t _blockLimit = 1000)
      : m_txValidator(std::move(_txValidator)),
        m_txResultFactory(std::move(_txResultFactory)),
        m_blockFactory(std::move(_blockFactory)),
        m_ledger(std::move(_ledger)),
        m_txPoolNonceChecker(std::move(_txpoolNonceChecker)),
        m_blockLimit(_blockLimit)
    {}

    virtual ~TxPoolConfig() {}
    virtual void setPoolLimit(size_t _poolLimit) { m_poolLimit = _poolLimit; }
    virtual size_t poolLimit() const { return m_poolLimit; }

    NonceCheckerInterface::Ptr txPoolNonceChecker() { return m_txPoolNonceChecker; }

    TxValidatorInterface::Ptr txValidator() { return m_txValidator; }
    bcos::protocol::TransactionSubmitResultFactory::Ptr txResultFactory()
    {
        return m_txResultFactory;
    }

    bcos::protocol::BlockFactory::Ptr blockFactory() { return m_blockFactory; }
    void setBlockFactory(bcos::protocol::BlockFactory::Ptr _blockFactory)
    {
        m_blockFactory = _blockFactory;
    }

    bcos::protocol::TransactionFactory::Ptr txFactory()
    {
        return m_blockFactory->transactionFactory();
    }
    std::shared_ptr<bcos::ledger::LedgerInterface> ledger() { return m_ledger; }
    int64_t blockLimit() const { return m_blockLimit; }

private:
    TxValidatorInterface::Ptr m_txValidator;
    bcos::protocol::TransactionSubmitResultFactory::Ptr m_txResultFactory;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    std::shared_ptr<bcos::ledger::LedgerInterface> m_ledger;
    NonceCheckerInterface::Ptr m_txPoolNonceChecker;
    size_t m_poolLimit = 15000;
    int64_t m_blockLimit = 1000;
};
}  // namespace txpool
}  // namespace bcos