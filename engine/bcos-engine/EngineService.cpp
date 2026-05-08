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
 * @file EngineService.cpp
 */

#include "EngineService.h"

#include <algorithm>
#include <charconv>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

using namespace bcos::engine;

namespace
{
constexpr std::size_t c_hashBytes = 32;
constexpr std::size_t c_addressBytes = 20;
constexpr std::size_t c_logsBloomBytes = 256;
constexpr std::size_t c_payloadIdBytes = 8;

bool isHexString(std::string_view value)
{
    if (value.size() < 3 || value[0] != '0' || value[1] != 'x')
    {
        return false;
    }
    return std::all_of(value.begin() + 2, value.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    });
}

bool isFixedHex(std::string_view value, std::size_t bytes)
{
    return isHexString(value) && value.size() == (bytes * 2 + 2);
}

bool isQuantity(std::string_view value)
{
    return isHexString(value);
}

std::optional<std::int64_t> parseQuantity(std::string_view value)
{
    if (!isQuantity(value))
    {
        return std::nullopt;
    }

    std::uint64_t parsed = 0;
    auto begin = value.data() + 2;
    auto end = value.data() + value.size();
    auto result = std::from_chars(begin, end, parsed, 16);
    if (result.ec != std::errc{} || result.ptr != end)
    {
        return std::nullopt;
    }
    if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(parsed);
}

std::string hexQuantity(std::uint64_t value)
{
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

std::string encodePayloadSequence(std::uint64_t value)
{
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(static_cast<int>(c_payloadIdBytes * 2))
        << std::setfill('0') << value;
    return out.str();
}

std::string syntheticHash(std::string_view seed)
{
    std::string hex = "0x";
    hex.reserve(c_hashBytes * 2 + 2);
    auto payload = seed.substr(seed.rfind('x') + 1);
    while (hex.size() < c_hashBytes * 2 + 2)
    {
        hex.append(payload.begin(), payload.end());
    }
    hex.resize(c_hashBytes * 2 + 2);
    return hex;
}

std::vector<std::string> supportedCapabilities()
{
    return {"engine_exchangeCapabilities", "engine_forkchoiceUpdatedV1",
        "engine_forkchoiceUpdatedV2", "engine_forkchoiceUpdatedV3", "engine_getPayloadV1",
        "engine_getPayloadV2", "engine_getPayloadV3", "engine_newPayloadV1",
        "engine_newPayloadV2", "engine_newPayloadV3"};
}

bool isGetPayloadVersionCompatible(std::uint32_t requestedVersion, std::uint32_t storedVersion)
{
    if (requestedVersion == 1)
    {
        return storedVersion == 1;
    }
    if (requestedVersion == 2)
    {
        return storedVersion <= 2;
    }
    if (requestedVersion == 3)
    {
        return storedVersion <= 3;
    }
    return false;
}

std::optional<std::string> validatePayloadAttributes(
    const PayloadAttributes& payloadAttributes, std::uint32_t version)
{
    if (!isQuantity(payloadAttributes.timestamp))
    {
        return std::string("payloadAttributes.timestamp must be a hex quantity");
    }
    if (!isFixedHex(payloadAttributes.prevRandao, c_hashBytes))
    {
        return std::string("payloadAttributes.prevRandao must be a 32-byte hash");
    }
    if (!isFixedHex(payloadAttributes.suggestedFeeRecipient, c_addressBytes))
    {
        return std::string("payloadAttributes.suggestedFeeRecipient must be a 20-byte address");
    }
    if (version == 1 && payloadAttributes.withdrawals.has_value())
    {
        return std::string("withdrawals are not part of PayloadAttributesV1");
    }
    if (version <= 2 && payloadAttributes.parentBeaconBlockRoot.has_value())
    {
        return std::string("parentBeaconBlockRoot is only valid for PayloadAttributesV3");
    }
    if (version >= 2 && !payloadAttributes.withdrawals.has_value())
    {
        return std::string("withdrawals are required for PayloadAttributesV2 and V3");
    }
    if (version == 3)
    {
        if (!payloadAttributes.parentBeaconBlockRoot.has_value() ||
            !isFixedHex(*payloadAttributes.parentBeaconBlockRoot, c_hashBytes))
        {
            return std::string("parentBeaconBlockRoot must be a 32-byte hash for V3");
        }
    }
    return std::nullopt;
}

std::optional<std::string> validateExecutionPayload(
    const ExecutionPayload& executionPayload, std::uint32_t version)
{
    if (!isFixedHex(executionPayload.parentHash, c_hashBytes))
    {
        return std::string("executionPayload.parentHash must be a 32-byte hash");
    }
    if (!isFixedHex(executionPayload.feeRecipient, c_addressBytes))
    {
        return std::string("executionPayload.feeRecipient must be a 20-byte address");
    }
    if (!isFixedHex(executionPayload.stateRoot, c_hashBytes) ||
        !isFixedHex(executionPayload.receiptsRoot, c_hashBytes) ||
        !isFixedHex(executionPayload.prevRandao, c_hashBytes) ||
        !isFixedHex(executionPayload.blockHash, c_hashBytes))
    {
        return std::string("executionPayload contains an invalid 32-byte field");
    }
    if (!isFixedHex(executionPayload.logsBloom, c_logsBloomBytes))
    {
        return std::string("executionPayload.logsBloom must be 256 bytes");
    }
    if (!isQuantity(executionPayload.blockNumber) || !isQuantity(executionPayload.gasLimit) ||
        !isQuantity(executionPayload.gasUsed) || !isQuantity(executionPayload.timestamp) ||
        !isQuantity(executionPayload.baseFeePerGas))
    {
        return std::string("executionPayload quantities must be hex encoded");
    }
    if (!isHexString(executionPayload.extraData))
    {
        return std::string("executionPayload.extraData must be hex encoded");
    }
    for (auto const& transaction : executionPayload.transactions)
    {
        if (!isHexString(transaction) || transaction.size() <= 2)
        {
            return std::string("executionPayload.transactions entries must be non-empty hex bytes");
        }
    }
    if (version == 1 && executionPayload.withdrawals.has_value())
    {
        return std::string("withdrawals are not part of ExecutionPayloadV1");
    }
    if (version >= 2 && !executionPayload.withdrawals.has_value())
    {
        return std::string("withdrawals are required for ExecutionPayloadV2 and V3");
    }
    if (version <= 2 &&
        (executionPayload.blobGasUsed.has_value() || executionPayload.excessBlobGas.has_value()))
    {
        return std::string("blob gas fields are only valid for ExecutionPayloadV3");
    }
    if (version == 3)
    {
        if (!executionPayload.blobGasUsed.has_value() ||
            !executionPayload.excessBlobGas.has_value() ||
            !isQuantity(*executionPayload.blobGasUsed) ||
            !isQuantity(*executionPayload.excessBlobGas))
        {
            return std::string("blob gas fields are required for ExecutionPayloadV3");
        }
    }
    return std::nullopt;
}
}  // namespace

EngineService::EngineService(bcos::rpc::NodeService::Ptr nodeService)
  : m_nodeService(std::move(nodeService))
{}

bcos::task::Task<std::vector<std::string>> EngineService::exchangeCapabilities(
    std::vector<std::string> remoteCapabilities)
{
    (void)remoteCapabilities;
    co_return supportedCapabilities();
}

bcos::task::Task<ForkchoiceUpdatedResult> EngineService::updateForkchoice(
    const ForkchoiceState& forkchoiceState,
    const std::optional<PayloadAttributes>& payloadAttributes, std::uint32_t version)
{
    co_return handleForkchoiceUpdate(forkchoiceState, payloadAttributes, version);
}

bcos::task::Task<GetPayloadResult> EngineService::getPayload(
    const PayloadID& payloadId, std::uint32_t version)
{
    co_return handleGetPayload(payloadId, version);
}

bcos::task::Task<PayloadStatus> EngineService::newPayload(
    const NewPayloadRequest& request, std::uint32_t version)
{
    co_return handleNewPayload(request, version);
}

std::optional<std::int64_t> EngineService::getSafeBlockNumber() const
{
    std::shared_lock lock(x_state);
    return m_safeBlockNumber;
}

std::optional<std::int64_t> EngineService::getFinalizedBlockNumber() const
{
    std::shared_lock lock(x_state);
    return m_finalizedBlockNumber;
}

bool EngineService::isVersionSupported(std::uint32_t version)
{
    return version >= static_cast<std::uint32_t>(EngineApiVersion::V1) &&
           version <= static_cast<std::uint32_t>(EngineApiVersion::V3);
}

PayloadStatus EngineService::makeStatus(PayloadValidationStatus status,
    std::optional<std::string> latestValidHash, std::optional<std::string> validationError)
{
    PayloadStatus statusObject;
    statusObject.status = status;
    statusObject.latestValidHash = std::move(latestValidHash);
    statusObject.validationError = std::move(validationError);
    return statusObject;
}

ForkchoiceUpdatedResult EngineService::handleForkchoiceUpdate(
    const ForkchoiceState& forkchoiceState,
    const std::optional<PayloadAttributes>& payloadAttributes, std::uint32_t version)
{
    if (!isVersionSupported(version))
    {
        throw std::invalid_argument("Unsupported Engine API version");
    }
    if (!isFixedHex(forkchoiceState.headBlockHash, c_hashBytes) ||
        !isFixedHex(forkchoiceState.safeBlockHash, c_hashBytes) ||
        !isFixedHex(forkchoiceState.finalizedBlockHash, c_hashBytes))
    {
        ForkchoiceUpdatedResult result;
        result.payloadStatus = makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
            std::string("forkchoice hashes must be 32-byte hex values"));
        return result;
    }
    if (payloadAttributes)
    {
        if (auto validationError = validatePayloadAttributes(*payloadAttributes, version);
            validationError.has_value())
        {
            ForkchoiceUpdatedResult result;
            result.payloadStatus =
                makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError);
            return result;
        }
    }

    std::unique_lock lock(x_state);
    m_forkchoiceState = forkchoiceState;
    updateTrackedBlockNumbers(forkchoiceState);

    ForkchoiceUpdatedResult result;
    result.payloadStatus =
        makeStatus(PayloadValidationStatus::Valid, forkchoiceState.headBlockHash, std::nullopt);
    if (!payloadAttributes)
    {
        return result;
    }

    auto payloadId = nextPayloadID();
    auto payload = buildPayloadSkeleton(forkchoiceState, *payloadAttributes, payloadId, version);
    PayloadEntry entry;
    entry.version = version;
    entry.executionPayload = std::move(payload);
    entry.blockValue = "0x0";
    if (version == static_cast<std::uint32_t>(EngineApiVersion::V3))
    {
        entry.blobsBundle = BlobsBundleV1{};
    }

    m_blockHashToPayloadId[entry.executionPayload.blockHash] = payloadId;
    m_payloadCache[payloadId] = entry;
    result.payloadId = payloadId;
    return result;
}

GetPayloadResult EngineService::handleGetPayload(
    const PayloadID& payloadId, std::uint32_t version) const
{
    if (!isVersionSupported(version))
    {
        throw std::invalid_argument("Unsupported Engine API version");
    }

    std::shared_lock lock(x_state);
    auto it = m_payloadCache.find(payloadId);
    if (it == m_payloadCache.end())
    {
        throw std::out_of_range("Unknown payload");
    }
    if (!isGetPayloadVersionCompatible(version, it->second.version))
    {
        throw std::invalid_argument("Payload version is incompatible with requested method version");
    }

    GetPayloadResult result;
    result.executionPayload = it->second.executionPayload;
    result.blockValue = it->second.blockValue;
    result.blobsBundle = it->second.blobsBundle;
    result.shouldOverrideBuilder = it->second.shouldOverrideBuilder;
    return result;
}

PayloadStatus EngineService::handleNewPayload(
    const NewPayloadRequest& request, std::uint32_t version)
{
    if (!isVersionSupported(version))
    {
        throw std::invalid_argument("Unsupported Engine API version");
    }

    if (auto validationError = validateExecutionPayload(request.executionPayload, version);
        validationError.has_value())
    {
        auto status = validationError->find("blockHash") != std::string::npos ?
                          PayloadValidationStatus::InvalidBlockHash :
                          PayloadValidationStatus::Invalid;
        return makeStatus(status, std::nullopt, validationError);
    }
    if (version <= 2 && request.parentBeaconBlockRoot.has_value())
    {
        return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
            std::string("parentBeaconBlockRoot is only valid for newPayloadV3"));
    }
    if (version == 3)
    {
        if (!request.parentBeaconBlockRoot.has_value() ||
            !isFixedHex(*request.parentBeaconBlockRoot, c_hashBytes))
        {
            return makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
                std::string("parentBeaconBlockRoot must be a 32-byte hash for newPayloadV3"));
        }
        if (request.expectedBlobVersionedHashes.empty() &&
            !request.executionPayload.transactions.empty())
        {
            return makeStatus(PayloadValidationStatus::Accepted, std::nullopt, std::nullopt);
        }
    }

    std::unique_lock lock(x_state);
    auto parentKnown = request.executionPayload.parentHash == m_forkchoiceState.headBlockHash ||
                       m_blockHashToPayloadId.contains(request.executionPayload.parentHash);
    if (!parentKnown)
    {
        return makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt);
    }

    auto payloadIdIt = m_blockHashToPayloadId.find(request.executionPayload.blockHash);
    PayloadID payloadId;
    if (payloadIdIt == m_blockHashToPayloadId.end())
    {
        payloadId = nextPayloadID();
        m_blockHashToPayloadId.emplace(request.executionPayload.blockHash, payloadId);
    }
    else
    {
        payloadId = payloadIdIt->second;
    }

    PayloadEntry entry;
    entry.version = version;
    entry.executionPayload = request.executionPayload;
    entry.blockValue = "0x0";
    if (version == static_cast<std::uint32_t>(EngineApiVersion::V3))
    {
        entry.blobsBundle = BlobsBundleV1{};
    }
    m_payloadCache[payloadId] = std::move(entry);

    return makeStatus(PayloadValidationStatus::Valid, request.executionPayload.blockHash, std::nullopt);
}

PayloadID EngineService::nextPayloadID()
{
    return encodePayloadSequence(m_nextPayloadSequence++);
}

ExecutionPayload EngineService::buildPayloadSkeleton(const ForkchoiceState& forkchoiceState,
    const PayloadAttributes& payloadAttributes, const PayloadID& payloadId,
    std::uint32_t version) const
{
    auto nextBlockNumber = std::uint64_t{0};
    if (auto currentBlockNumber = lookupBlockNumberByHash(forkchoiceState.headBlockHash);
        currentBlockNumber.has_value())
    {
        nextBlockNumber = static_cast<std::uint64_t>(*currentBlockNumber + 1);
    }

    ExecutionPayload executionPayload;
    executionPayload.parentHash = forkchoiceState.headBlockHash;
    executionPayload.feeRecipient = payloadAttributes.suggestedFeeRecipient;
    executionPayload.stateRoot = syntheticHash(std::string("state") + payloadId);
    executionPayload.receiptsRoot = syntheticHash(std::string("receipts") + payloadId);
    executionPayload.logsBloom = std::string("0x") + std::string(c_logsBloomBytes * 2, '0');
    executionPayload.prevRandao = payloadAttributes.prevRandao;
    executionPayload.blockNumber = hexQuantity(nextBlockNumber);
    executionPayload.gasLimit = "0x0";
    executionPayload.gasUsed = "0x0";
    executionPayload.timestamp = payloadAttributes.timestamp;
    executionPayload.extraData = "0x";
    executionPayload.baseFeePerGas = "0x0";
    executionPayload.blockHash = syntheticHash(payloadId);

    if (version >= static_cast<std::uint32_t>(EngineApiVersion::V2))
    {
        executionPayload.withdrawals =
            payloadAttributes.withdrawals.value_or(std::vector<WithdrawalV1>{});
    }
    if (version >= static_cast<std::uint32_t>(EngineApiVersion::V3))
    {
        executionPayload.blobGasUsed = "0x0";
        executionPayload.excessBlobGas = "0x0";
    }
    return executionPayload;
}

std::optional<std::int64_t> EngineService::lookupBlockNumberByHash(const std::string& blockHash) const
{
    auto it = m_blockHashToPayloadId.find(blockHash);
    if (it == m_blockHashToPayloadId.end())
    {
        return std::nullopt;
    }

    auto payloadIt = m_payloadCache.find(it->second);
    if (payloadIt == m_payloadCache.end())
    {
        return std::nullopt;
    }
    return parseQuantity(payloadIt->second.executionPayload.blockNumber);
}

void EngineService::updateTrackedBlockNumbers(const ForkchoiceState& forkchoiceState)
{
    m_safeBlockNumber = lookupBlockNumberByHash(forkchoiceState.safeBlockHash);
    m_finalizedBlockNumber = lookupBlockNumberByHash(forkchoiceState.finalizedBlockHash);
}