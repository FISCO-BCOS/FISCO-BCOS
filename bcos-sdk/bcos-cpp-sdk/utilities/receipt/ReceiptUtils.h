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
 * @file ReceiptUtils.h
 * @author: kyonGuo
 * @date 2024/2/26
 */

#pragma once

#include "bcos-framework/protocol/Protocol.h"
#include <bcos-tars-protocol/protocol/TransactionReceiptImpl.h>
#include <json/json.h>

namespace bcostars
{
using ReceiptDataUniquePtr = std::unique_ptr<TransactionReceiptData>;
using ReceiptUniquePtr = std::unique_ptr<TransactionReceipt>;
}  // namespace bcostars
namespace bcos::cppsdk::utilities
{
// receiptData
[[maybe_unused]] static std::string TarsReceiptDataWriteToJsonString(
    bcostars::TransactionReceiptData const& receiptData)
{
    Json::Value root;
    root["version"] = receiptData.version;
    root["gasUsed"] = receiptData.gasUsed;
    root["contractAddress"] = receiptData.contractAddress;
    root["status"] = receiptData.status;
    root["output"] = bcos::toHexStringWithPrefix(receiptData.output);
    root["logEntries"] = Json::Value(Json::arrayValue);
    for (auto const& log : receiptData.logEntries)
    {
        Json::Value logEntry;
        logEntry["address"] = log.address;
        logEntry["topics"] = Json::Value(Json::arrayValue);
        for (auto const& topic : log.topic)
        {
            logEntry["topics"].append(bcos::toHexStringWithPrefix(topic));
        }
        logEntry["data"] = bcos::toHexStringWithPrefix(log.data);
        root["logEntries"].append(logEntry);
    }
    root["blockNumber"] = (int64_t)receiptData.blockNumber;
    if ((uint32_t)receiptData.version >= (uint32_t)protocol::TransactionVersion::V1_VERSION)
    {
        root["effectiveGasPrice"] = receiptData.effectiveGasPrice;
    }
    Json::FastWriter writer;
    auto json = writer.write(root);
    return json;
}

[[maybe_unused]] static bcostars::ReceiptDataUniquePtr TarsReceiptDataReadFromJsonValue(
    Json::Value root)
{
    auto receiptData = std::make_unique<bcostars::TransactionReceiptData>();
    receiptData->resetDefautlt();
    if (root.isMember("version") && root["version"].isInt())
    {
        receiptData->version = root["version"].asInt();
    }
    if (root.isMember("gasUsed") && root["gasUsed"].isString())
    {
        receiptData->gasUsed = root["gasUsed"].asString();
    }
    if (root.isMember("contractAddress") && root["contractAddress"].isString())
    {
        receiptData->contractAddress = root["contractAddress"].asString();
    }
    if (root.isMember("status") && root["status"].isInt())
    {
        receiptData->status = root["status"].asInt();
    }
    if (root.isMember("output") && root["output"].isString())
    {
        auto outputBytes = bcos::fromHexWithPrefix(root["output"].asString());
        std::copy(outputBytes.begin(), outputBytes.end(), std::back_inserter(receiptData->output));
    }
    if (root.isMember("logEntries") && root["logEntries"].isArray())
    {
        for (auto const& log : root["logEntries"])
        {
            bcostars::LogEntry logEntry;
            if (log.isMember("address") && log["address"].isString())
            {
                logEntry.address = log["address"].asString();
            }
            if (log.isMember("topics") && log["topics"].isArray())
            {
                size_t i = 0;
                for (auto const& topic : log["topics"])
                {
                    auto topicBytes = bcos::fromHexWithPrefix(topic.asString());
                    std::vector<tars::Char> v;
                    std::copy(topicBytes.begin(), topicBytes.end(), std::back_inserter(v));
                    logEntry.topic.push_back(v);
                }
            }
            if (log.isMember("data") && log["data"].isString())
            {
                auto dataBytes = bcos::fromHexWithPrefix(log["data"].asString());
                std::copy(dataBytes.begin(), dataBytes.end(), std::back_inserter(logEntry.data));
            }
            receiptData->logEntries.push_back(logEntry);
        }
    }
    receiptData->blockNumber = root["blockNumber"].asUInt64();
    if ((uint32_t)receiptData->version >= (uint32_t)protocol::TransactionVersion::V1_VERSION)
    {
        receiptData->effectiveGasPrice = root["effectiveGasPrice"].asUInt();
    }
    return receiptData;
}

[[maybe_unused]] static bcostars::ReceiptDataUniquePtr TarsReceiptDataReadFromJsonString(
    std::string const& json)
{
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(json, root))
    {
        return nullptr;
    }
    return TarsReceiptDataReadFromJsonValue(root);
}
}  // namespace bcos::cppsdk::utilities
