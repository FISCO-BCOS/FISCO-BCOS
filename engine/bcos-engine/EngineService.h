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

#include "bcos-rpc/groupmgr/NodeService.h"
#include "bcos-task/Task.h"

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
    std::string index;
    std::string validatorIndex;
    std::string address;
    std::string amount;
};

struct BlobsBundleV1
{
    std::vector<std::string> commitments;
    std::vector<std::string> proofs;
    std::vector<std::string> blobs;
};

struct ForkchoiceState
{
    std::string headBlockHash;
    std::string safeBlockHash;
    std::string finalizedBlockHash;
};

struct PayloadAttributes
{
    std::string timestamp;
    std::string prevRandao;
    std::string suggestedFeeRecipient;
    std::optional<std::vector<WithdrawalV1>> withdrawals;
    std::optional<std::string> parentBeaconBlockRoot;
};

struct ExecutionPayload
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
    std::optional<std::vector<WithdrawalV1>> withdrawals;
    std::optional<std::string> blobGasUsed;
    std::optional<std::string> excessBlobGas;
};

struct NewPayloadRequest
{
    ExecutionPayload executionPayload;
    std::vector<std::string> expectedBlobVersionedHashes;
    std::optional<std::string> parentBeaconBlockRoot;
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
    std::optional<std::string> latestValidHash;
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
    std::string blockValue{"0x0"};
    std::optional<BlobsBundleV1> blobsBundle;
    bool shouldOverrideBuilder = false;
};

class EngineServiceInterface
{
public:
    using Ptr = std::shared_ptr<EngineServiceInterface>;

    EngineServiceInterface() = default;
    virtual ~EngineServiceInterface() = default;
    EngineServiceInterface(const EngineServiceInterface&) = delete;
    EngineServiceInterface(EngineServiceInterface&&) = delete;
    EngineServiceInterface& operator=(const EngineServiceInterface&) = delete;
    EngineServiceInterface& operator=(EngineServiceInterface&&) = delete;

    virtual bcos::task::Task<std::vector<std::string>> exchangeCapabilities(
        std::vector<std::string> remoteCapabilities) = 0;
    virtual bcos::task::Task<ForkchoiceUpdatedResult> updateForkchoice(
        const ForkchoiceState& forkchoiceState,
        const std::optional<PayloadAttributes>& payloadAttributes, std::uint32_t version) = 0;
    virtual bcos::task::Task<GetPayloadResult> getPayload(
        const PayloadID& payloadId, std::uint32_t version) = 0;
    virtual bcos::task::Task<PayloadStatus> newPayload(
        const NewPayloadRequest& request, std::uint32_t version) = 0;
    virtual std::optional<std::int64_t> getSafeBlockNumber() const = 0;
    virtual std::optional<std::int64_t> getFinalizedBlockNumber() const = 0;
};

class EngineService final : public EngineServiceInterface
{
public:
    using Ptr = std::shared_ptr<EngineService>;

    explicit EngineService(bcos::rpc::NodeService::Ptr nodeService);
    ~EngineService() override = default;
    EngineService(const EngineService&) = delete;
    EngineService(EngineService&&) = delete;
    EngineService& operator=(const EngineService&) = delete;
    EngineService& operator=(EngineService&&) = delete;

    bcos::task::Task<std::vector<std::string>> exchangeCapabilities(
        std::vector<std::string> remoteCapabilities) override;
    bcos::task::Task<ForkchoiceUpdatedResult> updateForkchoice(
        const ForkchoiceState& forkchoiceState,
        const std::optional<PayloadAttributes>& payloadAttributes, std::uint32_t version) override;
    bcos::task::Task<GetPayloadResult> getPayload(
        const PayloadID& payloadId, std::uint32_t version) override;
    bcos::task::Task<PayloadStatus> newPayload(
        const NewPayloadRequest& request, std::uint32_t version) override;
    std::optional<std::int64_t> getSafeBlockNumber() const override;
    std::optional<std::int64_t> getFinalizedBlockNumber() const override;

private:
    struct PayloadEntry
    {
        std::uint32_t version = 0;
        ExecutionPayload executionPayload;
        std::string blockValue{"0x0"};
        std::optional<BlobsBundleV1> blobsBundle;
        bool shouldOverrideBuilder = false;
    };

    static bool isVersionSupported(std::uint32_t version);
    static PayloadStatus makeStatus(PayloadValidationStatus status,
        std::optional<std::string> latestValidHash = std::nullopt,
        std::optional<std::string> validationError = std::nullopt);

    ForkchoiceUpdatedResult handleForkchoiceUpdate(
        const ForkchoiceState& forkchoiceState,
        const std::optional<PayloadAttributes>& payloadAttributes, std::uint32_t version);
    GetPayloadResult handleGetPayload(const PayloadID& payloadId, std::uint32_t version) const;
    PayloadStatus handleNewPayload(const NewPayloadRequest& request, std::uint32_t version);

    PayloadID nextPayloadID();
    ExecutionPayload buildPayloadSkeleton(const ForkchoiceState& forkchoiceState,
        const PayloadAttributes& payloadAttributes, const PayloadID& payloadId,
        std::uint32_t version) const;
    std::optional<std::int64_t> lookupBlockNumberByHash(const std::string& blockHash) const;
    void updateTrackedBlockNumbers(const ForkchoiceState& forkchoiceState);

    bcos::rpc::NodeService::Ptr m_nodeService;

    mutable std::shared_mutex x_state;
    ForkchoiceState m_forkchoiceState;
    std::optional<std::int64_t> m_safeBlockNumber;
    std::optional<std::int64_t> m_finalizedBlockNumber;
    std::unordered_map<PayloadID, PayloadEntry> m_payloadCache;
    std::unordered_map<std::string, PayloadID> m_blockHashToPayloadId;
    std::uint64_t m_nextPayloadSequence = 1;
};
}  // namespace bcos::engine