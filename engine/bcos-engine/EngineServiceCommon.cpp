/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "EngineServiceCommon.h"

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/engine/RawTransactionDispatch.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "engine/bcos-engine/PayloadId.h"
#include <boost/assert.hpp>
#include <span>

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
        return payloadVersion <= 3;
    case ApiVersion::V4:
        // Match release EngineServiceImpl: GetPayloadV4 accepts only PayloadV3 builds
        // (op-geth GetPayloadV4 passes []PayloadVersion{PayloadV3}).
        return payloadVersion == 3;
    case ApiVersion::V5:
        return payloadVersion == 3;
    }
    return false;
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
    // forkchoiceUpdatedV4 is the one absentee, and genuinely so: the forkchoice version
    // window tops out at V3 (isForkchoiceVersionSupported), so the endpoint answers
    // -38005. getPayloadV5 and newPayloadV4 were added by B4.
    return {"engine_exchangeCapabilities", "engine_forkchoiceUpdatedV1",
        "engine_forkchoiceUpdatedV2", "engine_forkchoiceUpdatedV3", "engine_getPayloadV1",
        "engine_getPayloadV2", "engine_getPayloadV3", "engine_getPayloadV4", "engine_getPayloadV5",
        "engine_newPayloadV1", "engine_newPayloadV2", "engine_newPayloadV3", "engine_newPayloadV4"};
}

namespace
{
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

std::pair<std::uint32_t, std::uint32_t> decodeEip1559Params(std::span<const bcos::byte> params)
{
    BOOST_ASSERT(params.size() >= 8);
    auto readU32 = [&](std::size_t offset) {
        return (static_cast<std::uint32_t>(params[offset]) << 24) |
               (static_cast<std::uint32_t>(params[offset + 1]) << 16) |
               (static_cast<std::uint32_t>(params[offset + 2]) << 8) |
               static_cast<std::uint32_t>(params[offset + 3]);
    };
    return {readU32(0), readU32(4)};
}
}  // namespace

std::optional<std::string> validatePayloadAttributes(
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
    if (version == 3 && !payloadAttributes.parentBeaconBlockRoot.has_value())
    {
        return std::string("parentBeaconBlockRoot must be a 32-byte hash for V3");
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

std::optional<PayloadID> derivePayloadId(
    const PayloadAttributes& payloadAttributes, const h256& parentHash, std::uint32_t version)
{
    std::vector<h256> txHashes;
    if (payloadAttributes.transactions.has_value())
    {
        txHashes.reserve(payloadAttributes.transactions->size());
        for (auto const& hexTx : *payloadAttributes.transactions)
        {
            try
            {
                auto raw = bcos::fromHex(hexTx);
                txHashes.emplace_back(bcos::crypto::keccak256Hash(bcos::ref(raw)));
            }
            catch (bcos::BadHexCharacter const&)
            {
                return std::nullopt;
            }
        }
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

}  // namespace bcos::engine::engine_common
