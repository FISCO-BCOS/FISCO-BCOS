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

protocol::TransactionStatus TransactionValidator::checkTransaction(
    const protocol::Transaction& _tx, bool isHandleException)
{
    if (_tx.value().length() > TRANSACTION_VALUE_MAX_LENGTH)
    {
        if (isHandleException)
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(JsonRpcError::InvalidParams, "Transaction value overflow"));
        }
        else
        {
            return protocol::TransactionStatus::OverFlowValue;
        }
    }
    if (_tx.type() == static_cast<uint8_t>(protocol::TransactionType::Web3Transaction))
    {
        if (_tx.input().size() > MAX_INITCODE_SIZE)
        {
            if (isHandleException)
            {
                BOOST_THROW_EXCEPTION(
                    JsonRpcException(JsonRpcError::InvalidParams, "Transaction input too large"));
            }
            else
            {
                return protocol::TransactionStatus::MaxInitCodeSizeExceeded;
            }
        }
    }
    return protocol::TransactionStatus::None;
}
