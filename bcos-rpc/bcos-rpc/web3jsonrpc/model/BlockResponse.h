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
 * @file BlockResponse.h
 * @author: kyonGuo
 * @date 2024/4/11
 */

#pragma once
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-rpc/web3jsonrpc/model/TransactionResponse.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>

namespace bcos::rpc
{
void combineBlockResponse(
    Json::Value& result, const bcos::protocol::Block& block, bool fullTxs = false);

/// The block's identity hash as an Ethereum client sees it: the RLP hash on an OP header
/// (whose stored hash() differs — see Ledger's blockHashOverride contract), the stored hash
/// otherwise. Every producer of a blockHash field must use this: the block response, the
/// transaction-by-block-number response and the eth_getLogs log entries all publish values
/// a client correlates with eth_getBlockByNumber(...).hash.
bcos::crypto::HashType blockIdentityHash(const bcos::protocol::BlockHeader& header);
}  // namespace bcos::rpc