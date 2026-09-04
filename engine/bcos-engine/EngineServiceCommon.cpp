/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "EngineServiceCommon.h"

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
    return {"engine_exchangeCapabilities", "engine_forkchoiceUpdatedV1",
        "engine_forkchoiceUpdatedV2", "engine_forkchoiceUpdatedV3", "engine_getPayloadV1",
        "engine_getPayloadV2", "engine_getPayloadV3", "engine_getPayloadV4", "engine_getPayloadV5",
        "engine_newPayloadV1", "engine_newPayloadV2", "engine_newPayloadV3", "engine_newPayloadV4"};
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
std::pair<std::uint32_t, std::uint32_t> decodeEip1559Params(std::span<const bcos::byte> params)
{
    BOOST_ASSERT(params.size() >= 8);
    return {readU32BE(params, 0), readU32BE(params, 4)};
}
}  // namespace

std::optional<std::string> validatePayloadAttributes(
    const PayloadAttributes& payloadAttributes, std::uint32_t version)
{
    if (payloadAttributes.transactions.has_value())
    {
        if (payloadAttributes.transactions->size() > kMaxForcedTxCount)
        {
            return "payloadAttributes.transactions exceeds the forced-tx count ceiling";
        }
        std::size_t totalBytes = 0;
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
            totalBytes += raw.size();
            if (totalBytes > kMaxForcedTxBytes)
            {
                return "payloadAttributes.transactions exceeds the forced-tx byte ceiling";
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
    // Keep-local-body contract (finding BL): optional V3-omitted fields
    // (withdrawalsRoot / blob-gas) are compared only when BOTH sides sent them.
    // Submitted-absent vs built-present is not a mismatch — GetPayloadV3 may omit
    // a field the local payload still carries, and the CL is echoing the hash,
    // not re-deriving every optional.
    auto mismatch = [](char const* field) {
        return std::string("executionPayload.") + field +
               " does not match the payload this node built under the submitted blockHash";
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
    if (submitted.withdrawalsRoot.has_value() && built.withdrawalsRoot.has_value() &&
        *submitted.withdrawalsRoot != *built.withdrawalsRoot)
    {
        return mismatch("withdrawalsRoot");
    }
    if (submitted.blobGasUsed.has_value() && built.blobGasUsed.has_value() &&
        *submitted.blobGasUsed != *built.blobGasUsed)
    {
        return mismatch("blobGasUsed");
    }
    if (submitted.excessBlobGas.has_value() && built.excessBlobGas.has_value() &&
        *submitted.excessBlobGas != *built.excessBlobGas)
    {
        return mismatch("excessBlobGas");
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
    static const auto kEmptyOmmersHash = bcos::crypto::HashType(
        "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347");
    header.setUncleHash(kEmptyOmmersHash);
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
        BOOST_THROW_EXCEPTION(std::runtime_error{
            "EngineService: failed to compute Eth RLP hash: " + error->errorMessage()});
    }
}

}  // namespace bcos::engine::detail
