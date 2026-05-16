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

#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/Bloom.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Exceptions.h"
#include "bcos-utilities/FixedBytes.h"
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bcos::engine
{
DERIVE_BCOS_EXCEPTION(UnsupportedEngineApiVersion);
DERIVE_BCOS_EXCEPTION(GlobalStateStorageNotConfigured);
DERIVE_BCOS_EXCEPTION(UnknownForkchoiceHeadBlock);
DERIVE_BCOS_EXCEPTION(InvalidForkchoiceState);
DERIVE_BCOS_EXCEPTION(UnknownPayload);
DERIVE_BCOS_EXCEPTION(IncompatiblePayloadVersion);

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

namespace detail
{
std::string encodePayloadSequence(std::uint64_t value);

bcos::h256 syntheticHash(std::string_view seed);

std::vector<std::string> supportedCapabilities();

bool isGetPayloadVersionCompatible(EngineApiVersion requestVersion, std::uint32_t payloadVersion);

std::optional<std::string> validatePayloadAttributes(
    const PayloadAttributes& payloadAttributes, std::uint32_t version);

std::optional<std::string> validateExecutionPayload(
    const ExecutionPayload& executionPayload, std::uint32_t version);
}  // namespace detail

template <class MemPoolType, class GlobalStateStorageType>
class EngineService
{
public:
    EngineService(MemPoolType& memPool, GlobalStateStorageType& globalStateStorage)
      : m_memPool(std::ref(memPool)), m_globalStateStorage(std::ref(globalStateStorage))
    {}
    ~EngineService() = default;
    EngineService(const EngineService&) = delete;
    EngineService(EngineService&&) = delete;
    EngineService& operator=(const EngineService&) = delete;
    EngineService& operator=(EngineService&&) = delete;

    bcos::task::Task<std::vector<std::string>> exchangeCapabilities(
        std::vector<std::string> remoteCapabilities)
    {
        (void)remoteCapabilities;
        co_return detail::supportedCapabilities();
    }

    bcos::task::Task<ForkchoiceUpdatedResult> updateForkchoice(
        const ForkchoiceState& forkchoiceState, const PayloadAttributes* payloadAttributes,
        std::uint32_t version)
    {
        if (!isVersionSupported(version))
        {
            BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                                  << bcos::errinfo_comment{"Unsupported Engine API version"});
        }
        if (payloadAttributes != nullptr)
        {
            if (auto validationError =
                    detail::validatePayloadAttributes(*payloadAttributes, version);
                validationError.has_value())
            {
                ForkchoiceUpdatedResult result{
                    .payloadStatus =
                        makeStatus(PayloadValidationStatus::Invalid, std::nullopt, validationError),
                    .payloadId = std::nullopt,
                };
                co_return result;
            }
        }

        auto view = m_globalStateStorage.get().fork();
        auto headBlockNumber = co_await bcos::ledger::getBlockNumber(
            view, forkchoiceState.headBlockHash, bcos::ledger::fromStorage);
        auto safeBlockNumber = co_await bcos::ledger::getBlockNumber(
            view, forkchoiceState.safeBlockHash, bcos::ledger::fromStorage);
        auto finalizedBlockNumber = co_await bcos::ledger::getBlockNumber(
            view, forkchoiceState.finalizedBlockHash, bcos::ledger::fromStorage);

        if (!headBlockNumber.has_value() || !safeBlockNumber.has_value() ||
            !finalizedBlockNumber.has_value())
        {
            ForkchoiceUpdatedResult result{
                .payloadStatus =
                    makeStatus(PayloadValidationStatus::Syncing, std::nullopt, std::nullopt),
                .payloadId = std::nullopt,
            };
            co_return result;
        }
        if (*safeBlockNumber > *headBlockNumber)
        {
            BOOST_THROW_EXCEPTION(
                InvalidForkchoiceState{} << bcos::errinfo_comment{
                    "Forkchoice safe block number must not exceed head block number"});
        }
        if (*finalizedBlockNumber > *headBlockNumber)
        {
            BOOST_THROW_EXCEPTION(
                InvalidForkchoiceState{} << bcos::errinfo_comment{
                    "Forkchoice finalized block number must not exceed head block number"});
        }
        if (*finalizedBlockNumber > *safeBlockNumber)
        {
            BOOST_THROW_EXCEPTION(
                InvalidForkchoiceState{} << bcos::errinfo_comment{
                    "Forkchoice finalized block number must not exceed safe block number"});
        }

        std::unique_lock lock(x_state);
        if (m_trackedHeadBlock.has_value())
        {
            auto const& trackedHeadBlock = *m_trackedHeadBlock;
            if (*headBlockNumber < trackedHeadBlock.blockNumber)
            {
                ForkchoiceUpdatedResult result{
                    .payloadStatus = makeStatus(PayloadValidationStatus::Valid,
                        forkchoiceState.headBlockHash, std::nullopt),
                    .payloadId = std::nullopt,
                };
                co_return result;
            }
            if (*headBlockNumber == trackedHeadBlock.blockNumber)
            {
                if (forkchoiceState.headBlockHash != trackedHeadBlock.hash)
                {
                    BOOST_THROW_EXCEPTION(
                        InvalidForkchoiceState{} << bcos::errinfo_comment{
                            "Forkchoice head block hash conflicts with tracked block number"});
                }
            }
            else if (*headBlockNumber != trackedHeadBlock.blockNumber + 1)
            {
                BOOST_THROW_EXCEPTION(
                    InvalidForkchoiceState{} << bcos::errinfo_comment{
                        "Forkchoice head block number must increase by exactly 1"});
            }
        }

        m_forkchoiceState = forkchoiceState;
        m_trackedHeadBlock = TrackedHeadBlock{
            .hash = forkchoiceState.headBlockHash,
            .blockNumber = *headBlockNumber,
        };
        updateTrackedBlockNumbers(safeBlockNumber, finalizedBlockNumber);
        m_memPool.get().remove(view);

        ForkchoiceUpdatedResult result{
            .payloadStatus = makeStatus(
                PayloadValidationStatus::Valid, forkchoiceState.headBlockHash, std::nullopt),
            .payloadId = std::nullopt,
        };
        if (payloadAttributes == nullptr)
        {
            co_return result;
        }

        auto payloadId = nextPayloadID();
        auto payload =
            buildPayloadSkeleton(forkchoiceState, *payloadAttributes, payloadId, version);
        PayloadEntry entry{
            .version = version,
            .executionPayload = std::move(payload),
            .blockValue = 0,
            .blobsBundle = std::nullopt,
            .shouldOverrideBuilder = false,
        };
        if (version == static_cast<std::uint32_t>(EngineApiVersion::V3))
        {
            entry.blobsBundle = BlobsBundleV1{};
        }

        m_blockHashToPayloadId[entry.executionPayload.blockHash] = payloadId;
        m_payloadCache[payloadId] = entry;
        result.payloadId = payloadId;
        co_return result;
    }

    bcos::task::Task<GetPayloadResult> getPayload(const PayloadID& payloadId, std::uint32_t version)
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
    struct TrackedHeadBlock
    {
        h256 hash;
        bcos::protocol::BlockNumber blockNumber = 0;
    };

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
        return PayloadStatus{
            .status = status,
            .latestValidHash = latestValidHash,
            .validationError = std::move(validationError),
        };
    }

    GetPayloadResult handleGetPayload(const PayloadID& payloadId, std::uint32_t version) const
    {
        if (!isVersionSupported(version))
        {
            BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                                  << bcos::errinfo_comment{"Unsupported Engine API version"});
        }

        std::shared_lock lock(x_state);
        auto it = m_payloadCache.find(payloadId);
        if (it == m_payloadCache.end())
        {
            BOOST_THROW_EXCEPTION(UnknownPayload{} << bcos::errinfo_comment{"Unknown payload"});
        }
        if (!detail::isGetPayloadVersionCompatible(
                static_cast<EngineApiVersion>(version), it->second.version))
        {
            BOOST_THROW_EXCEPTION(
                IncompatiblePayloadVersion{} << bcos::errinfo_comment{
                    "Payload version is incompatible with requested method version"});
        }

        return GetPayloadResult{
            .executionPayload = it->second.executionPayload,
            .blockValue = it->second.blockValue,
            .blobsBundle = it->second.blobsBundle,
            .shouldOverrideBuilder = it->second.shouldOverrideBuilder,
        };
    }

    PayloadStatus handleNewPayload(const NewPayloadRequest& request, std::uint32_t version)
    {
        if (!isVersionSupported(version))
        {
            BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                                  << bcos::errinfo_comment{"Unsupported Engine API version"});
        }

        if (auto validationError =
                detail::validateExecutionPayload(request.executionPayload, version);
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

        PayloadEntry entry{
            .version = version,
            .executionPayload = request.executionPayload,
            .blockValue = 0,
            .blobsBundle = std::nullopt,
            .shouldOverrideBuilder = false,
        };
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

        ExecutionPayload executionPayload{
            .parentHash = forkchoiceState.headBlockHash,
            .feeRecipient = payloadAttributes.suggestedFeeRecipient,
            .stateRoot = detail::syntheticHash(std::string("state") + payloadId),
            .receiptsRoot = detail::syntheticHash(std::string("receipts") + payloadId),
            .logsBloom = Bloom{},
            .prevRandao = payloadAttributes.prevRandao,
            .blockNumber = nextBlockNumber,
            .gasLimit = 0,
            .gasUsed = 0,
            .timestamp = payloadAttributes.timestamp,
            .extraData = {},
            .baseFeePerGas = 0,
            .blockHash = detail::syntheticHash(payloadId),
            .transactions = {},
            .withdrawals = std::nullopt,
            .blobGasUsed = std::nullopt,
            .excessBlobGas = std::nullopt,
        };

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

    void updateTrackedBlockNumbers(std::optional<bcos::protocol::BlockNumber> safeBlockNumber,
        std::optional<bcos::protocol::BlockNumber> finalizedBlockNumber)
    {
        m_safeBlockNumber = safeBlockNumber;
        m_finalizedBlockNumber = finalizedBlockNumber;
    }

    mutable std::shared_mutex x_state;
    std::reference_wrapper<MemPoolType> m_memPool;
    std::reference_wrapper<GlobalStateStorageType> m_globalStateStorage;
    ForkchoiceState m_forkchoiceState;
    std::optional<TrackedHeadBlock> m_trackedHeadBlock;
    std::optional<bcos::protocol::BlockNumber> m_safeBlockNumber;
    std::optional<bcos::protocol::BlockNumber> m_finalizedBlockNumber;
    std::unordered_map<PayloadID, PayloadEntry> m_payloadCache;
    std::unordered_map<h256, PayloadID> m_blockHashToPayloadId;
    std::uint64_t m_nextPayloadSequence = 1;
};

}  // namespace bcos::engine
