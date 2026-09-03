/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <bcos-framework/engine/RawTransactionDispatch.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <evmc/evmc.h>

#include <memory>
#include <optional>
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
/// Optional V3-omitted fields are compared only when the CL sent them.
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
/// op-geth ValidateHolocene1559Params: reject only e!=0 && d==0.
/// Attribute (0,0) is legal and encodeOptimismExtraData translates it to Canyon 250/6.
/// Header extraData (0,0) and (d>0,e==0) are also accepted at the gate.
inline std::optional<std::string> validateHolocene1559Params(
    std::uint32_t denominator, std::uint32_t elasticity)
{
    if (elasticity != 0 && denominator == 0)
    {
        return std::string(
            "holocene eip-1559 params cannot have a 0 denominator unless elasticity is also 0");
    }
    return std::nullopt;
}
/// op-geth ReadCanonicalHash(number) != submitted hash → not canonical.
/// Missing NUMBER_2_HASH is not a mismatch: HASH_2_NUMBER already resolved the
/// block (legacy test fixtures write only that index).
inline bool forkchoiceHashIsCanonical(
    const h256& submitted, const std::optional<h256>& canonicalAtNumber)
{
    return !canonicalAtNumber.has_value() || *canonicalAtNumber == submitted;
}
std::optional<std::string> validatePayloadAttributes(
    const PayloadAttributes& payloadAttributes, std::uint32_t version);
std::optional<PayloadID> derivePayloadId(
    const PayloadAttributes& payloadAttributes, const h256& parentHash, std::uint32_t version);
PayloadStatus makeStatus(PayloadValidationStatus status,
    std::optional<h256> latestValidHash = std::nullopt,
    std::optional<std::string> validationError = std::nullopt);
}  // namespace engine_common

}  // namespace bcos::engine
