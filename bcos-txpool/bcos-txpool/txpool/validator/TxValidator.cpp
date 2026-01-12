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
#include "bcos-framework/bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/LegacyStorageMethods.h"
#include "bcos-framework/txpool/Constant.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/DataConvertUtility.h"

#include <bcos-rpc/jsonrpc/Common.h>

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txpool;

TransactionStatus TxValidator::verify(const bcos::protocol::Transaction& _tx)
{
    if (_tx.invalid()) [[unlikely]]
    {
        return TransactionStatus::InvalidSignature;
    }
    if (_tx.type() == static_cast<uint8_t>(TransactionType::BCOSTransaction))
    {
        // check groupId and chainId
        if (_tx.groupId() != m_groupId) [[unlikely]]
        {
            return TransactionStatus::InvalidGroupId;
        }
        if (_tx.chainId() != m_chainId) [[unlikely]]
        {
            return TransactionStatus::InvalidChainId;
        }
    }

    // should check the transaction signature first, because the sender of transaction will be force
    // remove in front module check signature
    try
    {
        _tx.verify(*m_cryptoSuite->hashImpl(), *m_cryptoSuite->signatureImpl());
    }
    catch (...)
    {
        return TransactionStatus::InvalidSignature;
    }

    // should check the transaction signature first, because sender is empty
    if (const auto status = checkTransaction(_tx); status != TransactionStatus::None)
    {
        return status;
    }

    if (isSystemTransaction(_tx))
    {
        _tx.setSystemTx(true);
    }
    m_txPoolNonceChecker->insert(std::string(_tx.nonce()));
    if (_tx.type() == static_cast<uint8_t>(TransactionType::Web3Transaction))
    {
        task::syncWait(m_web3NonceChecker->insertMemoryNonce(
            std::string(_tx.sender()), std::string(_tx.nonce())));
    }
    return TransactionStatus::None;
}

bcos::protocol::TransactionStatus TxValidator::checkTransaction(
    const bcos::protocol::Transaction& _tx, bool onlyCheckLedgerNonce)
{
    if (_tx.type() == static_cast<uint8_t>(TransactionType::Web3Transaction))
    {
        return checkWeb3Nonce(_tx, onlyCheckLedgerNonce);
    }
    // compare with nonces cached in memory, only check nonce in txpool
    if (!onlyCheckLedgerNonce)
    {
        if (auto status = checkTxpoolNonce(_tx); status != TransactionStatus::None)
        {
            return status;
        }
    }
    // check ledger nonce and block limit
    auto status = checkLedgerNonceAndBlockLimit(_tx);
    return status;
}


TransactionStatus TxValidator::checkLedgerNonceAndBlockLimit(const bcos::protocol::Transaction& _tx)
{
    // compare with nonces stored on-chain, and check block limit inside
    auto status = m_ledgerNonceChecker->checkNonce(_tx);
    if (status != TransactionStatus::None)
    {
        return status;
    }
    if (isSystemTransaction(_tx))
    {
        _tx.setSystemTx(true);
    }
    return TransactionStatus::None;
}

TransactionStatus TxValidator::checkTxpoolNonce(const bcos::protocol::Transaction& _tx)
{
    return m_txPoolNonceChecker->checkNonce(_tx);
}

bcos::protocol::TransactionStatus TxValidator::checkWeb3Nonce(
    const bcos::protocol::Transaction& _tx, bool onlyCheckLedgerNonce)
{
    if (_tx.type() != static_cast<uint8_t>(TransactionType::Web3Transaction)) [[likely]]
    {
        return TransactionStatus::None;
    }
    return task::syncWait(web3NonceChecker()->checkWeb3Nonce(_tx, onlyCheckLedgerNonce));
}

TransactionStatus TxValidator::validateTransaction(const bcos::protocol::Transaction& _tx)
{
    if (_tx.value().length() > TRANSACTION_VALUE_MAX_LENGTH)
    {
        return TransactionStatus::OverFlowValue;
    }
    // EIP-3860: Limit and meter initcode
    if (_tx.type() == TransactionType::Web3Transaction)
    {
        if (_tx.input().size() > MAX_INITCODE_SIZE)
        {
            TX_VALIDATOR_CHECKER_LOG(TRACE) << LOG_BADGE("ValidateTransaction")
                                            << LOG_DESC("RejectTransactionWithLargeInitCode")
                                            << LOG_KV("txSize", _tx.input().size())
                                            << LOG_KV("maxInitCodeSize", MAX_INITCODE_SIZE);
            // Reject transactions with initcode larger than MAX_INITCODE_SIZE
            return TransactionStatus::MaxInitCodeSizeExceeded;
        }
    }

    return TransactionStatus::None;
}

task::Task<TransactionStatus> TxValidator::validateBalance(
    const bcos::protocol::Transaction& _tx, std::shared_ptr<bcos::ledger::LedgerInterface> _ledger)
{
    auto sender = toHex(_tx.sender());

    u256 balanceValue{};

    // Try to get pending balance from scheduler first
    if (auto scheduler = m_scheduler.lock())
    {
        try
        {
            const auto currentBlockNumber = co_await ledger::getCurrentBlockNumber(*_ledger);
            if (const auto balanceEntry = co_await scheduler->getPendingStorageAt(
                    sender, ledger::ACCOUNT_TABLE_FIELDS::BALANCE, currentBlockNumber))
            {
                if (const auto balanceStr = balanceEntry->get(); !balanceStr.empty())
                {
                    balanceValue = boost::lexical_cast<u256>(balanceStr);
                    TX_VALIDATOR_CHECKER_LOG(TRACE)
                        << LOG_BADGE("ValidateTransactionWithState")
                        << LOG_DESC("Get balance from scheduler pending storage")
                        << LOG_KV("sender", sender) << LOG_KV("balance", balanceValue);
                }
            }
        }
        catch (std::exception const& e)
        {
            TX_VALIDATOR_CHECKER_LOG(WARNING)
                << LOG_BADGE("ValidateTransactionWithState")
                << LOG_DESC("Failed to get balance from scheduler, fallback to ledger")
                << LOG_KV("error", boost::diagnostic_information(e));
        }
    }
    // if gasPriceConfig is not set, we can skip the balance check
    bool skipBalanceCheck = false;
    if (auto gasPriceConfig = co_await ledger::getSystemConfig(
            *_ledger->getStateStorage(), ledger::SYSTEM_KEY_TX_GAS_PRICE, ledger::fromStorage))
    {
        if (auto& [gasPriceStr, blockNumber] = gasPriceConfig.value();
            gasPriceStr == "0x0" || gasPriceStr == "0")
        {
            skipBalanceCheck = true;
            TX_VALIDATOR_CHECKER_LOG(TRACE) << LOG_BADGE("validateBalance")
                                            << LOG_DESC("Skip balance check due to zero gas price")
                                            << LOG_KV("gasPrice", gasPriceStr);
        }
    }
    if (auto txValue = u256(_tx.value());
        !skipBalanceCheck && (balanceValue < txValue || balanceValue == 0))
    {
        TX_VALIDATOR_CHECKER_LOG(TRACE)
            << LOG_BADGE("ValidateTransactionWithState") << LOG_DESC("InsufficientFunds")
            << LOG_KV("sender", sender) << LOG_KV("balance", balanceValue)
            << LOG_KV("txValue", txValue);
        co_return TransactionStatus::InsufficientFunds;
    }

    co_return TransactionStatus::None;
}

task::Task<protocol::TransactionStatus> TxValidator::validateChainId(
    const bcos::protocol::Transaction& _tx, std::shared_ptr<bcos::ledger::LedgerInterface> _ledger)
{
    if (_tx.type() != TransactionType::Web3Transaction)
    {
        co_return TransactionStatus::None;
    }
    if (auto config = co_await ledger::getSystemConfig(*_ledger, ledger::SYSTEM_KEY_WEB3_CHAIN_ID))
    {
        auto [chainId, _] = config.value();
        // if legacy tx, chainId is empty or 0, skip the check
        if (!_tx.chainId().empty() && _tx.chainId() != "0")
        {
            // for EIP-155, check chainId
            if (_tx.chainId() != chainId)
            {
                co_return TransactionStatus::InvalidChainId;
            }
        }
    }
    co_return TransactionStatus::None;
}
