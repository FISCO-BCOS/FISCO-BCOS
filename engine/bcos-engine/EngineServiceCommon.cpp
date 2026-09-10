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
#include <bcos-ledger/mpt/EthTrieRoots.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <boost/assert.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <span>
#include <stdexcept>
#include <vector>

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
        // render the V3 wire shape (blob-gas pair, beacon root).
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
            // reject on hex length before fromHex allocates the body.
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
    if (version <= static_cast<std::uint32_t>(ApiVersion::V2) &&
        payloadAttributes.parentBeaconBlockRoot.has_value())
    {
        return std::string("parentBeaconBlockRoot is only valid for PayloadAttributesV3");
    }
    if (version >= static_cast<std::uint32_t>(ApiVersion::V2) &&
        !payloadAttributes.withdrawals.has_value())
    {
        return std::string("withdrawals are required for PayloadAttributesV2 and V3");
    }
    if (version >= static_cast<std::uint32_t>(ApiVersion::V2) &&
        payloadAttributes.withdrawals.has_value() && !payloadAttributes.withdrawals->empty())
    {
        return std::string(
            "non-empty withdrawals are not supported until the withdrawals trie root is "
            "computed");
    }
    if (version >= static_cast<std::uint32_t>(ApiVersion::V3) &&
        !payloadAttributes.parentBeaconBlockRoot.has_value())
    {
        // op-geth ForkchoiceUpdatedV3/V4 both reject missing BeaconRoot when attrs present.
        return std::string("parentBeaconBlockRoot must be a 32-byte hash for V3 and later");
    }
    if (version <= static_cast<std::uint32_t>(ApiVersion::V2) &&
        payloadAttributes.eip1559Params.has_value())
    {
        return std::string("eip1559Params is only valid for PayloadAttributesV3");
    }
    if (version <= static_cast<std::uint32_t>(ApiVersion::V2) &&
        payloadAttributes.minBaseFee.has_value())
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
    // V4/V5 responses embed the full V3 field set, so the V3 shape requirements
    // apply at V3 and above — no upper bound.
    if (requestVersion >= static_cast<std::uint32_t>(ApiVersion::V3) &&
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
        BOOST_THROW_EXCEPTION(InvalidEngineEncoding{} << bcos::errinfo_comment{
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
    if (version >= static_cast<std::uint32_t>(ApiVersion::V2) &&
        !executionPayload.withdrawals.has_value())
    {
        return std::string("withdrawals are required for ExecutionPayloadV2 and later");
    }
    if (version >= static_cast<std::uint32_t>(ApiVersion::V2) &&
        executionPayload.withdrawals.has_value() && !executionPayload.withdrawals->empty())
    {
        // Mirror validatePayloadAttributes: this node cannot compute a real withdrawals
        // trie root (empty-trie placeholder), so a non-empty list is uncommittable at
        // every version that carries the field — not just V4+ (Isthmus).
        return std::string(
            "non-empty withdrawals are not supported until the withdrawals trie root is "
            "computed");
    }
    if (version <= static_cast<std::uint32_t>(ApiVersion::V2) &&
        (executionPayload.blobGasUsed.has_value() || executionPayload.excessBlobGas.has_value()))
    {
        return std::string("blob gas fields are only valid for ExecutionPayloadV3 and later");
    }
    if (version >= static_cast<std::uint32_t>(ApiVersion::V3) &&
        (!executionPayload.blobGasUsed.has_value() || !executionPayload.excessBlobGas.has_value()))
    {
        return std::string("blob gas fields are required for ExecutionPayloadV3 and later");
    }
    if (version >= static_cast<std::uint32_t>(ApiVersion::V4))
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
        // blockAccessList / slotNumber are intentionally NOT required here: the V4
        // wire dialect does not carry them (the CL cannot send what the shape omits),
        // and no in-tree builder fills them — requiring their presence would reject
        // every honest newPayloadV4, including echoes of this node's own builds.
    }
    // Pre-Holocene payloads must carry empty extraData.
    if (version <= static_cast<std::uint32_t>(ApiVersion::V2) &&
        !executionPayload.extraData.empty())
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
    auto mismatch = [](char const* field) {
        return std::string("executionPayload.") + field +
               " does not match the payload this node built under the submitted blockHash";
    };
    auto optionalPresentMismatch = [&](char const* field, auto const& submittedField,
                                       auto const& builtField) -> std::optional<std::string> {
        if (submittedField.has_value() && builtField.has_value() && *submittedField != *builtField)
        {
            return mismatch(field);
        }
        return std::nullopt;
    };
    // Presence-XOR, not present-vs-present: validateExecutionPayload forces blobGasUsed /
    // excessBlobGas per method version, so a payload that omits a field the built copy carries
    // (a V3 echo of a V4 build) is a real mismatch. Comparing only when both sides are engaged
    // would accept exactly that echo.
    auto optionalHashPresence = [&](char const* field, auto const& submittedField,
                                    auto const& builtField) -> std::optional<std::string> {
        if (submittedField.has_value() != builtField.has_value())
        {
            return mismatch(field);
        }
        if (submittedField.has_value() && *submittedField != *builtField)
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
    if (submitted.withdrawalsRoot.has_value() && built.withdrawalsRoot.has_value())
    {
        if (*submitted.withdrawalsRoot != *built.withdrawalsRoot)
        {
            return mismatch("withdrawalsRoot");
        }
    }
    else if (submitted.withdrawalsRoot.has_value() != built.withdrawalsRoot.has_value())
    {
        auto const& present = submitted.withdrawalsRoot.has_value() ? *submitted.withdrawalsRoot :
                                                                      *built.withdrawalsRoot;
        if (present != withdrawalsRootFor(submitted))
        {
            return mismatch("withdrawalsRoot");
        }
    }
    if (submitted.withdrawals != built.withdrawals)
    {
        return mismatch("withdrawals");
    }
    if (auto error = optionalHashPresence("blobGasUsed", submitted.blobGasUsed, built.blobGasUsed))
    {
        return error;
    }
    if (auto error =
            optionalHashPresence("excessBlobGas", submitted.excessBlobGas, built.excessBlobGas))
    {
        return error;
    }
    if (auto error = optionalPresentMismatch(
            "blockAccessList", submitted.blockAccessList, built.blockAccessList))
    {
        return error;
    }
    if (auto error = optionalPresentMismatch("slotNumber", submitted.slotNumber, built.slotNumber))
    {
        return error;
    }
    return std::nullopt;
}

namespace
{
/// Cache-miss reconstruction of transactionsRoot. Same entry point as the two other
/// producers (engine_common::buildHeaderCommitments, opstack-executor computeOpTxRoot):
/// all three MUST agree or newPayload rejects this node's own payloads.
/// calculateTransactionsRoot owns the empty-list -> emptyRootHash() contract
/// (computeIndexedTrieRoot), so it is not restated here.
bcos::h256 transactionsRootFromPayload(const ExecutionPayload& payload)
{
    std::vector<bcos::bytesConstRef> rawEnvelopes;
    rawEnvelopes.reserve(payload.transactions.size());
    for (auto const& tx : payload.transactions)
    {
        rawEnvelopes.push_back(bcos::ref(tx.raw));
    }
    return bcos::ledger::mpt::calculateTransactionsRoot(rawEnvelopes);
}
}  // namespace

std::optional<std::string> matchReconstructedEthBlockHash(
    const bcos::protocol::BlockHeaderFactory::Ptr& factory, const ExecutionPayload& payload,
    const std::optional<bcos::h256>& parentBeaconBlockRoot,
    bcos::protocol::EthBlockVersion forkVersion)
{
    // A null factory is a node-local wiring fault, not a defect in the submitted payload: it must
    // reach the caller's internal-error mapping, never the InvalidBlockHash string (a hard
    // consensus rejection op-node does not retry).
    if (!factory)
    {
        BOOST_THROW_EXCEPTION(OpExecutionInternalError{} << bcos::errinfo_comment{
                                  "EngineService: block header factory is null"});
    }
    // Only a genuine hash mismatch, or a header the submitted fields cannot reconstruct into a
    // valid Ethereum header, returns a message (both callers fold it into InvalidBlockHash).
    // Everything that reconstructs the header from the submitted fields is outside the try: a
    // header-factory or transactionsRoot-MPT fault is node-local and must propagate to the
    // caller's internal-error mapping (-32603), never be folded into InvalidBlockHash.
    auto header = factory->createBlockHeader();
    const auto number = payload.blockNumber;
    header->setNumber(number);
    header->setTimestamp(static_cast<int64_t>(payload.timestamp));
    header->setParentInfo(
        bcos::protocol::ParentInfo{.blockNumber = number - 1, .blockHash = payload.parentHash});
    header->setCoinbase(payload.feeRecipient);
    header->setStateRoot(payload.stateRoot);
    header->setTxsRoot(transactionsRootFromPayload(payload));
    header->setReceiptsRoot(payload.receiptsRoot);
    header->setGasLimit(payload.gasLimit);
    header->setGasUsed(payload.gasUsed);
    header->setExtraData(payload.extraData);
    header->setPrevRandao(payload.prevRandao);

    if (forkVersion >= bcos::protocol::EthBlockVersion::CANCUN &&
        (!payload.blobGasUsed.has_value() || !payload.excessBlobGas.has_value() ||
            !parentBeaconBlockRoot.has_value()))
    {
        return std::string("blockHash does not match the reconstructed block header");
    }

    try
    {
        finalizeEthBlockHeader(
            *header, payload, parentBeaconBlockRoot, forkVersion, payload.withdrawalsRoot);
    }
    catch (OpExecutionInternalError const&)
    {
        // finalizeEthBlockHeader raises this only when calculateRLPHash -> validateHeader rejects
        // the header rebuilt from the SUBMITTED fields (a zero stateRoot / receiptsRoot, a
        // sub-second timestamp, a missing fork field): a payload-content fault, so the
        // invalid-payload answer is right. Any other exception inside finalize is node-local and
        // propagates (the whole body used to be wrapped in a bare catch (...) that reported all
        // of them as a hash mismatch).
        return std::string("blockHash does not match the reconstructed block header");
    }
    if (header->hash() != payload.blockHash)
    {
        return std::string("blockHash does not match the reconstructed block header");
    }
    return std::nullopt;
}

std::optional<bcos::protocol::EthBlockVersion> tryEthBlockVersionFor(evmc_revision rev)
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
        // EVMC_EXPERIMENTAL ("experimental" is a nameable, round-trippable
        // SYS_CONFIG value) and anything newer than this binary maps. Whether that
        // is fatal is not this function's call: the build path must fail loudly (a
        // guessed era hashes a header the chain never configured), the newPayload
        // cache-miss path must not (it would blame the submitted block). Each
        // caller picks, via this mapping or the throwing adapter below.
        return std::nullopt;
    }
}

bcos::protocol::EthBlockVersion ethBlockVersionFor(evmc_revision rev)
{
    if (auto version = tryEthBlockVersionFor(rev))
    {
        return *version;
    }
    BOOST_THROW_EXCEPTION(
        UnsupportedFork{} << bcos::errinfo_comment{"EngineService: unsupported EVM revision " +
                                                   std::to_string(static_cast<int>(rev)) +
                                                   " for Eth header fork derivation"});
}

std::optional<bcos::protocol::EthBlockVersion> ethBlockVersionForBlock(
    ledger::LedgerConfig const& ledgerConfig, bcos::protocol::BlockNumber blockNumber)
{
    auto const revision = ledgerConfig.evmcRevisionForBlock(blockNumber);
    if (!revision.has_value())
    {
        return std::nullopt;
    }
    return tryEthBlockVersionFor(*revision);
}

void finalizeEthBlockHeader(bcos::protocol::BlockHeader& header, const ExecutionPayload& payload,
    std::optional<bcos::h256> parentBeaconBlockRoot, bcos::protocol::EthBlockVersion forkVersion,
    std::optional<bcos::h256> withdrawalsRoot)
{
    header.setUncleHash(engine_common::c_emptyOmmersHash);
    header.setDifficulty(bcos::u256(0));
    header.setNonce(engine_common::c_posNonce);

    header.setLogsBloom(bcos::bytesConstRef(payload.logsBloom.data(), payload.logsBloom.size()));
    header.setBaseFee(payload.baseFeePerGas);

    if (forkVersion >= bcos::protocol::EthBlockVersion::SHANGHAI)
    {
        header.setWithdrawalsRoot(withdrawalsRoot.value_or(withdrawalsRootFor(payload)));
    }

    if (forkVersion >= bcos::protocol::EthBlockVersion::CANCUN)
    {
        header.setBlobGasUsed(payload.blobGasUsed.value());
        header.setExcessBlobGas(payload.excessBlobGas.value());
        header.setParentBeaconBlockRoot(parentBeaconBlockRoot.value());
    }

    if (forkVersion >= bcos::protocol::EthBlockVersion::PRAGUE)
    {
        header.setRequestsHash(engine_common::c_emptyRequestsHash);
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
