/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file TransactionValidator.cpp
 * @author: asherli
 * @date 2024/12/12
 */
#include "TransactionValidator.h"
#include "bcos-framework/txpool/Constant.h"
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-rpc/Common.h>
#include <bcos-rpc/jsonrpc/Common.h>

#include <json/json.h>

using namespace bcos;
using namespace bcos::rpc;

// bool TransactionValidator::checkWeb3ReplayedProtected(
//     std::optional<uint64_t> _txChainId, std::string _chainId)
// {
//     if(!_txChainId.has_value()){
//         return false;
//     }
//     auto chainId = std::to_string(_txChainId.value());
//     return chainId == _chainId;
// }

// bool TransactionValidator::checkBcosReplayedProtected(
//     protocol::Transaction::Ptr _tx, std::string_view _chainId)
// {
//     return _tx->chainId() == _chainId;
// }

protocol::TransactionStatus TransactionValidator::ValidateTransaction(
    protocol::Transaction::Ptr _tx)
{
    if (_tx->value().length() > TRANSACTION_VALUE_MAX_LENGTH)
    {
        return protocol::TransactionStatus::OverFlowValue;
    }
    if (_tx->type() == static_cast<uint8_t>(protocol::TransactionType::Web3Transaction))
    {
        if (_tx->size() > MAX_INITCODE_SIZE)
        {
            return protocol::TransactionStatus::MaxInitCodeSizeExceeded;
        }
    }
    // auto sender = _tx->sender();
    // auto accountCodeHashOpt =
    //     co_await (_ledger->getStorageAt(sender, bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH, 0));
    // auto accountCodeHash = accountCodeHashOpt ? accountCodeHashOpt.value() : storage::Entry();
    // if (!accountCodeHash.get().empty())
    // {
    //     co_return protocol::TransactionStatus::SenderNoEOA;
    // }
    // auto balanceOpt =
    //     co_await (_ledger->getStorageAt(sender, bcos::ledger::ACCOUNT_TABLE_FIELDS::BALANCE, 0));
    // auto balanceValue = balanceOpt ? u256(balanceOpt.value().get()) : u256(0);
    // auto txValue = u256(_tx->value());
    // if (balanceValue < txValue)
    // {
    //     co_return protocol::TransactionStatus::InsufficientFunds;
    // }
    return protocol::TransactionStatus::None;
}

void TransactionValidator::handleTransactionStatus(protocol::TransactionStatus transactionStatus)
{
    if (transactionStatus != protocol::TransactionStatus::None)
    {
        if (transactionStatus == protocol::TransactionStatus::OverFlowValue)
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(JsonRpcError::InvalidParams, "Transaction value overflow"));
        }
        else if (transactionStatus == protocol::TransactionStatus::MaxInitCodeSizeExceeded)
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(JsonRpcError::InvalidParams, "Transaction data too large"));
        }
        // else if (transactionStatus == protocol::TransactionStatus::SenderNoEOA)
        // {
        //     BOOST_THROW_EXCEPTION(JsonRpcException(
        //         JsonRpcError::InvalidParams, "Transaction sender account is not EOA"));
        // }
        // else if (transactionStatus == protocol::TransactionStatus::InsufficientFunds)
        // {
        //     BOOST_THROW_EXCEPTION(JsonRpcException(
        //         JsonRpcError::InvalidParams, "Transaction sender account has no enough balance"));
        // }
        else
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(JsonRpcError::InvalidParams, "Transaction invalid"));
        }
    }
}
