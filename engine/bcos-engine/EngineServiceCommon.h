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
 * @file EngineServiceCommon.h
 * @brief Shared validators and helpers for the Engine API services
 */

#pragma once

#include <bcos-framework/engine/RawTransactionDispatch.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-ledger/mpt/Constants.h>
#include <evmc/evmc.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::engine
{

/// One shared definition (was duplicated in EngineTracker.h and EngineServiceImpl.h).
struct TrackedHeadBlock
{
    h256 hash;
    bcos::protocol::BlockNumber blockNumber = 0;
};

struct BuiltPayload
{
    std::uint32_t version = 0;
    ExecutionPayload executionPayload;
    u256 blockValue = 0;
    std::optional<BlobsBundleV1> blobsBundle;
    bool shouldOverrideBuilder = false;
    std::optional<h256> parentBeaconBlockRoot;
};

using BuiltPayloadPtr = std::shared_ptr<const BuiltPayload>;

namespace detail
{
/// Holocene/Jovian extraData from CL attributes. Attribute 0,0 becomes Canyon 250/6
/// (op-core EncodeHoloceneExtraData / EncodeJovianExtraData).
bcos::bytes encodeOptimismExtraData(const PayloadAttributes& payloadAttributes);

std::optional<std::string> validateExecutionPayload(
    const ExecutionPayload& executionPayload, std::uint32_t version);
/// Hash-relevant fields vs the locally built payload (op-geth ExecutableDataToBlock).
/// Keep-local-body (BL): optional V3 fields (withdrawalsRoot / blobGasUsed /
/// excessBlobGas) are compared only when both sides have them. Presence XOR
/// (omit vs value) is not a mismatch.
std::optional<std::string> compareWithBuiltPayload(
    const ExecutionPayload& submitted, const ExecutionPayload& built);
bcos::protocol::EthBlockVersion ethBlockVersionFor(evmc_revision rev);
void finalizeEthBlockHeader(bcos::protocol::BlockHeader& header, const ExecutionPayload& payload,
    std::optional<bcos::h256> parentBeaconBlockRoot, bcos::protocol::EthBlockVersion forkVersion);

inline bcos::h256 withdrawalsRootFor(const ExecutionPayload& /*payload*/)
{
    return bcos::ledger::mpt::emptyRootHash();
}
}  // namespace detail

/// Shared Engine-API service surface, distinct from implementation-internal detail
/// helpers: these validators/status/shape helpers are consumed across the engine-split
/// stack (the live EngineServiceImpl here, EngineTracker, and the Eth/Op services in
/// #5548/#5549), so they get a named home instead of the private detail namespace
/// (finding F28).
namespace engine_common
{
/// Upstream pin for Engine API comments in this extract:
/// op-geth d401af16f2dd94b010a72eaef10e07ac10b31931
/// (eth/catalyst/api.go, miner/payload_building.go).
std::vector<std::string> supportedCapabilities();
bool isGetPayloadVersionCompatible(ApiVersion requestVersion, std::uint32_t payloadVersion);
/// op-geth ForkchoiceUpdatedV3/V4 both store PayloadV3; GetPayloadV4 requires PayloadV3.
std::uint32_t payloadShapeVersion(std::uint32_t methodVersion);
std::optional<std::string> validateRawTransactionKind(
    bcos::engine::RawTransactionKind kind, std::size_t index);
/// EIP-1559 attribute pairing rule (finding AO): the pair must be both-zero or both
/// non-zero. (0,0) is legal attribute input — encodeOptimismExtraData translates it to
/// the Canyon constants 250/6 — but a mixed pair such as (d>0,e==0) would be encoded
/// verbatim as a zero-elasticity header that calcOpBaseFee can never extend, bricking
/// the chain on top of it. Committed headers are validated separately with a strict
/// non-zero rule (validateOpExtraDataShape) since encode never produces a zero header.
inline std::optional<std::string> validateHolocene1559Params(
    std::uint32_t denominator, std::uint32_t elasticity)
{
    if ((denominator == 0) != (elasticity == 0))
    {
        return std::string(
            "holocene eip-1559 params denominator and elasticity must be both zero or "
            "both non-zero");
    }
    return std::nullopt;
}
/// op-geth ReadCanonicalHash(number) != submitted hash → not canonical.
/// Missing NUMBER_2_HASH is also not canonical (fail closed). Fixtures must write
/// both HASH_2_NUMBER and NUMBER_2_HASH (`registerVerifiedBlock` already does).
inline bool forkchoiceHashIsCanonical(
    const h256& submitted, const std::optional<h256>& canonicalAtNumber)
{
    return canonicalAtNumber.has_value() && *canonicalAtNumber == submitted;
}
/// High semantic ceiling for FCU forced txs (finding BY). Not a ~256 miner
/// limit — deposit blocks can exceed that. HTTP's default 10MiB body already
/// bounds the RPC path; this rejects before keccak when a caller bypasses it.
/// Forced DA overflow is still not INVALID (OP deposits are undroppable).
inline constexpr std::size_t c_maxForcedTxCount = 16384;
inline constexpr std::size_t c_maxForcedTxBytes = 8 * 1024 * 1024;

/// Decoded byte count of a hex string, matching `fromHex` (optional 0x, odd nibble pads).
/// Used to reject over-ceiling forced txs before allocating the decoded buffer (finding BY).
inline std::size_t decodedHexByteCount(std::string_view hex)
{
    if (hex.size() >= 2 && (hex[0] == '0') && (hex[1] == 'x' || hex[1] == 'X'))
    {
        hex.remove_prefix(2);
    }
    return (hex.size() + 1) / 2;
}

std::optional<std::string> validatePayloadAttributes(const PayloadAttributes& payloadAttributes,
    std::uint32_t version, std::vector<bcos::bytes>* decodedForcedTxs = nullptr);
/// `decodedForcedTxs` reuses bytes from validate (finding AE). Hex fallback is
/// gone: if attributes carry transactions, pass the validated decoded bodies.
/// An empty span with a non-empty transactions list returns nullopt.
std::optional<PayloadID> derivePayloadId(const PayloadAttributes& payloadAttributes,
    const h256& parentHash, std::uint32_t version,
    std::span<const bcos::bytes> decodedForcedTxs = {});
PayloadStatus makeStatus(PayloadValidationStatus status,
    std::optional<h256> latestValidHash = std::nullopt,
    std::optional<std::string> validationError = std::nullopt);
/// Shared getPayload shape gate (leftover Impl + EngineTracker). Throws
/// IncompatiblePayloadVersion when the request version cannot render the stored body.
void requireGetPayloadShape(std::uint32_t builtVersion, const ExecutionPayload& payload,
    std::optional<h256> const& parentBeaconBlockRoot, std::uint32_t requestVersion);
/// Shared getPayload version window (finding F30: one definition for the leftover
/// Impl and EngineTracker, both of which serve getPayload).
inline bool isGetPayloadVersionSupported(std::uint32_t version)
{
    return version >= static_cast<std::uint32_t>(ApiVersion::V1) &&
           version <= static_cast<std::uint32_t>(ApiVersion::V5);
}
/// Shared getPayload response assembly (finding N5): the leftover Impl and
/// EngineTracker build the same GetPayloadData from structurally identical entries;
/// one definition so the V4+ executionRequests semantics cannot drift between the
/// two serving paths.
template <class EntryT>
GetPayloadResult assembleGetPayloadData(const EntryT& entry, std::uint32_t version)
{
    return std::make_unique<GetPayloadData>(GetPayloadData{
        .executionPayload = entry.executionPayload,
        .blockValue = entry.blockValue,
        .blobsBundle = entry.blobsBundle,
        .shouldOverrideBuilder = entry.shouldOverrideBuilder,
        // getPayloadV4/V5 responses must carry executionRequests; Karst never has
        // any, so the value is a present-but-empty list (serialized as []).
        .executionRequests = version >= static_cast<std::uint32_t>(ApiVersion::V4) ?
                                 std::optional<std::vector<bytes>>{std::in_place} :
                                 std::nullopt,
        .parentBeaconBlockRoot = entry.parentBeaconBlockRoot,
    });
}
}  // namespace engine_common

}  // namespace bcos::engine
