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
 * @file Types.h
 * @brief Engine API type definitions (Engine API V1/V2/V3)
 */

#pragma once

#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-utilities/Bloom.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/FixedBytes.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bcos::engine
{

/// Default maximum number of transactions per block when building a payload.
/// Used as the fallback blockTxCountLimit in EngineServiceImpl and EngineServiceInitializer.
inline constexpr int64_t c_defaultBlockTxCountLimit = 1000;

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

    /// OP-mode carrier fields (op-validator-loop design §4.2/§5.2). Both are optional and unread
    /// by the generic (non-OP) engine path — zero behavioral change for existing callers.
    /// - rawTransactions: the block's transactions as raw EIP-2718 envelope bytes (typed tx
    ///   MarshalBinary() output, including the OP 0x7E deposit envelope). This is the OP path's
    ///   only transaction carrier consumed by `bcos::evm::engine::OpSchedulerImpl::executeOpBlock`
    ///   (via its `rawTxBytes` parameter) — `transactions` above (bcos::protocol::Transactions)
    ///   is the generic-path carrier and is not populated/read on the OP path.
    /// - withdrawalsRoot: OP Isthmus+ extends the payload with an explicit withdrawals-root field
    ///   (= MessagePasser storage root) that cannot be derived from the (always-empty)
    ///   `withdrawals` list above — op-geth's NewPayloadV4 requires it on OP chains (design §5.2).
    std::optional<std::vector<bytes>> rawTransactions;
    std::optional<h256> withdrawalsRoot;
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

}  // namespace bcos::engine
