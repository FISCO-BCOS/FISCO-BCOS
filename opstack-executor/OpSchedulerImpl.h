// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpSchedulerImpl — dual-signature OP scheduler component. `executeBlock` satisfies the
// scheduler_v1::TransactionScheduler concept check only (the concept is unconditional; OP mode
// never calls it — throws immediately); `executeOpBlock` is the real OP-mode entry point, called
// from handleNewPayload's OP branch.
//
// Layering: a pure template header under bcos-evm/bcos-evm/engine/, same shape as Storage2State.h
// — depends on bcos-framework (Storage template parameter is instantiated against
// storage2/MultiLayerStorage::ViewType, protocol:: types) but is not itself part of the
// bcos-evm-opstack static library (header-only, no .cpp).
//
// vm ownership: one evmc::VM (evmone) per scheduler, constructed once (evmc_create_evmone()) and
// reused across blocks; thread-safety rests on the engine execution segment being serialized under
// x_state, not on any locking inside this class.

#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/FixedBytes.h>
#include <evmone/evmone.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpBlockSeal.h>
#include <opstack-executor/OpEngineSeam.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <opstack-executor/Storage2State.h>
#include <bcos-evm/eth/state/block.hpp>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-evm/eth/state/state_diff.hpp>
#include <bcos-evm/eth/state/state_view.hpp>
#include <bcos-evm/eth/state/system_contracts.hpp>
#include <bcos-evm/eth/state/transaction.hpp>
#include <cstdint>
#include <cstring>
#include <evmc/evmc.hpp>
#include <evmone_precompiles/secp256k1.hpp>
#include <intx/intx.hpp>
#include <limits>
#include <map>
#include <optional>
#include <range/v3/range/concepts.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <test/utils/rlp.hpp>
#include <test/utils/rlp_encode.hpp>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::evm::engine
{

/// Thrown for anything OP block execution classifies as a consensus-level rejection (error
/// table): malformed/undecodable raw tx bytes, processOpBlock's own semantic throws
/// (empty block, first tx not the L1 attributes deposit, gas-pool overrun, ...). Maps to INVALID
/// on the caller side, never -32603.
struct OpConsensusError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// Thrown when the ledger bridge's poison flag is set (a storage2-layer failure, not a consensus
/// violation — Storage2State.h's poison-flag error channel contract). Maps to JSON-RPC -32603
/// internal error on the caller side, never INVALID.
struct OpStorageError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// OP block execution environment was folded into `protocol::BlockHeader` when
/// PR #5385 gave the FISCO header tars slots for all 8 former OpBlockEnv fields (prevRandao/baseFee
/// -> coinbase/baseFee/prevRandao/parentBeaconBlockRoot/gasLimit/extraData/blobGasUsed/parentHash
/// -> parentInfo). The engine now fills one header object and `executeOpBlock`/`toBlockInfo` read
/// the accessors directly.

/// Six-way comparison surface: `seal`'s receiptsRoot/logsBloom/withdrawalsRoot
/// (bcos::evm::opstack::OpBlockSeal, unchanged structure) plus three members below
/// (stateRoot/gasUsed/txRoot) that are deliberately NOT folded into OpBlockSeal.
struct OpExecuteBlockResult
{
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts;
    bcos::evm::opstack::OpBlockSeal seal;
    bcos::h256 stateRoot;
    uint64_t gasUsed;
    bcos::h256 txRoot;
};

namespace detail
{

// ---- bcos:: <-> evmc:: fixed-size conversions ----
// Precedent: bcos-evm/bcos-evm/ledger/Storage2State.h's applyModifiedEntry (codeHash: evmc
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

// `toBcosH256` (evmc::bytes32 -> bcos::h256) moved to OpEngineSeam.h: that header is included
// above and needs the same conversion, and two identical inline definitions of one name in
// `bcos::evm::engine::detail` would be a redefinition error. Call sites below are unchanged.

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

inline evmone::state::BlockInfo toBlockInfo(const bcos::protocol::BlockHeader& env)
{
    evmone::state::BlockInfo blk;
    blk.number = static_cast<int64_t>(env.number());
    // FISCO tars store milliseconds; evmone wants seconds (blockHash/execution surface is always
    // in seconds).
    blk.timestamp = static_cast<uint64_t>(env.timestamp()) / 1000;
    blk.gas_limit = narrowU256ToU64(env.gasLimit(), "BlockInfo::gasLimit");
    blk.base_fee = narrowU256ToU64(env.baseFee().value(), "BlockInfo::baseFee");
    blk.coinbase = toEvmcAddress(env.coinbase());
    blk.prev_randao = toEvmcBytes32(env.prevRandao());
    blk.parent_beacon_block_root = toEvmcBytes32(env.parentBeaconBlockRoot().value());
    blk.extra_data = evmc::bytes(env.extraData().begin(), env.extraData().end());
    blk.blob_gas_used = narrowU256ToU64(env.blobGasUsed().value(), "BlockInfo::blobGasUsed");
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
// here pulls in only a header include-path dependency (already wired for this test binary via
// bcos-evm/test/CMakeLists.txt's `codec` link, added by Task 3), not a new link edge for the
// bcos-evm-opstack static library.
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
//
// The dedicated end-to-end test cases for these rules live on the source branch (not ported here);
// this branch's contract is pinned by the shared length-prefix test in bcos-codec's RLPTest.cpp
// and by OpSchedulerImplSmokeTest.cpp. A change that relaxes strictness should turn the
// source-branch suite red first, and the 33-vector t8n corpus is the proof the invariant never
// false-rejects canonical op-geth bytes.

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

/// EIP-7702 authorization-tuple yParity (C2, final review batch B). op-geth's
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
/// true=0x01, matching Task 3's own encode side: bcos-rpc's DepositTxHandler::encode encodes
/// this field via the generic UnsignedByte scalar path (`isSystemTransactionByte ? 1 : 0`), not
/// `bcos::codec::rlp::encode(bytes&, bool&)` — no such overload exists in RLPEncode.h (`bool`
/// does not satisfy the `UnsignedByte`/`UnsignedIntegral` concepts). Correspondingly,
/// `bcos::codec::rlp::decode(bytesRef&, bool&)` (RLPDecode.h) — which rejects payloadLength != 1
/// and therefore rejects the empty-string encoding of `false` — is the WRONG decode overload for
/// this field; decoding it as a generic scalar (decodeU64Scalar, which does treat
/// payloadLength==0 as value 0) is the byte-correct match for what op-geth actually emits.
///
/// Strictness (B4-2): Go's `rlp` accepts exactly two encodings for a bool -- the empty string
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
    // Exactly 20 bytes (B4-2). The previous `decode(in, bcos::Address&)` route accepted any
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
        auth.v = decodeAuthYParityScalar(entryBody);  // C2: uint8-width, see helper
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
    requireLowSSignature(r, s);  // C3 (coordinator review): EIP-2 malleability guard.
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

/// DepositTx (OpDepositTx.h field order, cross-checked against Task 3's encode side,
/// bcos-rpc TxHandler.cpp DepositTxHandler::encode): [sourceHash, from, to, mint, value, gas,
/// isSystemTransaction, data] — no signature (`from` is explicit); `signedEnvelope` stays
/// empty per OpBlockExecute.h's OpBlockTx contract ("empty for deposit", OpBlockExecute.h:18).
inline bcos::evm::opstack::OpBlockTx decodeDepositTx(bcos::bytes rawEntry)
{
    // envelope shape: 0x7E || rlp([sourceHash, from, to, mint, value, gas, isSystemTransaction,
    // data]) — `body` starts at the LIST HEADER, not at the first field (fix C1, coordinator
    // review: golden bytes are literally `0x7e f9…`); enterList() consumes that header and
    // returns a view scoped to the list's own payload for the field-by-field decode below.
    bcos::bytesRef body(rawEntry.data() + 1, rawEntry.size() - 1);
    auto listBody = enterList(body);
    bcos::evm::opstack::DepositTx dep;
    dep.source_hash = decodeHashField(listBody);
    dep.from = decodeAddressField(listBody);
    dep.to = decodeOptionalAddressField(listBody);
    // mint/value nilability: nil and a present-but-zero big.Int are RLP-indistinguishable (both
    // encode to the empty string) — DepositTxHandler::encode's comment (TxHandler.cpp) resolves
    // this same ambiguity the same way (a plain, non-optional scalar defaulting to 0); decoding
    // DepositTx::mint as present-and-possibly-zero rather than guessing nullopt mirrors that,
    // even though DepositTx::mint (OpDepositTx.h:29) is itself std::optional for execution-side
    // reasons this decoder cannot recover from the wire bytes alone.
    dep.mint = decodeU256Scalar(listBody);
    dep.value = decodeU256Scalar(listBody);
    dep.gas_limit =
        narrowGasLimit(decodeU64Scalar(listBody), "deposit.gas");  // C4 (coordinator review).
    dep.is_system_tx = decodeBoolField(listBody);
    dep.data = decodeBytesField(listBody);
    expectExhausted(listBody, "deposit envelope fields");
    expectExhausted(body, "deposit envelope (trailing bytes after the field list)");
    return bcos::evm::opstack::OpBlockTx{.tx = std::move(dep), .signedEnvelope = {}};
}

/// EIP-1559 (type 0x02): [chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to,
/// value, data, accessList, yParity, r, s] — field order per rlp_encode.cpp:43-47.
///
/// `chainId` (the scheduler's own chain id, threaded in from `executeOpBlock`'s `m_chainId` via
/// `decodeOneRawTx`) is compared against the decoded `tx.chain_id` immediately below (C2,
/// coordinator review): nothing downstream (`OpValidate.cpp` -> `validate_transaction`,
/// eth/state/state.cpp:420-529) checks chain id at all — it is used only for the CHAINID opcode
/// and EIP-7702 authorization matching — so a transaction signed for a *different* chain replays
/// unmodified here (ecrecover succeeds, sender is correct, execution is normal, block is VALID),
/// while op-geth's own signer rejects it at decode time (`ErrInvalidChainId`,
/// transaction_signing.go:284-285) and the whole block never reaches execution.
inline bcos::evm::opstack::OpBlockTx decodeEip1559Tx(bcos::bytes rawEntry, uint64_t chainId)
{
    const evmc::bytes fullEnvelope(rawEntry.begin(), rawEntry.end());
    // envelope shape: 0x02 || rlp([...]) — same list-header fix as decodeDepositTx (C1).
    bcos::bytesRef body(rawEntry.data() + 1, rawEntry.size() - 1);
    auto listBody = enterList(body);
    evmone::state::Transaction tx;
    tx.type = evmone::state::Transaction::Type::eip1559;
    tx.chain_id = decodeU64Scalar(listBody);
    if (tx.chain_id != chainId)
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: chain id mismatch (eip1559)");
    tx.nonce = decodeU64Scalar(listBody);
    tx.max_priority_gas_price = decodeU256Scalar(listBody);
    tx.max_gas_price = decodeU256Scalar(listBody);
    tx.gas_limit =
        narrowGasLimit(decodeU64Scalar(listBody), "eip1559.gasLimit");  // C4 (coordinator review).
    tx.to = decodeOptionalAddressField(listBody);
    tx.value = decodeU256Scalar(listBody);
    tx.data = decodeBytesField(listBody);
    tx.access_list = decodeAccessList(listBody);
    const auto yParity = decodeU256Scalar(listBody);
    // I1 (coordinator review): op-geth rejects yParity > 1 as "invalid y parity" before it ever
    // reaches sender recovery; without this check a >1 value gets silently truncated by
    // `static_cast<uint8_t>` into a bogus-but-plausible 0/1 that could pass downstream, and
    // `recoverTxSender`'s `yParity != 0` test would treat any nonzero value (including 256,
    // 257, ...) as parity=1.
    if (yParity > 1)
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: invalid y parity (eip1559)");
    tx.r = decodeU256Scalar(listBody);
    tx.s = decodeU256Scalar(listBody);
    expectExhausted(listBody, "eip1559 envelope fields");
    expectExhausted(body, "eip1559 envelope (trailing bytes after the field list)");
    tx.v = static_cast<uint8_t>(yParity);

    const auto signingPreimage =
        evmc::bytes{0x02} + evmone::rlp::encode_tuple(tx.chain_id, tx.nonce,
                                tx.max_priority_gas_price, tx.max_gas_price,
                                static_cast<uint64_t>(tx.gas_limit), toAddressView(tx.to), tx.value,
                                tx.data, tx.access_list);
    tx.sender = recoverTxSender(signingPreimage, yParity, tx.r, tx.s);

    return bcos::evm::opstack::OpBlockTx{.tx = std::move(tx), .signedEnvelope = fullEnvelope};
}

/// EIP-7702 set-code (type 0x04): [chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit,
/// to, value, data, accessList, authorizationList, yParity, r, s] — field order per
/// rlp_encode.cpp:66-70. `chainId` cross-check: see decodeEip1559Tx's comment (C2, same rationale,
/// same outer-tx-only scope — EIP-7702 authorization tuples carry their own independent chain_id
/// field, already checked inside evmone::state::process_authorization_list, and are unaffected).
inline bcos::evm::opstack::OpBlockTx decodeSetCodeTx(bcos::bytes rawEntry, uint64_t chainId)
{
    const evmc::bytes fullEnvelope(rawEntry.begin(), rawEntry.end());
    // envelope shape: 0x04 || rlp([...]) — same list-header fix as decodeDepositTx (C1).
    bcos::bytesRef body(rawEntry.data() + 1, rawEntry.size() - 1);
    auto listBody = enterList(body);
    evmone::state::Transaction tx;
    tx.type = evmone::state::Transaction::Type::set_code;
    tx.chain_id = decodeU64Scalar(listBody);
    if (tx.chain_id != chainId)
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: chain id mismatch (setcode)");
    tx.nonce = decodeU64Scalar(listBody);
    tx.max_priority_gas_price = decodeU256Scalar(listBody);
    tx.max_gas_price = decodeU256Scalar(listBody);
    tx.gas_limit =
        narrowGasLimit(decodeU64Scalar(listBody), "setcode.gasLimit");  // C4 (coordinator review).
    tx.to = decodeOptionalAddressField(listBody);
    tx.value = decodeU256Scalar(listBody);
    tx.data = decodeBytesField(listBody);
    tx.access_list = decodeAccessList(listBody);
    tx.authorization_list = decodeAuthorizationList(listBody);
    const auto yParity = decodeU256Scalar(listBody);
    // I1 (coordinator review): see the matching check in decodeEip1559Tx — same op-geth
    // "invalid y parity" rejection, same truncation/mod-256 hazard if left unchecked.
    if (yParity > 1)
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: invalid y parity (setcode)");
    tx.r = decodeU256Scalar(listBody);
    tx.s = decodeU256Scalar(listBody);
    expectExhausted(listBody, "setcode envelope fields");
    expectExhausted(body, "setcode envelope (trailing bytes after the field list)");
    tx.v = static_cast<uint8_t>(yParity);

    const auto signingPreimage =
        evmc::bytes{0x04} + evmone::rlp::encode_tuple(tx.chain_id, tx.nonce,
                                tx.max_priority_gas_price, tx.max_gas_price,
                                static_cast<uint64_t>(tx.gas_limit), toAddressView(tx.to), tx.value,
                                tx.data, tx.access_list, tx.authorization_list);
    tx.sender = recoverTxSender(signingPreimage, yParity, tx.r, tx.s);

    return bcos::evm::opstack::OpBlockTx{.tx = std::move(tx), .signedEnvelope = fullEnvelope};
}

/// EIP-2930 access-list tx (type 0x01): [chainId, nonce, gasPrice, gasLimit, to, value, data,
/// accessList, yParity, r, s] — field order per rlp_encode.cpp:29-37. Legacy/access-list carry a
/// single `gasPrice`, which evmone's Transaction models as `max_gas_price` with
/// `max_priority_gas_price` set equal (validate_transaction asserts priority<=cap; state.cpp:466).
/// `chainId` cross-check: same rationale/scope as decodeEip1559Tx (C2).
inline bcos::evm::opstack::OpBlockTx decodeAccessListTx(bcos::bytes rawEntry, uint64_t chainId)
{
    const evmc::bytes fullEnvelope(rawEntry.begin(), rawEntry.end());
    // envelope shape: 0x01 || rlp([...]) — same list-header handling as decodeEip1559Tx (C1).
    bcos::bytesRef body(rawEntry.data() + 1, rawEntry.size() - 1);
    auto listBody = enterList(body);
    evmone::state::Transaction tx;
    tx.type = evmone::state::Transaction::Type::access_list;
    tx.chain_id = decodeU64Scalar(listBody);
    if (tx.chain_id != chainId)
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: chain id mismatch (access_list)");
    tx.nonce = decodeU64Scalar(listBody);
    const auto gasPrice = decodeU256Scalar(listBody);
    tx.max_gas_price = gasPrice;
    tx.max_priority_gas_price = gasPrice;  // legacy/2930: single gasPrice, priority == cap
    tx.gas_limit = narrowGasLimit(decodeU64Scalar(listBody), "access_list.gasLimit");
    tx.to = decodeOptionalAddressField(listBody);
    tx.value = decodeU256Scalar(listBody);
    tx.data = decodeBytesField(listBody);
    tx.access_list = decodeAccessList(listBody);
    const auto yParity = decodeU256Scalar(listBody);
    if (yParity > 1)  // I1: same op-geth "invalid y parity" rejection as the typed decoders.
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: invalid y parity (access_list)");
    tx.r = decodeU256Scalar(listBody);
    tx.s = decodeU256Scalar(listBody);
    expectExhausted(listBody, "access_list envelope fields");
    expectExhausted(body, "access_list envelope (trailing bytes after the field list)");
    tx.v = static_cast<uint8_t>(yParity);

    const auto signingPreimage =
        evmc::bytes{0x01} + evmone::rlp::encode_tuple(tx.chain_id, tx.nonce, tx.max_gas_price,
                                static_cast<uint64_t>(tx.gas_limit), toAddressView(tx.to), tx.value,
                                tx.data, tx.access_list);
    tx.sender = recoverTxSender(signingPreimage, yParity, tx.r, tx.s);

    return bcos::evm::opstack::OpBlockTx{.tx = std::move(tx), .signedEnvelope = fullEnvelope};
}

/// Legacy tx (no EIP-2718 type byte; the envelope IS the RLP list): [nonce, gasPrice, gasLimit, to,
/// value, data, v, r, s] — field order per rlp_encode.cpp:23-26. Handles BOTH forms:
///   * pre-EIP-155: v ∈ {27,28}, parity = v-27, signing preimage = rlp([nonce,gasPrice,gasLimit,to,
///     value,data]) (6 items).
///   * EIP-155: v = chainId*2 + 35 + parity, signing preimage = rlp([...6 fields, chainId, 0, 0])
///     (9 items). The derived chainId must equal the scheduler's chainId (same cross-check/scope as
///     the typed decoders).
/// `v` is read as a canonical scalar (decodeU256Scalar → no leading zero, width-bounded), so
/// v-canonicality is enforced identically to the other types — a non-minimal v cannot silently
/// produce a divergent txRoot. txRoot itself is taken over the raw wire bytes (computeOpTxRoot,
/// OpEngineSeam.h), so `tx.v`'s uint8 width does not bound the legacy v space; the full v lives in
/// `signedEnvelope` and only parity/chainId are extracted here.
inline bcos::evm::opstack::OpBlockTx decodeLegacyTx(bcos::bytes rawEntry, uint64_t chainId)
{
    const evmc::bytes fullEnvelope(rawEntry.begin(), rawEntry.end());
    bcos::bytesRef body(rawEntry.data(), rawEntry.size());  // no type byte to strip
    auto listBody = enterList(body);
    evmone::state::Transaction tx;
    tx.type = evmone::state::Transaction::Type::legacy;
    tx.nonce = decodeU64Scalar(listBody);
    const auto gasPrice = decodeU256Scalar(listBody);
    tx.max_gas_price = gasPrice;
    tx.max_priority_gas_price = gasPrice;  // legacy: single gasPrice, priority == cap
    tx.gas_limit = narrowGasLimit(decodeU64Scalar(listBody), "legacy.gasLimit");
    tx.to = decodeOptionalAddressField(listBody);
    tx.value = decodeU256Scalar(listBody);
    tx.data = decodeBytesField(listBody);
    const auto v = decodeU256Scalar(listBody);
    tx.r = decodeU256Scalar(listBody);
    tx.s = decodeU256Scalar(listBody);
    expectExhausted(listBody, "legacy envelope fields");
    expectExhausted(body, "legacy envelope (trailing bytes after the field list)");

    // Derive parity + chainId from v (EIP-155 vs pre-155), rejecting any other v as op-geth does.
    intx::uint256 parity;
    evmc::bytes signingPreimage;
    if (v == 27 || v == 28)
    {
        parity = v - 27;
        tx.chain_id = 0;
        signingPreimage = evmone::rlp::encode_tuple(tx.nonce, tx.max_gas_price,
            static_cast<uint64_t>(tx.gas_limit), toAddressView(tx.to), tx.value, tx.data);
    }
    else if (v >= 35)
    {
        const auto rem = v - 35;
        parity = rem % 2;
        const auto derivedChainId = rem / 2;
        if (derivedChainId != intx::uint256{chainId})
            throw OpConsensusError("OpSchedulerImpl: raw tx decode: chain id mismatch (legacy)");
        tx.chain_id = chainId;
        signingPreimage = evmone::rlp::encode_tuple(tx.nonce, tx.max_gas_price,
            static_cast<uint64_t>(tx.gas_limit), toAddressView(tx.to), tx.value, tx.data,
            tx.chain_id, uint64_t{0}, uint64_t{0});
    }
    else
    {
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: invalid legacy v");
    }
    tx.v = static_cast<uint8_t>(parity);
    tx.sender = recoverTxSender(signingPreimage, parity, tx.r, tx.s);

    return bcos::evm::opstack::OpBlockTx{.tx = std::move(tx), .signedEnvelope = fullEnvelope};
}

/// Dispatches on the EIP-2718 type byte. The OP validator accepts every transaction type op-geth's
/// own `DecodeTransactions` does: deposit (0x7e), the three typed signed shapes (0x01 access-list,
/// 0x02 eip1559, 0x04 set-code), and the untyped legacy form (first byte ≥ 0xc0, an RLP list
/// header). Blob (0x03) stays out on purpose — L2 blocks carry no blob txs. Earlier this list was
/// only {0x7e, 0x02, 0x04}, on the mistaken premise that "these are the only shapes the variant
/// understands"; the execution variant (OpBlockExecute.h) has in fact always carried
/// legacy/access_list too (state.cpp:460-475) — the old set merely mirrored the t8n corpus's three
/// `_op_type` values, i.e. test coverage, not a capability boundary.
///
/// `chainId` is threaded straight through to the two typed-tx decoders — deposit has no chain_id
/// field and does not take the parameter. This function stays a free function in `detail`, not a
/// member of `OpSchedulerImpl`, precisely so this parameter never touches the class template's own
/// member-function signatures (which are eagerly instantiated whenever `OpSchedulerImpl<Storage>`
/// is named, unlike member bodies — an OP dependent name must not leak into a signature the
/// generic composition root also instantiates).
/// Re-encodes a decoded OpBlockTx canonically from its parsed fields, so the caller can compare it
/// byte-for-byte with the raw envelope. Uses evmone's canonical RLP for every type (no bcos-codec
/// type conversions): deposits are rebuilt as `0x7e || rlp([...8 fields])` (the isSystemTransaction
/// bool re-emits via the `uint64 0/1` shape, whose RLP — empty string / 0x01 — matches the wire
/// bool exactly); legacy reconstructs the full v (pre-155 27+parity, EIP-155 chainId*2+35+parity)
/// that `tx.v`'s uint8 cannot hold; the three signed typed forms round-trip through
/// `evmone::state::rlp_encode`.
inline bcos::bytes canonicalEnvelopeBytes(const bcos::evm::opstack::OpBlockTx& btx)
{
    if (std::holds_alternative<bcos::evm::opstack::DepositTx>(btx.tx))
    {
        const auto& d = std::get<bcos::evm::opstack::DepositTx>(btx.tx);
        auto body =
            evmone::rlp::encode_tuple(evmc::bytes_view(d.source_hash), evmc::bytes_view(d.from),
                d.to.has_value() ? evmc::bytes_view(*d.to) : evmc::bytes_view{}, d.mint.value_or(0),
                d.value, static_cast<uint64_t>(d.gas_limit),
                static_cast<uint64_t>(d.is_system_tx ? 1 : 0), d.data);
        bcos::bytes out;
        out.reserve(body.size() + 1);
        out.push_back(0x7e);
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }
    const auto& tx = std::get<evmone::state::Transaction>(btx.tx);
    if (tx.type == evmone::state::Transaction::Type::legacy)
    {
        const auto fullV = (tx.chain_id == 0) ? intx::uint256{27} + tx.v :
                                                intx::uint256{tx.chain_id} * 2 + 35 + tx.v;
        auto e = evmone::rlp::encode_tuple(tx.nonce, tx.max_gas_price,
            static_cast<uint64_t>(tx.gas_limit), toAddressView(tx.to), tx.value, tx.data, fullV,
            tx.r, tx.s);
        return {e.begin(), e.end()};
    }
    auto e = evmone::state::rlp_encode(tx);  // 0x01/0x02/0x04 with their EIP-2718 type prefix
    return {e.begin(), e.end()};
}

/// Whole-envelope canonical-encoding invariant: decode → re-encode → byte-compare against the
/// original wire bytes, rejecting any mismatch. Defense-in-depth INDEPENDENT of the per-field and
/// length-prefix checks — it fails closed if a future decoder change ever lets a non-canonical
/// byte through, keeping `computeOpTxRoot`'s "hash the raw bytes == op-geth DeriveSha" equivalence
/// a runtime-checked invariant rather than a prose promise. Cost is one re-encode per tx,
/// negligible beside the ecrecover each decoder already runs, so it stays enabled in all builds
/// (a debug-only guard would compile out of this Release consensus path — exactly where it must
/// hold). Proven free of false-rejects by the 33-vector golden corpus and the t8n legs, whose
/// real op-geth bytes all round-trip.
inline void assertCanonicalRoundTrip(
    const bcos::bytes& rawEntry, const bcos::evm::opstack::OpBlockTx& decoded)
{
    auto reencoded = canonicalEnvelopeBytes(decoded);
    if (reencoded.size() != rawEntry.size() ||
        !std::equal(reencoded.begin(), reencoded.end(), rawEntry.begin()))
        throw OpConsensusError(
            "OpSchedulerImpl: raw tx decode: non-canonical envelope (re-encode mismatch)");
}

inline bcos::evm::opstack::OpBlockTx decodeOneRawTx(bcos::bytes rawEntry, uint64_t chainId)
{
    if (rawEntry.empty())
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: empty envelope");
    constexpr uint8_t kDepositTypeByte = 0x7e;     // DepositTxHandler::encode type byte (Task 3)
    constexpr uint8_t kAccessListTypeByte = 0x01;  // evmone::state::Transaction::Type::access_list
    constexpr uint8_t kEip1559TypeByte = 0x02;     // evmone::state::Transaction::Type::eip1559
    constexpr uint8_t kSetCodeTypeByte = 0x04;     // evmone::state::Transaction::Type::set_code
    constexpr uint8_t kRlpListBase = 0xc0;  // EIP-2718: first byte >= 0xc0 is an untyped RLP list
    // Keep a copy of the wire bytes for the whole-envelope round-trip invariant below; the
    // sub-decoders consume (move from / crop) rawEntry as they parse.
    const bcos::bytes original = rawEntry;
    const auto typeByte = rawEntry[0];
    auto decoded = [&]() -> bcos::evm::opstack::OpBlockTx {
        if (typeByte >= kRlpListBase)  // legacy: no type byte, the envelope is the RLP list itself
            return decodeLegacyTx(std::move(rawEntry), chainId);
        if (typeByte == kDepositTypeByte)
            return decodeDepositTx(std::move(rawEntry));
        if (typeByte == kAccessListTypeByte)
            return decodeAccessListTx(std::move(rawEntry), chainId);
        if (typeByte == kEip1559TypeByte)
            return decodeEip1559Tx(std::move(rawEntry), chainId);
        if (typeByte == kSetCodeTypeByte)
            return decodeSetCodeTx(std::move(rawEntry), chainId);
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: unsupported tx type byte 0x" +
                               std::to_string(static_cast<unsigned>(typeByte)));
    }();
    assertCanonicalRoundTrip(original, decoded);
    return decoded;
}

}  // namespace detail

/// OP scheduler component: dual signature, constructed once per [receiptFactory, chainId,
/// fork-timestamps] combination (composition-root-owned; this class never reads chainId/fork
/// thresholds from SystemConfigs itself).
template <class Storage>
class OpSchedulerImpl
{
public:
    OpSchedulerImpl(bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory, uint64_t chainId,
        bcos::evm::opstack::OpForkTimestamps forkTimestamps)
      : m_receiptFactory(std::move(receiptFactory)),
        m_chainId(chainId),
        m_forkTimestamps(forkTimestamps),
        m_vm(evmc_create_evmone())
    {}

    // ---- engine-facing seam surface ----
    //
    // The engine's newPayload OP branch reaches every name below as a **dependent name on its
    // `SchedulerType` template parameter** (`typename SchedulerType::BlockEnv`,
    // `SchedulerType::computeTxRoot(...)`, ...) — the only channel available: engine must not
    // `#include` anything from bcos-evm (library purity, see the `c_opMode` comment in
    // EngineServiceImpl.h), and dependent names are looked up at instantiation, inside
    // `if constexpr (c_opMode)`, in a TU that has already included this header. The definitions
    // live in `OpEngineSeam.h`; this block only re-publishes them under the class scope the
    // engine can reach.

    /// The block-execution environment the engine fills in from the payload — the FISCO
    /// `protocol::BlockHeader` itself (PR #5385 gave every former OpBlockEnv field a tars slot).
    using BlockEnv = bcos::protocol::BlockHeader;
    /// What `executeOpBlock` returns.
    using ExecuteResult = OpExecuteBlockResult;
    /// Consensus-level rejection -> engine maps to INVALID.
    using ConsensusError = OpConsensusError;
    /// Storage-layer failure -> engine maps to JSON-RPC -32603, never INVALID.
    using StorageError = OpStorageError;
    /// c_ethRawTxTable = SYS_ETH_HASH_2_RAWTX (s_eth_hash_2_rawtx). No longer written since
    /// plan B (2026-08-10): registerOpBlock writes SYS_HASH_2_TX via opEnvelopeToTars instead.
    /// The constant is kept only for read-side test assertions that the rawtx table is absent.
    static constexpr std::string_view c_ethRawTxTable = SYS_ETH_HASH_2_RAWTX;

    /// The six-way comparison surface (plus the two seal-only outputs) in bcos:: types.
    static OpBlockCommitments commitmentsOf(const OpExecuteBlockResult& result)
    {
        return bcos::evm::engine::commitmentsOf(
            result.seal, result.stateRoot, result.gasUsed, result.txRoot);
    }

    /// transactionsRoot over raw EIP-2718 envelopes — the engine needs it *before* execution to
    /// reconstruct the header for the blockHash check (`ExecutionPayload` carries no
    /// transactionsRoot field); `executeOpBlock`'s step 6 calls the same function.
    static bcos::h256 computeTxRoot(::ranges::input_range auto const& rawTxBytes)
    {
        return computeOpTxRoot(rawTxBytes);
    }

    /// Isthmus activation predicate for the engine's -38005 timestamp x version gate. The
    /// threshold comparison deliberately lives on this side of the seam, next to `configAt`,
    /// rather than being reimplemented in the engine.
    ///
    /// It is a *separate* function from `configAt` on purpose: `configAt` cannot answer this
    /// question — it resolves sub-`isthmusTime` timestamps to the Isthmus config as well
    /// (documented in OpForkSchedule.h: "Timestamps below isthmusTime also resolve to Isthmus"),
    /// because the minimal loop has no pre-Isthmus config to fall back to. The version gate, by
    /// contrast, must reject pre-Isthmus timestamps outright (-38005 on any version), so it needs
    /// the raw threshold. Both read the same injected `m_forkTimestamps.isthmusTime`, so there is
    /// still exactly one source of truth for the value.
    [[nodiscard]] bool isIsthmusActiveAt(uint64_t timestamp) const noexcept
    {
        return timestamp >= m_forkTimestamps.isthmusTime;
    }

    /// Jovian activation predicate. The engine needs it for one fork-dependent static check: the
    /// header's `blobGasUsed` slot must be 0 under Isthmus, but from Jovian on the same slot is
    /// repurposed as the DA footprint and is validated by seal comparison instead (OpBlockSeal.h).
    /// Same "threshold comparison stays on this side" reasoning as `isIsthmusActiveAt`.
    [[nodiscard]] bool isJovianActiveAt(uint64_t timestamp) const noexcept
    {
        return timestamp >= m_forkTimestamps.jovianTime;
    }

    OpSchedulerImpl(const OpSchedulerImpl&) = delete;
    OpSchedulerImpl(OpSchedulerImpl&&) = delete;
    OpSchedulerImpl& operator=(const OpSchedulerImpl&) = delete;
    OpSchedulerImpl& operator=(OpSchedulerImpl&&) = delete;
    ~OpSchedulerImpl() = default;

    /// Dummy signature satisfying scheduler_v1::TransactionScheduler (concept-check purpose only
    /// — the concept is an unconditional compile-time constraint on EngineServiceImpl's
    /// SchedulerType template parameter, independent of runtime reachability; OP mode never calls
    /// this). Throws immediately, before any co_await/co_return: safe because bcos::task::Task's
    /// promise_type uses std::suspend_always at initial_suspend (libtask/bcos-task/Task.h:55), so
    /// the coroutine body does not run until the coroutine is actually resumed (syncWait/co_await);
    /// unhandled_exception() (Task.h:63-71) is the standard propagation path for exceptions raised
    /// inside a Task<T> coroutine body.
    task::Task<std::vector<bcos::protocol::TransactionReceipt::Ptr>> executeBlock(
        Storage& /*storage*/, auto& /*executor*/,
        bcos::protocol::BlockHeader const& /*blockHeader*/,
        ::ranges::input_range auto const& /*transactions*/,
        bcos::ledger::LedgerConfig const& /*ledgerConfig*/)
    {
        throw std::logic_error(
            "OpSchedulerImpl::executeBlock: not supported in OP mode; use executeOpBlock");
        co_return {};  // unreachable; satisfies the coroutine's declared return type
    }

    /// OP-mode entry point (called from handleNewPayload's OP branch). Steps:
    ///   1. sort/decode rawTxBytes into OpBlockTx (deposit/eip1559/set_code dispatch,
    ///      detail::decodeOneRawTx);
    ///   2. one Storage2State<Storage> bridge instance for this block ("one per block");
    ///   3. processOpBlock (bridge doubles as StateView and applyDiff sink);
    ///   4. poison-flag check (bridge.poisoned() -> OpStorageError; any other throw from
    ///      processOpBlock -> OpConsensusError — error-classification table; poisoned() is
    ///      checked *first* because Storage2State's read methods are noexcept and swallow
    ///      storage failures into the poison flag rather than propagating them, so it is
    ///      authoritative over whatever processOpBlock did or threw — Storage2State.h's
    ///      poison-flag error channel contract);
    ///   5. sealOpBlock (needs the post-finalize MessagePasser storage snapshot) + stateRootOf,
    ///      both while the bridge is still alive ("compute before the bridge is destroyed");
    ///   6. txRoot over the caller-supplied rawTxBytes (independent of step 1's parsed
    ///      interpretation — txRoot commits to the exact wire bytes) + gasUsed, folded into the
    ///      six-way comparison surface (OpExecuteBlockResult).
    task::Task<OpExecuteBlockResult> executeOpBlock(
        Storage& storage, BlockEnv const& env, ::ranges::input_range auto const& rawTxBytes)
    {
        // Step 1: sort/decode. m_chainId is passed as a plain uint64_t argument (not a new
        // parameter on this method, and not an OP-specific type) — see decodeOneRawTx's comment
        // on why the OP dependent-name-in-signature hazard does not apply here.
        std::vector<bcos::evm::opstack::OpBlockTx> txs;
        for (auto const& rawItem : rawTxBytes)
            txs.push_back(detail::decodeOneRawTx(
                bcos::bytes(std::begin(rawItem), std::end(rawItem)), m_chainId));

        // Step 2: one bridge instance for this block.
        bcos::evm::evmstate::Storage2State<Storage> bridge(storage);

        const auto blk = detail::toBlockInfo(env);
        // RecentBlockHashes lazily loads ancestor hashes; the seed {N-1: parentHash} is set in
        // the constructor. hashErr is this block's poison channel (storage fault ->
        // OpStorageError, not INVALID).
        std::optional<std::string> hashErr;
        detail::RecentBlockHashes<Storage> hashes(
            storage, blk.number, detail::toEvmcBytes32(env.parentInfo().blockHash), &hashErr);

        // tars stores milliseconds; fork configs consume seconds (blockHash/execution surface is
        // always in seconds).
        const auto& cfg = bcos::evm::opstack::configAt(
            static_cast<uint64_t>(env.timestamp()) / 1000, m_forkTimestamps);

        const auto applyDiff = [&bridge](const evmone::state::StateDiff& diff) {
            bridge.applyDiff(diff);
        };

        // Step 3+4.
        bcos::evm::opstack::OpBlockResult result;
        try
        {
            result = bcos::evm::opstack::processOpBlock(
                bridge, blk, hashes, txs, cfg, m_vm, m_chainId, m_receiptFactory, applyDiff);
        }
        catch (const std::exception& e)
        {
            // This typed catch binds only the NON-runtime_error families — std::bad_alloc
            // (direct std::exception child) and the std::logic_error family (which
            // system_contracts.cpp throws for a fatal system-call failure). Every
            // std::runtime_error and its subclasses — including all of processOpBlock's
            // block-level consensus rejections — escape this catch via the RTTI bypass explained
            // in the catch(...) clause below and are handled THERE as OpConsensusError → INVALID.
            // So whatever binds here is by construction a LOCAL fault (allocation failure /
            // internal invariant), which must never vote against the block → OpStorageError
            // (-32603), not OpConsensusError.
            if (bridge.poisoned() || hashErr.has_value())
                throw OpStorageError(
                    hashErr.has_value() ? *hashErr : std::string(bridge.firstError()));
            throw OpStorageError(e.what());
        }
        catch (...)
        {
            // Typed-catch RTTI bypass (same phenomenon documented and worked around in
            // T8nReplayHarness.h): the -fno-rtti libevmone.a brings a hidden non-unique typeinfo
            // for std::exception, so `catch (const std::exception&)` above does NOT reliably bind
            // std::runtime_error thrown by evmone/opstack-linked code. Without this fallback those
            // throws would propagate out of executeOpBlock as raw, unclassified exceptions,
            // silently breaking the INVALID vs -32603 dispatch. Re-applies the *same*
            // poisoned()-first classification without relying on typeid matching; the original
            // message is unrecoverable here (no typed handle on the caught object).
            if (bridge.poisoned() || hashErr.has_value())
                throw OpStorageError(
                    hashErr.has_value() ? *hashErr : std::string(bridge.firstError()));
            throw OpConsensusError(
                "OpSchedulerImpl: processOpBlock threw a block-level error (typed catch bypassed "
                "by a known RTTI issue across the -fno-rtti evmone library boundary; original "
                "exception message unavailable, see this catch(...) clause's comment)");
        }
        if (bridge.poisoned() || hashErr.has_value())
            throw OpStorageError(hashErr.has_value() ? *hashErr : std::string(bridge.firstError()));

        // Step 5: MessagePasser post-finalize storage snapshot (OpBlockSeal.h contract) + seal +
        // stateRoot, bridge still alive throughout.
        std::map<evmc::bytes32, evmc::bytes32> messagePasserStorage;
        bridge.visitAccounts([&](const auto& accountView) {
            if (accountView.addr == bcos::evm::opstack::OP_L2_TO_L1_MESSAGE_PASSER)
            {
                messagePasserStorage = accountView.storage;
                return false;  // found it; not a poison condition (Storage2State.h contract)
            }
            return true;
        });
        if (bridge.poisoned())
            throw OpStorageError(std::string(bridge.firstError()));

        const auto seal = bcos::evm::opstack::sealOpBlock(result, cfg, messagePasserStorage);
        const auto stateRootHash = bcos::evm::stateRootOf(bridge);
        if (bridge.poisoned())
            throw OpStorageError(std::string(bridge.firstError()));

        // Step 6: txRoot (trie key = canonical RLP encoding of the index, trie value = the raw
        // tx bytes as-is) + gasUsed. NOTE: this is NOT op-geth's `DeriveSha` convention —
        // `DeriveSha` re-encodes each transaction canonically from the parsed struct while this
        // hashes the wire bytes; the two coincide only because the decoders above reject every
        // non-canonical encoding (per-field strictness, the shared length-prefix fix, and the
        // whole-envelope `assertCanonicalRoundTrip` invariant — see `computeOpTxRoot`'s comment in
        // OpEngineSeam.h). The trie construction lives in `OpEngineSeam.h`'s `computeOpTxRoot` so
        // the engine's newPayload OP branch can derive the same value *before* execution, for the
        // header reconstruction the blockHash check depends on — same function, two call sites, no
        // second implementation.
        const auto txRoot = computeOpTxRoot(rawTxBytes);

        // Plan A phase 2: the execution layer already produced bcos::protocol::TransactionReceipt
        // objects (OP metadata in opStackMeta, effective gas price on the top-level field), so no
        // mapOpReceipt projection happens here — the result receipts ARE the framework receipts.
        co_return OpExecuteBlockResult{
            .receipts = std::move(result.receipts),
            .seal = seal,
            .stateRoot = detail::toBcosH256(stateRootHash),
            .gasUsed = static_cast<uint64_t>(result.gasUsed),
            .txRoot = txRoot,
        };
    }

private:
    bcos::protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    uint64_t m_chainId;
    bcos::evm::opstack::OpForkTimestamps m_forkTimestamps;
    evmc::VM m_vm;  // evmc_create_evmone(), one instance per scheduler
};

}  // namespace bcos::evm::engine
