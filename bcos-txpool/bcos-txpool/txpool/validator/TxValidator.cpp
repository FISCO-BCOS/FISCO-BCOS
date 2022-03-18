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
 * @file TxValidator.cpp
 * @author: yujiechen
 * @date 2021-05-11
 */
#include "TxValidator.h"

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txpool;

TransactionStatus TxValidator::verify(bcos::protocol::Transaction::ConstPtr _tx)
{
    if (_tx->invalid())
    {
        return TransactionStatus::InvalidSignature;
    }
    // check groupId and chainId
    if (_tx->groupId() != m_groupId)
    {
        return TransactionStatus::InvalidGroupId;
    }
    if (_tx->chainId() != m_chainId)
    {
        return TransactionStatus::InvalidChainId;
    }
    // compare with nonces cached in memory
    auto status = m_txPoolNonceChecker->checkNonce(_tx, false);
    if (status != TransactionStatus::None)
    {
        return status;
    }
    status = submittedToChain(_tx);
    if (status != TransactionStatus::None)
    {
        return status;
    }
    // check signature
    try
    {
        _tx->verify();
    }
    catch (std::exception const& e)
    {
        return TransactionStatus::InvalidSignature;
    }

    if (isSystemTransaction(_tx))
    {
        _tx->setSystemTx(true);
    }
    m_txPoolNonceChecker->insert(_tx->nonce());
    return TransactionStatus::None;
}

TransactionStatus TxValidator::submittedToChain(bcos::protocol::Transaction::ConstPtr _tx)
{
    // compare with nonces stored on-chain
    auto status = m_ledgerNonceChecker->checkNonce(_tx);
    if (status != TransactionStatus::None)
    {
        return status;
    }
    return TransactionStatus::None;
}