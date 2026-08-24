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
#include "bcos-utilities/Exceptions.h"
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
    V5 = 5,
};

using PayloadID = std::string;

/// Engine API error conditions shared by the service implementation and the RPC
/// endpoint layer, which maps them to Engine API error codes: UnknownPayload ->
/// -38001, the two version mismatches -> -38005 Unsupported fork.
DERIVE_BCOS_EXCEPTION(UnsupportedEngineApiVersion);
DERIVE_BCOS_EXCEPTION(UnknownPayload);
DERIVE_BCOS_EXCEPTION(IncompatiblePayloadVersion);

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
    // Internal milliseconds, matching BlockHeader::timestamp(); the RPC boundary
    // converts from/to Engine-API Unix seconds (EngineHelper.cpp).
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
    // Block gas limit dictated by the L1 SystemConfig via the CL. 64-bit by protocol:
    // op-node serializes it as a Uint64Quantity (op-service/eth/types.go,
    // PayloadAttributes.GasLimit *Uint64Quantity).
    std::optional<std::uint64_t> gasLimit;
    // Holocene: 8 bytes = 4-byte EIP-1559 denominator followed by 4-byte elasticity
    // (op-node: EIP1559Params *Bytes8).
    std::optional<bytes> eip1559Params;
    // Jovian: minimum base fee, in wei. 64-bit by protocol: op-node declares it as
    // MinBaseFee *uint64 (op-service/eth/types.go) and the Jovian block-header
    // extraData packs it as a big-endian u64 at bytes [9, 17)
    // (specs.optimism.io jovian/exec-engine); the spec's bare "QUANTITY" wording does
    // not widen it. Values beyond uint64 are rejected at JSON parse (strict
    // fromQuantity), matching what the extraData encoding could never carry.
    std::optional<std::uint64_t> minBaseFee;
};

/// One Engine-API transaction. The raw EIP-2718 bytes are the canonical wire form and
/// are preserved byte-for-byte through parse -> payload cache -> serialization (getPayload
/// must return exactly what newPayload received). The decoded form is the executable
/// representation where one exists: locally built payloads carry the mempool transaction
/// here; externally received payloads and deposits carry raw bytes only until execution
/// wiring decodes them.
struct EngineTransaction
{
    bytes raw;
    protocol::Transaction::Ptr decoded;
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
    /// Transaction envelopes: each `EngineTransaction::raw` carries the EIP-2718
    /// encoded bytes (including the OP 0x7E deposit envelope). Single authoritative
    /// carrier for both generic and OP engine paths.
    std::vector<EngineTransaction> transactions;
    bytes extraData;
    Address feeRecipient;
    // Internal milliseconds, matching BlockHeader::timestamp(); the RPC boundary
    // converts from/to Engine-API Unix seconds (EngineHelper.cpp).
    std::uint64_t timestamp = 0;
    bcos::protocol::BlockNumber blockNumber = 0;

    // Required by ExecutionPayloadV2/V3/V4.
    std::optional<std::vector<WithdrawalV1>> withdrawals;

    // Required by ExecutionPayloadV3/V4.
    std::optional<u256> blobGasUsed;
    std::optional<u256> excessBlobGas;

    // Required by ExecutionPayloadV4.
    std::optional<bytes> blockAccessList = std::nullopt;
    std::optional<std::uint64_t> slotNumber = std::nullopt;

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
    // Required by engine_getPayloadV1/V2/V3/V4/V5.
    ExecutionPayload executionPayload;

    // Required by engine_getPayloadV2/V3/V4/V5.
    u256 blockValue = 0;

    // Required by engine_getPayloadV3/V4/V5. getPayloadV5 answers the Osaka BlobsBundleV2
    // (execution-apis osaka.md), which has the same three-array shape as V1 and differs
    // only in that `proofs` carries cell proofs. OP L2 forbids blob transactions entirely,
    // so all three arrays are always empty and BlobsBundleV1 covers both response shapes;
    // a distinct V2 type would carry no distinct data.
    std::optional<BlobsBundleV1> blobsBundle;

    // Required by engine_getPayloadV3/V4/V5.
    bool shouldOverrideBuilder = false;

    // Required by engine_getPayloadV4/V5. Karst carries no execution-layer requests, so
    // getPayloadV5 responds with an empty array.
    std::optional<std::vector<bytes>> executionRequests;

    // OP Stack getPayload response extension: the beacon root the payload was built
    // with, echoed back to newPayload by the CL.
    std::optional<h256> parentBeaconBlockRoot;
};

using GetPayloadResult = std::unique_ptr<GetPayloadData>;

}  // namespace bcos::engine
