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
 * @brief implementation of TxValidator
 * @file TxValidator.h
 * @author: yujiechen
 * @date 2021-05-11
 */
#pragma once
#include "bcos-txpool/txpool/interfaces/NonceCheckerInterface.h"
#include "bcos-txpool/txpool/interfaces/TxValidatorInterface.h"
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-utilities/DataConvertUtility.h>

#include <utility>
namespace bcos::txpool
{
class TxValidator : public TxValidatorInterface
{
public:
    using Ptr = std::shared_ptr<TxValidator>;
    TxValidator(NonceCheckerInterface::Ptr _txPoolNonceChecker,
        bcos::crypto::CryptoSuite::Ptr _cryptoSuite, std::string const& _groupId,
        std::string const& _chainId)
      : m_txPoolNonceChecker(std::move(_txPoolNonceChecker)),
        m_cryptoSuite(std::move(_cryptoSuite)),
        m_groupId(_groupId),
        m_chainId(_chainId)
    {}
    ~TxValidator() override = default;

    bcos::protocol::TransactionStatus verify(bcos::protocol::Transaction::ConstPtr _tx) override;
    bcos::protocol::TransactionStatus checkLedgerNonceAndBlockLimit(
        bcos::protocol::Transaction::ConstPtr _tx) override;
    bcos::protocol::TransactionStatus checkTxpoolNonce(
        bcos::protocol::Transaction::ConstPtr _tx) override;

    LedgerNonceChecker::Ptr ledgerNonceChecker() override { return m_ledgerNonceChecker; }
    void setLedgerNonceChecker(LedgerNonceChecker::Ptr _ledgerNonceChecker) override
    {
        m_ledgerNonceChecker = std::move(_ledgerNonceChecker);
    }

protected:
    virtual inline bool isSystemTransaction(bcos::protocol::Transaction::ConstPtr const& _tx)
    {
        return bcos::precompiled::c_systemTxsAddress.contains(_tx->to());
    }

private:
    // check the transaction nonce in txpool
    NonceCheckerInterface::Ptr m_txPoolNonceChecker;
    // check the transaction nonce in ledger, maintenance block number to nonce list mapping, and
    // nonce list which already committed to ledger
    LedgerNonceChecker::Ptr m_ledgerNonceChecker;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    std::string m_groupId;
    std::string m_chainId;
};
}  // namespace bcos::txpool