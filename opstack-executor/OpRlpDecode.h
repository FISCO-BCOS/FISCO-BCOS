// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Decode primitives for the OP raw-tx envelope decoder (split out of OpSchedulerImpl.h).
// bcos<->evmc conversions / bounds-checked narrowing / RLP scalar primitives /
// composite decoders. These depend only on bcos-codec RLP + evmone/evmc/intx +
// protocol::BlockHeader — deliberately NOT the evmone package's test headers (those live in
// OpTxDecode.h).

#include <opstack-executor/OpErrors.h>

#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-evm/eth/state/block.hpp>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-evm/eth/state/transaction.hpp>
#include <cstdint>
#include <cstring>
#include <evmc/evmc.hpp>
#include <evmone_precompiles/secp256k1.hpp>
#include <intx/intx.hpp>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bcos::evm::engine::detail
{

// ---- bcos:: <-> evmc:: fixed-size conversions ----
// Precedent: bcos-evm/bcos-evm/evmstate/Storage2State.h's applyModifiedEntry (codeHash: evmc
// bytes32.bytes -> bcos::h256 via the FixedBytes(byte const*, size_t) constructor) and
// addressFromTableName (raw memcpy into evmc::address.bytes).

inline evmc::address toEvmcAddress(const bcos::Address& a) noexcept
{
    evmc::address out{};
    std::memcpy(out.bytes, a.data(), sizeof(out.bytes));
    return out;
}

inline evmc::bytes32 toEvmcBytes32(const bcos::h256& h) noexcept
{
    evmc::bytes32 out{};
    std::memcpy(out.bytes, h.data(), sizeof(out.bytes));
    return out;
}

// `toBcosH256` (evmc::bytes32 -> bcos::h256) lives in OpErrors.h: that header needs the same
// conversion, and two identical inline definitions of one name in `bcos::evm::engine::detail`
// would be a redefinition error.

/// bcos::u256 -> uint64_t, explicit bounds-checked narrowing — NOT a raw static_cast/convert_to.
/// This repo has a documented silent-truncation incident with unchecked wide-integer narrowing
/// (MEMORY costofprecompiled-int64-overflow); Storage2State.h's fetchAccount nonce handling
/// established the "widen -> explicit > max check -> narrow" discipline this mirrors.
inline uint64_t narrowU256ToU64(const bcos::u256& v, const char* fieldName)
{
    static const bcos::u256 kMaxU64(std::numeric_limits<uint64_t>::max());
    if (v > kMaxU64)
        throw OpConsensusError(
            std::string("OpSchedulerImpl: field exceeds uint64_t range: ") + fieldName);
    return static_cast<uint64_t>(v);
}

/// uint64_t -> int64_t, explicit bounds-checked narrowing — same "widen -> explicit check ->
/// narrow" discipline as `narrowU256ToU64` above, applied to the wire-decoded gas_limit scalar.
/// DepositTx::gas_limit / evmone::state::Transaction::gas_limit are both `int64_t`; a canonical
/// 8-byte RLP scalar (e.g. `0xFFFFFFFFFFFFFFFF`) decodes to a `uint64_t` that silently becomes
/// *negative* under a raw `static_cast<int64_t>`. On the deposit path that negative value would
/// survive into `blockGasLeft -= gas_used` and *raise* the remaining block gas pool by ~2^63,
/// letting later txs exceed the real gasLimit while gasUsed wraps to a plausible uint64_t — a
/// block op-geth rejects outright (`GasPool.SubGas` -> `ErrGasLimitReached`). `fieldName`
/// distinguishes the per-call-site message (deposit/eip1559/setcode).
inline int64_t narrowGasLimit(uint64_t v, const char* fieldName)
{
    if (v > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        throw OpConsensusError(
            std::string("OpSchedulerImpl: raw tx decode: gas limit exceeds int64_t range: ") +
            fieldName);
    return static_cast<int64_t>(v);
}

/// EIP-2 signature-malleability guard (r,s in [1, n-1], s <= n/2). `evmmax::secp256k1::ecrecover`
/// only requires `0 < r,s < n`; real Ethereum signers additionally reject high-s
/// (op-geth `crypto.ValidateSignatureValues`, homestead=true). Without this check an attacker
/// can rewrite a legitimately-signed `(r,s,yParity)` to `(r, n-s, 1-yParity)`: sender recovery
/// and execution outcome are unchanged but the raw bytes (hence txRoot/blockHash) differ — this
/// scheduler would accept the rewritten block as VALID where op-geth rejects it. Applied to the
/// *outer* transaction's own signature (same four-way bound and curve constant as the EIP-7702
/// authorization-tuple checks in state.cpp / OpTransition.cpp).
inline void requireLowSSignature(const intx::uint256& r, const intx::uint256& s)
{
    constexpr auto kSecpOrder = evmmax::secp256k1::Curve::ORDER;
    constexpr auto kSecpHalfOrder = kSecpOrder / 2;
    if (r == 0 || r >= kSecpOrder || s == 0 || s >= kSecpOrder || s > kSecpHalfOrder)
        throw OpConsensusError(
            "OpSchedulerImpl: raw tx decode: invalid signature (r/s out of [1,n-1], or s exceeds "
            "secp256k1n/2 — EIP-2 malleable signature)");
}

/// Build the OP block context from a FISCO header — the single implementation, merged from the
/// former `OpstackExecutor::buildOpBlockInfo`. Two deliberate semantic knobs
/// encode the two call contracts:
/// - `gasLimitOverride`: block-execution reads the header's own gasLimit (default); the per-tx /
///   eth_call path injects the head block's gasLimit as blockGasLeft (a minimal test header may
///   leave gasLimit==0, so the caller resolves it via `opBlockGasLimit` first).
/// - `lenientOptionals`: block-execution uses `.value()` — an unset optional is an engine-fill bug
///   and throws (→ OpStorageError); the eth_call / minimal-test-header path tolerates unset
///   optional header fields as 0.
inline evmone::state::BlockInfo toBlockInfo(const bcos::protocol::BlockHeader& env,
    std::optional<uint64_t> gasLimitOverride = std::nullopt, bool lenientOptionals = false)
{
    evmone::state::BlockInfo blk;
    blk.number = static_cast<int64_t>(env.number());
    // FISCO tars store milliseconds; evmone wants seconds (blockHash/execution surface is always
    // in seconds).
    blk.timestamp = static_cast<uint64_t>(env.timestamp()) / 1000;
    blk.gas_limit = gasLimitOverride.has_value() ?
                        static_cast<int64_t>(*gasLimitOverride) :
                        narrowU256ToU64(env.gasLimit(), "BlockInfo::gasLimit");
    blk.base_fee = narrowU256ToU64(
        lenientOptionals ? env.baseFee().value_or(bcos::u256{0}) : env.baseFee().value(),
        "BlockInfo::baseFee");
    blk.coinbase = toEvmcAddress(env.coinbase());
    blk.prev_randao = toEvmcBytes32(env.prevRandao());
    blk.parent_beacon_block_root =
        toEvmcBytes32(lenientOptionals ? env.parentBeaconBlockRoot().value_or(bcos::h256{}) :
                                         env.parentBeaconBlockRoot().value());
    blk.extra_data = evmc::bytes(env.extraData().begin(), env.extraData().end());
    blk.blob_gas_used = narrowU256ToU64(
        lenientOptionals ? env.blobGasUsed().value_or(bcos::u256{0}) : env.blobGasUsed().value(),
        "BlockInfo::blobGasUsed");
    return blk;
}

// ---- raw tx envelope decode (sorting step: deposit 0x7E / access_list 0x01 / eip1559 0x02 /
// setcode 0x04 / legacy (>= 0xc0); blob (0x03) is deliberately unsupported — L2 blocks carry no
// blob txs). ----
//
// Decode primitives reused from bcos-codec/rlp/RLPDecode.h — the same already-compiled,
// production decode path bcos-rpc/web3jsonrpc/model/Web3Transaction.cpp's decodeTransaction()
// uses for the standard (non-OP) eth_sendRawTransaction path (EIP-1559 in particular: the `to`
// nilability handling, decodeHeader/decodeItems call shapes, and the `decode(in, Address&)`
// call are copied verbatim from that precedent). RLPDecode.h is entirely `inline` — including it
// here pulls in only a header include-path dependency (already wired via the `codec` link), not a
// new link edge for the bcos-evm-opstack static library.
//
// Typed-tx signing-preimage field order and the access_list/authorization_list RLP shapes are
// taken from the encode-direction sibling bcos-evm/bcos-evm/eth/utils/rlp_encode.cpp's
// rlp_encode(const Transaction&) (lines 38-71) and rlp_encode(const Authorization&) (lines
// 96-100) — reusing evmone::rlp::encode_tuple/encode(vector<T>) exactly as that function does
// (address/bytes32's implicit `operator bytes_view()` and Authorization's ADL-found
// evmone::state::rlp_encode overload are the same mechanisms that function already relies on).

inline void throwOnDecodeError(bcos::Error::UniquePtr&& err)
{
    if (err != nullptr)
        // Surface errorMessage(), not what(): bcos::Error inherits what() from boost::exception,
        // which returns an empty/opaque string here, whereas errorMessage() carries the RLP
        // decoder's own diagnostic ("Non-canonical length prefix: leading zero byte", etc.). This
        // makes an OP raw-tx decode failure attributable to the specific canonical-encoding rule
        // that fired rather than a bare "decode failed".
        throw OpConsensusError(
            std::string("OpSchedulerImpl: raw tx decode failed: ") + err->errorMessage());
}

// ---- canonical-encoding strictness ----
//
// Go's `rlp` (which produced every byte this decoder will ever legitimately see) rejects
// non-canonical encodings outright; bcos-codec/rlp/RLPDecode.h is permissive by comparison
// (fromBigEndian keeps the low 8 bytes of an over-long uint64, FixedBytes right-pads/truncates),
// so strictness is layered:
//   (1) per-field, OP-local in the primitives below — readCanonicalScalar/readFixedWidth/
//       decodeBoolField reject leading-zero scalars, over-wide integers, wrong-width address/hash
//       fields, and non-0x01 bools;
//   (2) length prefix, in the SHARED decoder's decodeHeader — a non-minimal leading-zero length
//       prefix is rejected for all consumers;
//   (3) whole-envelope round-trip — `assertCanonicalRoundTrip` decodes, re-encodes canonically,
//       and requires byte-identity with the raw envelope, a defense-in-depth backstop that fails
//       closed on any residual non-canonicality (1)/(2) miss.
//
// The stakes: a deposit envelope carries no signature, so nothing else cross-checks its fields —
// a 19-byte `from`/`to` or a 33-byte `sourceHash` would otherwise be silently padded/truncated
// into a DIFFERENT account. And `computeOpTxRoot` hashes the raw wire bytes while op-geth's
// `DeriveSha` re-encodes each tx canonically; for non-canonical input the two would diverge, so
// this strictness also keeps the block hash in agreement. With all three layers, non-canonical
// input does not survive decoding.

/// Reads one RLP string payload, requiring it to be a canonical unsigned scalar: no leading zero
/// byte, no wider than `maxBytes`. Returns a view of the payload and advances `in` past it.
inline bcos::bytesRef readCanonicalScalar(
    bcos::bytesRef& in, std::size_t maxBytes, const char* fieldKind)
{
    auto&& [err, header] = bcos::codec::rlp::decodeHeader(in);
    throwOnDecodeError(std::move(err));
    if (header.isList)
        throw OpConsensusError(std::string("OpSchedulerImpl: raw tx decode: expected scalar (") +
                               fieldKind + "), got list");
    if (header.payloadLength > maxBytes)
        throw OpConsensusError(
            std::string("OpSchedulerImpl: raw tx decode: scalar too wide for ") + fieldKind);
    if (header.payloadLength > 0 && in[0] == 0)
        throw OpConsensusError(
            std::string("OpSchedulerImpl: raw tx decode: non-canonical leading zero in ") +
            fieldKind);
    auto payload = in.getCroppedData(0, header.payloadLength);
    in = in.getCroppedData(header.payloadLength);
    return payload;
}

/// Reads one RLP string payload, requiring EXACTLY `size` bytes (op-geth's
/// "input string too short/long for common.Address|common.Hash").
inline bcos::bytesRef readFixedWidth(bcos::bytesRef& in, std::size_t size, const char* fieldKind)
{
    auto&& [err, header] = bcos::codec::rlp::decodeHeader(in);
    throwOnDecodeError(std::move(err));
    if (header.isList)
        throw OpConsensusError(std::string("OpSchedulerImpl: raw tx decode: expected string (") +
                               fieldKind + "), got list");
    if (header.payloadLength != size)
        throw OpConsensusError(
            std::string("OpSchedulerImpl: raw tx decode: wrong length for ") + fieldKind);
    auto payload = in.getCroppedData(0, size);
    in = in.getCroppedData(size);
    return payload;
}

inline intx::uint256 decodeU256Scalar(bcos::bytesRef& in)
{
    auto payload = readCanonicalScalar(in, 32, "uint256");
    evmc::bytes32 buf{};
    if (!payload.empty())
        std::memcpy(buf.bytes + (32 - payload.size()), payload.data(), payload.size());
    return intx::be::load<intx::uint256>(buf);
}

inline uint64_t decodeU64Scalar(bcos::bytesRef& in)
{
    auto payload = readCanonicalScalar(in, sizeof(uint64_t), "uint64");
    uint64_t value = 0;
    for (auto byteValue : payload)
        value = (value << 8U) | byteValue;
    return value;
}

/// EIP-7702 authorization-tuple yParity. op-geth's
/// `SetCodeAuthorization.V` is a **uint8** (core/types/tx_setcode.go:76), so its RLP scalar must
/// fit in a single byte: a wider encoding (e.g. `0x82 0x01 0x00` == 256) overflows that uint8 and
/// makes op-geth's RLP DecodeRLP fail, which invalidates the WHOLE transaction and therefore the
/// block. Reading this field as a full uint256 (decodeU256Scalar) instead silently accepts 256,
/// after which OpTransition.cpp:67's `auth.v > 1` guard merely *skips* that one authorization and
/// leaves the block VALID — a consensus split from op-geth (measured: `auth.v = 256` was accepted).
///
/// This is strictly the encoding-WIDTH check. The value-RANGE handling (yParity in [2,255]) is a
/// DIFFERENT concern and is deliberately NOT rejected here: EIP-7702 requires such an authorization
/// to be *skipped*, not fatal — which OpTransition.cpp:67 already does, matching op-geth's
/// `applyAuthorization` `continue`. So a 1-byte value like 0x02 decodes fine here and is skipped at
/// execution; only a >1-byte encoding is a decode-time rejection.
inline intx::uint256 decodeAuthYParityScalar(bcos::bytesRef& in)
{
    auto payload = readCanonicalScalar(in, 1, "authorization yParity");
    intx::uint256 value = 0;
    if (!payload.empty())
        value = payload[0];
    return value;
}

/// isSystemTransaction (op-geth's DepositTx.IsSystemTransaction, encoded as a plain RLP scalar
/// 0/1 — Go's rlp package's *native* bool encoding, which false=empty-string(payloadLength=0)/
/// true=0x01, matching the encode side: bcos-rpc's DepositTxHandler::encode encodes
/// this field via the generic UnsignedByte scalar path (`isSystemTransactionByte ? 1 : 0`), not
/// `bcos::codec::rlp::encode(bytes&, bool&)` — no such overload exists in RLPEncode.h (`bool`
/// does not satisfy the `UnsignedByte`/`UnsignedIntegral` concepts). Correspondingly,
/// `bcos::codec::rlp::decode(bytesRef&, bool&)` (RLPDecode.h) — which rejects payloadLength != 1
/// and therefore rejects the empty-string encoding of `false` — is the WRONG decode overload for
/// this field; decoding it as a generic scalar (decodeU64Scalar, which does treat
/// payloadLength==0 as value 0) is the byte-correct match for what op-geth actually emits.
///
/// Strictness: Go's `rlp` accepts exactly two encodings for a bool -- the empty string
/// (false) and single-byte `0x01` (true) -- and answers "rlp: invalid boolean value" to anything
/// else, including the single byte `0x00`. Accepting "any non-zero scalar" (or a canonical `0x00`,
/// which does not exist in Go's output) would let two different encodings mean the same
/// transaction, i.e. two block hashes for one block.
inline bool decodeBoolField(bcos::bytesRef& in)
{
    auto payload = readCanonicalScalar(in, 1, "bool");
    if (payload.empty())
        return false;
    if (payload[0] != 1)
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: invalid boolean value");
    return true;
}

inline evmc::address decodeAddressField(bcos::bytesRef& in)
{
    // Exactly 20 bytes. The previous `decode(in, bcos::Address&)` route accepted any
    // length and right-padded/truncated -- on a deposit envelope (no signature to cross-check it)
    // that silently substitutes a DIFFERENT address.
    auto payload = readFixedWidth(in, sizeof(evmc::address::bytes), "address");
    evmc::address out{};
    std::memcpy(out.bytes, payload.data(), sizeof(out.bytes));
    return out;
}

inline evmc::bytes32 decodeHashField(bcos::bytesRef& in)
{
    auto payload = readFixedWidth(in, sizeof(evmc::bytes32::bytes), "hash");
    evmc::bytes32 out{};
    std::memcpy(out.bytes, payload.data(), sizeof(out.bytes));
    return out;
}

inline evmc::bytes decodeBytesField(bcos::bytesRef& in)
{
    bcos::bytes b;
    throwOnDecodeError(bcos::codec::rlp::decode(in, b));
    return evmc::bytes(b.begin(), b.end());
}

/// `to` nilability (Web3Transaction.cpp:446-459/504-517 precedent): an empty RLP string (a lone
/// 0x80 byte) means contract-creation; anything else decodes as a 20-byte address.
inline std::optional<evmc::address> decodeOptionalAddressField(bcos::bytesRef& in)
{
    if (in.empty())
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: input too short for 'to'");
    if (in[0] == bcos::codec::rlp::BYTES_HEAD_BASE)
    {
        in = in.getCroppedData(1);
        return std::nullopt;
    }
    return decodeAddressField(in);
}

/// Enters a nested RLP list, returning a view scoped to its payload and advancing `in` past the
/// whole list (header + payload).
inline bcos::bytesRef enterList(bcos::bytesRef& in)
{
    auto&& [err, header] = bcos::codec::rlp::decodeHeader(in);
    throwOnDecodeError(std::move(err));
    if (!header.isList)
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: expected list");
    auto payload = in.getCroppedData(0, header.payloadLength);
    in = in.getCroppedData(header.payloadLength);
    return payload;
}

inline void expectExhausted(const bcos::bytesRef& body, const char* what)
{
    if (!body.empty())
        throw OpConsensusError(
            std::string("OpSchedulerImpl: raw tx decode: unexpected trailing bytes in ") + what);
}

inline evmone::state::AccessList decodeAccessList(bcos::bytesRef& in)
{
    evmone::state::AccessList out;
    auto listBody = enterList(in);
    while (!listBody.empty())
    {
        auto entryBody = enterList(listBody);
        auto address = decodeAddressField(entryBody);
        std::vector<evmc::bytes32> keys;
        auto keysBody = enterList(entryBody);
        while (!keysBody.empty())
            keys.push_back(decodeHashField(keysBody));
        expectExhausted(entryBody, "access list entry");
        out.emplace_back(address, std::move(keys));
    }
    return out;
}

/// EIP-7702 authorization tuples: [chainId, address, nonce, yParity, r, s], field order per
/// evmone::state::rlp_encode(const Authorization&) (rlp_encode.cpp:96-100). `signer` is left
/// std::nullopt on purpose: OpTransition.cpp's own authorization processing
/// (process_authorization_list -> recoverAuthority, OpTransition.cpp:36-45) ecrecovers it
/// internally during execution over a *different* signing preimage (0x05 magic byte, not the
/// outer tx's type byte) — duplicating that here would be both redundant and a second place the
/// two recovery paths could silently drift apart.
inline evmone::state::AuthorizationList decodeAuthorizationList(bcos::bytesRef& in)
{
    evmone::state::AuthorizationList out;
    auto listBody = enterList(in);
    while (!listBody.empty())
    {
        auto entryBody = enterList(listBody);
        evmone::state::Authorization auth;
        auth.chain_id = decodeU256Scalar(entryBody);
        auth.addr = decodeAddressField(entryBody);
        auth.nonce = decodeU64Scalar(entryBody);
        auth.v = decodeAuthYParityScalar(entryBody);  // uint8-width, see helper
        auth.r = decodeU256Scalar(entryBody);
        auth.s = decodeU256Scalar(entryBody);
        expectExhausted(entryBody, "authorization tuple");
        out.push_back(std::move(auth));
    }
    return out;
}

inline evmc::bytes_view toAddressView(const std::optional<evmc::address>& addr) noexcept
{
    return addr.has_value() ? evmc::bytes_view(*addr) : evmc::bytes_view{};
}

/// Recovers the outer transaction's sender via ecrecover over the type-specific signing preimage
/// (`typeByte || rlp(payload_fields_without_signature)`). Same ecrecover call shape as
/// bcos-evm/test/opstack/T8nReplayHarness.h's replayRecoverAuthority / OpTransition.cpp's
/// recoverAuthority — precedent for both the hash-then-ecrecover pattern and the exact
/// evmmax::secp256k1::ecrecover call signature (this is a *different* signature than
/// authorization-tuple recovery: different preimage, same underlying primitive).
inline evmc::address recoverTxSender(const evmc::bytes& signingPreimage,
    const intx::uint256& yParity, const intx::uint256& r, const intx::uint256& s)
{
    requireLowSSignature(r, s);  // EIP-2 malleability guard.
    const auto hash =
        evmone::keccak256(evmc::bytes_view{signingPreimage.data(), signingPreimage.size()});
    const auto rBytes = intx::be::store<evmc::bytes32>(r);
    const auto sBytes = intx::be::store<evmc::bytes32>(s);
    auto recovered = evmmax::secp256k1::ecrecover(std::span<const uint8_t, 32>{hash.bytes, 32},
        std::span<const uint8_t, 32>{rBytes.bytes, 32},
        std::span<const uint8_t, 32>{sBytes.bytes, 32}, yParity != 0);
    if (!recovered.has_value())
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: sender ecrecover failed");
    return *recovered;
}
}  // namespace bcos::evm::engine::detail
