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
 * @file ReceiptResponse.h
 * @author: kyonGuo
 * @date 2024/4/11
 */

#pragma once

#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include <json/value.h>

namespace bcos::rpc
{
class Web3Transaction;

void combineReceiptResponse(Json::Value& result, protocol::TransactionReceipt& receipt,
    const bcos::protocol::Transaction& tx, const crypto::HashType& blockHash);

/// Receipt response for an OP-block transaction, built from a decoded Web3Transaction (OP blocks
/// carry raw EIP-2718 envelopes, not tars Transaction objects). Also emits the OP extension
/// fields (l1Fee/operatorFee/daFootprint/... from receipt.opReceiptMeta()) that
/// combineReceiptResponse's legacy caller does not carry.
void combineReceiptResponseFromWeb3(Json::Value& result, const Web3Transaction& tx,
    protocol::TransactionReceipt& receipt, const crypto::HashType& blockHash);
}  // namespace bcos::rpc
