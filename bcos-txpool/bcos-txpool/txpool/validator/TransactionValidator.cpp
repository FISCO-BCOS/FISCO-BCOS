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
#include "bcos-task/Wait.h"
#include "bcos-framework/txpool/Constant.h"

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::txpool;

TransactionStatus TransactionValidator::ValidateTransaction(
    bcos::protocol::Transaction::ConstPtr _tx)
{
    // TODO: EIP-155: Simple replay attack protection
    //  u256 value
    //  TODO: EIP-2681: Limit account nonce to 2^64-1
    if (_tx->value().length() > TRANSACTION_VALUE_MAX_LENGTH)
    {
        return TransactionStatus::OverFlowValue;
    }
    // EIP-3860: Limit and meter initcode
    if (_tx->size() > MAX_INITCODE_SIZE)
    {
        return TransactionStatus::OversizedData;
    }

    return TransactionStatus::None;
}

task::Task<TransactionStatus> TransactionValidator::ValidateTransactionWithState(
    bcos::protocol::Transaction::ConstPtr _tx,
    std::shared_ptr<bcos::ledger::LedgerInterface> _ledger)
{
    // EIP-3607: Reject transactions from senders with deployed code
    auto sender = _tx->sender();
    auto accountCodeHashOpt = co_await(
        _ledger->getStorageAt(sender, bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH, 0));
    auto accountCodeHash = accountCodeHashOpt ? accountCodeHashOpt.value() : storage::Entry();
    if (!accountCodeHash.get().empty())
    {
        co_return TransactionStatus::NoEOAAccount;
    }

    // TODO: add calculate gas price
    auto balanceOpt = co_await(
        _ledger->getStorageAt(sender, bcos::ledger::ACCOUNT_TABLE_FIELDS::BALANCE, 0));
    auto balanceValue = balanceOpt ? u256(balanceOpt.value().get()) : u256(0);
    auto txValue = u256(_tx->value());
    if (balanceValue < txValue)
    {
        co_return TransactionStatus::NoEnoughBalance;
    }

    co_return TransactionStatus::None;
}
