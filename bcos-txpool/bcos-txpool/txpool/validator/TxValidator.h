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
#include <bcos-framework/interfaces/executor/PrecompiledTypeDef.h>
#include <bcos-utilities/DataConvertUtility.h>
namespace bcos
{
namespace txpool
{
class TxValidator : public TxValidatorInterface
{
public:
    using Ptr = std::shared_ptr<TxValidator>;
    TxValidator(NonceCheckerInterface::Ptr _txPoolNonceChecker,
        bcos::crypto::CryptoSuite::Ptr _cryptoSuite, std::string const& _groupId,
        std::string const& _chainId)
      : m_txPoolNonceChecker(_txPoolNonceChecker),
        m_cryptoSuite(_cryptoSuite),
        m_groupId(_groupId),
        m_chainId(_chainId)
    {}
    ~TxValidator() override {}

    bcos::protocol::TransactionStatus verify(bcos::protocol::Transaction::ConstPtr _tx) override;
    bcos::protocol::TransactionStatus submittedToChain(
        bcos::protocol::Transaction::ConstPtr _tx) override;

protected:
    virtual bool isSystemTransaction(bcos::protocol::Transaction::ConstPtr _tx)
    {
        auto txAddress = _tx->to();
        return bcos::precompiled::c_systemTxsAddress.count(
            std::string(txAddress.begin(), txAddress.end()));
    }

private:
    NonceCheckerInterface::Ptr m_txPoolNonceChecker;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    std::string m_groupId;
    std::string m_chainId;
};
}  // namespace txpool
}  // namespace bcos