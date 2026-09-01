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
 * @file EngineServiceImpl.cpp
 */

#include "EngineServiceImpl.h"
#include "bcos-framework/engine/RawTransactionDispatch.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <boost/assert.hpp>
#include <boost/throw_exception.hpp>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>

// EIP-2718 envelope -> tars Transaction (Web3Transaction decode).
#include "bcos-rlp-protocol/Web3Transaction.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <optional>

namespace bcos::engine::detail
{
std::optional<bcostars::Transaction> opEnvelopeToTars(
    bcos::bytes const& env, bcos::crypto::HashType const& txHash)
{
    bcos::rpc::Web3Transaction web3Tx;
    bcos::bytesRef envRef{const_cast<bcos::byte*>(env.data()), env.size()};
    if (auto err = bcos::codec::rlp::decode(envRef, web3Tx); err)
    {
        return std::nullopt;  // malformed / unknown type; 0x04 is already supported
    }
    auto tarsTx = web3Tx.takeToTarsTransaction();
    // extraTransactionHash is what tx.hash() returns.
    tarsTx.extraTransactionHash.assign(txHash.begin(), txHash.end());
    // Non-deposits: sender is a 0x-hex string; store raw 20 bytes. Deposits already have it.
    if (tarsTx.sender.empty())
    {
        try
        {
            auto sender = bcos::fromHex(web3Tx.sender());
            tarsTx.sender.assign(sender.begin(), sender.end());
        }
        catch (std::exception const&)
        {
            // Bad signature: let the caller treat the envelope as INVALID.
            return std::nullopt;
        }
    }
    return tarsTx;
}
}  // namespace bcos::engine::detail

namespace
{
constexpr std::size_t c_hashBytes = 32;

// Post-merge OP header constants (not in ExecutionPayload). Re-declared so engine
// does not depend on bcos-evm.
const bcos::h256 c_emptyOmmersHash{
    std::string{"0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"}};
const bcos::h64 c_posNonce{std::string{"0x0000000000000000"}};
const bcos::h256 c_opEmptyRequestsHash{
    std::string{"0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}};
}  // namespace

void bcos::engine::detail::applyOpHeaderConstants(bcos::protocol::BlockHeader& header)
{
    // Post-merge OP chain constants: ommersHash = keccak256(rlp([])), difficulty = 0, nonce = 0.
    header.setUncleHash(c_emptyOmmersHash);
    header.setDifficulty(bcos::u256(0));
    header.setNonce(c_posNonce);
}

bcos::h256 bcos::engine::detail::syntheticHash(std::string_view seed)
{
    std::string hex = "0x";
    hex.reserve((c_hashBytes * 2) + 2);
    auto payload = seed.substr(seed.rfind('x') + 1);
    while (hex.size() < ((c_hashBytes * 2) + 2))
    {
        hex.append(payload.begin(), payload.end());
    }
    hex.resize((c_hashBytes * 2) + 2);
    return bcos::h256(bcos::fromHex(hex));
}

std::vector<std::string> bcos::engine::detail::supportedCapabilities()
{
    // Everything this node implements, not a fork-narrowed subset. op-geth advertises its
    // full `caps` list regardless of the active fork and lets the CL pick; op-node picks
    // its method versions from the rollup config (forkchoiceUpdatedV3 / getPayloadV5 /
    // newPayloadV4 on Karst) without needing the EL to prune the list for it. Narrowing
    // here would also break the pre-Karst callers this node still serves — the v1 Engine
    // API harness behind unsafe_allow_v1_executor and the V1-V3 integration suites.
    //
    // forkchoiceUpdatedV4 is the one absentee, and genuinely so: the forkchoice version
    // window tops out at V3 (isForkchoiceVersionSupported), so the endpoint answers
    // -38005. getPayloadV5 and newPayloadV4 were added by B4.
    return {"engine_exchangeCapabilities", "engine_forkchoiceUpdatedV1",
        "engine_forkchoiceUpdatedV2", "engine_forkchoiceUpdatedV3", "engine_getPayloadV1",
        "engine_getPayloadV2", "engine_getPayloadV3", "engine_getPayloadV4", "engine_getPayloadV5",
        "engine_newPayloadV1", "engine_newPayloadV2", "engine_newPayloadV3", "engine_newPayloadV4"};
}

std::vector<std::string> bcos::engine::detail::supportedOpCapabilities()
{
    // OP Engine API is V4-only.
    auto caps = supportedCapabilities();
    caps.push_back("engine_forkchoiceUpdatedV4");
    caps.push_back("engine_getPayloadV4");
    caps.push_back("engine_newPayloadV4");
    return caps;
}

bool bcos::engine::detail::isGetPayloadVersionCompatible(
    ApiVersion requestVersion, std::uint32_t payloadVersion, bool opMode)
{
    if (requestVersion == ApiVersion::V1)
    {
        return payloadVersion == 1;
    }
    if (requestVersion == ApiVersion::V2)
    {
        return payloadVersion <= 2;
    }
    if (requestVersion == ApiVersion::V3)
    {
        return payloadVersion <= 3;
    }
    if (requestVersion == ApiVersion::V4)
    {
        // op-geth's GetPayloadV4 passes []engine.PayloadVersion{engine.PayloadV3} and
        // answers UnsupportedFork for anything else (eth/catalyst/api.go:498-511) — a
        // committed V1/V2 build must not be re-servable through the V4 window. The OP
        // path widens the window to <=4: OP builds always carry the V3+ shape
        // (withdrawalsRoot, blobGasUsed), so a V4-tagged build serializes losslessly in
        // the V4 response.
        return opMode ? payloadVersion <= 4 : payloadVersion == 3;
    }
    if (requestVersion == ApiVersion::V5)
    {
        // Exactly V3 builds, matching op-geth's GetPayloadV5, which passes
        // []engine.PayloadVersion{engine.PayloadV3} to its getPayload helper and answers
        // engine.UnsupportedFork for anything else (eth/catalyst/api.go:498-511, 531-533).
        // A Karst CL always pairs getPayloadV5 with a forkchoiceUpdatedV3 build, and the
        // built-in single-node CL uses the same V3/V5/V4 triple. Accepting V1/V2 builds
        // here would serialize them in the V5 response shape, fabricating a zero
        // withdrawalsRoot and omitting the required blobGasUsed / excessBlobGas. A V1/V2
        // CL is unaffected: it fetches its builds through getPayloadV1/V2, which still
        // accept them.
        return payloadVersion == 3;
    }
    return false;
}

namespace
{
/// Blob or unknown-type txs invalidate the whole carrier (not dropped individually).
std::optional<std::string> validateRawTransactionKind(
    bcos::engine::RawTransactionKind kind, std::size_t index)
{
    using bcos::engine::RawTransactionKind;
    if (kind == RawTransactionKind::Blob)
    {
        return "blob transactions are not allowed (transaction index " + std::to_string(index) +
               ")";
    }
    if (kind == RawTransactionKind::Unsupported)
    {
        return "unsupported transaction type (transaction index " + std::to_string(index) + ")";
    }
    return std::nullopt;
}
}  // namespace

namespace
{
constexpr bcos::byte c_holoceneExtraDataVersion = 0x00;
constexpr bcos::byte c_jovianExtraDataVersion = 0x01;
constexpr std::size_t c_holoceneExtraDataBytes = 9;
constexpr std::size_t c_jovianExtraDataBytes = 17;
constexpr std::uint32_t c_eip1559DenominatorCanyon = 250;
constexpr std::uint32_t c_eip1559ElasticityCanyon = 6;
constexpr std::size_t c_eip1559ParamsBytes = 8;

std::pair<std::uint32_t, std::uint32_t> decodeEip1559Params(std::span<const bcos::byte> params)
{
    BOOST_ASSERT(params.size() >= c_eip1559ParamsBytes);
    auto denominator = bcos::fromBigEndian<std::uint32_t>(params.first(4));
    auto elasticity = bcos::fromBigEndian<std::uint32_t>(params.subspan(4, 4));
    return {denominator, elasticity};
}

std::optional<std::string> validateOptimismExtraDataShape(const bcos::bytes& extraData)
{
    if (extraData.empty())
    {
        return std::nullopt;
    }
    if (extraData.size() != c_holoceneExtraDataBytes && extraData.size() != c_jovianExtraDataBytes)
    {
        return "executionPayload.extraData must be empty (pre-Holocene), " +
               std::to_string(c_holoceneExtraDataBytes) + " bytes (Holocene) or " +
               std::to_string(c_jovianExtraDataBytes) + " bytes (Jovian), got " +
               std::to_string(extraData.size());
    }
    auto const expectedVersion = extraData.size() == c_jovianExtraDataBytes ?
                                     c_jovianExtraDataVersion :
                                     c_holoceneExtraDataVersion;
    if (extraData[0] != expectedVersion)
    {
        return "executionPayload.extraData version byte must be " +
               std::to_string(static_cast<unsigned>(expectedVersion)) + " for a " +
               std::to_string(extraData.size()) + "-byte extraData, got " +
               std::to_string(static_cast<unsigned>(extraData[0]));
    }
    auto [denominator, elasticity] = decodeEip1559Params(
        std::span<const bcos::byte>(extraData).subspan(1, c_eip1559ParamsBytes));
    if (denominator == 0 || elasticity == 0)
    {
        return std::string(
            "executionPayload.extraData must encode a non-zero EIP-1559 denominator and "
            "elasticity");
    }
    return std::nullopt;
}
}  // namespace

bcos::bytes bcos::engine::detail::encodeOptimismExtraData(
    const PayloadAttributes& payloadAttributes)
{
    if (!payloadAttributes.eip1559Params.has_value())
    {
        return {};
    }
    if (payloadAttributes.eip1559Params->size() != c_eip1559ParamsBytes)
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument{
            "encodeOptimismExtraData requires exactly 8 bytes of eip1559Params"});
    }
    auto [denominator, elasticity] = decodeEip1559Params(*payloadAttributes.eip1559Params);
    if (denominator == 0 && elasticity == 0)
    {
        denominator = c_eip1559DenominatorCanyon;
        elasticity = c_eip1559ElasticityCanyon;
    }

    bool jovian = payloadAttributes.minBaseFee.has_value();
    bcos::bytes extraData(jovian ? c_jovianExtraDataBytes : c_holoceneExtraDataBytes, 0);
    extraData[0] = jovian ? c_jovianExtraDataVersion : c_holoceneExtraDataVersion;
    auto out = std::span(extraData);
    auto denominatorField = out.subspan(1, 4);
    bcos::toBigEndian(denominator, denominatorField);
    auto elasticityField = out.subspan(5, 4);
    bcos::toBigEndian(elasticity, elasticityField);
    if (jovian)
    {
        auto minBaseFeeField = out.subspan(9, 8);
        bcos::toBigEndian(*payloadAttributes.minBaseFee, minBaseFeeField);
    }
    return extraData;
}

std::optional<std::string> bcos::engine::detail::validatePayloadAttributes(
    const PayloadAttributes& payloadAttributes, std::uint32_t version)
{
    if (payloadAttributes.transactions.has_value())
    {
        for (std::size_t i = 0; i < payloadAttributes.transactions->size(); ++i)
        {
            bcos::bytes raw;
            try
            {
                raw = bcos::fromHex((*payloadAttributes.transactions)[i]);
            }
            catch (std::exception const&)
            {
                return "payloadAttributes.transactions[" + std::to_string(i) +
                       "] is not a hex string";
            }
            if (auto error = validateRawTransactionKind(dispatchRawTransaction(bcos::ref(raw)), i))
            {
                return error;
            }
        }
    }
    if (version == 1 && payloadAttributes.withdrawals.has_value())
    {
        return std::string("withdrawals are not part of PayloadAttributesV1");
    }
    if (version <= 2 && payloadAttributes.parentBeaconBlockRoot.has_value())
    {
        return std::string("parentBeaconBlockRoot is only valid for PayloadAttributesV3 and V4");
    }
    if (version >= 2 && !payloadAttributes.withdrawals.has_value())
    {
        return std::string("withdrawals are required for PayloadAttributesV2, V3 and V4");
    }
    if (version >= 3 && !payloadAttributes.parentBeaconBlockRoot.has_value())
    {
        return std::string("parentBeaconBlockRoot must be a 32-byte hash for V3 and V4");
    }
    if (version <= 2 && payloadAttributes.eip1559Params.has_value())
    {
        return std::string("eip1559Params is only valid for PayloadAttributesV3");
    }
    if (version <= 2 && payloadAttributes.minBaseFee.has_value())
    {
        return std::string("minBaseFee is only valid for PayloadAttributesV3");
    }
    if (payloadAttributes.minBaseFee.has_value() && !payloadAttributes.eip1559Params.has_value())
    {
        return std::string("minBaseFee requires eip1559Params (Jovian attributes carry both)");
    }
    if (payloadAttributes.eip1559Params.has_value())
    {
        if (payloadAttributes.eip1559Params->size() != 8)
        {
            return std::string("eip1559Params must be exactly 8 bytes");
        }
        auto [denominator, elasticity] = decodeEip1559Params(*payloadAttributes.eip1559Params);
        if ((denominator == 0) != (elasticity == 0))
        {
            return std::string(
                "eip1559Params denominator and elasticity must be both zero or both non-zero");
        }
    }
    return std::nullopt;
}

std::optional<std::string> bcos::engine::detail::validateOpPayloadAttributes(
    const PayloadAttributes& payloadAttributes, bool jovianActive)
{
    // OP FCU attrs: gasLimit, eip1559Params, empty withdrawals, Jovian minBaseFee.
    if (!payloadAttributes.gasLimit.has_value())
    {
        return std::string("gasLimit parameter is required (OP rollup)");
    }
    if (!payloadAttributes.eip1559Params.has_value())
    {
        return std::string("eip1559Params is required on the OP path (Holocene+)");
    }
    if (payloadAttributes.eip1559Params->size() != 8)
    {
        return std::string("eip1559Params must be exactly 8 bytes");
    }
    // denom and elasticity must both be zero or both non-zero.
    const auto readU32BE = [&](std::size_t off) {
        return (static_cast<std::uint32_t>((*payloadAttributes.eip1559Params)[off]) << 24) |
               (static_cast<std::uint32_t>((*payloadAttributes.eip1559Params)[off + 1]) << 16) |
               (static_cast<std::uint32_t>((*payloadAttributes.eip1559Params)[off + 2]) << 8) |
               static_cast<std::uint32_t>((*payloadAttributes.eip1559Params)[off + 3]);
    };
    const auto denominator = readU32BE(0);
    const auto elasticity = readU32BE(4);
    if ((denominator == 0) != (elasticity == 0))
    {
        return std::string(
            "eip1559Params denominator and elasticity must both be zero or both non-zero");
    }
    // OP withdrawals list must be empty.
    if (payloadAttributes.withdrawals.has_value() && !payloadAttributes.withdrawals->empty())
    {
        return std::string("withdrawals must be empty on the OP path");
    }
    if (jovianActive && !payloadAttributes.minBaseFee.has_value())
    {
        return std::string("minBaseFee is required after the Jovian fork");
    }
    if (!jovianActive && payloadAttributes.minBaseFee.has_value())
    {
        return std::string("minBaseFee must be null before the Jovian fork");
    }
    return std::nullopt;
}

std::optional<std::string> bcos::engine::detail::validateExecutionPayload(
    const ExecutionPayload& executionPayload, std::uint32_t version)
{
    for (std::size_t i = 0; i < executionPayload.transactions.size(); ++i)
    {
        auto const& raw = executionPayload.transactions[i].raw;
        if (raw.empty())
        {
            return "executionPayload.transactions[" + std::to_string(i) + "] is empty";
        }
        if (auto error = validateRawTransactionKind(dispatchRawTransaction(bcos::ref(raw)), i))
        {
            return error;
        }
    }
    if (version == 1 && executionPayload.withdrawals.has_value())
    {
        return std::string("withdrawals are not part of ExecutionPayloadV1");
    }
    if (version >= 2 && !executionPayload.withdrawals.has_value())
    {
        return std::string("withdrawals are required for ExecutionPayloadV2 and later");
    }
    // Isthmus (ExecutionPayloadV4+): the withdrawals operation list must be present AND
    // empty. op-geth enforces exactly this before building the block — "expected non-nil
    // empty withdrawals operation list in Isthmus" (beacon/engine/types.go:324-326) — and
    // an OP L2 has no withdrawal operations to carry, the L1 accounting lives in the
    // L2ToL1MessagePasser storage root instead.
    if (version >= 4 && executionPayload.withdrawals.has_value() &&
        !executionPayload.withdrawals->empty())
    {
        return std::string(
            "withdrawals must be an empty list for ExecutionPayloadV4 and later (Isthmus)");
    }
    if (version <= 2 &&
        (executionPayload.blobGasUsed.has_value() || executionPayload.excessBlobGas.has_value()))
    {
        return std::string("blob gas fields are only valid for ExecutionPayloadV3 and later");
    }
    if (version >= 3 &&
        (!executionPayload.blobGasUsed.has_value() || !executionPayload.excessBlobGas.has_value()))
    {
        return std::string("blob gas fields are required for ExecutionPayloadV3 and later");
    }
    // Isthmus: an ExecutionPayloadV4 always carries the L2ToL1MessagePasser storage root.
    // Pre-V4 payloads with the field present are tolerated (mirrors the parse side, which
    // ignores it below V4 the way op-geth's NewPayloadV3 performs no withdrawalsRoot check).
    if (version >= 4 && !executionPayload.withdrawalsRoot.has_value())
    {
        return std::string("withdrawalsRoot is required for ExecutionPayloadV4 and later");
    }
    if (auto error = validateOptimismExtraDataShape(executionPayload.extraData))
    {
        return error;
    }
    return std::nullopt;
}

std::optional<std::string> bcos::engine::detail::compareWithBuiltPayload(
    const ExecutionPayload& submitted, const ExecutionPayload& built)
{
    if (submitted.extraData != built.extraData)
    {
        return std::string(
            "executionPayload.extraData does not match the payload this node built under the "
            "submitted blockHash");
    }
    return std::nullopt;
}

// OP newPayload helpers (non-template; used only when c_opMode).

std::optional<std::uint64_t> bcos::engine::detail::narrowU256ToU64(const u256& value)
{
    static const u256 maxU64(std::numeric_limits<std::uint64_t>::max());
    if (value > maxU64)
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(value);
}

bcos::h2048 bcos::engine::detail::toEthLogsBloom(const Bloom& logsBloom)
{
    // Bloom and h2048 are the same 256 bytes; explicit ctor (brace-init would not compile).
    return bcos::h2048(logsBloom.data(), logsBloom.size());
}

std::optional<std::string> bcos::engine::detail::validateOpNewPayloadRequest(
    const NewPayloadRequest& request, bool jovianActive)
{
    // Static checks before parentKnown. Failures are INVALID with latestValidHash = null.
    const auto& payload = request.executionPayload;

    if (!payload.rawTransactions.has_value())
    {
        // The OP path's only transaction carrier (Types.h). Its absence is not a semantic
        // rejection of a block but a malformed request; with no RPC layer to raise -32602 this
        // cycle, INVALID with a field-naming validationError is the honest local answer.
        return std::string("executionPayload.rawTransactions is required on the OP path");
    }
    // The ETH header stores timestamp as int64 (ms); reject overflow fail-closed instead of
    // wrapping at rebuildOpEthHeader's cast.
    if (payload.timestamp >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
        return std::string("timestamp exceeds the int64 range of the ETH header field");
    }
    if (!payload.withdrawals.has_value() || !payload.withdrawals->empty())
    {
        return std::string("withdrawals must be present and empty on the OP path");
    }
    if (!request.expectedBlobVersionedHashes.empty())
    {
        return std::string("expectedBlobVersionedHashes must be an empty array on the OP path");
    }
    if (!request.parentBeaconBlockRoot.has_value())
    {
        return std::string("parentBeaconBlockRoot must be a 32-byte hash for newPayloadV4");
    }
    if (!payload.withdrawalsRoot.has_value())
    {
        // Isthmus+: withdrawalsRoot is required (cannot be derived from the empty list).
        return std::string("withdrawalsRoot is required on the OP path (Isthmus+)");
    }
    if (!payload.excessBlobGas.has_value() || *payload.excessBlobGas != 0)
    {
        return std::string("excessBlobGas must be present and zero on the OP path");
    }
    if (!payload.blobGasUsed.has_value())
    {
        return std::string("blobGasUsed must be present on the OP path");
    }
    if (!jovianActive && *payload.blobGasUsed != 0)
    {
        // Isthmus: blobGasUsed is a blob counter (must be 0). Jovian reuses the slot as DA
        // footprint and checks it in the seal comparison.
        return std::string("blobGasUsed must be zero before Jovian (OP Isthmus)");
    }

    // Width/sign checks so rebuildOpEthHeader does not wrap.
    if (payload.blockNumber < 0)
    {
        return std::string("blockNumber must not be negative");
    }
    if (!narrowU256ToU64(payload.gasLimit).has_value())
    {
        return std::string("gasLimit exceeds the uint64 range of the ETH header field");
    }
    // Execution stores gasLimit as int64.
    if (*narrowU256ToU64(payload.gasLimit) >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
        return std::string("gasLimit exceeds the maximum block gas limit (2^63-1)");
    }
    // gasUsed > gasLimit is rejected after execution via seal comparison, not here.
    // extraData: Isthmus 9 bytes (0x00 + denom + elasticity); Jovian 17 bytes (0x01 + same +
    // minBaseFee). denom and elasticity must be non-zero.
    {
        const auto& extra = payload.extraData;
        if (jovianActive)
        {
            if (extra.size() != 17)
            {
                return std::string("extraData must be exactly 17 bytes on the OP path (Jovian)");
            }
            if (extra[0] != 0x01)
            {
                return std::string("extraData version byte must be 0x01 on the OP path (Jovian)");
            }
        }
        else
        {
            if (extra.size() != 9)
            {
                return std::string("extraData must be exactly 9 bytes on the OP path (Isthmus)");
            }
            if (extra[0] != 0x00)
            {
                return std::string("extraData version byte must be 0x00 on the OP path (Isthmus)");
            }
        }
        // extra[1:5] denominator, extra[5:9] elasticity (big-endian uint32).
        const auto readU32BE = [&extra](std::size_t off) {
            return (static_cast<std::uint32_t>(extra[off]) << 24) |
                   (static_cast<std::uint32_t>(extra[off + 1]) << 16) |
                   (static_cast<std::uint32_t>(extra[off + 2]) << 8) |
                   static_cast<std::uint32_t>(extra[off + 3]);
        };
        if (readU32BE(1) == 0)
        {
            return std::string("extraData must encode a non-zero eip-1559 denominator");
        }
        if (readU32BE(5) == 0)
        {
            return std::string("extraData must encode a non-zero eip-1559 elasticity");
        }
    }
    if (!narrowU256ToU64(payload.gasUsed).has_value())
    {
        return std::string("gasUsed exceeds the uint64 range of the ETH header field");
    }
    if (!narrowU256ToU64(*payload.blobGasUsed).has_value())
    {
        return std::string("blobGasUsed exceeds the uint64 range of the ETH header field");
    }

    // Jovian: DA footprint (blobGasUsed) must not exceed gasLimit.
    if (jovianActive && *payload.blobGasUsed > payload.gasLimit)
    {
        return std::string("DA footprint (blobGasUsed) exceeds the block gas limit");
    }

    // OP carries no execution requests.
    if (request.executionRequests.has_value() && !request.executionRequests->empty())
    {
        return std::string("executionRequests must be absent or empty on the OP path");
    }
    return std::nullopt;
}

bcos::protocol::BlockHeader::Ptr bcos::engine::detail::rebuildOpEthHeader(
    const bcos::protocol::BlockHeaderFactory::Ptr& factory, const ExecutionPayload& payload,
    const h256& transactionsRoot, const h256& parentBeaconBlockRoot)
{
    // Requires validateOpNewPayloadRequest to have passed (optionals are engaged).
    auto header = factory->createBlockHeader();
    const auto number = static_cast<bcos::protocol::BlockNumber>(payload.blockNumber);
    header->setNumber(number);
    header->setTimestamp(static_cast<int64_t>(payload.timestamp));
    header->setParentInfo(
        bcos::protocol::ParentInfo{.blockNumber = number - 1, .blockHash = payload.parentHash});
    header->setCoinbase(payload.feeRecipient);
    header->setStateRoot(payload.stateRoot);
    header->setTxsRoot(transactionsRoot);
    header->setReceiptsRoot(payload.receiptsRoot);
    const auto bloom = toEthLogsBloom(payload.logsBloom);
    header->setLogsBloom(bcos::bytesConstRef(bloom.data(), bloom.size()));
    header->setGasLimit(payload.gasLimit);
    header->setGasUsed(payload.gasUsed);
    header->setExtraData(payload.extraData);
    header->setPrevRandao(payload.prevRandao);
    header->setBaseFee(payload.baseFeePerGas);
    header->setWithdrawalsRoot(payload.withdrawalsRoot.value());
    header->setBlobGasUsed(payload.blobGasUsed.value());
    // excessBlobGas is 0 after validation.
    header->setExcessBlobGas(bcos::u256(0));
    header->setParentBeaconBlockRoot(parentBeaconBlockRoot);
    header->setRequestsHash(c_opEmptyRequestsHash);
    // Post-merge uncleHash/difficulty/nonce constants.
    applyOpHeaderConstants(*header);
    return header;
}
