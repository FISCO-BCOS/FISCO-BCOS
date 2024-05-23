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
#include "Bloom.h"


#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-rpc/web3jsonrpc/model/Log.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>

#include <bcos-crypto/ChecksumAddress.h>
#include <ostream>

namespace bcos::rpc
{
[[maybe_unused]] static void combineReceiptResponse(Json::Value& result,
    protocol::TransactionReceipt::ConstPtr&& receipt, bcos::protocol::Transaction::ConstPtr&& tx,
    bcos::protocol::Block::Ptr&& block)
{
    if (!result.isObject())
    {
        return;
    }
    uint8_t status = (receipt->status() == 0 ? 1 : 0);
    result["status"] = toQuantity(status);
    result["transactionHash"] = tx->hash().hexPrefixed();
    size_t transactionIndex = 0;
    crypto::HashType blockHash;
    uint64_t blockNumber = 0;
    if (block)
    {
        blockHash = block->blockHeader()->hash();
        blockNumber = block->blockHeader()->number();
        for (; transactionIndex < block->transactionsHashSize(); transactionIndex++)
        {
            if (block->transactionHash(transactionIndex) == tx->hash())
            {
                break;
            }
        }
    }
    result["transactionIndex"] = toQuantity(transactionIndex);
    result["blockHash"] = blockHash.hexPrefixed();
    result["blockNumber"] = toQuantity(blockNumber);
    auto from = toHex(tx->sender());
    toChecksumAddress(from, bcos::crypto::keccak256Hash(bcos::bytesConstRef(from)).hex());
    result["from"] = "0x" + std::move(from);
    if (tx->to().empty())
    {
        result["to"] = Json::nullValue;
    }
    else
    {
        auto toView = tx->to();
        auto to = std::string(toView.starts_with("0x") ? toView.substr(2) : toView);
        toChecksumAddress(to, bcos::crypto::keccak256Hash(bcos::bytesConstRef(to)).hex());
        result["to"] = "0x" + std::move(to);
    }
    result["cumulativeGasUsed"] = "0x0";
    result["effectiveGasPrice"] =
        receipt->effectiveGasPrice().empty() ? "0x0" : std::string(receipt->effectiveGasPrice());
    result["gasUsed"] = toQuantity((uint64_t)receipt->gasUsed());
    if (receipt->contractAddress().empty())
    {
        result["contractAddress"] = Json::nullValue;
    }
    else
    {
        auto contractAddress = std::string(receipt->contractAddress());
        toChecksumAddress(contractAddress,
            bcos::crypto::keccak256Hash(bcos::bytesConstRef(contractAddress)).hex());
        result["contractAddress"] = "0x" + std::move(contractAddress);
    }
    result["logs"] = Json::arrayValue;
    auto mutableReceipt = const_cast<bcos::protocol::TransactionReceipt*>(receipt.get());
    auto receiptLog = mutableReceipt->takeLogEntries();
    for (size_t i = 0; i < receiptLog.size(); i++)
    {
        Json::Value log;
        auto address = std::string(receiptLog[i].address());
        toChecksumAddress(address, bcos::crypto::keccak256Hash(bcos::bytesConstRef(address)).hex());
        log["address"] = "0x" + std::move(address);
        log["topics"] = Json::arrayValue;
        for (const auto& topic : receiptLog[i].topics())
        {
            log["topics"].append(topic.hexPrefixed());
        }
        log["data"] = toHexStringWithPrefix(receiptLog[i].data());
        log["logIndex"] = toQuantity(i);
        log["blockNumber"] = toQuantity(blockNumber);
        log["blockHash"] = blockHash.hexPrefixed();
        log["transactionIndex"] = toQuantity(transactionIndex);
        log["transactionHash"] = tx->hash().hexPrefixed();
        log["removed"] = false;
        result["logs"].append(std::move(log));
    }
    Logs logs;
    logs.reserve(receiptLog.size());
    for (size_t i = 0; i < receiptLog.size(); i++)
    {
        rpc::Log log{.address = std::move(receiptLog[i].takeAddress()),
            .topics = std::move(receiptLog[i].takeTopics()),
            .data = std::move(receiptLog[i].takeData())};
        log.logIndex = i;
        logs.push_back(std::move(log));
    }
    auto logsBloom = getLogsBloom(logs);
    result["logsBloom"] = toHexStringWithPrefix(logsBloom);
    auto type = TransactionType::Legacy;
    if (!tx->extraTransactionBytes().empty())
    {
        if (auto firstByte = tx->extraTransactionBytes()[0];
            firstByte < bcos::codec::rlp::BYTES_HEAD_BASE)
        {
            type = static_cast<TransactionType>(firstByte);
        }
    }
    result["type"] = toQuantity(static_cast<uint64_t>(type));
}
}  // namespace bcos::rpc