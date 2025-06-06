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
 * @file TransactionValidator.h
 * @author: asherli
 * @date 2024/12/12
 */
#pragma once

#include "bcos-protocol/TransactionStatus.h"
#include <bcos-framework/protocol/Transaction.h>

namespace bcos::rpc
{
class TransactionValidator
{
public:
    // EIP-2681: Limit account nonce to 2^64-1
    // EIP-3860: Limit and meter initcode
    // EIP-3607: Reject transactions from senders with deployed code
    // Enough balance
    static protocol::TransactionStatus checkTransaction(
        const protocol::Transaction& _tx, bool isHandleException = false);
};

}  // namespace bcos::rpc