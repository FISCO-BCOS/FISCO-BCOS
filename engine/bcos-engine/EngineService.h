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
#include <cstdint>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
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

class EngineService
{
public:
    using Ptr = std::shared_ptr<EngineService>;

    explicit EngineService() = default;
    ~EngineService() = default;
    EngineService(const EngineService&) = delete;
    EngineService(EngineService&&) = delete;
    EngineService& operator=(const EngineService&) = delete;
    EngineService& operator=(EngineService&&) = delete;

    bcos::task::Task<std::vector<std::string>> exchangeCapabilities(
        std::vector<std::string> remoteCapabilities);
    bcos::task::Task<ForkchoiceUpdatedResult> updateForkchoice(
        const ForkchoiceState& forkchoiceState,
        const std::optional<PayloadAttributes>& payloadAttributes, std::uint32_t version);
    bcos::task::Task<GetPayloadResult> getPayload(
        const PayloadID& payloadId, std::uint32_t version);
    bcos::task::Task<PayloadStatus> newPayload(
        const NewPayloadRequest& request, std::uint32_t version);
    std::optional<bcos::protocol::BlockNumber> getSafeBlockNumber() const;
    std::optional<bcos::protocol::BlockNumber> getFinalizedBlockNumber() const;

private:
    struct PayloadEntry
    {
        std::uint32_t version = 0;
        ExecutionPayload executionPayload;
        u256 blockValue = 0;
        std::optional<BlobsBundleV1> blobsBundle;
        bool shouldOverrideBuilder = false;
    };

    static bool isVersionSupported(std::uint32_t version);
    static PayloadStatus makeStatus(PayloadValidationStatus status,
        std::optional<h256> latestValidHash = std::nullopt,
        std::optional<std::string> validationError = std::nullopt);

    ForkchoiceUpdatedResult handleForkchoiceUpdate(const ForkchoiceState& forkchoiceState,
        const std::optional<PayloadAttributes>& payloadAttributes, std::uint32_t version);
    GetPayloadResult handleGetPayload(const PayloadID& payloadId, std::uint32_t version) const;
    PayloadStatus handleNewPayload(const NewPayloadRequest& request, std::uint32_t version);

    PayloadID nextPayloadID();
    ExecutionPayload buildPayloadSkeleton(const ForkchoiceState& forkchoiceState,
        const PayloadAttributes& payloadAttributes, const PayloadID& payloadId,
        std::uint32_t version) const;
    std::optional<bcos::protocol::BlockNumber> lookupBlockNumberByHash(const h256& blockHash) const;
    void updateTrackedBlockNumbers(const ForkchoiceState& forkchoiceState);

    mutable std::shared_mutex x_state;
    ForkchoiceState m_forkchoiceState;
    std::optional<bcos::protocol::BlockNumber> m_safeBlockNumber;
    std::optional<bcos::protocol::BlockNumber> m_finalizedBlockNumber;
    std::unordered_map<PayloadID, PayloadEntry> m_payloadCache;
    std::unordered_map<h256, PayloadID> m_blockHashToPayloadId;
    std::uint64_t m_nextPayloadSequence = 1;
};
}  // namespace bcos::engine