/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file TransactionUtils.h
 * @author: kyonGuo
 * @date 2023/11/28
 */

#pragma once
// if windows, manual include tup/Tars.h first
#ifdef _WIN32
#include <tup/Tars.h>
#endif
#include <bcos-framework/protocol/ProtocolInfo.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <bcos-utilities/DataConvertUtility.h>

#include <json/json.h>
namespace bcostars
{
using TransactionDataPtr = std::shared_ptr<bcostars::TransactionData>;
using TransactionDataUniquePtr = std::unique_ptr<bcostars::TransactionData>;
using TransactionDataConstPtr = std::shared_ptr<const bcostars::TransactionData>;
using TransactionPtr = std::shared_ptr<bcostars::Transaction>;
using TransactionUniquePtr = std::unique_ptr<bcostars::Transaction>;
using TransactionConstPtr = std::shared_ptr<const bcostars::Transaction>;
}  // namespace bcostars
namespace bcos::cppsdk::utilities
{
// txData
[[maybe_unused]] static Json::Value TarsTransactionDataWriteToJsonValue(
    bcostars::TransactionData const& txData)
{
    Json::Value root;
    root["version"] = txData.version;
    root["chainID"] = txData.chainID;
    root["groupID"] = txData.groupID;
    root["blockLimit"] = (int64_t)txData.blockLimit;
    root["nonce"] = txData.nonce;
    root["to"] = txData.to;
    root["input"] = bcos::toHexStringWithPrefix(txData.input);
    root["abi"] = txData.abi;
    if ((uint32_t)txData.version >= (uint32_t)bcos::protocol::TransactionVersion::V1_VERSION)
    {
        root["value"] = txData.value;
        root["gasPrice"] = txData.gasPrice;
        root["gasLimit"] = (int64_t)txData.gasLimit;
        root["maxFeePerGas"] = txData.maxFeePerGas;
        root["maxPriorityFeePerGas"] = txData.maxPriorityFeePerGas;
    }
    if ((uint32_t)txData.version >= (uint32_t)bcos::protocol::TransactionVersion::V2_VERSION)
    {
        root["extension"] = bcos::toHexStringWithPrefix(txData.extension);
    }
    return root;
}

[[maybe_unused]] static std::string TarsTransactionDataWriteToJsonString(
    bcostars::TransactionData const& txData)
{
    Json::Value root = TarsTransactionDataWriteToJsonValue(txData);
    Json::FastWriter writer;
    auto json = writer.write(root);
    return json;
}

[[maybe_unused]] static bcostars::TransactionDataUniquePtr TarsTransactionDataReadFromJsonValue(
    Json::Value const& root)
{
    auto txData = std::make_unique<bcostars::TransactionData>();
    txData->resetDefautlt();
    if (root.isMember("version") && root["version"].isInt())
    {
        txData->version = root["version"].asInt();
    }
    if (root.isMember("chainID") && root["chainID"].isString())
    {
        txData->chainID = root["chainID"].asString();
    }
    if (root.isMember("groupID") && root["groupID"].isString())
    {
        txData->groupID = root["groupID"].asString();
    }
    if (root.isMember("blockLimit") && root["blockLimit"].isInt64())
    {
        txData->blockLimit = root["blockLimit"].asInt64();
    }
    if (root.isMember("nonce") && root["nonce"].isString())
    {
        txData->nonce = root["nonce"].asString();
    }
    if (root.isMember("to") && root["to"].isString())
    {
        txData->to = root["to"].asString();
    }
    if (root.isMember("input") && root["input"].isString())
    {
        auto inputHex = root["input"].asString();
        auto inputBytes = bcos::fromHexWithPrefix(inputHex);
        std::copy(inputBytes.begin(), inputBytes.end(), std::back_inserter(txData->input));
    }
    if (root.isMember("abi") && root["abi"].isString())
    {
        txData->abi = root["abi"].asString();
    }
    if ((uint32_t)txData->version >= (uint32_t)bcos::protocol::TransactionVersion::V1_VERSION)
    {
        if (root.isMember("value") && root["value"].isString())
        {
            txData->value = root["value"].asString();
        }
        if (root.isMember("gasPrice") && root["gasPrice"].isString())
        {
            txData->gasPrice = root["gasPrice"].asString();
        }
        if (root.isMember("gasLimit") && root["gasLimit"].isInt64())
        {
            txData->gasLimit = root["gasLimit"].asInt64();
        }
        if (root.isMember("maxFeePerGas") && root["maxFeePerGas"].isString())
        {
            txData->maxFeePerGas = root["maxFeePerGas"].asString();
        }
        if (root.isMember("maxPriorityFeePerGas") && root["maxPriorityFeePerGas"].isString())
        {
            txData->maxPriorityFeePerGas = root["maxPriorityFeePerGas"].asString();
        }
    }
    if ((uint32_t)txData->version >= (uint32_t)bcos::protocol::TransactionVersion::V2_VERSION)
    {
        if (root.isMember("extension") && root["extension"].isString())
        {
            auto inputBytes = bcos::fromHexWithPrefix(root["extension"].asString());
            std::copy(inputBytes.begin(), inputBytes.end(), std::back_inserter(txData->extension));
        }
    }
    return txData;
}

[[maybe_unused]] static bcostars::TransactionDataUniquePtr TarsTransactionDataReadFromJsonString(
    std::string json)
{
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(json, root))
    {
        return nullptr;
    }
    return TarsTransactionDataReadFromJsonValue(root);
}

// tx
[[maybe_unused]] static std::string TarsTransactionWriteToJsonString(
    bcostars::TransactionUniquePtr const& tx)
{
    Json::Value root;
    root["data"] = TarsTransactionDataWriteToJsonValue(tx->data);
    root["dataHash"] = bcos::toHexStringWithPrefix(tx->dataHash);
    root["signature"] = bcos::toHexStringWithPrefix(tx->signature);
    root["importTime"] = (uint64_t)tx->importTime;
    root["attribute"] = tx->attribute;
    root["sender"] = bcos::toHexStringWithPrefix(tx->sender);
    root["extraData"] = tx->extraData;
    Json::FastWriter writer;
    auto json = writer.write(root);
    return json;
}

[[maybe_unused]] static bcostars::TransactionUniquePtr TarsTransactionReadFromJsonString(
    std::string json)
{
    auto tx = std::make_unique<bcostars::Transaction>();
    tx->resetDefautlt();
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(json, root))
    {
        return nullptr;
    }
    if (root.isMember("data") && root["data"].isObject())
    {
        tx->data = *TarsTransactionDataReadFromJsonValue(root["data"]);
    }
    if (root.isMember("dataHash") && root["dataHash"].isString())
    {
        auto dataHash = bcos::fromHexWithPrefix(root["dataHash"].asString());
        std::copy(dataHash.begin(), dataHash.end(), std::back_inserter(tx->dataHash));
    }
    if (root.isMember("signature") && root["signature"].isString())
    {
        auto signature = bcos::fromHexWithPrefix(root["signature"].asString());
        std::copy(signature.begin(), signature.end(), std::back_inserter(tx->signature));
    }
    if (root.isMember("importTime") && root["importTime"].isInt64())
    {
        tx->importTime = root["importTime"].asInt64();
    }
    if (root.isMember("attribute") && root["attribute"].isInt())
    {
        tx->attribute = root["attribute"].asInt();
    }
    if (root.isMember("sender") && root["sender"].isString())
    {
        auto sender = bcos::fromHexWithPrefix(root["sender"].asString());
        std::copy(sender.begin(), sender.end(), std::back_inserter(tx->sender));
    }
    if (root.isMember("extraData") && root["extraData"].isString())
    {
        tx->extraData = root["extraData"].asString();
    }
    return tx;
}
}  // namespace bcos::cppsdk::utilities