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
#include "EngineServiceCommon.h"
#include "bcos-framework/engine/RawTransactionDispatch.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-framework/engine/OpBaseFee.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <boost/throw_exception.hpp>
#include <span>
#include <stdexcept>
#include <utility>

namespace
{
constexpr std::size_t c_hashBytes = 32;
constexpr std::size_t c_payloadIdBytes = 8;
}  // namespace

std::string bcos::engine::detail::encodePayloadSequence(std::uint64_t value)
{
    return bcos::toHex(value, "0x");
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
    // Note: serving a method VERSION is not the same as being able to BUILD with it.
    // buildPayload requires an on-chain EVM revision (executor_version >= 2); the
    // unsafe_allow_v1_executor harness (executor_version < 2) can no longer build any
    // payload, only answer capability/state queries.
    //
    // forkchoiceUpdatedV4 is the one absentee, and genuinely so: the forkchoice version
    // window tops out at V3 (isForkchoiceVersionSupported), so the endpoint answers
    // -38005. getPayloadV5 and newPayloadV4 were added by B4.
    return {"engine_exchangeCapabilities", "engine_forkchoiceUpdatedV1",
        "engine_forkchoiceUpdatedV2", "engine_forkchoiceUpdatedV3", "engine_getPayloadV1",
        "engine_getPayloadV2", "engine_getPayloadV3", "engine_getPayloadV4", "engine_getPayloadV5",
        "engine_newPayloadV1", "engine_newPayloadV2", "engine_newPayloadV3", "engine_newPayloadV4"};
}

bool bcos::engine::detail::isGetPayloadVersionCompatible(
    ApiVersion requestVersion, std::uint32_t payloadVersion)
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
        // Same window as V5 below, and for the same reason: op-geth's GetPayloadV4 also
        // passes []engine.PayloadVersion{engine.PayloadV3} to its getPayload helper
        // (eth/catalyst/api.go GetPayloadV4/GetPayloadV5 differ only in the accepted fork
        // list). No BUILD is ever tagged above V3 — isForkchoiceVersionSupported tops out
        // there — so the two kinds of entry `<= 4` used to let through were both wrong:
        // a V1/V2 build, which serializeExecutionPayload cannot render in the V4 shape
        // (no withdrawalsRoot -> -32603, no blobGasUsed / excessBlobGas), and the V4-tagged
        // entry handleNewPayload leaves behind after a commit (PayloadEntry::version is
        // rewritten with the newPayload version), which would replay an already-committed
        // payload. V5 rejects both; V4 now behaves the same.
        return payloadVersion == 3;
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

bcos::bytes bcos::engine::detail::encodeOptimismExtraData(
    const PayloadAttributes& payloadAttributes)
{
    if (!payloadAttributes.eip1559Params.has_value())
    {
        // Pre-Holocene: extraData must be empty (op-core/eip1559/eip1559.go:27-28).
        return {};
    }
    if (payloadAttributes.eip1559Params->size() != c_eip1559ParamsBytes)
    {
        // A precondition, not a wire error: the RPC parse layer and then
        // validatePayloadAttributes both reject any other length, so reaching this
        // means an in-process PayloadAttributes producer bypassed the gate. Fail
        // loudly rather than read out of bounds (decodeEip1559Params' span
        // arithmetic is UB on a short span), and rather than return empty, which
        // would silently stamp pre-Holocene extraData on a Holocene block and stall
        // op-node at read-back.
        BOOST_THROW_EXCEPTION(std::invalid_argument{
            "encodeOptimismExtraData requires exactly 8 bytes of eip1559Params"});
    }
    auto [denominator, elasticity] = decodeEip1559Params(*payloadAttributes.eip1559Params);
    // Canyon translation of all-zero attribute params. op-node sends eip1559Params = 0,0
    // while SystemConfig has not set the parameters, and expects the EL to translate them
    // to the chain's Canyon constants (op-node engine_consolidate.go
    // checkExtraDataParamsMatch). DEPLOYMENT CONSTRAINT: a chain whose rollup.json sets
    // different chain_op_config values would fail consolidation; these stay pinned to the
    // OP Stack defaults in OpBaseFee.h (250, 6).
    if (denominator == 0 && elasticity == 0)
    {
        denominator = c_eip1559DenominatorCanyon;
        elasticity = c_eip1559ElasticityCanyon;
    }

    // Jovian 17-byte form (EncodeJovianExtraData, eip1559.go:152-162) when the CL sent
    // minBaseFee, Holocene 9-byte form (EncodeHoloceneExtraData, eip1559.go:74-83)
    // otherwise. checkExtraDataParamsMatch requires the block to carry minBaseFee iff
    // the attributes did.
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
            if (auto error = engine_common::validateRawTransactionKind(
                    dispatchRawTransaction(bcos::ref(raw)), i))
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
        // The withdrawals trie root is not computed yet (finalizeEthBlockHeader commits
        // the empty-trie root as a placeholder), so a non-empty list would hash a root
        // that differs from what geth derives (DeriveSha(withdrawals)) and every peer
        // would reject the block. Reject the pairing until the real root is computed.
        return std::string(
            "non-empty withdrawals are not supported until the withdrawals trie root is "
            "computed");
    }
    if (version >= 3 && !payloadAttributes.parentBeaconBlockRoot.has_value())
    {
        // op-geth ForkchoiceUpdatedV3/V4 both reject missing BeaconRoot when attrs present.
        return std::string("parentBeaconBlockRoot must be a 32-byte hash for V3 and later");
    }
    // eip1559Params (Holocene) and minBaseFee (Jovian) reach the EL only on a
    // forkchoiceUpdatedV3. op-node's FCU version ladder tops out at V3 — Config
    // ::ForkchoiceUpdatedVersion (op-node/rollup/types.go:727-745, v1.19.3) answers
    // FCUV3 from Ecotone onwards, FCUV2 for Canyon and FCUV1 before it, and there is
    // no FCUV4 constant at all (op-service/eth/types.go:799-801) — while Holocene and
    // Jovian both activate after Ecotone. So a conforming CL carries both fields on V3
    // and only V3.
    //
    // Note this is NOT how op-geth expresses the same rule, and the difference is worth
    // recording. op-geth has a single engine.PayloadAttributes shared by all three FCU
    // versions, and it does carry EIP1559Params / MinBaseFee at every version
    // (beacon/engine/types.go:83-89); its ForkchoiceUpdatedV1/V2/V3 wrappers check only
    // withdrawals, the beacon root and the fork window (eth/catalyst/api.go:166-214) and
    // never look at these two fields. The rejection happens later, in the shared path:
    // checkOptimismPayloadAttributes (eth/catalyst/api_optimism.go:40-64, called from
    // api.go:254) answers "non-empty eip155Params pre-Holocene" as a -38003
    // InvalidPayloadAttributes, keyed off the FORK SCHEDULE (cfg.IsHolocene(timestamp)),
    // not off the Engine API method version. This service has no fork schedule to key
    // off, so the FCU version is the only equivalent signal available — sound precisely
    // because op-node's version ladder above pins these fields to V3.
    //
    // Without this gate a V1/V2 forkchoiceUpdated carrying
    // eip1559Params would stamp Holocene extraData on a pre-Holocene build, which a
    // spec-conformant CL then rejects on read-back ("extraData must be empty before
    // Holocene", op-core/eip1559/eip1559.go:27-28).
    if (version <= 2 && payloadAttributes.eip1559Params.has_value())
    {
        return std::string("eip1559Params is only valid for PayloadAttributesV3");
    }
    if (version <= 2 && payloadAttributes.minBaseFee.has_value())
    {
        return std::string("minBaseFee is only valid for PayloadAttributesV3");
    }
    // Jovian attributes always pair minBaseFee with eip1559Params: op-node fills both
    // once Jovian is active (op-service/eth/types.go PayloadAttributes), and a
    // post-Jovian block must carry the 17-byte extraData whose [1:9) params come from
    // eip1559Params — minBaseFee alone leaves the params half undefined. op-geth rejects
    // such attributes as invalid rather than guessing (engine error -38003
    // InvalidPayloadAttributes); this service reports attribute errors through the
    // INVALID payload status channel instead (see updateForkchoice).
    if (payloadAttributes.minBaseFee.has_value() && !payloadAttributes.eip1559Params.has_value())
    {
        return std::string("minBaseFee requires eip1559Params (Jovian attributes carry both)");
    }
    if (payloadAttributes.eip1559Params.has_value())
    {
        // The RPC parse layer already enforces exactly 8 bytes (EngineHelper.cpp), but
        // this gate is the precondition encodeOptimismExtraData and the decode below
        // rely on, so enforce it here too for in-process PayloadAttributes producers.
        if (payloadAttributes.eip1559Params->size() != 8)
        {
            return std::string("eip1559Params must be exactly 8 bytes");
        }
        // ValidateHolocene1559Params: reject only e!=0 && d==0. 0,0 is valid
        // attribute input and is translated to Canyon constants by encodeOptimismExtraData.
        auto [denominator, elasticity] = decodeEip1559Params(*payloadAttributes.eip1559Params);
        if (auto error =
                bcos::engine::engine_common::validateHolocene1559Params(denominator, elasticity))
        {
            return error;
        }
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
    // The submitted root must equal the value this node itself commits (withdrawalsRootFor
    // — currently the empty-trie placeholder): a CL submitting a foreign root under the
    // blockHash this node minted would otherwise commit a header hash nobody can reproduce.
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
    // extraData is a V1-onwards field, so this applies at every newPayload version.
    // Since this PR makes extraData part of the block hash, an unchecked extraData is
    // an unchecked block-hash input. Shape rules live in OpBaseFee.h (shared with
    // calcOpBaseFee); prefix the helper's short reason for the INVALID channel.
    if (auto error = validateOpExtraDataShape(executionPayload.extraData))
    {
        return "executionPayload.extraData " + *error;
    }
    return std::nullopt;
}

std::optional<std::string> bcos::engine::detail::compareWithBuiltPayload(
    const ExecutionPayload& submitted, const ExecutionPayload& built)
{
    // op-geth ExecutableDataToBlock re-derives keccak256(rlp(header)) from every
    // hash-relevant field. Compare the fields this node actually built (cache hit).
    // Optional V3-omitted fields (withdrawalsRoot / blob-gas) are compared only when
    // the CL sent them — a missing optional is not a mismatch.
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

bcos::protocol::EthBlockVersion bcos::engine::detail::ethBlockVersionFor(evmc_revision rev)
{
    // Map the chain's EVM revision to the header fork era. PoS/Engine blocks are always
    // LONDON-shaped or later; revisions below LONDON cannot occur on this path and are
    // mapped to LONDON (the minimal post-merge shape). OSAKA has no distinct EthBlockVersion
    // enumerator — its header RLP carries no new fields beyond PRAGUE, so it maps to PRAGUE.
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
        // Asymmetric arm: revisions strictly below LONDON (FRONTIER..BERLIN) cannot
        // appear on a PoS/Engine chain, so the minimal post-merge shape is a defensible
        // floor. Any revision ABOVE the highest mapped one — a future EVMC bump adding a
        // revision after OSAKA — must fail loudly instead of hashing under the wrong fork
        // rules: that would drop withdrawalsRoot, the blob trio and requestsHash from the
        // RLP with no diagnostic, and every peer recomputing under the chain's real fork
        // would reject the block.
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

void bcos::engine::detail::finalizeEthBlockHeader(bcos::protocol::BlockHeader& header,
    const ExecutionPayload& payload, std::optional<bcos::h256> parentBeaconBlockRoot,
    bcos::protocol::EthBlockVersion forkVersion)
{
    // Post-merge constants: the empty-ommers hash (keccak256(rlp([]))), difficulty 0 and nonce 0
    // are fixed on every PoS Ethereum block.
    static const auto kEmptyOmmersHash = bcos::crypto::HashType(
        "0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347");
    header.setUncleHash(kEmptyOmmersHash);
    header.setDifficulty(bcos::u256(0));
    header.setNonce(bcos::h64(0));

    // The header itself must carry the 256-byte bloom: calculateRLPHash reads it from the header
    // (handleNewPayload sets it on the block wrapper separately).
    header.setLogsBloom(bcos::bytesConstRef(payload.logsBloom.data(), payload.logsBloom.size()));

    // EIP-1559 base fee (LONDON+). FISCO-BCOS does not compute a real base fee yet, so this is
    // whatever buildPayload placed in the payload (currently 0).
    header.setBaseFee(payload.baseFeePerGas);

    // SHANGHAI+ : withdrawalsRoot. The withdrawals trie root is not computed yet, so the
    // empty-trie root is used as a placeholder (same bytes as the served payload — see
    // withdrawalsRootFor).
    if (forkVersion >= bcos::protocol::EthBlockVersion::SHANGHAI)
    {
        header.setWithdrawalsRoot(bcos::engine::detail::withdrawalsRootFor(payload));
    }

    // CANCUN+ : blob gas fields and parent beacon block root. buildPayload always fills the
    // blob pair for the V3+ payload shape (and validatePayloadAttributes requires the beacon
    // root), so require the values instead of defaulting to zero — a missing field must not
    // silently hash as an explicit zero (absent and present-zero would share one sentinel).
    if (forkVersion >= bcos::protocol::EthBlockVersion::CANCUN)
    {
        header.setBlobGasUsed(payload.blobGasUsed.value());
        header.setExcessBlobGas(payload.excessBlobGas.value());
        header.setParentBeaconBlockRoot(parentBeaconBlockRoot.value());
    }

    // PRAGUE : EIP-7685 execution-requests hash. FISCO-BCOS produces no execution
    // requests, so the canonical empty-requests hash (sha256 of the empty input,
    // 0xe3b0c442…) is used — the value the RLP hash must carry to validate as PRAGUE.
    if (forkVersion >= bcos::protocol::EthBlockVersion::PRAGUE)
    {
        header.setRequestsHash(bcos::crypto::HashType(
            "0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    }

    // Mark the header as an Eth header, then compute and inject its RLP hash.
    header.setEthBlockVersion(forkVersion);
    if (auto error = bcos::protocol::EthBlockHeader::calculateRLPHash(header))
    {
        BOOST_THROW_EXCEPTION(std::runtime_error{
            "EngineService: failed to compute Eth RLP hash: " + error->errorMessage()});
    }
}
