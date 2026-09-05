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
 * @file EngineServiceCommon.cpp
 * @brief Definitions of the shared Engine API validators and helpers
 */

#include "EngineServiceCommon.h"

// Upstream pin: op-geth d401af16f2dd94b010a72eaef10e07ac10b31931
// (eth/catalyst/api.go GetPayloadVn / forkchoiceUpdated, miner/payload_building.go).

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/engine/Errors.h"
#include "bcos-framework/engine/OpBaseFee.h"
#include "bcos-framework/engine/RawTransactionDispatch.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "engine/bcos-engine/PayloadId.h"
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <boost/assert.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <span>
#include <stdexcept>

namespace bcos::engine::engine_common
{

bool isGetPayloadVersionCompatible(ApiVersion requestVersion, std::uint32_t payloadVersion)
{
    switch (requestVersion)
    {
    case ApiVersion::V1:
        return payloadVersion == 1;
    case ApiVersion::V2:
        return payloadVersion <= 2;
    case ApiVersion::V3:
        // GetPayloadV3 answers only PayloadV3 builds (op-geth passes
        // []engine.PayloadVersion{engine.PayloadV3}); V1/V2-tagged entries cannot
        // render the V3 wire shape (blob-gas pair, beacon root) - finding AN.
        return payloadVersion == 3;
    case ApiVersion::V4:
        // Match release EngineServiceImpl: GetPayloadV4 accepts only PayloadV3 builds
        // (op-geth GetPayloadV4 passes []PayloadVersion{PayloadV3}).
        return payloadVersion == 3;
    case ApiVersion::V5:
        return payloadVersion == 3;
    }
    return false;
}

std::uint32_t payloadShapeVersion(std::uint32_t methodVersion)
{
    // op-geth: ForkchoiceUpdatedV3/V4 call forkchoiceUpdated(..., PayloadV3);
    // GetPayloadV4 accepts only payloadID.Is(PayloadV3).
    return std::min(methodVersion, static_cast<std::uint32_t>(ApiVersion::V3));
}

std::vector<std::string> supportedCapabilities()
{
    // Everything this node implements, not a fork-narrowed subset. op-geth advertises its
    // full `caps` list regardless of the active fork and lets the CL pick; op-node picks
    // its method versions from the rollup config (forkchoiceUpdatedV3 / getPayloadV5 /
    // newPayloadV4 on Karst) without needing the EL to prune the list for it. Narrowing
    // here would also break the pre-Karst callers this node still serves — the v1 Engine
    // API harness behind unsafe_allow_v1_executor and the V1-V3 integration suites.
    //
    // Eth and Op advertise the same list. FCU V4 is unimplemented (Endpoint -38005)
    // and absent upstream (op-geth / op-node top out at V3), so it is not listed.
    // A V4-shaped build still stores PayloadV3 (payloadShapeVersion).
    static const std::vector<std::string> caps{"engine_exchangeCapabilities",
        "engine_forkchoiceUpdatedV1", "engine_forkchoiceUpdatedV2", "engine_forkchoiceUpdatedV3",
        "engine_getPayloadV1", "engine_getPayloadV2", "engine_getPayloadV3", "engine_getPayloadV4",
        "engine_getPayloadV5", "engine_newPayloadV1", "engine_newPayloadV2", "engine_newPayloadV3",
        "engine_newPayloadV4"};
    return caps;
}

/// Shared over the two transaction carriers (attributes hex strings and payload raw
/// bytes): a blob (type-3) or unsupported/unknown-type transaction invalidates the whole
/// carrier — it is never dropped individually. Blob rejection is FISCO's OP policy, not an
/// op-geth check (decodeTyped accepts 0x03; see the OpScheduler.h type-byte gate note).
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

namespace
{
[[nodiscard]] std::uint32_t readU32BE(std::span<const bcos::byte> bytes, std::size_t off)
{
    if (off + 4 > bytes.size())
    {
        BOOST_THROW_EXCEPTION(
            InvalidEngineEncoding{} << bcos::errinfo_comment{"readU32BE: offset out of range"});
    }
    return (static_cast<std::uint32_t>(bytes[off]) << 24) |
           (static_cast<std::uint32_t>(bytes[off + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[off + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[off + 3]);
}

std::pair<std::uint32_t, std::uint32_t> decodeEip1559Params(std::span<const bcos::byte> params)
{
    if (params.size() < 8)
    {
        BOOST_THROW_EXCEPTION(InvalidEngineEncoding{} <<
                              bcos::errinfo_comment{"decodeEip1559Params: need 8 bytes"});
    }
    return {readU32BE(params, 0), readU32BE(params, 4)};
}
}  // namespace

std::optional<std::string> validatePayloadAttributes(const PayloadAttributes& payloadAttributes,
    std::uint32_t version, std::vector<bcos::bytes>* decodedForcedTxs)
{
    if (decodedForcedTxs != nullptr)
    {
        decodedForcedTxs->clear();
    }
    if (payloadAttributes.transactions.has_value())
    {
        if (payloadAttributes.transactions->size() > c_maxForcedTxCount)
        {
            return "payloadAttributes.transactions exceeds the forced-tx count ceiling";
        }
        std::size_t totalBytes = 0;
        if (decodedForcedTxs != nullptr)
        {
            decodedForcedTxs->reserve(payloadAttributes.transactions->size());
        }
        for (std::size_t i = 0; i < payloadAttributes.transactions->size(); ++i)
        {
            auto const& hexTx = (*payloadAttributes.transactions)[i];
            // Finding BY: reject on hex length before fromHex allocates the body.
            auto const estimated = decodedHexByteCount(hexTx);
            if (estimated > c_maxForcedTxBytes || totalBytes > c_maxForcedTxBytes - estimated)
            {
                return "payloadAttributes.transactions exceeds the forced-tx byte ceiling";
            }
            bcos::bytes raw;
            try
            {
                raw = bcos::fromHex(hexTx);
            }
            catch (std::exception const&)
            {
                return "payloadAttributes.transactions[" + std::to_string(i) +
                       "] is not a hex string";
            }
            totalBytes += raw.size();
            if (auto error = validateRawTransactionKind(dispatchRawTransaction(bcos::ref(raw)), i))
            {
                return error;
            }
            if (decodedForcedTxs != nullptr)
            {
                decodedForcedTxs->push_back(std::move(raw));
            }
        }
    }
    if (version == 1 && payloadAttributes.withdrawals.has_value())
    {
        return std::string("withdrawals are not part of PayloadAttributesV1");
    }
    if (version <= 2 && payloadAttributes.parentBeaconBlockRoot.has_value())
    {
        return std::string("parentBeaconBlockRoot is only valid for PayloadAttributesV3");
    }
    if (version >= 2 && !payloadAttributes.withdrawals.has_value())
    {
        return std::string("withdrawals are required for PayloadAttributesV2 and V3");
    }
    if (version >= 2 && payloadAttributes.withdrawals.has_value() &&
        !payloadAttributes.withdrawals->empty())
    {
        return std::string(
            "non-empty withdrawals are not supported until the withdrawals trie root is "
            "computed");
    }
    if (version >= 3 && !payloadAttributes.parentBeaconBlockRoot.has_value())
    {
        // op-geth ForkchoiceUpdatedV3/V4 both reject missing BeaconRoot when attrs present.
        return std::string("parentBeaconBlockRoot must be a 32-byte hash for V3 and later");
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
        if (auto error = validateHolocene1559Params(denominator, elasticity))
        {
            return error;
        }
    }
    return std::nullopt;
}

std::optional<PayloadID> derivePayloadId(const PayloadAttributes& payloadAttributes,
    const h256& parentHash, std::uint32_t version, std::span<const bcos::bytes> decodedForcedTxs)
{
    std::vector<h256> txHashes;
    if (!decodedForcedTxs.empty())
    {
        txHashes.reserve(decodedForcedTxs.size());
        for (auto const& raw : decodedForcedTxs)
        {
            txHashes.emplace_back(bcos::crypto::keccak256Hash(bcos::ref(raw)));
        }
    }
    else if (payloadAttributes.transactions.has_value() && !payloadAttributes.transactions->empty())
    {
        // No hex fallback: callers must pass validatePayloadAttributes' decoded
        // bodies. An empty span here would otherwise unbounded-fromHex.
        return std::nullopt;
    }
    return bcos::engine::derivePayloadId(
        payloadAttributes, parentHash, txHashes, static_cast<uint8_t>(version));
}

PayloadStatus makeStatus(PayloadValidationStatus status, std::optional<h256> latestValidHash,
    std::optional<std::string> validationError)
{
    return PayloadStatus{
        .latestValidHash = latestValidHash,
        .validationError = std::move(validationError),
        .status = status,
    };
}

void requireGetPayloadShape(std::uint32_t builtVersion, const ExecutionPayload& payload,
    std::optional<h256> const& parentBeaconBlockRoot, std::uint32_t requestVersion)
{
    if (!isGetPayloadVersionCompatible(static_cast<ApiVersion>(requestVersion), builtVersion))
    {
        BOOST_THROW_EXCEPTION(IncompatiblePayloadVersion{} << bcos::errinfo_comment{
                                  "Payload version is incompatible with requested method version"});
    }
    if (requestVersion >= static_cast<std::uint32_t>(ApiVersion::V4) &&
        !payload.withdrawalsRoot.has_value())
    {
        BOOST_THROW_EXCEPTION(IncompatiblePayloadVersion{} << bcos::errinfo_comment{
                                  "Payload does not carry the V4+ response shape"});
    }
    if (requestVersion >= static_cast<std::uint32_t>(ApiVersion::V3) &&
        requestVersion < static_cast<std::uint32_t>(ApiVersion::V4) &&
        (!payload.blobGasUsed.has_value() || !payload.excessBlobGas.has_value() ||
            !parentBeaconBlockRoot.has_value()))
    {
        BOOST_THROW_EXCEPTION(IncompatiblePayloadVersion{} << bcos::errinfo_comment{
                                  "Payload does not carry the V3+ response shape"});
    }
}

}  // namespace bcos::engine::engine_common

namespace bcos::engine::detail
{

bcos::bytes encodeOptimismExtraData(const PayloadAttributes& payloadAttributes)
{
    if (!payloadAttributes.eip1559Params.has_value())
    {
        // Pre-Holocene: extraData must be empty (op-core/eip1559/eip1559.go:27-28).
        return {};
    }
    if (payloadAttributes.eip1559Params->size() != c_eip1559ParamsBytes)
    {
        BOOST_THROW_EXCEPTION(InvalidEngineEncoding{} <<
                              bcos::errinfo_comment{
                                  "encodeOptimismExtraData requires exactly 8 bytes of "
                                  "eip1559Params"});
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
    auto denominatorOut = out.subspan(1, 4);
    bcos::toBigEndian(denominator, denominatorOut);
    auto elasticityOut = out.subspan(5, 4);
    bcos::toBigEndian(elasticity, elasticityOut);
    if (jovian)
    {
        auto minBaseFeeOut = out.subspan(9, 8);
        bcos::toBigEndian(*payloadAttributes.minBaseFee, minBaseFeeOut);
    }
    return extraData;
}

std::optional<std::string> validateExecutionPayload(
    const ExecutionPayload& executionPayload, std::uint32_t version)
{
    for (std::size_t i = 0; i < executionPayload.transactions.size(); ++i)
    {
        auto const& raw = executionPayload.transactions[i].raw;
        if (raw.empty())
        {
            return "executionPayload.transactions[" + std::to_string(i) + "] is empty";
        }
        if (auto error = engine_common::validateRawTransactionKind(
                dispatchRawTransaction(bcos::ref(raw)), i))
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
    if (version >= 2 && executionPayload.withdrawals.has_value() &&
        !executionPayload.withdrawals->empty())
    {
        // Mirror validatePayloadAttributes: this node cannot compute a real withdrawals
        // trie root (empty-trie placeholder), so a non-empty list is uncommittable at
        // every version that carries the field — not just V4+ (Isthmus).
        return std::string(
            "non-empty withdrawals are not supported until the withdrawals trie root is "
            "computed");
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
    if (version >= 4)
    {
        if (!executionPayload.withdrawalsRoot.has_value())
        {
            return std::string("withdrawalsRoot is required for ExecutionPayloadV4 and later");
        }
        auto expectedRoot = withdrawalsRootFor(executionPayload);
        if (*executionPayload.withdrawalsRoot != expectedRoot)
        {
            return std::string(
                "withdrawalsRoot does not match the value this node commits "
                "for the built header");
        }
        // finding N3: the V4 payload shape — blockAccessList and slotNumber are part
        // of the version's fields; a payload missing them is malformed (fail closed,
        // mirroring the withdrawalsRoot gate). No in-tree builder sets them yet, so
        // nothing built by this node can hit this.
        if (!executionPayload.blockAccessList.has_value() || !executionPayload.slotNumber.has_value())
        {
            return std::string(
                "blockAccessList and slotNumber are required for ExecutionPayloadV4 and later");
        }
    }
    // finding N4: pre-Holocene payloads (V1/V2) carry an empty extraData; the OP
    // Holocene/Jovian shape below would otherwise accept a 9/17-byte extraData here
    // while the attributes side rejects eip1559Params at V3- — same fork window,
    // same rule (predicate symmetry).
    if (version <= 2 && !executionPayload.extraData.empty())
    {
        return std::string("extraData must be empty for ExecutionPayloadV1/V2 (pre-Holocene)");
    }
    if (auto error = validateOpExtraDataShape(executionPayload.extraData))
    {
        return "executionPayload.extraData " + *error;
    }
    return std::nullopt;
}

std::optional<std::string> compareWithBuiltPayload(
    const ExecutionPayload& submitted, const ExecutionPayload& built)
{
    // op-geth ExecutableDataToBlock re-derives keccak256(rlp(header)) from every
    // hash-relevant field. Compare the fields this node actually built (cache hit).
    // Keep-local-body (finding BL): optional V3 fields are compared only when
    // BOTH sides have them. Presence XOR is not INVALID — GetPayloadV3 may omit
    // a field the local payload still carries, and the CL is echoing the hash.
    auto mismatch = [](char const* field) {
        return std::string("executionPayload.") + field +
               " does not match the payload this node built under the submitted blockHash";
    };
    // Presence XOR is not a mismatch. GetPayloadV3 may omit a V4-only field the
    // in-memory payload still carries; a V2 body may omit blob-gas that a later
    // cache entry filled. Only a present-vs-present value disagreement is INVALID.
    auto optionalMismatch = [&](char const* field, auto const& submittedField,
                                auto const& builtField) -> std::optional<std::string> {
        if (submittedField.has_value() && builtField.has_value() && *submittedField != *builtField)
        {
            return mismatch(field);
        }
        return std::nullopt;
    };
    if (submitted.extraData != built.extraData)
    {
        return mismatch("extraData");
    }
    if (submitted.parentHash != built.parentHash)
    {
        return mismatch("parentHash");
    }
    if (submitted.stateRoot != built.stateRoot)
    {
        return mismatch("stateRoot");
    }
    if (submitted.receiptsRoot != built.receiptsRoot)
    {
        return mismatch("receiptsRoot");
    }
    if (submitted.logsBloom != built.logsBloom)
    {
        return mismatch("logsBloom");
    }
    if (submitted.prevRandao != built.prevRandao)
    {
        return mismatch("prevRandao");
    }
    if (submitted.gasLimit != built.gasLimit)
    {
        return mismatch("gasLimit");
    }
    if (submitted.gasUsed != built.gasUsed)
    {
        return mismatch("gasUsed");
    }
    if (submitted.baseFeePerGas != built.baseFeePerGas)
    {
        return mismatch("baseFeePerGas");
    }
    if (submitted.blockHash != built.blockHash)
    {
        return mismatch("blockHash");
    }
    if (submitted.feeRecipient != built.feeRecipient)
    {
        return mismatch("feeRecipient");
    }
    if (submitted.timestamp != built.timestamp)
    {
        return mismatch("timestamp");
    }
    if (submitted.blockNumber != built.blockNumber)
    {
        return mismatch("blockNumber");
    }
    if (submitted.transactions.size() != built.transactions.size())
    {
        return mismatch("transactions");
    }
    for (std::size_t i = 0; i < submitted.transactions.size(); ++i)
    {
        if (submitted.transactions[i].raw != built.transactions[i].raw)
        {
            return mismatch("transactions");
        }
    }
    if (auto error =
            optionalMismatch("withdrawalsRoot", submitted.withdrawalsRoot, built.withdrawalsRoot))
    {
        return error;
    }
    // The withdrawals LIST is the field withdrawalsRoot commits to: the root arm
    // above alone lets a tampered list through whenever both sides omit the root
    // (V2/V3 wire) or the submitted root is copied from the build. Compare the
    // lists directly — an honest echo always carries the built list.
    if (submitted.withdrawals != built.withdrawals)
    {
        return mismatch("withdrawals");
    }
    if (auto error = optionalMismatch("blobGasUsed", submitted.blobGasUsed, built.blobGasUsed))
    {
        return error;
    }
    if (auto error =
            optionalMismatch("excessBlobGas", submitted.excessBlobGas, built.excessBlobGas))
    {
        return error;
    }
    // finding N3: the V4 fields are part of the payload this node built — an echo
    // that rewrites or drops them must not pass under the same blockHash.
    if (auto error = optionalMismatch(
            "blockAccessList", submitted.blockAccessList, built.blockAccessList))
    {
        return error;
    }
    if (auto error = optionalMismatch("slotNumber", submitted.slotNumber, built.slotNumber))
    {
        return error;
    }
    return std::nullopt;
}

bcos::protocol::EthBlockVersion ethBlockVersionFor(evmc_revision rev)
{
    switch (rev)
    {
    case EVMC_LONDON:
    case EVMC_PARIS:
        return bcos::protocol::EthBlockVersion::LONDON;
    case EVMC_SHANGHAI:
        return bcos::protocol::EthBlockVersion::SHANGHAI;
    case EVMC_CANCUN:
        return bcos::protocol::EthBlockVersion::CANCUN;
    case EVMC_PRAGUE:
    case EVMC_OSAKA:
        return bcos::protocol::EthBlockVersion::PRAGUE;
    default:
        if (rev < EVMC_LONDON)
        {
            return bcos::protocol::EthBlockVersion::LONDON;
        }
        BOOST_THROW_EXCEPTION(
            UnsupportedFork{} << bcos::errinfo_comment{"EngineService: unsupported EVM revision " +
                                                       std::to_string(static_cast<int>(rev)) +
                                                       " for Eth header fork derivation"});
    }
}

void finalizeEthBlockHeader(bcos::protocol::BlockHeader& header, const ExecutionPayload& payload,
    std::optional<bcos::h256> parentBeaconBlockRoot, bcos::protocol::EthBlockVersion forkVersion)
{
    static const auto c_emptyOmmersHash = bcos::crypto::HashType(
        "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347");
    header.setUncleHash(c_emptyOmmersHash);
    header.setDifficulty(bcos::u256(0));
    header.setNonce(bcos::h64(0));

    header.setLogsBloom(bcos::bytesConstRef(payload.logsBloom.data(), payload.logsBloom.size()));
    header.setBaseFee(payload.baseFeePerGas);

    if (forkVersion >= bcos::protocol::EthBlockVersion::SHANGHAI)
    {
        header.setWithdrawalsRoot(bcos::engine::detail::withdrawalsRootFor(payload));
    }

    if (forkVersion >= bcos::protocol::EthBlockVersion::CANCUN)
    {
        header.setBlobGasUsed(payload.blobGasUsed.value());
        header.setExcessBlobGas(payload.excessBlobGas.value());
        header.setParentBeaconBlockRoot(parentBeaconBlockRoot.value());
    }

    if (forkVersion >= bcos::protocol::EthBlockVersion::PRAGUE)
    {
        header.setRequestsHash(bcos::crypto::HashType(
            "0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    }

    header.setEthBlockVersion(forkVersion);
    if (auto error = bcos::protocol::EthBlockHeader::calculateRLPHash(header))
    {
        BOOST_THROW_EXCEPTION(
            OpExecutionInternalError{} << bcos::errinfo_comment{
                "EngineService: failed to compute Eth RLP hash: " + error->errorMessage()});
    }
}

}  // namespace bcos::engine::detail
