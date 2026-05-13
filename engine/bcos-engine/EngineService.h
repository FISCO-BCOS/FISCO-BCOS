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
 * @file EngineService.h
 * @brief Minimal Engine API service abstraction and in-memory implementation
 */

#pragma once

#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/Bloom.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/FixedBytes.h"
#include "mempool/bcos-mempool/MemPoolImpl.h"
#include <cstdint>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos::engine
{
enum class EngineApiVersion : std::uint8_t
{
    V1 = 1,
    V2 = 2,
    V3 = 3,
};

using PayloadID = std::string;

struct WithdrawalV1
{
    u256 index = 0;
    u256 validatorIndex = 0;
    Address address;
    u256 amount = 0;
};

struct BlobsBundleV1
{
    std::vector<bytes> commitments;
    std::vector<bytes> proofs;
    std::vector<bytes> blobs;
};

struct ForkchoiceState
{
    h256 headBlockHash;
    h256 safeBlockHash;
    h256 finalizedBlockHash;
};

struct PayloadAttributes
{
    std::uint64_t timestamp = 0;
    h256 prevRandao;
    Address suggestedFeeRecipient;
    std::optional<std::vector<WithdrawalV1>> withdrawals;
    std::optional<h256> parentBeaconBlockRoot;
};

struct ExecutionPayload
{
    h256 parentHash;
    Address feeRecipient;
    h256 stateRoot;
    h256 receiptsRoot;
    Bloom logsBloom{};
    h256 prevRandao;
    bcos::protocol::BlockNumber blockNumber = 0;
    u256 gasLimit = 0;
    u256 gasUsed = 0;
    std::uint64_t timestamp = 0;
    bytes extraData;
    u256 baseFeePerGas = 0;
    h256 blockHash;
    bcos::protocol::Transactions transactions;
    std::optional<std::vector<WithdrawalV1>> withdrawals;
    std::optional<u256> blobGasUsed;
    std::optional<u256> excessBlobGas;
};

struct NewPayloadRequest
{
    ExecutionPayload executionPayload;
    std::vector<h256> expectedBlobVersionedHashes;
    std::optional<h256> parentBeaconBlockRoot;
};

enum class PayloadValidationStatus : std::uint8_t
{
    Valid,
    Invalid,
    Syncing,
    Accepted,
    InvalidBlockHash,
};

struct PayloadStatus
{
    PayloadValidationStatus status = PayloadValidationStatus::Syncing;
    std::optional<h256> latestValidHash;
    std::optional<std::string> validationError;
};

struct ForkchoiceUpdatedResult
{
    PayloadStatus payloadStatus;
    std::optional<PayloadID> payloadId;
};

struct GetPayloadResult
{
    ExecutionPayload executionPayload;
    u256 blockValue = 0;
    std::optional<BlobsBundleV1> blobsBundle;
    bool shouldOverrideBuilder = false;
};

struct NoGlobalStateStorage
{};

namespace detail
{
inline constexpr std::size_t c_hashBytes = 32;
inline constexpr std::size_t c_payloadIdBytes = 8;

inline std::string encodePayloadSequence(std::uint64_t value)
{
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(static_cast<int>(c_payloadIdBytes * 2))
        << std::setfill('0') << value;
    return out.str();
}

inline bcos::h256 syntheticHash(std::string_view seed)
{
    std::string hex = "0x";
    hex.reserve((c_hashBytes * 2) + 2);
    auto payload = seed.substr(seed.rfind('x') + 1);
    while (hex.size() < ((c_hashBytes * 2) + 2))
    {
        hex.append(payload.begin(), payload.end());
    }
    hex.resize((c_hashBytes * 2) + 2);
    return bcos::h256(hex.substr(2));
}

inline std::vector<std::string> supportedCapabilities()
{
    return {"engine_exchangeCapabilities", "engine_forkchoiceUpdatedV1",
        "engine_forkchoiceUpdatedV2", "engine_forkchoiceUpdatedV3", "engine_getPayloadV1",
        "engine_getPayloadV2", "engine_getPayloadV3", "engine_newPayloadV1",
        "engine_newPayloadV2", "engine_newPayloadV3"};
}

inline bool isGetPayloadVersionCompatible(
    EngineApiVersion requestVersion, std::uint32_t payloadVersion)
{
    if (requestVersion == EngineApiVersion::V1)
    {
        return payloadVersion == 1;
    }
    if (requestVersion == EngineApiVersion::V2)
    {
        return payloadVersion <= 2;
    }
    if (requestVersion == EngineApiVersion::V3)
    {
        return payloadVersion <= 3;
    }
    return false;
}

inline std::optional<std::string> validatePayloadAttributes(
    const PayloadAttributes& payloadAttributes, std::uint32_t version)
{
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
    if (version == 3 && !payloadAttributes.parentBeaconBlockRoot.has_value())
    {
        return std::string("parentBeaconBlockRoot must be a 32-byte hash for V3");
    }
    return std::nullopt;
}

inline std::optional<std::string> validateExecutionPayload(
    const ExecutionPayload& executionPayload, std::uint32_t version)
{
    for (auto const& transaction : executionPayload.transactions)
    {
        if (!transaction)
        {
            return std::string("executionPayload.transactions entries must not be null");
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
    if (version == 3 &&
        (!executionPayload.blobGasUsed.has_value() || !executionPayload.excessBlobGas.has_value()))
    {
        return std::string("blob gas fields are required for ExecutionPayloadV3");
    }
    return std::nullopt;
}
}  // namespace detail

template <class MemPoolType = bcos::txpool::MemPoolImpl,
    class GlobalStateStorageType = NoGlobalStateStorage>
class BasicEngineService
{
public:
        using Ptr = std::shared_ptr<BasicEngineService<MemPoolType, GlobalStateStorageType>>;

    BasicEngineService() = default;
        BasicEngineService(MemPoolType& memPool, GlobalStateStorageType& globalStateStorage)
            : m_memPool(&memPool), m_globalStateStorage(&globalStateStorage)
    {}
    ~BasicEngineService() = default;
    BasicEngineService(const BasicEngineService&) = delete;
    BasicEngineService(BasicEngineService&&) = delete;
    BasicEngineService& operator=(const BasicEngineService&) = delete;
    BasicEngineService& operator=(BasicEngineService&&) = delete;

    MemPoolType* memPool() noexcept { return m_memPool; }
    MemPoolType const* memPool() const noexcept { return m_memPool; }
    GlobalStateStorageType* globalStateStorage() noexcept { return m_globalStateStorage; }
    GlobalStateStorageType const* globalStateStorage() const noexcept
    {
        return m_globalStateStorage;
    }

    bcos::task::Task<std::vector<std::string>> exchangeCapabilities(
        std::vector<std::string> remoteCapabilities)
    {
        (void)remoteCapabilities;
        co_return detail::supportedCapabilities();
    }

    bcos::task::Task<ForkchoiceUpdatedResult> updateForkchoice(
        const ForkchoiceState& forkchoiceState,
        const std::optional<PayloadAttributes>& payloadAttributes, std::uint32_t version)
    {
        co_return handleForkchoiceUpdate(forkchoiceState, payloadAttributes, version);
    }

    bcos::task::Task<GetPayloadResult> getPayload(
        const PayloadID& payloadId, std::uint32_t version)
    {
        co_return handleGetPayload(payloadId, version);
    }

    bcos::task::Task<PayloadStatus> newPayload(
        const NewPayloadRequest& request, std::uint32_t version)
    {
        co_return handleNewPayload(request, version);
    }

    std::optional<bcos::protocol::BlockNumber> getSafeBlockNumber() const
    {
        std::shared_lock lock(x_state);
        return m_safeBlockNumber;
    }

    std::optional<bcos::protocol::BlockNumber> getFinalizedBlockNumber() const
    {
        std::shared_lock lock(x_state);
        return m_finalizedBlockNumber;
    }

private:
    struct PayloadEntry
    {
        std::uint32_t version = 0;
        ExecutionPayload executionPayload;
        u256 blockValue = 0;
        std::optional<BlobsBundleV1> blobsBundle;
        bool shouldOverrideBuilder = false;
    };

    static bool isVersionSupported(std::uint32_t version)
    {
        return version >= static_cast<std::uint32_t>(EngineApiVersion::V1) &&
               version <= static_cast<std::uint32_t>(EngineApiVersion::V3);
    }

    static PayloadStatus makeStatus(PayloadValidationStatus status,
        std::optional<h256> latestValidHash = std::nullopt,
        std::optional<std::string> validationError = std::nullopt)
    {
        PayloadStatus statusObject;
        statusObject.status = status;
        statusObject.latestValidHash = latestValidHash;
        statusObject.validationError = std::move(validationError);
        return statusObject;
    }

    ForkchoiceUpdatedResult handleForkchoiceUpdate(const ForkchoiceState& forkchoiceState,
        const std::optional<PayloadAttributes>& payloadAttributes, std::uint32_t version)
    {
        if (!isVersionSupported(version))
        {
            throw std::invalid_argument("Unsupported Engine API version");
        }
        if (payloadAttributes)
        {
            if (auto validationError = detail::validatePayloadAttributes(*payloadAttributes, version);
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
        result.payloadStatus = makeStatus(
            PayloadValidationStatus::Valid, forkchoiceState.headBlockHash, std::nullopt);
        if (!payloadAttributes)
        {
            return result;
        }

        auto payloadId = nextPayloadID();
        auto payload = buildPayloadSkeleton(forkchoiceState, *payloadAttributes, payloadId, version);
        PayloadEntry entry;
        entry.version = version;
        entry.executionPayload = std::move(payload);
        entry.blockValue = 0;
        if (version == static_cast<std::uint32_t>(EngineApiVersion::V3))
        {
            entry.blobsBundle = BlobsBundleV1{};
        }

        m_blockHashToPayloadId[entry.executionPayload.blockHash] = payloadId;
        m_payloadCache[payloadId] = entry;
        result.payloadId = payloadId;
        return result;
    }

    GetPayloadResult handleGetPayload(const PayloadID& payloadId, std::uint32_t version) const
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
        if (!detail::isGetPayloadVersionCompatible(
                static_cast<EngineApiVersion>(version), it->second.version))
        {
            throw std::invalid_argument(
                "Payload version is incompatible with requested method version");
        }

        GetPayloadResult result;
        result.executionPayload = it->second.executionPayload;
        result.blockValue = it->second.blockValue;
        result.blobsBundle = it->second.blobsBundle;
        result.shouldOverrideBuilder = it->second.shouldOverrideBuilder;
        return result;
    }

    PayloadStatus handleNewPayload(const NewPayloadRequest& request, std::uint32_t version)
    {
        if (!isVersionSupported(version))
        {
            throw std::invalid_argument("Unsupported Engine API version");
        }

        if (auto validationError = detail::validateExecutionPayload(request.executionPayload, version);
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
            if (!request.parentBeaconBlockRoot.has_value())
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
        entry.blockValue = 0;
        if (version == static_cast<std::uint32_t>(EngineApiVersion::V3))
        {
            entry.blobsBundle = BlobsBundleV1{};
        }
        m_payloadCache[payloadId] = std::move(entry);

        return makeStatus(
            PayloadValidationStatus::Valid, request.executionPayload.blockHash, std::nullopt);
    }

    PayloadID nextPayloadID() { return detail::encodePayloadSequence(m_nextPayloadSequence++); }

    ExecutionPayload buildPayloadSkeleton(const ForkchoiceState& forkchoiceState,
        const PayloadAttributes& payloadAttributes, const PayloadID& payloadId,
        std::uint32_t version) const
    {
        auto nextBlockNumber = bcos::protocol::BlockNumber{0};
        if (auto currentBlockNumber = lookupBlockNumberByHash(forkchoiceState.headBlockHash);
            currentBlockNumber.has_value())
        {
            nextBlockNumber = *currentBlockNumber + 1;
        }

        ExecutionPayload executionPayload;
        executionPayload.parentHash = forkchoiceState.headBlockHash;
        executionPayload.feeRecipient = payloadAttributes.suggestedFeeRecipient;
        executionPayload.stateRoot = detail::syntheticHash(std::string("state") + payloadId);
        executionPayload.receiptsRoot = detail::syntheticHash(std::string("receipts") + payloadId);
        executionPayload.logsBloom = Bloom{};
        executionPayload.prevRandao = payloadAttributes.prevRandao;
        executionPayload.blockNumber = nextBlockNumber;
        executionPayload.gasLimit = 0;
        executionPayload.gasUsed = 0;
        executionPayload.timestamp = payloadAttributes.timestamp;
        executionPayload.extraData = {};
        executionPayload.baseFeePerGas = 0;
        executionPayload.blockHash = detail::syntheticHash(payloadId);

        if (version >= static_cast<std::uint32_t>(EngineApiVersion::V2))
        {
            executionPayload.withdrawals =
                payloadAttributes.withdrawals.value_or(std::vector<WithdrawalV1>{});
        }
        if (version >= static_cast<std::uint32_t>(EngineApiVersion::V3))
        {
            executionPayload.blobGasUsed = u256(0);
            executionPayload.excessBlobGas = u256(0);
        }
        return executionPayload;
    }

    std::optional<bcos::protocol::BlockNumber> lookupBlockNumberByHash(const h256& blockHash) const
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
        return payloadIt->second.executionPayload.blockNumber;
    }

    void updateTrackedBlockNumbers(const ForkchoiceState& forkchoiceState)
    {
        m_safeBlockNumber = lookupBlockNumberByHash(forkchoiceState.safeBlockHash);
        m_finalizedBlockNumber = lookupBlockNumberByHash(forkchoiceState.finalizedBlockHash);
    }

    mutable std::shared_mutex x_state;
    MemPoolType* m_memPool = nullptr;
    GlobalStateStorageType* m_globalStateStorage = nullptr;
    ForkchoiceState m_forkchoiceState;
    std::optional<bcos::protocol::BlockNumber> m_safeBlockNumber;
    std::optional<bcos::protocol::BlockNumber> m_finalizedBlockNumber;
    std::unordered_map<PayloadID, PayloadEntry> m_payloadCache;
    std::unordered_map<h256, PayloadID> m_blockHashToPayloadId;
    std::uint64_t m_nextPayloadSequence = 1;
};

using EngineService = BasicEngineService<>;
}  // namespace bcos::engine
