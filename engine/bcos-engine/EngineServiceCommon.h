/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
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
#include <vector>

namespace bcos::engine
{

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
/// Keep-local-body: optional V3-omitted fields are compared only when both sides
/// sent them (submitted-absent vs built-present is not a mismatch).
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

namespace engine_common
{
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
[[nodiscard]] inline std::uint32_t readU32BE(std::span<const bcos::byte> bytes, std::size_t off)
{
    return (static_cast<std::uint32_t>(bytes[off]) << 24) |
           (static_cast<std::uint32_t>(bytes[off + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[off + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[off + 3]);
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
inline constexpr std::size_t kMaxForcedTxCount = 16384;
inline constexpr std::size_t kMaxForcedTxBytes = 8 * 1024 * 1024;

std::optional<std::string> validatePayloadAttributes(
    const PayloadAttributes& payloadAttributes, std::uint32_t version);
std::optional<PayloadID> derivePayloadId(
    const PayloadAttributes& payloadAttributes, const h256& parentHash, std::uint32_t version);
PayloadStatus makeStatus(PayloadValidationStatus status,
    std::optional<h256> latestValidHash = std::nullopt,
    std::optional<std::string> validationError = std::nullopt);
}  // namespace engine_common

}  // namespace bcos::engine
