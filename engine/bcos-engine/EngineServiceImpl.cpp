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
#include <limits>

// OP receipt/tx queryability (option B) write-side conversion helper: raw EIP-2718 envelope ->
// tars Transaction. Web3Transaction.h already pulls in RLPDecode/TransactionImpl; RLPDecode is
// kept explicitly (rlp::decode is actually used, include self-sufficiency). Keccak256.h and
// TransactionImpl.h are not used directly in this file, so they were removed.
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"
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
        return std::nullopt;  // only malformed/un-enumerated envelopes -- no throw; 0x04 is already
                              // supported by Web3Transaction
    }
    auto tarsTx = web3Tx.takeToTarsTransaction();
    // The read side's tx.hash() returns extraTransactionHash; leaving it empty would throw
    // EmptyTransactionHash.
    tarsTx.extraTransactionHash.assign(txHash.begin(), txHash.end());
    // For non-deposits, fill sender (takeToTarsTransaction leaves it empty; the read side's from
    // reads tx.sender()). Note: web3Tx.sender() returns a "0x"-prefixed hex string
    // (Web3Transaction.cpp:207-223), while tarsTx.sender must be raw 20 bytes (the read side's
    // toHex(tx.sender()) expects raw bytes) -- must restore via fromHex, otherwise it
    // double-encodes into 84 garbage characters. The deposit branch already fills raw bytes
    // (:121); the sender.empty() guard skips it.
    if (tarsTx.sender.empty())
    {
        try
        {
            auto sender = bcos::fromHex(web3Tx.sender());  // DataConvertUtility.h:119-166, 0x-aware
            tarsTx.sender.assign(sender.begin(), sender.end());
        }
        catch (std::exception const&)
        {
            // web3Tx.sender() → Secp256k1Crypto::recoverAddress throws InvalidSignature on a bad
            // signature. Without this, the exception escapes to the RPC layer and is misclassified
            // as -32603; the caller's nullopt fallback (EngineServiceImpl.h:1184-1197) instead
            // carries the raw envelope to decodeOneRawTx, which issues the INVALID verdict.
            return std::nullopt;
        }
    }
    return tarsTx;
}
}  // namespace bcos::engine::detail

namespace
{
constexpr std::size_t c_hashBytes = 32;
constexpr std::size_t c_payloadIdBytes = 8;

// ---- ETH/OP header protocol constants ----
//
// These three header fields have no carrier in `ExecutionPayload` because they are fixed by the
// protocol on post-merge OP chains; the header reconstruction below must still emit them (they
// are real RLP fields — the header carries them via uncleHash()/difficulty()/nonce(),
// populated by applyOpHeaderConstants).
//
// Values are byte-identical to the two other places in this repo that pin them:
// `bcos-evm/test/opstack/EthBlockHeaderTest.cpp`'s `kEmptyOmmersHash`/`kPosNonce` (golden-
// anchored by the 33-vector gate), and `opstack-executor/OpBlockExecute.h`'s
// `OP_EMPTY_REQUESTS_HASH` for the third. The requests-hash copy here is *checked* rather than
// merely trusted: the OP branch compares the seal's own `requestsHash` against the reconstructed
// header's, so any drift between this copy and OpBlockExecute.h's surfaces as a comparison failure
// instead of a silent wrong block hash. (They are re-declared rather than included because
// `engine` must not depend on `bcos-evm` -- see EngineServiceImpl.h's `c_opMode` comment.)
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
    // Production interop downgrade: do not advertise `engine_newPayloadV4` /
    // `engine_getPayloadV4` in the OP-mode capability negotiation. Both only have
    // EngineServiceImpl-layer semantics; the RPC endpoint registration is not implemented, so
    // advertising V4 would let op-node negotiate to a non-existent endpoint and hit a -38005 stub
    // on every call. Honestly expose V3; restore the V4 entries here once the endpoints exist.
    return supportedCapabilities();
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
        // Tier-2: V4-built payloads (the OP composition's attribute-driven builds) are
        // served by getPayloadV4 only, mirroring the V3 rule's shape.
        return payloadVersion <= 4;
    }
    return false;
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

// ============================ OP-mode helpers ============================
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
    // `Bloom` is `std::array<byte, 256>` (bcos-utilities/Bloom.h);
    // `protocol::BlockHeader::logsBloom` is `h2048` -- same 256 bytes, same order, so this is a
    // plain byte copy through `FixedBytes(byte const*, size_t)`. Explicit constructor -- `return
    // {...}` (copy-list-initialization) would not compile.
    return bcos::h2048(logsBloom.data(), logsBloom.size());
}

bcos::u256 bcos::engine::detail::calcOpBaseFee(
    const bcos::protocol::BlockHeader& parent, bool parentIsJovian)
{
    // The minimal loop is Isthmus+-only (feature-driven fork selection), so the Holocene+
    // extraData decode below always applies — there is no pre-Isthmus base to fall back to;
    // `parentIsJovian` only gates the Jovian 8-byte minBaseFee tail.
    uint64_t elasticity = 2;
    uint64_t denominator = 8;
    std::optional<uint64_t> minBaseFee;

    // Holocene+ extraData: version byte + denominator (uint32 BE) + elasticity (uint32 BE).
    // Zero-value rejection is guaranteed by the parent's own validateOpNewPayloadRequest.
    const auto extra = parent.extraData();  // bytesConstRef; operator[] like bytes
    auto readU32BE = [&extra](std::size_t off) -> uint64_t {
        return (static_cast<uint64_t>(extra[off]) << 24) |
               (static_cast<uint64_t>(extra[off + 1]) << 16) |
               (static_cast<uint64_t>(extra[off + 2]) << 8) | static_cast<uint64_t>(extra[off + 3]);
    };
    denominator = readU32BE(1);
    elasticity = readU32BE(5);

    if (parentIsJovian)
    {
        // Jovian extraData extends Holocene with 8-byte minBaseFee (uint64 BE).
        auto readU64BE = [&extra](std::size_t off) -> uint64_t {
            return (static_cast<uint64_t>(extra[off]) << 56) |
                   (static_cast<uint64_t>(extra[off + 1]) << 48) |
                   (static_cast<uint64_t>(extra[off + 2]) << 40) |
                   (static_cast<uint64_t>(extra[off + 3]) << 32) |
                   (static_cast<uint64_t>(extra[off + 4]) << 24) |
                   (static_cast<uint64_t>(extra[off + 5]) << 16) |
                   (static_cast<uint64_t>(extra[off + 6]) << 8) |
                   static_cast<uint64_t>(extra[off + 7]);
        };
        minBaseFee = readU64BE(9);
    }

    const uint64_t parentGasTarget =
        detail::narrowU256ToU64(parent.gasLimit()).value() / elasticity;

    // Jovian: baseFee is based on max(total gas used, DA footprint) rather than plain gasUsed
    // (op-geth consensus/misc/eip1559/eip1559.go:99-107).
    uint64_t parentGasMetered = detail::narrowU256ToU64(parent.gasUsed()).value();
    if (parentIsJovian &&
        detail::narrowU256ToU64(parent.blobGasUsed().value()).value() > parentGasMetered)
    {
        parentGasMetered = detail::narrowU256ToU64(parent.blobGasUsed().value()).value();
    }

    // EIP-1559 base fee computation (mirrors op-geth eip1559.go:calcBaseFeeInner).
    if (parentGasMetered == parentGasTarget)
    {
        return parent.baseFee().value();
    }

    const bcos::u256 parentBaseFee = parent.baseFee().value();
    bcos::u256 result;

    if (parentGasMetered > parentGasTarget)
    {
        // baseFee increases: max(1, parentBaseFee * delta / parentGasTarget / denominator)
        const uint64_t delta = parentGasMetered - parentGasTarget;
        bcos::u256 deltaFee = parentBaseFee * delta;
        deltaFee /= parentGasTarget;
        deltaFee /= denominator;
        result = parentBaseFee + (deltaFee > 0 ? deltaFee : bcos::u256{1});
    }
    else
    {
        // baseFee decreases: parentBaseFee * delta / parentGasTarget / denominator
        const uint64_t delta = parentGasTarget - parentGasMetered;
        bcos::u256 deltaFee = parentBaseFee * delta;
        deltaFee /= parentGasTarget;
        deltaFee /= denominator;
        result = deltaFee < parentBaseFee ? parentBaseFee - deltaFee : bcos::u256{0};
    }

    // Jovian minBaseFee floor (op-geth eip1559.go:86-91).
    if (minBaseFee.has_value() && result < *minBaseFee)
    {
        result = bcos::u256{*minBaseFee};
    }

    return result;
}

std::optional<std::string> bcos::engine::detail::validateOpNewPayloadRequest(
    const NewPayloadRequest& request, bool jovianActive)
{
    // Static validation. Every failure here is reported by the caller as INVALID +
    // latestValidHash = null (the blockHash-mismatch bucket): all of these checks run before
    // parentKnown, so no ancestor has been established as valid at this point.
    //
    // Malformed-input cases assigned to JSON-RPC -32602 (missing/ill-typed members) are *not*
    // decidable here: they are decoding failures at the RPC parse layer, and RPC endpoint
    // registration is out of scope for this cycle. By the time a `NewPayloadRequest` exists, an
    // absent and an empty array are indistinguishable for the vector-typed members. What this
    // function can and does enforce is the *value* constraint ("present and empty" collapses to
    // "empty").
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
        // OP Isthmus+ payload extension: the MessagePasser storage root cannot be derived from
        // the (always empty) withdrawals list, so the header cannot be reconstructed without it.
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
        // (OpBlockExecute.h's OpBlockSeal) and is validated by seal comparison instead -- hence
        // this check
        // is gated on the fork, not unconditional.
        return std::string("blobGasUsed must be zero before Jovian (OP Isthmus)");
    }

    // Range checks for the header fields whose `ExecutionPayload` type is wider (or signed)
    // relative to the ETH header's uint64_t. Doing them here makes `rebuildOpEthHeader` total.
    //
    // `blockNumber` is `bcos::protocol::BlockNumber` (int64_t), not u256 -- the narrowing hazard
    // is the sign, not the width: a negative value would wrap to a huge uint64 in the header and,
    // worse, be lexical_cast into a bogus registration key. Same "explicit check before
    // narrowing" discipline as `narrowU256ToU64` below, which exists because of this repo's
    // documented silent-truncation incident.
    if (payload.blockNumber < 0)
    {
        return std::string("blockNumber must not be negative");
    }
    if (!narrowU256ToU64(payload.gasLimit).has_value())
    {
        return std::string("gasLimit exceeds the uint64 range of the ETH header field");
    }
    // gasLimit's *effective* ceiling is int64, not uint64: the execution side narrows it once
    // more with a plain `static_cast<int64_t>` when filling `evmone::state::BlockInfo::gas_limit`
    // (OpSchedulerSeam.h's `toBlockInfo`), so anything above 2^63-1 becomes a NEGATIVE block gas
    // pool. Same "unchecked signed narrowing" class as the deposit `gas_limit` finding, fixed the
    // same way -- explicitly, at the boundary. op-geth pins the identical bound as
    // `params.MaxGasLimit` (consensus/beacon/consensus.go:262-264). No acceptance surface
    // changes: such a block is rejected either way today (the first deposit cannot be paid out of
    // a negative pool); what changes is that it is rejected for the stated reason instead of by
    // accident.
    if (*narrowU256ToU64(payload.gasLimit) >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
        return std::string("gasLimit exceeds the maximum block gas limit (2^63-1)");
    }
    // `gasUsed <= gasLimit` has NO pre-execution header check here on purpose, unlike op-geth's
    // beacon `verifyHeader` (`consensus/beacon/consensus.go:266-268`, reached before any state is
    // touched). It is guaranteed by the execution side instead, in two layers that together match
    // op-geth's pre-execution rejection:
    //   1. by construction — `processOpBlock` (`OpBlockExecute.cpp:126`) starts the gas pool at the
    //      block gas limit and each tx is gated by `tx.gasLimit <= blockGasLeft` (evmone
    //      validate/transition) with `gasUsed <= tx.gasLimit`, so the computed `cumulative`
    //      (= OpExecuteBlockResult::gasUsed) can never exceed the block gas limit;
    //   2. by commitment comparison — `payload.gasUsed` is one of the six-way comparison fields
    //      (bcos-evm/bcos-evm/engine/OpSchedulerSeam.h:94-103, struct OpExecuteBlockResult),
    //      pinned to the computed value, so a payload that CLAIMS `gasUsed > gasLimit` fails the
    //      comparison after execution -> INVALID.
    // Behaviorally equivalent to op-geth, with a different timing: FISCO rejects only after
    // executing the block, op-geth rejects before execution (the same "no full
    // VerifyHeader/ValidateBody equivalent" structural note as in
    // docs/opstack-opgeth-e2e-comparison.md). Not a gap, but deliberately not mirrored here.
    // extraData: OP Holocene+ header shape. op-geth validates it in
    // `consensus/misc/eip1559/eip1559_optimism.go`'s `ValidateHoloceneExtraData` (Isthmus) /
    // `ValidateJovianExtraData` (Jovian), reached from BOTH the block-verify path
    // (`consensus/beacon/consensus.go:240`) and the newPayload path
    // (`eth/catalyst/api_optimism.go:22`):
    //   - Isthmus: exactly 9 bytes = 0x00 version ‖ uint32 denominator ‖ uint32 elasticity (big
    //     endian), with denominator and elasticity both non-zero;
    //   - Jovian: exactly 17 bytes = 0x01 version ‖ the same 8 eip-1559 bytes ‖ uint64 minBaseFee
    //     (minBaseFee arbitrary, not validated).
    // The OP path is always Isthmus+ (`withdrawalsRoot` is required above), so an empty extraData
    // is never valid here. This shape check subsumes the old 32-byte ETH length bound (9 and 17
    // are both < 32): a caller-supplied blob longer than 32 bytes now fails the length branch
    // below with a shape message instead of the generic bound.
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
        // denominator = extra[1:5], elasticity = extra[5:9], both big-endian uint32 and both
        // required non-zero (op-geth `validateHoloceneExtraDataPart`). The offsets are identical
        // for Isthmus and Jovian; only the total length and version byte differ.
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

    // Jovian DA-footprint block limit. From Jovian on, the header `blobGasUsed` slot carries the
    // block's DA footprint (OpBlockExecute.h's OpBlockSeal); a block whose DA footprint exceeds its
    // own
    // gasLimit is rejected. op-geth checks the recomputed footprint against `block.GasLimit()` in
    // `core/block_validator.go:131` (Jovian branch, DA footprint from
    // `core/types/rollup_cost.go`'s `CalcDAFootprint`). Checking the payload's claimed
    // `blobGasUsed` here is equivalent given the step-5 seal comparison already pins `blobGasUsed`
    // to the computed footprint; it only ever widens rejection (single direction), never accepts a
    // block op-geth would reject. Isthmus keeps `blobGasUsed == 0` (checked above), so this is
    // Jovian-gated. Both operands are u256, compared directly.
    if (jovianActive && *payload.blobGasUsed > payload.gasLimit)
    {
        return std::string("DA footprint (blobGasUsed) exceeds the block gas limit");
    }

    // The OP path carries no execution requests (there is no engine API to set them -- the RPC
    // layer always sends nullopt, EngineHelper.cpp:100-105), so a present-and-NON-empty list
    // contradicts the protocol's request shape and is rejected here, in the same bucket as every
    // other static check above (INVALID + latestValidHash=null, before parentKnown). This is the
    // explicit check the sentinel static_assert (EngineNewPayloadGateTest.cpp, mutation class #7's
    // compile-time half) forced once `NewPayloadRequest::executionRequests` existed; the mutation
    // test now sets the real carrier instead of the requestsHash surrogate. Rejecting rather than
    // hashing keeps the reconstructed header's `requestsHash` pin to the OP empty-requests
    // constant provably consistent: a non-empty list never reaches the reconstruction.
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
    // 21 fields, all of which land in the FISCO BlockHeader (tars, PR #5385); the 3 post-merge
    // constants (ommersHash/difficulty/nonce) via applyOpHeaderConstants (read back by
    // encodeOpHeader/opHeaderHash). Field sources: 17 verbatim from the payload (extraData
    // kept "as-is", never re-derived), 1 caller-derived transactionsRoot (the payload has no such
    // field), constants at the top of this file. timestamp is stored in milliseconds per FISCO
    // convention (blockHash/RLP/execution always use seconds; tars storage uses milliseconds).
    //
    // Precondition: `validateOpNewPayloadRequest` returned nullopt for this request -- that is
    // what guarantees the optionals below are engaged. A violated precondition surfaces as a
    // thrown `std::bad_optional_access`, i.e. loudly, rather than as a quietly wrong block hash.
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
    // excessBlobGas is pinned to 0 by validation above (consistent with the retired
    // EthBlockHeader).
    header->setExcessBlobGas(bcos::u256(0));
    header->setParentBeaconBlockRoot(parentBeaconBlockRoot);
    header->setRequestsHash(c_opEmptyRequestsHash);
    // The 3 post-merge constants (uncleHash/difficulty/nonce) — read back by
    // encodeOpHeader/opHeaderHash.
    applyOpHeaderConstants(*header);
    return header;
}
