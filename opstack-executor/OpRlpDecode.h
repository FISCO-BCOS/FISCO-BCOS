// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Decode primitives for the OP raw-tx envelope decoder: bcos<->evmc conversions, bounds-checked
// narrowing, RLP scalar primitives, composite decoders.

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

// `toBcosH256` (evmc::bytes32 -> bcos::h256) lives in OpCommitments.h (two identical inline
// definitions of one name in the same namespace would be a redefinition error).

/// Bounds-checked u256→u64 narrowing — explicit > max check, never raw static_cast
/// (silent-truncation guard).
inline uint64_t narrowU256ToU64(const bcos::u256& v, const char* fieldName)
{
    static const bcos::u256 kMaxU64(std::numeric_limits<uint64_t>::max());
    if (v > kMaxU64)
        throw OpConsensusError(
            std::string("OpSchedulerImpl: field exceeds uint64_t range: ") + fieldName);
    return static_cast<uint64_t>(v);
}

/// Bounds-checked u64→int64 narrowing: a canonical 8-byte RLP scalar would silently become
/// *negative* under a raw cast, then *raise* the remaining block gas pool by ~2^63 (op-geth
/// rejects this with ErrGasLimitReached). `fieldName` distinguishes the per-call-site message.
inline int64_t narrowGasLimit(uint64_t v, const char* fieldName)
{
    if (v > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        throw OpConsensusError(
            std::string("OpSchedulerImpl: raw tx decode: gas limit exceeds int64_t range: ") +
            fieldName);
    return static_cast<int64_t>(v);
}

/// EIP-2 malleability guard (s <= n/2): without it a rewritten (r, n-s, 1-yParity) recovers to the
/// same sender but the raw bytes (hence txRoot/blockHash) differ — op-geth rejects it, we must too.
inline void requireLowSSignature(const intx::uint256& r, const intx::uint256& s)
{
    constexpr auto kSecpOrder = evmmax::secp256k1::Curve::ORDER;
    constexpr auto kSecpHalfOrder = kSecpOrder / 2;
    if (r == 0 || r >= kSecpOrder || s == 0 || s >= kSecpOrder || s > kSecpHalfOrder)
        throw OpConsensusError(
            "OpSchedulerImpl: raw tx decode: invalid signature (r/s out of [1,n-1], or s exceeds "
            "secp256k1n/2 — EIP-2 malleable signature)");
}

/// Build the OP block context from a FISCO header. `gasLimitOverride` injects the head block's
/// gasLimit as blockGasLeft (a minimal test header may leave gasLimit==0); `lenientOptionals`
/// tolerates unset optional header fields as 0 (eth_call path), while block execution uses
/// `.value()` and throws on an unset field.
inline evmone::state::BlockInfo toBlockInfo(const bcos::protocol::BlockHeader& env,
    std::optional<uint64_t> gasLimitOverride = std::nullopt, bool lenientOptionals = false)
{
    evmone::state::BlockInfo blk;
    blk.number = static_cast<int64_t>(env.number());
    // FISCO tars store milliseconds; evmone wants seconds.
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

// ---- raw tx envelope decode (deposit 0x7E / access_list 0x01 / eip1559 0x02 / setcode 0x04 /
// legacy >= 0xc0; blob 0x03 deliberately unsupported — L2 blocks carry no blob txs). ----
// Primitives reuse the production bcos-codec RLP decode path; signing-preimage field order comes
// from the encode-side rlp_encode.cpp.

inline void throwOnDecodeError(bcos::Error::UniquePtr&& err)
{
    if (err != nullptr)
        // errorMessage(), not what(): bcos::Error's what() is empty/opaque via boost::exception,
        // whereas errorMessage() carries the RLP decoder's own diagnostic.
        throw OpConsensusError(
            std::string("OpSchedulerImpl: raw tx decode failed: ") + err->errorMessage());
}

// ---- canonical-encoding strictness ----
//
// Go's `rlp` (source of every byte this decoder will see) rejects non-canonical encodings;
// bcos-codec RLP is permissive by comparison, so strictness is layered:
//   (1) per-field: readCanonicalScalar/readFixedWidth/decodeBoolField reject leading-zero
//       scalars, over-wide integers, wrong-width address/hash, non-0x01 bools;
//   (2) length prefix: decodeHeader rejects a non-minimal leading-zero prefix;
//   (3) whole-envelope round-trip: assertCanonicalRoundTrip requires byte-identity with the raw
//       envelope — a defense-in-depth backstop that fails closed on residual non-canonicality.
// The stakes: a deposit envelope carries no signature to cross-check its fields, and
// computeOpTxRoot hashes the raw bytes — non-canonical input would silently alter an account or
// diverge the block hash from op-geth's DeriveSha.

/// Reads one RLP string payload, requiring a canonical unsigned scalar: no leading zero byte, no
/// wider than `maxBytes`. Advances `in` past it.
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

/// Reads one RLP string payload requiring EXACTLY `size` bytes (op-geth's
/// "input string too short/long").
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

/// EIP-7702 authorization yParity: op-geth stores it as uint8, so a wider RLP scalar (e.g. 256)
/// must reject the whole tx — reading it as a full uint256 would silently accept 256 and let the
/// block pass. Encoding-WIDTH only: a 1-byte value in [2,255] decodes fine and is skipped at
/// execution (EIP-7702 says skip, not fatal).
inline intx::uint256 decodeAuthYParityScalar(bcos::bytesRef& in)
{
    auto payload = readCanonicalScalar(in, 1, "authorization yParity");
    intx::uint256 value = 0;
    if (!payload.empty())
        value = payload[0];
    return value;
}

/// Go-native bool encoding: false = empty string, true = single byte 0x01. Decode as a generic
/// scalar (the bool RLP overload rejects the empty-string encoding of `false`). Accept exactly
/// {empty, 0x01} — anything else (including 0x00) is "rlp: invalid boolean value", and two
/// encodings of one bool would mean two block hashes for one block.
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
    // Exactly 20 bytes — a variable-length read would silently substitute a different address.
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

/// `to` nilability: an empty RLP string (lone 0x80 byte) means contract-creation; anything else
/// decodes as a 20-byte address.
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

/// Enters a nested RLP list: returns a view scoped to its payload, advancing `in` past the whole
/// list.
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

/// EIP-7702 authorization tuples: [chainId, address, nonce, yParity, r, s]. `signer` is left
/// nullopt on purpose — OpTransition ecrecovers it internally over a different preimage (0x05
/// magic byte), so duplicating that here would add a second drift-prone recovery path.
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
/// (`typeByte || rlp(payload_fields_without_signature)`).
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

/// Envelope-shape consensus checks relocated from the retired raw-tx decoders (OpTxDecode.h):
/// chain-id binding, EIP-2 low-s, yParity<=1. Runs before execution on the Transaction-object
/// path (OpstackExecutor::m_prepare). Validates only — never constructs a Transaction, no
/// ecrecover (the executor's opValidate recovers the sender itself). Dispatch mirrors the
/// retired decodeOneRawTx: deposit (0x7e) has no signature (decodeDepositTx owns its strictness)
/// and returns; typed 0x01/0x02/0x04 and legacy (>= 0xc0) are checked. Unknown type bytes are the
/// caller's concern (the execute hook rejects them before this is reached).
inline void validateEnvelopeSignature(bcos::bytes const& rawEntry, uint64_t chainId)
{
    if (rawEntry.empty())
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: empty envelope");
    constexpr uint8_t kRlpListBase = 0xc0;
    constexpr uint8_t kDepositTypeByte = 0x7e;
    const auto typeByte = rawEntry[0];
    if (typeByte >= kRlpListBase)
    {
        // legacy: [nonce, gasPrice, gasLimit, to, value, data, v, r, s]
        // The decode primitives below take a mutable bytesRef (they only read the buffer, but
        // advance the ref), so the const& envelope is copied into a local mutable buffer — the
        // same copy-by-value the retired decode*Tx entry points made.
        bcos::bytes bodyCopy = rawEntry;
        bcos::bytesRef body(bodyCopy.data(), bodyCopy.size());
        auto listBody = enterList(body);
        (void)decodeU64Scalar(listBody);            // nonce
        (void)decodeU256Scalar(listBody);           // gasPrice
        (void)narrowGasLimit(decodeU64Scalar(listBody), "legacy.gasLimit");
        (void)decodeOptionalAddressField(listBody); // to
        (void)decodeU256Scalar(listBody);           // value
        (void)decodeBytesField(listBody);           // data
        const auto v = decodeU256Scalar(listBody);
        const auto r = decodeU256Scalar(listBody);
        const auto s = decodeU256Scalar(listBody);
        expectExhausted(listBody, "legacy envelope fields");
        expectExhausted(body, "legacy envelope (trailing bytes after the field list)");
        if (v != 27 && v != 28)
        {
            // EIP-155: v = chainId*2 + 35 + parity. parity>1 pollutes the division -> derived
            // chainId mismatch, so the chain-id check also rejects invalid parity here.
            if (v < 35)
                throw OpConsensusError("OpSchedulerImpl: raw tx decode: invalid legacy v");
            if ((v - 35) / 2 != intx::uint256{chainId})
                throw OpConsensusError("OpSchedulerImpl: raw tx decode: chain id mismatch (legacy)");
        }
        requireLowSSignature(r, s);
        return;
    }
    if (typeByte == kDepositTypeByte)
        return;  // deposit: unsigned; decodeDepositTx owns its strictness
    // typed 0x01/0x02/0x04: [chainId, nonce, fees, gasLimit, to, value, data, accessList,
    // (0x04) authorizationList, yParity, r, s] — field order per rlp_encode.cpp.
    bcos::bytes bodyCopy = rawEntry;  // mutable copy, see the legacy branch note above
    bcos::bytesRef body(bodyCopy.data() + 1, bodyCopy.size() - 1);
    auto listBody = enterList(body);
    const auto txChainId = decodeU64Scalar(listBody);
    if (txChainId != chainId)
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: chain id mismatch");
    (void)decodeU64Scalar(listBody);  // nonce
    switch (typeByte)
    {
    case 0x01:  // access-list: single gasPrice
        (void)decodeU256Scalar(listBody);
        break;
    case 0x02:  // eip1559
    case 0x04:  // set-code
        (void)decodeU256Scalar(listBody);  // maxPriorityFeePerGas
        (void)decodeU256Scalar(listBody);  // maxFeePerGas
        break;
    default:
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: unsupported tx type byte");
    }
    (void)narrowGasLimit(decodeU64Scalar(listBody), "typed.gasLimit");
    (void)decodeOptionalAddressField(listBody);  // to
    (void)decodeU256Scalar(listBody);            // value
    (void)decodeBytesField(listBody);            // data
    (void)decodeAccessList(listBody);            // accessList
    if (typeByte == 0x04)
        (void)decodeAuthorizationList(listBody);  // authorizationList
    const auto yParity = decodeU256Scalar(listBody);
    if (yParity > 1)
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: invalid y parity");
    const auto r = decodeU256Scalar(listBody);
    const auto s = decodeU256Scalar(listBody);
    expectExhausted(listBody, "typed envelope fields");
    expectExhausted(body, "typed envelope (trailing bytes after the field list)");
    requireLowSSignature(r, s);
}
}  // namespace bcos::evm::engine::detail
