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
#include "bcos-utilities/DataConvertUtility.h"
#include <limits>

namespace
{
constexpr std::size_t c_hashBytes = 32;
constexpr std::size_t c_payloadIdBytes = 8;

// ---- ETH/OP header protocol constants (op-validator-loop design §5.1) ----
//
// These three header fields have no carrier in `ExecutionPayload` because they are fixed by the
// protocol on post-merge OP chains; the header reconstruction below must still emit them (they
// are real RLP fields, see `bcos-codec/rlp/EthBlockHeader.h`'s own "Real field, not hardcoded"
// notes -- the struct carries them, the *caller* supplies the constant).
//
// Values are byte-identical to the two other places in this repo that pin them:
// `bcos-evm/test/opstack/EthBlockHeaderTest.cpp`'s `kEmptyOmmersHash`/`kPosNonce` (Task 3's
// 33-vector golden gate, so these two are golden-anchored), and
// `bcos-evm/bcos-evm/opstack/OpBlockSeal.h`'s `OP_EMPTY_REQUESTS_HASH` for the third. The
// requests-hash copy here is *checked* rather than merely trusted: the OP branch compares the
// seal's own `requestsHash` against the reconstructed header's, so any drift between this copy
// and OpBlockSeal.h's surfaces as a comparison failure instead of a silent wrong block hash.
// (They are re-declared rather than included because `engine` must not depend on `bcos-evm` --
// see EngineServiceImpl.h's `c_opMode` comment.)
const bcos::h256 c_emptyOmmersHash{
    std::string{"0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"}};
const bcos::h64 c_posNonce{std::string{"0x0000000000000000"}};
const bcos::h256 c_opEmptyRequestsHash{
    std::string{"0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}};
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
    return {"engine_exchangeCapabilities", "engine_forkchoiceUpdatedV1",
        "engine_forkchoiceUpdatedV2", "engine_forkchoiceUpdatedV3", "engine_getPayloadV1",
        "engine_getPayloadV2", "engine_getPayloadV3", "engine_newPayloadV1", "engine_newPayloadV2",
        "engine_newPayloadV3"};
}

std::vector<std::string> bcos::engine::detail::supportedOpCapabilities()
{
    // task-5a (spec §6.3): OP composition root only -- appends the V4 entries to the generic
    // list. `newPayloadV4`/`getPayloadV4`'s semantics are `EngineServiceImpl`-layer only; RPC
    // endpoint registration is out of scope for this whole feature (spec §6.4 欠账台账,裁定
    // A6), so advertising these two capabilities here does not imply an RPC-reachable method.
    auto capabilities = supportedCapabilities();
    capabilities.push_back("engine_newPayloadV4");
    capabilities.push_back("engine_getPayloadV4");
    return capabilities;
}

bool bcos::engine::detail::isGetPayloadVersionCompatible(
    EngineApiVersion requestVersion, std::uint32_t payloadVersion)
{
    if (requestVersion == EngineApiVersion::V1)
    {
        return payloadVersion == 1;
    }
    if (requestVersion == EngineApiVersion::V2)
    {
        return payloadVersion <= 2;
    }
    if (requestVersion == EngineApiVersion::V3)
    {
        return payloadVersion <= 3;
    }
    return false;
}

std::optional<std::string> bcos::engine::detail::validatePayloadAttributes(
    const PayloadAttributes& payloadAttributes, std::uint32_t version)
{
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
    for (auto const& transaction : executionPayload.transactions)
    {
        if (!transaction)
        {
            return std::string("executionPayload.transactions entries must not be null");
        }
    }
    if (version == 1 && executionPayload.withdrawals.has_value())
    {
        return std::string("withdrawals are not part of ExecutionPayloadV1");
    }
    if (version >= 2 && !executionPayload.withdrawals.has_value())
    {
        return std::string("withdrawals are required for ExecutionPayloadV2 and V3");
    }
    if (version <= 2 &&
        (executionPayload.blobGasUsed.has_value() || executionPayload.excessBlobGas.has_value()))
    {
        return std::string("blob gas fields are only valid for ExecutionPayloadV3");
    }
    if (version == 3 &&
        (!executionPayload.blobGasUsed.has_value() || !executionPayload.excessBlobGas.has_value()))
    {
        return std::string("blob gas fields are required for ExecutionPayloadV3");
    }
    return std::nullopt;
}

// ============================ OP-mode helpers (task-5b, design §6.1) ============================
// Reached only from `EngineServiceImpl::handleOpNewPayload`, i.e. only from an instantiation with
// `c_opMode == true`. They are plain non-template functions living in this .cpp (rather than in
// the header) because none of them touch a template parameter: they work purely on
// `NewPayloadRequest`/`ExecutionPayload` plus a couple of caller-supplied derived values.

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
    // `Bloom` is `std::array<byte, 256>` (bcos-utilities/Bloom.h); `EthBlockHeader::logsBloom` is
    // `h2048` -- same 256 bytes, same order, so this is a plain byte copy through
    // `FixedBytes(byte const*, size_t)`.
    // Explicit constructor -- `return {...}` (copy-list-initialization) would not compile.
    return bcos::h2048(logsBloom.data(), logsBloom.size());
}

std::optional<std::string> bcos::engine::detail::validateOpNewPayloadRequest(
    const NewPayloadRequest& request, bool jovianActive)
{
    // Design §6.1 step 2 "静态校验". Every failure here is reported by the caller as
    // INVALID + latestValidHash = null (the "blockHash 失配桶"): all of these checks run before
    // parentKnown, so no ancestor has been established as valid at this point.
    //
    // Malformed-input cases that the spec assigns to JSON-RPC -32602 (missing/ill-typed members)
    // are *not* decidable here: they are decoding failures at the RPC parse layer, and RPC
    // endpoint registration is out of scope for this cycle (spec §6.3/§6.4, 裁定 A6). By the time
    // a `NewPayloadRequest` exists, an absent and an empty array are indistinguishable for the
    // vector-typed members. What this function can and does enforce is the *value* constraint
    // ("present and empty" collapses to "empty").
    const auto& payload = request.executionPayload;

    if (!payload.rawTransactions.has_value())
    {
        // The OP path's only transaction carrier (Types.h). Its absence is not a semantic
        // rejection of a block but a malformed request; with no RPC layer to raise -32602 this
        // cycle, INVALID with a field-naming validationError is the honest local answer.
        return std::string("executionPayload.rawTransactions is required on the OP path");
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
        // OP Isthmus+ payload extension (design §5.2): the MessagePasser storage root cannot be
        // derived from the (always empty) withdrawals list, so the header cannot be reconstructed
        // without it.
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
        // Isthmus: the slot is a genuine blob-gas counter and OP blocks carry no blobs, so it
        // must be 0. From Jovian on the same header slot is repurposed as the DA footprint
        // (design §5.1 / OpBlockSeal.h:31-38) and is validated by seal comparison instead --
        // hence this check is gated on the fork, not unconditional.
        return std::string("blobGasUsed must be zero before Jovian (OP Isthmus)");
    }

    // Range checks for the header fields whose `ExecutionPayload` type is wider (or signed)
    // relative to the ETH header's uint64_t (design §5.1). Doing them here makes
    // `rebuildOpEthHeader` total.
    //
    // `blockNumber` is `bcos::protocol::BlockNumber` (int64_t), not u256 -- the narrowing hazard
    // is the sign, not the width (task-5b review M1): a negative value would wrap to a huge
    // uint64 in the header and, worse, be lexical_cast into a bogus registration key. Same
    // "explicit check before narrowing" discipline as `narrowU256ToU64` below, which exists
    // because of this repo's documented silent-truncation incident.
    if (payload.blockNumber < 0)
    {
        return std::string("blockNumber must not be negative");
    }
    if (!narrowU256ToU64(payload.gasLimit).has_value())
    {
        return std::string("gasLimit exceeds the uint64 range of the ETH header field");
    }
    // gasLimit's *effective* ceiling is int64, not uint64 (final review B2-3): the execution side
    // narrows it once more with a plain `static_cast<int64_t>` when filling
    // `evmone::state::BlockInfo::gas_limit` (OpSchedulerImpl.h's `toBlockInfo`), so anything above
    // 2^63-1 becomes a NEGATIVE block gas pool. Same "unchecked signed narrowing" class as the
    // batch-1 deposit `gas_limit` finding, fixed the same way -- explicitly, at the boundary.
    // op-geth pins the identical bound as `params.MaxGasLimit`
    // (consensus/beacon/consensus.go:262-264). No acceptance surface changes: such a block is
    // rejected either way today (the first deposit cannot be paid out of a negative pool); what
    // changes is that it is rejected for the stated reason instead of by accident.
    if (*narrowU256ToU64(payload.gasLimit) >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
        return std::string("gasLimit exceeds the maximum block gas limit (2^63-1)");
    }
    // extraData: at most 32 bytes (final review B2-4), op-geth
    // beacon/engine/types.go:294-296. This is NOT the OP-shape check parked in §6.4 (Isthmus 9
    // bytes / Jovian 17 bytes, version byte, decodability) -- that one is about the *contents* of
    // a well-sized field, whereas this is the plain ETH-level length bound that applies to every
    // chain, and it is the one that keeps an unbounded caller-supplied blob out of the header RLP.
    constexpr std::size_t c_maxExtraDataSize = 32;
    if (payload.extraData.size() > c_maxExtraDataSize)
    {
        return std::string("extraData exceeds the 32-byte maximum");
    }
    if (!narrowU256ToU64(payload.gasUsed).has_value())
    {
        return std::string("gasUsed exceeds the uint64 range of the ETH header field");
    }
    if (!narrowU256ToU64(*payload.blobGasUsed).has_value())
    {
        return std::string("blobGasUsed exceeds the uint64 range of the ETH header field");
    }

    // NOT checked here -- `executionRequests` (design §6.1 step 2 "executionRequests 在场且空"):
    // `NewPayloadRequest` (bcos-framework/engine/Types.h) has no such member, so a non-empty
    // requests list has no way to reach this layer at all and the constraint is vacuously
    // satisfied. It is not silently dropped, either: the reconstructed header pins `requestsHash`
    // to the OP empty-requests constant, so once an `executionRequests` carrier does exist, a
    // non-empty list will produce a blockHash mismatch -- exactly the bucket the spec assigns it
    // to ("非空 -> INVALID + latestValidHash=null,归 blockHash 失配桶"). Recorded as a deviation
    // in the task-5b report.
    return std::nullopt;
}

bcos::codec::rlp::EthBlockHeader bcos::engine::detail::rebuildOpEthHeader(
    const ExecutionPayload& payload, const h256& transactionsRoot,
    const h256& parentBeaconBlockRoot)
{
    // Design §6.1 step 2 / §5.1: 21 fields, in `EthBlockHeader`'s declared order. 17 come from
    // the payload verbatim (extraData included -- "原样", never re-derived or shape-checked, see
    // §6.4's extraData 欠账), 1 is the caller-derived transactionsRoot (the payload has no such
    // field -- op-geth's `ExecutableDataToBlock` likewise derives it from the transaction list),
    // and 3 are the protocol constants pinned at the top of this file.
    //
    // Precondition: `validateOpNewPayloadRequest` returned nullopt for this request -- that is
    // what guarantees the optionals below are engaged and the u256 -> uint64 narrowings are in
    // range. A violated precondition surfaces as a thrown `std::bad_optional_access`, i.e. loudly,
    // rather than as a quietly wrong block hash.
    return bcos::codec::rlp::EthBlockHeader{
        .parentHash = payload.parentHash,
        .ommersHash = c_emptyOmmersHash,
        .feeRecipient = payload.feeRecipient,
        .stateRoot = payload.stateRoot,
        .transactionsRoot = transactionsRoot,
        .receiptsRoot = payload.receiptsRoot,
        .logsBloom = toEthLogsBloom(payload.logsBloom),
        .difficulty = 0,
        .number = static_cast<std::uint64_t>(payload.blockNumber),
        .gasLimit = narrowU256ToU64(payload.gasLimit).value(),
        .gasUsed = narrowU256ToU64(payload.gasUsed).value(),
        .timestamp = payload.timestamp,
        .extraData = payload.extraData,
        .prevRandao = payload.prevRandao,
        .nonce = c_posNonce,
        .baseFeePerGas = payload.baseFeePerGas,
        .withdrawalsRoot = payload.withdrawalsRoot.value(),
        .blobGasUsed = narrowU256ToU64(payload.blobGasUsed.value()).value(),
        // excessBlobGas is pinned to 0 by validation above; narrowing it would be a no-op.
        .excessBlobGas = 0,
        .parentBeaconBlockRoot = parentBeaconBlockRoot,
        .requestsHash = c_opEmptyRequestsHash,
    };
}
