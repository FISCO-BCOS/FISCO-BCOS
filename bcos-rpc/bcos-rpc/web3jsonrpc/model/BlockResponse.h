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
[[maybe_unused]] static void combineBlockResponse(
    Json::Value& result, bcos::protocol::Block::Ptr&& block, bool fullTxs = false)
{
    result["number"] = toQuantity(block->blockHeader()->number());
    result["hash"] = block->blockHeader()->hash().hexPrefixed();
    for (const auto& info : block->blockHeader()->parentInfo())
    {
        result["parentHash"] = info.blockHash.hexPrefixed();
    }
    result["nonce"] = "0x0000000000000000";
    // result["sha3Uncles"] = "0x";
    // result["logsBloom"] = "0x";
    result["transactionsRoot"] = block->blockHeader()->txsRoot().hexPrefixed();
    result["stateRoot"] = block->blockHeader()->stateRoot().hexPrefixed();
    result["receiptsRoot"] = block->blockHeader()->receiptsRoot().hexPrefixed();
    result["miner"] = Address().hexPrefixed();
    result["difficulty"] = "0x0";
    result["totalDifficulty"] = "0x0";
    result["extraData"] = toHexStringWithPrefix(block->blockHeader()->extraData());
    result["size"] = "0xffff";
    result["gasLimit"] = toQuantity(30000000ull);
    result["gasUsed"] = toQuantity((uint64_t)block->blockHeader()->gasUsed());
    result["timestamp"] = toQuantity(block->blockHeader()->timestamp());
    if (fullTxs)
    {
        Json::Value txList = Json::arrayValue;
        for (size_t i = 0; i < block->transactionsSize(); i++)
        {
            Json::Value txJson = Json::objectValue;
            auto tx = block->transaction(i);
            combineTxResponse(txJson, std::move(tx), nullptr, block);
            txList.append(txJson);
        }
        result["transactions"] = std::move(txList);
    }
    else
    {
        Json::Value txHashesList = Json::arrayValue;
        for (size_t i = 0; i < block->transactionsHashSize(); i++)
        {
            txHashesList.append(block->transactionHash(i).hexPrefixed());
        }
        result["transactions"] = std::move(txHashesList);
    }
    result["uncles"] = Json::Value(Json::arrayValue);
}
}  // namespace bcos::rpc