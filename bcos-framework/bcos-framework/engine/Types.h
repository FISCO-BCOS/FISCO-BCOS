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
 * @brief Engine API type definitions shared by Engine API versions
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

enum class ApiVersion : std::uint8_t
{
    V1 = 1,
    V2 = 2,
    V3 = 3,
    V4 = 4,
};

using PayloadID = std::string;

struct WithdrawalV1
{
    u256 index = 0;
    u256 validatorIndex = 0;
    u256 amount = 0;
    Address address;
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
    // Required by PayloadAttributesV1/V2/V3/V4.
    h256 prevRandao;
    Address suggestedFeeRecipient;
    std::uint64_t timestamp = 0;

    // Required by PayloadAttributesV2/V3/V4.
    std::optional<std::vector<WithdrawalV1>> withdrawals;

    // Required by PayloadAttributesV3/V4.
    std::optional<h256> parentBeaconBlockRoot;

    // OP Stack payload attributes (optimism/specs engine.md). All optional so that
    // vanilla Ethereum attributes keep their current behavior when absent.
    // EIP-2718 raw transaction hex strings, passed through verbatim without decoding;
    // decoding/dispatch belongs to the raw-bytes carrier and type-dispatch work.
    std::optional<std::vector<std::string>> transactions;
    // When true the payload must not include transactions picked from the mempool.
    std::optional<bool> noTxPool;
    // Block gas limit dictated by the L1 SystemConfig via the CL.
    std::optional<std::uint64_t> gasLimit;
    // Holocene: 8 bytes = 4-byte EIP-1559 denominator followed by 4-byte elasticity.
    std::optional<bytes> eip1559Params;
    // Jovian: minimum base fee, in wei.
    std::optional<std::uint64_t> minBaseFee;
};

struct ExecutionPayload
{
    // Required by ExecutionPayloadV1/V2/V3/V4.
    Bloom logsBloom{};
    h256 parentHash;
    h256 stateRoot;
    h256 receiptsRoot;
    h256 prevRandao;
    u256 gasLimit = 0;
    u256 gasUsed = 0;
    u256 baseFeePerGas = 0;
    h256 blockHash;
    bcos::protocol::Transactions transactions;
    bytes extraData;
    Address feeRecipient;
    std::uint64_t timestamp = 0;
    bcos::protocol::BlockNumber blockNumber = 0;

    // Required by ExecutionPayloadV2/V3/V4.
    std::optional<std::vector<WithdrawalV1>> withdrawals;

    // Required by ExecutionPayloadV3/V4.
    std::optional<u256> blobGasUsed;
    std::optional<u256> excessBlobGas;

    // Required by ExecutionPayloadV4/V5 (OP Stack, Isthmus onwards): storage root of
    // the L2ToL1MessagePasser predeploy. May carry a placeholder until real-value
    // header wiring lands.
    std::optional<h256> withdrawalsRoot;
};

struct NewPayloadRequest
{
    // Required by engine_newPayloadV1/V2/V3/V4/V5.
    ExecutionPayload executionPayload;

    // Required by engine_newPayloadV3/V4/V5.
    std::vector<h256> expectedBlobVersionedHashes;
    std::optional<h256> parentBeaconBlockRoot;

    // Required by engine_newPayloadV4/V5.
    std::optional<std::vector<bytes>> executionRequests;
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
    std::optional<h256> latestValidHash;
    std::optional<std::string> validationError;
    PayloadValidationStatus status = PayloadValidationStatus::Syncing;
};

struct ForkchoiceUpdatedResult
{
    PayloadStatus payloadStatus;
    std::optional<PayloadID> payloadId;
};

struct GetPayloadData
{
    // Required by engine_getPayloadV1/V2/V3/V4/V6.
    ExecutionPayload executionPayload;

    // Required by engine_getPayloadV2/V3/V4/V6.
    u256 blockValue = 0;

    // Required by engine_getPayloadV3/V4.
    std::optional<BlobsBundleV1> blobsBundle;

    // Required by engine_getPayloadV3/V4/V6.
    bool shouldOverrideBuilder = false;

    // Required by engine_getPayloadV4/V6.
    std::optional<std::vector<bytes>> executionRequests;

    // OP Stack getPayload response extension: the beacon root the payload was built
    // with, echoed back to newPayload by the CL.
    std::optional<h256> parentBeaconBlockRoot;
};

using GetPayloadResult = std::unique_ptr<GetPayloadData>;

}  // namespace bcos::engine
