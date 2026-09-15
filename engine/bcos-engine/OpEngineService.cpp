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
 * @file OpEngineService.cpp
 * @brief OP Engine API service validators (OP payload-attribute and newPayload-request validation)
 */

#include "OpEngineService.h"

#include "EngineServiceCommon.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-framework/engine/RawTransactionDispatch.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <opstack-executor/OpBlockExecute.h>
#include <limits>

namespace bcos::engine::engine_common::op
{
namespace
{
constexpr char const* c_opMaxBlockGasLimitMessage =
    "gasLimit exceeds the maximum block gas limit (2^63-1)";

constexpr bool gasLimitExceedsOpCap(std::uint64_t gasLimit) noexcept
{
    return gasLimit > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
}

// Consensus header constants live in engine_common (EngineServiceCommon.h) — shared with
// the Eth builder so the keccak256(rlp(header))-critical literals have exactly one home.
/// Transactions are the same contract in every window: the release carrier is
/// transactions[i].raw (no dual rawTransactions mirror), and empty is valid
/// (deposit-only / empty blocks).
std::optional<std::string> validateOpPayloadTransactions(const ExecutionPayload& payload)
{
    for (std::size_t i = 0; i < payload.transactions.size(); ++i)
    {
        if (payload.transactions[i].raw.empty())
        {
            return "executionPayload.transactions[" + std::to_string(i) + "] is empty";
        }
        if (auto error = engine_common::validateRawTransactionKind(
                dispatchRawTransaction(bcos::ref(payload.transactions[i].raw)), i))
        {
            return error;
        }
    }
    return std::nullopt;
}

/// Shanghai's withdrawal list appears at Canyon, so "absent" and "present but empty"
/// are different contracts: Bedrock/Regolith payloads omit the field entirely
/// (op-geth NewPayloadV2 before Shanghai expects nil, after it empty).
std::optional<std::string> validateOpPayloadWithdrawals(
    const ExecutionPayload& payload, OpForkId forkId)
{
    if (forkId >= OpForkId::Canyon)
    {
        if (!payload.withdrawals.has_value() || !payload.withdrawals->empty())
        {
            return std::string("withdrawals must be present and empty on the OP path");
        }
        return std::nullopt;
    }
    if (payload.withdrawals.has_value())
    {
        return std::string("withdrawals must be absent before the Canyon fork");
    }
    return std::nullopt;
}

std::optional<std::string> validateOpBlobVersionedHashes(const NewPayloadRequest& request)
{
    if (!request.expectedBlobVersionedHashes.empty())
    {
        return std::string("expectedBlobVersionedHashes must be an empty array on the OP path");
    }
    return std::nullopt;
}

/// Cancun's fields arrive with Ecotone (V3) and Prague's with Isthmus (V4); below each
/// window the corresponding field must be absent entirely, not merely zero.
std::optional<std::string> validateOpPayloadWindowFields(
    const NewPayloadRequest& request, const ExecutionPayload& payload, std::uint32_t version)
{
    if (version >= static_cast<std::uint32_t>(ApiVersion::V3))
    {
        if (!request.parentBeaconBlockRoot.has_value())
        {
            return std::string("parentBeaconBlockRoot must be a 32-byte hash for newPayloadV3+");
        }
        if (!payload.excessBlobGas.has_value() || *payload.excessBlobGas != 0)
        {
            return std::string("excessBlobGas must be present and zero on the OP path");
        }
        if (!payload.blobGasUsed.has_value())
        {
            return std::string("blobGasUsed must be present on the OP path");
        }
    }
    else
    {
        if (request.parentBeaconBlockRoot.has_value())
        {
            return std::string("parentBeaconBlockRoot must be absent before the Ecotone fork");
        }
        if (payload.excessBlobGas.has_value() || payload.blobGasUsed.has_value())
        {
            return std::string("blob gas fields must be absent before the Ecotone fork");
        }
    }
    if (version >= static_cast<std::uint32_t>(ApiVersion::V4))
    {
        if (!payload.withdrawalsRoot.has_value())
        {
            return std::string("withdrawalsRoot is required on the OP path (Isthmus+)");
        }
        // Same reasoning as the Eth sibling (EngineServiceImpl.h): the wire already
        // enforces the fourth newPayloadV4 parameter (parseNewPayloadRequest always sets
        // the list for V4), so accepting a missing list here would hand in-process
        // callers a laxer Isthmus contract than the wire.
        if (!request.executionRequests.has_value() || !request.executionRequests->empty())
        {
            return std::string("executionRequests must be a present-but-empty list on the OP path");
        }
    }
    else
    {
        if (payload.withdrawalsRoot.has_value())
        {
            return std::string("withdrawalsRoot must be absent before the Isthmus fork");
        }
        if (request.executionRequests.has_value())
        {
            return std::string("executionRequests must be absent before the Isthmus fork");
        }
    }
    return std::nullopt;
}

/// Header numbers with the ETH width caps, plus the extraData layout this fork requires.
std::optional<std::string> validateOpPayloadHeaderFields(
    const ExecutionPayload& payload, OpForkId forkId)
{
    if (payload.blockNumber < 0)
    {
        return std::string("blockNumber must not be negative");
    }
    if (!narrowU256ToU64(payload.gasLimit).has_value())
    {
        return std::string("gasLimit exceeds the uint64 range of the ETH header field");
    }
    if (gasLimitExceedsOpCap(*narrowU256ToU64(payload.gasLimit)))
    {
        return std::string(c_opMaxBlockGasLimitMessage);
    }
    if (auto error = validateOpExtraDataForLayout(payload.extraData, extraDataLayoutFor(forkId)))
    {
        return "executionPayload.extraData " + *error;
    }
    if (!narrowU256ToU64(payload.gasUsed).has_value())
    {
        return std::string("gasUsed exceeds the uint64 range of the ETH header field");
    }
    return std::nullopt;
}

/// blobGasUsed exists from V3 on: before Jovian it must be zero (the DA footprint only
/// enters the fee from Jovian), and from Jovian it may not exceed the block gas limit.
std::optional<std::string> validateOpBlobGasUsed(
    const ExecutionPayload& payload, OpForkId forkId, std::uint32_t version)
{
    if (version < static_cast<std::uint32_t>(ApiVersion::V3))
    {
        return std::nullopt;
    }
    auto const blobGasUsed = narrowU256ToU64(*payload.blobGasUsed);
    if (!blobGasUsed.has_value())
    {
        return std::string("blobGasUsed exceeds the uint64 range of the ETH header field");
    }
    if (forkId >= OpForkId::Jovian)
    {
        // op-geth core/block_validator.go:127 requires the header's blobGasUsed to equal the
        // locally recomputed DA footprint Σ before the gas-limit range check. This lane only
        // range-checked the field, so a payload disagreeing with the local Σ was accepted,
        // stamped into the header and fed into Jovian's baseFee = max(gasUsed, blobGasUsed).
        std::vector<bcos::bytesConstRef> envelopes;
        envelopes.reserve(payload.transactions.size());
        for (auto const& tx : payload.transactions)
        {
            envelopes.emplace_back(tx.raw.data(), tx.raw.size());
        }
        auto fpError = bcos::evm::opstack::DaFootprintError::None;
        auto const local = bcos::evm::opstack::daFootprintOfEnvelopes(envelopes, &fpError);
        if (!local.has_value())
        {
            // Fail closed, mirroring op-geth's CalcDAFootprint error on an envelope set it
            // cannot price (no leading L1-attributes deposit, malformed attributes) or a Σ that
            // would overflow uint64. Never accept an unverifiable slot. Both messages keep the
            // "DA footprint" substring tests key on.
            if (fpError == bcos::evm::opstack::DaFootprintError::Overflow)
            {
                return std::string(
                    "invalid DA footprint in blobGasUsed field (local DA footprint overflows "
                    "uint64)");
            }
            return std::string(
                "invalid DA footprint in blobGasUsed field (local DA footprint unavailable)");
        }
        if (*blobGasUsed != *local)
        {
            return "invalid DA footprint in blobGasUsed field (remote: " +
                   std::to_string(*blobGasUsed) + " local: " + std::to_string(*local) + ")";
        }
    }
    if (forkId < OpForkId::Jovian && *payload.blobGasUsed != 0)
    {
        return std::string("blobGasUsed must be zero before Jovian (OP Isthmus)");
    }
    if (forkId >= OpForkId::Jovian && *payload.blobGasUsed > payload.gasLimit)
    {
        return std::string("DA footprint (blobGasUsed) exceeds the block gas limit");
    }
    return std::nullopt;
}

}  // namespace

std::optional<bcostars::Transaction> opEnvelopeToTars(
    bcos::bytes const& env, bcos::crypto::HashType const& txHash, bool allowDeposit)
{
    bcos::rpc::Web3Transaction web3Tx;
    bcos::bytesRef envRef{const_cast<bcos::byte*>(env.data()), env.size()};
    if (auto err = bcos::codec::rlp::decode(envRef, web3Tx); err)
    {
        return std::nullopt;
    }
    if (!envRef.empty())
    {
        return std::nullopt;
    }
    // Deposit envelopes are an OP-Stack extension: the OP lane must accept them (the
    // CL submits deposits via payloadAttributes.transactions), but the shared decode
    // must not admit 0x7e on the Eth lane — the type is invalid outside OP and no
    // Eth client would re-execute the block, so executing one would fork the chain
    // from every honest peer. The Eth build path answers this as undecodable, which
    // updateForkchoice maps to a terminal INVALID — the same contract as any other
    // inadmissible payload content.
    if (web3Tx.type == bcos::rpc::TransactionType::Deposit && !allowDeposit)
    {
        return std::nullopt;
    }
    auto tarsTx = web3Tx.takeToTarsTransaction();
    tarsTx.extraTransactionHash.assign(txHash.begin(), txHash.end());
    if (tarsTx.sender.empty())
    {
        try
        {
            auto sender = bcos::fromHex(web3Tx.sender());
            tarsTx.sender.assign(sender.begin(), sender.end());
        }
        catch (std::exception const&)
        {
            return std::nullopt;
        }
    }
    return tarsTx;
}

void applyOpHeaderConstants(bcos::protocol::BlockHeader& header)
{
    header.setUncleHash(engine_common::c_emptyOmmersHash);
    header.setDifficulty(bcos::u256(0));
    header.setNonce(engine_common::c_posNonce);
}

std::vector<std::string> supportedOpCapabilities()
{
    // The OP lane's implemented window, NOT the Eth list. With the payload-timestamp
    // profile (engineApiFor / extraDataLayoutFor) the live method now varies by fork:
    // newPayloadV2 and getPayloadV2 run from Regolith (the baseline) up, V3
    // Ecotone/Fjord/Granite/Holocene, V4 Isthmus+, getPayloadV5 Karst. Advertising
    // profile can select keeps a CL from picking a method this lane rejects (-38005)
    // on every call. Still absent: newPayloadV1 (op-node starts at V2 — Bedrock is
    // its first fork), newPayloadV5 and FCU V4 (both exist upstream — op-geth
    // api.go NewPayloadV5 / ForkchoiceUpdatedV4, Amsterdam — but this lane does not
    // implement them; Amsterdam is beyond Karst, so op-node never selects them here).
    // op-geth advertises by reflection over every method it implements
    // (ExchangeCapabilities), i.e. also never a fork-trimmed subset.
    static const std::vector<std::string> caps{"engine_exchangeCapabilities",
        "engine_forkchoiceUpdatedV1", "engine_forkchoiceUpdatedV2", "engine_forkchoiceUpdatedV3",
        "engine_getPayloadV2", "engine_getPayloadV3", "engine_getPayloadV4", "engine_getPayloadV5",
        "engine_newPayloadV2", "engine_newPayloadV3", "engine_newPayloadV4"};
    return caps;
}

std::optional<std::uint64_t> narrowU256ToU64(const u256& value)
{
    if (!bcos::u256FitsUint64(value))
    {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(value);
}

bcos::h2048 toEthLogsBloom(const Bloom& logsBloom)
{
    return bcos::h2048(logsBloom.data(), logsBloom.size());
}

std::optional<std::string> validateOpPayloadAttributes(
    const PayloadAttributes& payloadAttributes, OpForkId forkId)
{
    if (!payloadAttributes.gasLimit.has_value())
    {
        return std::string("gasLimit parameter is required (OP rollup)");
    }
    // Same 2^63-1 cap as validateOpNewPayloadRequest. JSON parseQuantity already
    // rejects >uint64; this closes the window where FCU would stamp a payloadId
    // that newPayload then refuses (op-geth defers the cap to VerifyHeader).
    if (gasLimitExceedsOpCap(*payloadAttributes.gasLimit))
    {
        return std::string(c_opMaxBlockGasLimitMessage);
    }
    // The fork's extraData layout decides which 1559 fields these attributes may
    // carry (op-geth checkOptimismPayloadAttributes): pre-Holocene has neither,
    // Holocene adds the 8-byte params, Jovian adds minBaseFee.
    auto const layout = extraDataLayoutFor(forkId);
    if (layout == OpExtraDataLayout::Empty)
    {
        if (payloadAttributes.eip1559Params.has_value())
        {
            return std::string("eip1559Params is not allowed before the Holocene fork");
        }
    }
    else
    {
        if (!payloadAttributes.eip1559Params.has_value())
        {
            return std::string("eip1559Params is required on the OP path (Holocene+)");
        }
        if (payloadAttributes.eip1559Params->size() != 8)
        {
            return std::string("eip1559Params must be exactly 8 bytes");
        }
        const auto [denominator, elasticity] =
            bcos::engine::decodeEip1559Params(*payloadAttributes.eip1559Params);
        if (auto error = engine_common::validateHolocene1559Params(denominator, elasticity))
        {
            return error;
        }
    }
    if (payloadAttributes.withdrawals.has_value() && !payloadAttributes.withdrawals->empty())
    {
        return std::string("withdrawals must be empty on the OP path");
    }
    if (layout == OpExtraDataLayout::Jovian17)
    {
        if (!payloadAttributes.minBaseFee.has_value())
        {
            return std::string("minBaseFee is required after the Jovian fork");
        }
    }
    else if (payloadAttributes.minBaseFee.has_value())
    {
        return std::string("minBaseFee must be null before the Jovian fork");
    }
    return std::nullopt;
}

std::optional<std::string> validateOpNewPayloadRequest(
    const NewPayloadRequest& request, OpForkId forkId, std::uint32_t version)
{
    const auto& payload = request.executionPayload;
    // Each stage owns one contract axis of the (version, fork) pair. The set of rejected
    // payloads matches the old monolithic body, but the first-error ORDER does not:
    // withdrawalsRoot and executionRequests moved into validateOpPayloadWindowFields, which
    // now runs before validateOpPayloadHeaderFields, so a payload violating both a window
    // field and a header field returns the window field's message.
    if (auto error = validateOpPayloadTransactions(payload))
    {
        return error;
    }
    if (auto error = validateOpPayloadWithdrawals(payload, forkId))
    {
        return error;
    }
    if (auto error = validateOpBlobVersionedHashes(request))
    {
        return error;
    }
    if (auto error = validateOpPayloadWindowFields(request, payload, version))
    {
        return error;
    }
    if (auto error = validateOpPayloadHeaderFields(payload, forkId))
    {
        return error;
    }
    return validateOpBlobGasUsed(payload, forkId, version);
}

bcos::protocol::BlockHeader::Ptr rebuildOpEthHeader(
    const bcos::protocol::BlockHeaderFactory::Ptr& factory, const ExecutionPayload& payload,
    const h256& transactionsRoot, std::optional<h256> const& parentBeaconBlockRoot, OpForkId forkId)
{
    // Intentionally NO setEthBlockVersion (unlike detail::finalizeEthBlockHeader): the OP
    // header is a FISCO BlockHeader whose ethBlockVersion stays NON_ETH. That is exactly
    // what EthBlockHeader::computeHash documents itself for ("usable for FISCO-native/OP
    // headers (EthBlockVersion::NON_ETH) that calculateRLPHash's validateHeader rejects"),
    // while canonicalBlockHash routes OP-shaped headers (isOpEthereumBlock: NON_ETH plus
    // the fork fields, BlockHeaderHash.cpp) to that same computeHash. The RLP encoding
    // cannot depend on the version field: the ctor builds EthBlockHeaderData from field
    // presence (each optional fork field copied when set) and the shared codec encodes
    // exactly the set optionals positionally — EthBlockHeaderData carries no version input
    // at all. With every fork field stamped below, the encoding is the full 21-field form
    // op-geth produces, and the external-oracle golden test
    // (op_golden_vector_rebuild_matches_op_geth_block_hash, vendored corpus) pins it byte
    // for byte. calculateRLPHash (validateHeader path) is not usable on these headers by
    // design; finalizeEthBlockHeader needs setEthBlockVersion only because it goes through
    // calculateRLPHash on the Eth lane.
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
    // Header fields appear with their Ethereum fork, exactly as detail::finalizeEthBlockHeader
    // does on the Eth lane: a pre-Canyon block carries no withdrawals hash, a pre-Ecotone
    // block no blob pair or beacon root, a pre-Isthmus block no requests hash. Setting only
    // what the fork defines is what makes the RLP match op-geth, whose corresponding header
    // fields are optional/nil there.
    if (forkId >= OpForkId::Canyon)
    {
        // Isthmus+ (V4) carries the root in the payload and it is authoritative: using it
        // keeps the header byte-identical to what the CL hashed, and a wrong root still
        // fails the blockHash comparison. Before Isthmus the field does not exist, so the
        // EL derives it from the withdrawals list (always empty on OP).
        header->setWithdrawalsRoot(forkId >= OpForkId::Isthmus ?
                                       payload.withdrawalsRoot.value() :
                                       bcos::engine::detail::withdrawalsRootFor(payload));
    }
    if (forkId >= OpForkId::Ecotone)
    {
        // Both are guaranteed by validateOpNewPayloadRequest (V3+) / the accepted attrs
        // (V3 build), so .value() here matches detail::finalizeEthBlockHeader's own
        // precondition style rather than silently building a hash for a bogus block.
        header->setBlobGasUsed(payload.blobGasUsed.value());
        header->setExcessBlobGas(bcos::u256(0));
        header->setParentBeaconBlockRoot(parentBeaconBlockRoot.value());
    }
    if (forkId >= OpForkId::Isthmus)
    {
        header->setRequestsHash(engine_common::c_emptyRequestsHash);
    }
    applyOpHeaderConstants(*header);
    return header;
}

}  // namespace bcos::engine::engine_common::op
