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
 * @file TransactionResponse.h
 * @author: kyonGuo
 * @date 2024/4/16
 */

#pragma once
#include "Web3Transaction.h"


#include <bcos-crypto/ChecksumAddress.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>

namespace bcos::rpc
{
// block and receipt are nullable
static void combineTxResponse(Json::Value& result, bcos::protocol::Transaction::ConstPtr&& tx,
    protocol::TransactionReceipt::ConstPtr&& receipt, bcos::protocol::Block::Ptr const& block)
{
    if (!result.isObject())
    {
        return;
    }
    size_t transactionIndex = 0;
    crypto::HashType blockHash;
    uint64_t blockNumber = 0;
    if (block)
    {
        blockHash = block->blockHeader()->hash();
        blockNumber = block->blockHeader()->number();
        if (block->transactionsSize() == 0)
        {
            for (; transactionIndex < block->transactionsHashSize(); transactionIndex++)
            {
                if (block->transactionHash(transactionIndex) == tx->hash())
                {
                    break;
                }
            }
        }
        else
        {
            for (; transactionIndex < block->transactionsSize(); transactionIndex++)
            {
                if (block->transaction(transactionIndex)->hash() == tx->hash())
                {
                    break;
                }
            }
        }
    }
    result["blockHash"] = blockHash.hexPrefixed();
    result["blockNumber"] = toQuantity(blockNumber);
    result["transactionIndex"] = toQuantity(transactionIndex);
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
    result["gas"] = toQuantity(tx->gasLimit());
    auto gasPrice = tx->gasPrice();
    if (receipt && !receipt->effectiveGasPrice().empty())
    {
        gasPrice = receipt->effectiveGasPrice();
    }
    // FIXME)): return will case coredump in executor
    result["gasPrice"] = std::string(gasPrice.empty() ? "0x0" : gasPrice);
    result["hash"] = tx->hash().hexPrefixed();
    result["input"] = toHexStringWithPrefix(tx->input());

    if (tx->type() == bcos::protocol::TransactionType::BCOSTransaction) [[unlikely]]
    {
        result["type"] = toQuantity(UINT32_MAX);
        result["nonce"] = tx->nonce();
        result["value"] = std::string(tx->value().empty() ? "0x0" : tx->value());
        result["maxPriorityFeePerGas"] =
            std::string(tx->maxPriorityFeePerGas().empty() ? "0x0" : tx->maxPriorityFeePerGas());
        result["maxFeePerGas"] =
            std::string(tx->maxFeePerGas().empty() ? "0x0" : tx->maxFeePerGas());
        result["chainId"] = "0x0";
    }
    else [[likely]]
    {
        Web3Transaction web3Tx;
        auto extraBytesRef = bcos::bytesRef(const_cast<byte*>(tx->extraTransactionBytes().data()),
            tx->extraTransactionBytes().size());
        codec::rlp::decodeFromPayload(extraBytesRef, web3Tx);
        result["nonce"] = toQuantity(web3Tx.nonce);
        result["type"] = toQuantity(static_cast<uint8_t>(web3Tx.type));
        result["value"] = toQuantity(web3Tx.value);
        if (web3Tx.type >= TransactionType::EIP2930)
        {
            result["accessList"] = Json::arrayValue;
            result["accessList"].resize(web3Tx.accessList.size());
            for (size_t i = 0; i < web3Tx.accessList.size(); i++)
            {
                auto& accessList = web3Tx.accessList[i];
                Json::Value access = Json::objectValue;
                access["address"] = accessList.account.hexPrefixed();
                access["storageKeys"] = Json::arrayValue;
                access["storageKeys"].resize(accessList.storageKeys.size());
                for (size_t j = 0; j < accessList.storageKeys.size(); j++)
                {
                    Json::Value storageKey = accessList.storageKeys[j].hexPrefixed();
                    access["storageKeys"].append(std::move(storageKey));
                }
                result["accessList"].append(std::move(access));
            }
        }
        if (web3Tx.type >= TransactionType::EIP1559)
        {
            result["maxPriorityFeePerGas"] = toQuantity(web3Tx.maxPriorityFeePerGas);
            result["maxFeePerGas"] = toQuantity(web3Tx.maxFeePerGas);
        }
        result["chainId"] = toQuantity(web3Tx.chainId.value_or(0));
        if (web3Tx.type >= TransactionType::EIP4844)
        {
            result["maxFeePerBlobGas"] = web3Tx.maxFeePerBlobGas.str();
            result["blobVersionedHashes"] = Json::arrayValue;
            result["blobVersionedHashes"].resize(web3Tx.blobVersionedHashes.size());
            for (size_t i = 0; i < web3Tx.blobVersionedHashes.size(); i++)
            {
                Json::Value hash = web3Tx.blobVersionedHashes[i].hexPrefixed();
                result["blobVersionedHashes"].append(std::move(hash));
            }
        }
    }
    result["r"] = toQuantity(tx->signatureData().getCroppedData(0, 32));
    result["s"] = toQuantity(tx->signatureData().getCroppedData(32, 32));
    result["v"] = toQuantity(tx->signatureData().getCroppedData(64, 1));
}
}  // namespace bcos::rpc
