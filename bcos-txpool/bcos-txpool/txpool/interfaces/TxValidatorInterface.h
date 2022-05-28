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
 * @brief Interface to verify the validity of the transaction
 * @file TxValidatorInterface.h
 * @author: yujiechen
 * @date 2021-05-08
 */
#pragma once
#include "bcos-txpool/txpool/interfaces/NonceCheckerInterface.h"
#include <bcos-framework//protocol/Transaction.h>
#include <bcos-protocol/TransactionStatus.h>
namespace bcos
{
namespace txpool
{
class TxValidatorInterface
{
public:
    using Ptr = std::shared_ptr<TxValidatorInterface>;
    TxValidatorInterface() = default;
    virtual ~TxValidatorInterface() {}

    virtual bcos::protocol::TransactionStatus verify(bcos::protocol::Transaction::ConstPtr _tx) = 0;
    virtual bcos::protocol::TransactionStatus submittedToChain(
        bcos::protocol::Transaction::ConstPtr _tx) = 0;
    virtual NonceCheckerInterface::Ptr ledgerNonceChecker() { return m_ledgerNonceChecker; }
    virtual void setLedgerNonceChecker(NonceCheckerInterface::Ptr _ledgerNonceChecker)
    {
        m_ledgerNonceChecker = _ledgerNonceChecker;
    }

protected:
    NonceCheckerInterface::Ptr m_ledgerNonceChecker;
};
}  // namespace txpool
}  // namespace bcos