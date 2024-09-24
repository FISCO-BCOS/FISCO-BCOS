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
    if (_tx->invalid()) [[unlikely]]
    {
        return TransactionStatus::InvalidSignature;
    }
    // check groupId and chainId
    if (_tx->groupId() != m_groupId &&
        _tx->type() == static_cast<uint8_t>(TransactionType::BCOSTransaction)) [[unlikely]]
    {
        return TransactionStatus::InvalidGroupId;
    }
    if (_tx->chainId() != m_chainId &&
        _tx->type() == static_cast<uint8_t>(TransactionType::BCOSTransaction)) [[unlikely]]
    {
        return TransactionStatus::InvalidChainId;
    }
    if (const auto status = checkTransaction(_tx); status != TransactionStatus::None)
    {
        return status;
    }
    // check signature
    try
    {
        _tx->verify(*m_cryptoSuite->hashImpl(), *m_cryptoSuite->signatureImpl());
    }
    catch (...)
    {
        return TransactionStatus::InvalidSignature;
    }

    if (isSystemTransaction(_tx))
    {
        _tx->setSystemTx(true);
    }
    m_txPoolNonceChecker->insert(_tx->nonce());
    task::syncWait(m_web3NonceChecker->insertMemoryNonce(std::string(_tx->sender()), _tx->nonce()));
    return TransactionStatus::None;
}

bcos::protocol::TransactionStatus TxValidator::checkTransaction(
    bcos::protocol::Transaction::ConstPtr _tx, bool onlyCheckLedgerNonce)
{
    if (_tx->type() == static_cast<uint8_t>(TransactionType::Web3Transaction)) [[unlikely]]
    {
        auto const status = checkWeb3Nonce(_tx, onlyCheckLedgerNonce);
        if (status != TransactionStatus::None)
        {
            return status;
        }
        return TransactionStatus::None;
    }
    // compare with nonces cached in memory, only check nonce in txpool
    auto status = TransactionStatus::None;
    if (!onlyCheckLedgerNonce)
    {
        status = checkTxpoolNonce(_tx);
        if (status != TransactionStatus::None)
        {
            return status;
        }
    }
    // check ledger nonce and block limit
    status = checkLedgerNonceAndBlockLimit(_tx);
    return status;
}


TransactionStatus TxValidator::checkLedgerNonceAndBlockLimit(
    bcos::protocol::Transaction::ConstPtr _tx)
{
    // compare with nonces stored on-chain, and check block limit inside
    auto status = m_ledgerNonceChecker->checkNonce(_tx);
    if (status != TransactionStatus::None)
    {
        return status;
    }
    if (isSystemTransaction(_tx))
    {
        _tx->setSystemTx(true);
    }
    return TransactionStatus::None;
}

TransactionStatus TxValidator::checkTxpoolNonce(bcos::protocol::Transaction::ConstPtr _tx)
{
    return m_txPoolNonceChecker->checkNonce(_tx);
}

bcos::protocol::TransactionStatus TxValidator::checkWeb3Nonce(
    bcos::protocol::Transaction::ConstPtr _tx, bool onlyCheckLedgerNonce)
{
    if (_tx->type() != static_cast<uint8_t>(TransactionType::Web3Transaction)) [[likely]]
    {
        return TransactionStatus::None;
    }
    return task::syncWait(m_web3NonceChecker->checkWeb3Nonce(std::move(_tx), onlyCheckLedgerNonce));
}
