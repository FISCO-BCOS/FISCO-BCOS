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
 * @brief implementation for txpool check
 * @file TransactionValidator.cpp
 * @author: asherli
 * @date 2024-12-11
 */
#include "TransactionValidator.h"
#include "bcos-framework/bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/LegacyStorageMethods.h"
#include "bcos-framework/txpool/Constant.h"
#include "bcos-txpool/txpool/interfaces/TxValidatorInterface.h"
#include "bcos-utilities/DataConvertUtility.h"

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txpool;

TransactionStatus TransactionValidator::validateTransaction(const bcos::protocol::Transaction& _tx)
{
    // TODO: EIP-155: Simple replay attack protection
    //  u256 value
    //  TODO: EIP-2681: Limit account nonce to 2^64-1
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

task::Task<TransactionStatus> TransactionValidator::validateTransactionWithState(
    const bcos::protocol::Transaction& _tx, std::shared_ptr<bcos::ledger::LedgerInterface> _ledger)
{
    auto sender = toHex(_tx.sender());

    auto storage = _ledger->getStateStorage();
    if (!storage)
    {
        TX_VALIDATOR_CHECKER_LOG(WARNING) << LOG_BADGE("ValidateTransactionWithState")
                                          << LOG_DESC("Failed to get state storage from ledger");
        co_return TransactionStatus::None;
    }

    bcos::ledger::account::EVMAccount account(*storage, sender, false);

    // if gasPriceConfig is not set, we can skip the balance check
    bool skipBalanceCheck = false;
    if (auto gasPriceConfig = co_await ledger::getSystemConfig(
            *storage, ledger::SYSTEM_KEY_TX_GAS_PRICE, ledger::fromStorage))
    {
        if (auto& [gasPriceStr, blockNumber] = gasPriceConfig.value();
            gasPriceStr == "0x0" || gasPriceStr == "0")
        {
            skipBalanceCheck = true;
            TX_VALIDATOR_CHECKER_LOG(TRACE) << LOG_BADGE("ValidateTransactionWithState")
                                            << LOG_DESC("Skip balance check due to zero gas price")
                                            << LOG_KV("gasPrice", gasPriceStr);
        }
    }


    // EIP-3607: Reject transactions from senders with deployed code
    // EIP-7702: EOA accounts also have code
    // auto code = co_await account.code();
    // if (code != h256{})
    // {
    //     TX_VALIDATOR_CHECKER_LOG(TRACE)
    //         << LOG_BADGE("ValidateTransactionWithState")
    //         << LOG_DESC("RejectTransactionWithDeployedCode") << LOG_KV("sender", sender)
    //         << LOG_KV("code", code);
    //     co_return TransactionStatus::SenderNoEOA;
    // }

    if (!skipBalanceCheck)
    {
        // 检查账户余额
        auto balanceValue = co_await account.balance();
        auto txValue = u256(_tx.value());
        if (balanceValue < txValue)
        {
            TX_VALIDATOR_CHECKER_LOG(TRACE)
                << LOG_BADGE("ValidateTransactionWithState") << LOG_DESC("InsufficientFunds")
                << LOG_KV("sender", sender) << LOG_KV("balance", balanceValue)
                << LOG_KV("txValue", txValue);
            co_return TransactionStatus::InsufficientFunds;
        }
    }

    co_return TransactionStatus::None;
}
