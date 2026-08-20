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
#include <bcos-rlp-protocol/EthBlockHeader.h>

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

namespace
{
/// Shared over the two transaction carriers (attributes hex strings and payload raw
/// bytes): a blob (type-3) or unsupported/unknown-type transaction invalidates the whole
/// carrier — it is never dropped individually (L2 forbids blob transactions entirely).
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
        return std::string("parentBeaconBlockRoot is only valid for PayloadAttributesV3");
    }
    if (version >= 2 && !payloadAttributes.withdrawals.has_value())
    {
        return std::string("withdrawals are required for PayloadAttributesV2 and V3");
    }
    if (version == 3 && !payloadAttributes.parentBeaconBlockRoot.has_value())
    {
        return std::string("parentBeaconBlockRoot must be a 32-byte hash for V3");
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
    return std::nullopt;
}

bcos::protocol::EthBlockVersion bcos::engine::detail::ethBlockVersionFor(std::uint32_t version)
{
    switch (version)
    {
    case static_cast<std::uint32_t>(ApiVersion::V1):
        return bcos::protocol::EthBlockVersion::LONDON;
    case static_cast<std::uint32_t>(ApiVersion::V2):
        return bcos::protocol::EthBlockVersion::SHANGHAI;
    case static_cast<std::uint32_t>(ApiVersion::V3):
        return bcos::protocol::EthBlockVersion::CANCUN;
    case static_cast<std::uint32_t>(ApiVersion::V4):
        return bcos::protocol::EthBlockVersion::PRAGUE;
    default:
        // A version beyond the known fork window must fail loudly here rather than being
        // silently mapped to PRAGUE: the fork marker decides the RLP hash, so a wrong mapping
        // would corrupt every block built under the unknown version.
        BOOST_THROW_EXCEPTION(
            UnsupportedEngineApiVersion{} << bcos::errinfo_comment{
                "EngineService: unsupported Engine API version " + std::to_string(version)});
    }
}

void bcos::engine::detail::finalizeEthBlockHeader(bcos::protocol::BlockHeader& header,
    const ExecutionPayload& payload, std::optional<bcos::h256> parentBeaconBlockRoot,
    std::uint32_t version)
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

    // SHANGHAI+ (V2): withdrawalsRoot. The withdrawals trie root is not computed yet, so the
    // empty-trie root is used as a placeholder.
    if (version >= static_cast<std::uint32_t>(ApiVersion::V2))
    {
        header.setWithdrawalsRoot(bcos::ledger::mpt::emptyRootHash());
    }

    // CANCUN+ (V3): blob gas fields and parent beacon block root.
    if (version >= static_cast<std::uint32_t>(ApiVersion::V3))
    {
        header.setBlobGasUsed(payload.blobGasUsed.value_or(bcos::u256(0)));
        header.setExcessBlobGas(payload.excessBlobGas.value_or(bcos::u256(0)));
        header.setParentBeaconBlockRoot(parentBeaconBlockRoot.value_or(bcos::h256{}));
    }

    // PRAGUE (V4): EIP-7685 execution-requests hash. FISCO-BCOS produces no execution
    // requests, so the canonical empty-requests hash (sha256 of the empty input,
    // 0xe3b0c442…) is used — the value the RLP hash must carry to validate as PRAGUE.
    if (version >= static_cast<std::uint32_t>(ApiVersion::V4))
    {
        header.setRequestsHash(bcos::crypto::HashType(
            "0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    }

    // Mark the header as an Eth header, then compute and inject its RLP hash.
    header.setEthBlockVersion(ethBlockVersionFor(version));
    if (auto error = bcos::protocol::EthBlockHeader::calculateRLPHash(header))
    {
        BOOST_THROW_EXCEPTION(std::runtime_error{
            "EngineService: failed to compute Eth RLP hash: " + error->errorMessage()});
    }
}
