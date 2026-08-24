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
 * @file EngineTypes.cpp
 * @date 2026/5/21
 */

#include "EngineTypes.h"

namespace bcos::rpc
{
namespace
{
bool readStringField(Json::Value const& _root, char const* _field, std::string& _value)
{
    auto const* value = _root.find(_field);
    if (value == nullptr || !value->isString())
    {
        return false;
    }
    _value = value->asString();
    return true;
}

bool readOptionalStringField(
    Json::Value const& _root, char const* _field, std::optional<std::string>& _value)
{
    auto const* value = _root.find(_field);
    if (value == nullptr || value->isNull())
    {
        _value = std::nullopt;
        return true;
    }
    if (!value->isString())
    {
        return false;
    }
    _value = value->asString();
    return true;
}

bool readOptionalStringField(Json::Value const& _root, char const* _field, std::string& _value)
{
    auto const* value = _root.find(_field);
    if (value == nullptr || value->isNull())
    {
        _value.clear();
        return true;
    }
    if (!value->isString())
    {
        return false;
    }
    _value = value->asString();
    return true;
}

bool readOptionalBoolField(Json::Value const& _root, char const* _field, bool& _value)
{
    auto const* value = _root.find(_field);
    if (value == nullptr || value->isNull())
    {
        _value = false;
        return true;
    }
    if (!value->isBool())
    {
        return false;
    }
    _value = value->asBool();
    return true;
}

bool readOptionalStringArrayField(
    Json::Value const& _root, char const* _field, std::vector<std::string>& _value)
{
    auto const* value = _root.find(_field);
    if (value == nullptr || value->isNull())
    {
        _value.clear();
        return true;
    }
    if (!value->isArray())
    {
        return false;
    }
    std::vector<std::string> result;
    result.reserve(value->size());
    for (auto const& item : *value)
    {
        if (!item.isString())
        {
            return false;
        }
        result.emplace_back(item.asString());
    }
    _value = std::move(result);
    return true;
}

bool readStringArrayField(Json::Value const& _root, char const* _field, std::vector<std::string>& _value)
{
    auto const* value = _root.find(_field);
    if (value == nullptr || !value->isArray())
    {
        return false;
    }
    std::vector<std::string> result;
    result.reserve(value->size());
    for (auto const& item : *value)
    {
        if (!item.isString())
        {
            return false;
        }
        result.emplace_back(item.asString());
    }
    _value = std::move(result);
    return true;
}

bool readWithdrawalsField(Json::Value const& _root, char const* _field, std::vector<Withdrawal>& _value)
{
    auto const* value = _root.find(_field);
    if (value == nullptr || !value->isArray())
    {
        return false;
    }
    std::vector<Withdrawal> result;
    result.reserve(value->size());
    for (auto const& item : *value)
    {
        Withdrawal withdrawal;
        if (!decodeWithdrawal(item, withdrawal))
        {
            return false;
        }
        result.emplace_back(std::move(withdrawal));
    }
    _value = std::move(result);
    return true;
}

void appendOptionalStringField(Json::Value& _result, char const* _field, std::optional<std::string> const& _value)
{
    if (_value.has_value())
    {
        _result[_field] = _value.value();
    }
    else
    {
        _result[_field] = Json::Value();
    }
}

void appendStringArrayField(Json::Value& _result, char const* _field, std::vector<std::string> const& _value)
{
    auto array = Json::Value(Json::arrayValue);
    for (auto const& item : _value)
    {
        array.append(item);
    }
    _result[_field] = std::move(array);
}

void appendWithdrawalsField(Json::Value& _result, char const* _field, std::vector<Withdrawal> const& _value)
{
    auto array = Json::Value(Json::arrayValue);
    for (auto const& item : _value)
    {
        Json::Value withdrawal(Json::objectValue);
        appendWithdrawal(withdrawal, item);
        array.append(std::move(withdrawal));
    }
    _result[_field] = std::move(array);
}
}  // namespace

bool decodeWithdrawal(Json::Value const& _root, Withdrawal& _withdrawal)
{
    if (!_root.isObject())
    {
        return false;
    }
    return readStringField(_root, "index", _withdrawal.index) &&
           readStringField(_root, "validatorIndex", _withdrawal.validatorIndex) &&
           readStringField(_root, "address", _withdrawal.address) &&
           readStringField(_root, "amount", _withdrawal.amount);
}

bool decodeForkchoiceState(Json::Value const& _root, ForkchoiceState& _forkchoiceState)
{
    if (!_root.isObject())
    {
        return false;
    }
    return readStringField(_root, "headBlockHash", _forkchoiceState.headBlockHash) &&
           readStringField(_root, "safeBlockHash", _forkchoiceState.safeBlockHash) &&
           readStringField(_root, "finalizedBlockHash", _forkchoiceState.finalizedBlockHash);
}

bool decodePayloadAttributesV3(Json::Value const& _root, PayloadAttributesV3& _payloadAttributes)
{
    if (!_root.isObject())
    {
        return false;
    }
    return readStringField(_root, "timestamp", _payloadAttributes.timestamp) &&
           readStringField(_root, "prevRandao", _payloadAttributes.prevRandao) &&
           readStringField(_root, "suggestedFeeRecipient", _payloadAttributes.suggestedFeeRecipient) &&
           readWithdrawalsField(_root, "withdrawals", _payloadAttributes.withdrawals) &&
           readStringField(_root, "parentBeaconBlockRoot", _payloadAttributes.parentBeaconBlockRoot) &&
           readOptionalStringArrayField(_root, "transactions", _payloadAttributes.transactions) &&
           readOptionalBoolField(_root, "noTxPool", _payloadAttributes.noTxPool) &&
           readOptionalStringField(_root, "gasLimit", _payloadAttributes.gasLimit) &&
           readOptionalStringField(_root, "eip1559Params", _payloadAttributes.eip1559Params) &&
           readOptionalStringField(_root, "minBaseFee", _payloadAttributes.minBaseFee);
}

bool decodePayloadStatus(Json::Value const& _root, PayloadStatus& _payloadStatus)
{
    if (!_root.isObject())
    {
        return false;
    }
    return readStringField(_root, "status", _payloadStatus.status) &&
           readOptionalStringField(_root, "latestValidHash", _payloadStatus.latestValidHash) &&
           readOptionalStringField(_root, "validationError", _payloadStatus.validationError);
}

bool decodeExecutionPayloadV3(Json::Value const& _root, ExecutionPayloadV3& _executionPayload)
{
    if (!_root.isObject())
    {
        return false;
    }
    return readStringField(_root, "parentHash", _executionPayload.parentHash) &&
           readStringField(_root, "feeRecipient", _executionPayload.feeRecipient) &&
           readStringField(_root, "stateRoot", _executionPayload.stateRoot) &&
           readStringField(_root, "receiptsRoot", _executionPayload.receiptsRoot) &&
           readStringField(_root, "logsBloom", _executionPayload.logsBloom) &&
           readStringField(_root, "prevRandao", _executionPayload.prevRandao) &&
           readStringField(_root, "blockNumber", _executionPayload.blockNumber) &&
           readStringField(_root, "gasLimit", _executionPayload.gasLimit) &&
           readStringField(_root, "gasUsed", _executionPayload.gasUsed) &&
           readStringField(_root, "timestamp", _executionPayload.timestamp) &&
           readStringField(_root, "extraData", _executionPayload.extraData) &&
           readStringField(_root, "baseFeePerGas", _executionPayload.baseFeePerGas) &&
           readStringField(_root, "blockHash", _executionPayload.blockHash) &&
           readStringArrayField(_root, "transactions", _executionPayload.transactions) &&
           readWithdrawalsField(_root, "withdrawals", _executionPayload.withdrawals) &&
           readStringField(_root, "blobGasUsed", _executionPayload.blobGasUsed) &&
           readStringField(_root, "excessBlobGas", _executionPayload.excessBlobGas);
}

bool decodeBlobsBundle(Json::Value const& _root, BlobsBundle& _blobsBundle)
{
    if (!_root.isObject())
    {
        return false;
    }
    return readStringArrayField(_root, "commitments", _blobsBundle.commitments) &&
           readStringArrayField(_root, "proofs", _blobsBundle.proofs) &&
           readStringArrayField(_root, "blobs", _blobsBundle.blobs);
}

void appendWithdrawal(Json::Value& _result, Withdrawal const& _withdrawal)
{
    _result["index"] = _withdrawal.index;
    _result["validatorIndex"] = _withdrawal.validatorIndex;
    _result["address"] = _withdrawal.address;
    _result["amount"] = _withdrawal.amount;
}

void appendForkchoiceState(Json::Value& _result, ForkchoiceState const& _forkchoiceState)
{
    _result["headBlockHash"] = _forkchoiceState.headBlockHash;
    _result["safeBlockHash"] = _forkchoiceState.safeBlockHash;
    _result["finalizedBlockHash"] = _forkchoiceState.finalizedBlockHash;
}

void appendPayloadAttributesV3(Json::Value& _result, PayloadAttributesV3 const& _payloadAttributes)
{
    _result["timestamp"] = _payloadAttributes.timestamp;
    _result["prevRandao"] = _payloadAttributes.prevRandao;
    _result["suggestedFeeRecipient"] = _payloadAttributes.suggestedFeeRecipient;
    appendWithdrawalsField(_result, "withdrawals", _payloadAttributes.withdrawals);
    _result["parentBeaconBlockRoot"] = _payloadAttributes.parentBeaconBlockRoot;
    appendStringArrayField(_result, "transactions", _payloadAttributes.transactions);
    _result["noTxPool"] = _payloadAttributes.noTxPool;
    if (!_payloadAttributes.gasLimit.empty())
    {
        _result["gasLimit"] = _payloadAttributes.gasLimit;
    }
    if (!_payloadAttributes.eip1559Params.empty())
    {
        _result["eip1559Params"] = _payloadAttributes.eip1559Params;
    }
    if (!_payloadAttributes.minBaseFee.empty())
    {
        _result["minBaseFee"] = _payloadAttributes.minBaseFee;
    }
}

void appendPayloadStatus(Json::Value& _result, PayloadStatus const& _payloadStatus)
{
    _result["status"] = _payloadStatus.status;
    appendOptionalStringField(_result, "latestValidHash", _payloadStatus.latestValidHash);
    appendOptionalStringField(_result, "validationError", _payloadStatus.validationError);
}

void appendExecutionPayloadV3(Json::Value& _result, ExecutionPayloadV3 const& _executionPayload)
{
    _result["parentHash"] = _executionPayload.parentHash;
    _result["feeRecipient"] = _executionPayload.feeRecipient;
    _result["stateRoot"] = _executionPayload.stateRoot;
    _result["receiptsRoot"] = _executionPayload.receiptsRoot;
    _result["logsBloom"] = _executionPayload.logsBloom;
    _result["prevRandao"] = _executionPayload.prevRandao;
    _result["blockNumber"] = _executionPayload.blockNumber;
    _result["gasLimit"] = _executionPayload.gasLimit;
    _result["gasUsed"] = _executionPayload.gasUsed;
    _result["timestamp"] = _executionPayload.timestamp;
    _result["extraData"] = _executionPayload.extraData;
    _result["baseFeePerGas"] = _executionPayload.baseFeePerGas;
    _result["blockHash"] = _executionPayload.blockHash;
    appendStringArrayField(_result, "transactions", _executionPayload.transactions);
    appendWithdrawalsField(_result, "withdrawals", _executionPayload.withdrawals);
    _result["blobGasUsed"] = _executionPayload.blobGasUsed;
    _result["excessBlobGas"] = _executionPayload.excessBlobGas;
}

void appendBlobsBundle(Json::Value& _result, BlobsBundle const& _blobsBundle)
{
    appendStringArrayField(_result, "commitments", _blobsBundle.commitments);
    appendStringArrayField(_result, "proofs", _blobsBundle.proofs);
    appendStringArrayField(_result, "blobs", _blobsBundle.blobs);
}
}  // namespace bcos::rpc
