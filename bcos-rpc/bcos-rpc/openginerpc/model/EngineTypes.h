/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file EngineTypes.h
 * @date 2026/5/21
 */

#pragma once

#include <json/json.h>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace bcos::rpc
{
struct Withdrawal
{
    std::string index;
    std::string validatorIndex;
    std::string address;
    std::string amount;
};

struct ForkchoiceState
{
    std::string headBlockHash;
    std::string safeBlockHash;
    std::string finalizedBlockHash;
};

struct PayloadAttributesV3
{
    std::string timestamp;
    std::string prevRandao;
    std::string suggestedFeeRecipient;
    std::vector<Withdrawal> withdrawals;
    std::string parentBeaconBlockRoot;
    std::vector<std::string> transactions;
    bool noTxPool{false};
    std::string gasLimit;
    std::string eip1559Params;
    std::string minBaseFee;
};

struct PayloadStatus
{
    std::string status;
    std::optional<std::string> latestValidHash;
    std::optional<std::string> validationError;
};

struct ExecutionPayloadV3
{
    std::string parentHash;
    std::string feeRecipient;
    std::string stateRoot;
    std::string receiptsRoot;
    std::string logsBloom;
    std::string prevRandao;
    std::string blockNumber;
    std::string gasLimit;
    std::string gasUsed;
    std::string timestamp;
    std::string extraData;
    std::string baseFeePerGas;
    std::string blockHash;
    std::vector<std::string> transactions;
    std::vector<Withdrawal> withdrawals;
    std::string blobGasUsed;
    std::string excessBlobGas;
};

struct BlobsBundle
{
    std::vector<std::string> commitments;
    std::vector<std::string> proofs;
    std::vector<std::string> blobs;
};

[[nodiscard]] bool decodeWithdrawal(Json::Value const& _root, Withdrawal& _withdrawal);
[[nodiscard]] bool decodeForkchoiceState(Json::Value const& _root, ForkchoiceState& _forkchoiceState);
[[nodiscard]] bool decodePayloadAttributesV3(
    Json::Value const& _root, PayloadAttributesV3& _payloadAttributes);
[[nodiscard]] bool decodePayloadStatus(Json::Value const& _root, PayloadStatus& _payloadStatus);
[[nodiscard]] bool decodeExecutionPayloadV3(
    Json::Value const& _root, ExecutionPayloadV3& _executionPayload);
[[nodiscard]] bool decodeBlobsBundle(Json::Value const& _root, BlobsBundle& _blobsBundle);

void appendWithdrawal(Json::Value& _result, Withdrawal const& _withdrawal);
void appendForkchoiceState(Json::Value& _result, ForkchoiceState const& _forkchoiceState);
void appendPayloadAttributesV3(Json::Value& _result, PayloadAttributesV3 const& _payloadAttributes);
void appendPayloadStatus(Json::Value& _result, PayloadStatus const& _payloadStatus);
void appendExecutionPayloadV3(Json::Value& _result, ExecutionPayloadV3 const& _executionPayload);
void appendBlobsBundle(Json::Value& _result, BlobsBundle const& _blobsBundle);
}  // namespace bcos::rpc
