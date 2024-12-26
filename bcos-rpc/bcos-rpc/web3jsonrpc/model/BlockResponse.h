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
    // Only one parent block in BCOS. It is empty for genesis block
    for (const auto& info : block->blockHeader()->parentInfo())
    {
        result["parentHash"] = info.blockHash.hexPrefixed();
    }
    result["nonce"] = "0x0000000000000000";
    // empty uncle hash: keccak256(RLP([]))
    result["sha3Uncles"] = "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347";
    result["logsBloom"] = "0x";
    result["transactionsRoot"] = block->blockHeader()->txsRoot().hexPrefixed();
    result["stateRoot"] = block->blockHeader()->stateRoot().hexPrefixed();
    result["receiptsRoot"] = block->blockHeader()->receiptsRoot().hexPrefixed();
    if (std::cmp_greater(block->blockHeader()->sealerList().size(), block->blockHeader()->sealer()))
    {
        auto pk = block->blockHeader()->sealerList()[block->blockHeader()->sealer()];
        auto hash = crypto::keccak256Hash(bcos::ref(pk));
        Address address = right160(hash);
        auto addrString = address.hex();
        auto addrHash = crypto::keccak256Hash(bytesConstRef(addrString)).hex();
        toChecksumAddress(addrString, addrHash);
        result["miner"] = "0x" + addrString;
    }
    // genesis block
    if (block->blockHeader()->number() == 0)
    {
        result["miner"] = "0x0000000000000000000000000000000000000000";
        result["parentHash"] = "0x0000000000000000000000000000000000000000000000000000000000000000";
    }
    result["difficulty"] = "0x0";
    result["totalDifficulty"] = "0x0";
    result["extraData"] = toHexStringWithPrefix(block->blockHeader()->extraData());
    result["size"] = toQuantity(block->size());
    // TODO: change it wen block gas limit apply
    result["gasLimit"] = toQuantity(30000000ULL);
    result["gasUsed"] = toQuantity((uint64_t)block->blockHeader()->gasUsed());
    result["timestamp"] = toQuantity(block->blockHeader()->timestamp() / 1000);  // to seconds
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