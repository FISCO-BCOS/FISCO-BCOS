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
#include "bcos-tx-validator/TxValidator.h"
#include "txpool/interfaces/NonceCheckerInterface.h"
#include "txpool/utilities/Common.h"
#include "txpool/validator/LedgerNonceChecker.h"
#include "txpool/validator/Web3NonceChecker.h"
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/TransactionMetaData.h>
#include <bcos-framework/protocol/TransactionSubmitResultFactory.h>

namespace bcos::txpool
{
class TxPoolConfig
{
public:
    using Ptr = std::shared_ptr<TxPoolConfig>;
    TxPoolConfig(Web3NonceChecker::Ptr _web3NonceChecker,
        bcos::protocol::TransactionSubmitResultFactory::Ptr _txResultFactory,
        bcos::protocol::BlockFactory::Ptr _blockFactory,
        std::shared_ptr<bcos::ledger::LedgerInterface> _ledger,
        NonceCheckerInterface::Ptr _txpoolNonceChecker, int64_t _blockLimit, size_t _poolLimit,
        bool checkTransactionSignature);

    virtual ~TxPoolConfig() = default;
    virtual void setPoolLimit(size_t _poolLimit);
    virtual size_t poolLimit() const;

    NonceCheckerInterface::Ptr txPoolNonceChecker();

    /// The two BCOS nonce indexes this pool owns. They used to hang off the legacy
    /// TxValidatorInterface, whose only remaining job was to hold them -- its checking methods
    /// were superseded by bcos-tx-validator. Owned here directly instead.
    ///
    /// ledgerNonceChecker is null until TxPool::init learns the chain's block limit; callers
    /// must resolve it per use rather than capture it.
    Web3NonceChecker::Ptr web3NonceChecker() const { return m_web3NonceChecker; }
    LedgerNonceChecker::Ptr ledgerNonceChecker() const { return m_ledgerNonceChecker; }
    void setLedgerNonceChecker(LedgerNonceChecker::Ptr ledgerNonceChecker)
    {
        m_ledgerNonceChecker = std::move(ledgerNonceChecker);
    }

    /// SignaturePolicy for this chain, from experimental.check_transaction_signature.
    txvalidator::SignaturePolicy signaturePolicy() const
    {
        return m_checkTransactionSignature ? txvalidator::SignaturePolicy::Required :
                                             txvalidator::SignaturePolicy::Disabled;
    }
    bcos::protocol::TransactionSubmitResultFactory::Ptr txResultFactory();

    bcos::protocol::BlockFactory::Ptr blockFactory();
    void setBlockFactory(bcos::protocol::BlockFactory::Ptr _blockFactory);

    bcos::protocol::TransactionFactory::Ptr txFactory();
    std::shared_ptr<bcos::ledger::LedgerInterface> ledger();
    int64_t blockLimit() const;

    bool checkTransactionSignature() const;

private:
    Web3NonceChecker::Ptr m_web3NonceChecker;
    LedgerNonceChecker::Ptr m_ledgerNonceChecker;
    bcos::protocol::TransactionSubmitResultFactory::Ptr m_txResultFactory;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    std::shared_ptr<bcos::ledger::LedgerInterface> m_ledger;
    NonceCheckerInterface::Ptr m_txPoolNonceChecker;
    int64_t m_blockLimit = DEFAULT_BLOCK_LIMIT;
    size_t m_poolLimit = DEFAULT_POOL_LIMIT;
    bool m_checkTransactionSignature;
};
}  // namespace bcos::txpool