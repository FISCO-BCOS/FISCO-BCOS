// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpSchedulerImpl — dual-signature OP scheduler component (op-validator-minimal-loop design
// §4, task-4-brief.md). Two independently-motivated signatures on one class:
//   - `executeBlock`  : satisfies scheduler_v1::TransactionScheduler purely for the
//     engine/bcos-engine EngineServiceImpl compile-time concept check (design §2 "组件形态" —
//     the concept is unconditional, independent of whether the OP branch is runtime-reachable);
//     OP mode never calls it. Throws immediately.
//   - `executeOpBlock`: the real OP-mode entry point, called from handleNewPayload's OP branch
//     (T5b, out of this task's scope — this task delivers the component only).
//
// Layering (design §3 "模块布局"): a pure template header under bcos-evm/bcos-evm/engine/,
// same shape as bcos-evm/bcos-evm/ledger/Storage2Ledger.h — depends on bcos-framework (Storage
// template parameter is instantiated against storage2/MultiLayerStorage::ViewType, protocol::
// types) but is not itself part of the bcos-evm-opstack static library (header-only, no .cpp).
//
// vm ownership: evmc::VM (evmone) is a per-scheduler member, constructed once
// (evmc_create_evmone()) and reused across blocks — design §4.1 "vm 归属"; thread-safety rests
// on the engine execution segment being serialized under x_state (design §4.4), not on any
// locking inside this class.

#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/engine/OpEngineSeam.h>
#include <bcos-evm/engine/OpReceiptMap.h>
#include <bcos-evm/ledger/Storage2Ledger.h>
#include <bcos-evm/opstack/OpBlockExecute.h>
#include <bcos-evm/opstack/OpBlockSeal.h>
#include <bcos-evm/opstack/OpDepositTx.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
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
#include <bcos-evm/eth/state/block.hpp>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-evm/eth/state/state_diff.hpp>
#include <bcos-evm/eth/state/state_view.hpp>
#include <bcos-evm/eth/state/system_contracts.hpp>
#include <bcos-evm/eth/state/transaction.hpp>
#include <bcos-evm/eth/utils/mpt.hpp>
#include <bcos-evm/eth/utils/rlp.hpp>
#include <bcos-evm/eth/utils/rlp_encode.hpp>
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
#include <utility>
#include <variant>
#include <vector>

namespace bcos::evm::engine
{

/// Thrown for anything OP block execution classifies as a consensus-level rejection (spec §4.3
/// error table): malformed/undecodable raw tx bytes, processOpBlock's own semantic throws
/// (empty block, first tx not the L1 attributes deposit, gas-pool overrun, ...). Maps to INVALID
/// on the caller side (T5b), never -32603.
struct OpConsensusError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// Thrown when the ledger bridge's poison flag is set (a storage2-layer failure, not a consensus
/// violation — Storage2Ledger.h's "毒旗错误通道" contract). Maps to JSON-RPC -32603 internal
/// error on the caller side (T5b), never INVALID.
struct OpStorageError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// OP block execution environment (design §4.2). Field types mirror
/// bcos-framework/engine/Types.h's ExecutionPayload (bcos::h256/Address/u256/bytes) — this is
/// the adapter boundary between the engine-side payload representation and the opstack-side
/// (evmc::/intx::) execution primitives; OpSchedulerImpl converts internally (see
/// detail::toBlockInfo below), the same bridging role Storage2Ledger plays on the storage side.
struct OpBlockEnv
{
    const bcos::protocol::BlockHeader& fiscoHeader;
    bcos::h256 parentHash;
    bcos::h256 prevRandao;
    bcos::u256 baseFeePerGas;
    bcos::Address feeRecipient;
    /// OP semantics: the L1 origin's parent beacon root (EIP-4788 system-call input;
    /// participates in blockHash — design §4.2).
    bcos::h256 parentBeaconBlockRoot;
    /// FISCO's own header carries no gas-limit field; the payload is the sole source (execution
    /// gas-pool ceiling is otherwise unconstrained — design §4.2).
    uint64_t gasLimit;
    bcos::bytes extraData;
    uint64_t blobGasUsed;
};

/// Six-way comparison surface (design §4.1 "六项比对面"): `seal`'s
/// receiptsRoot/logsBloom/withdrawalsRoot (bcos::evm::opstack::OpBlockSeal, unchanged structure)
/// plus three members below (stateRoot/gasUsed/txRoot) that are deliberately NOT folded into
/// OpBlockSeal — rev.3's correction of rev.2's "merge into seal" miscount (design §4.1).
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
// Precedent: bcos-evm/bcos-evm/ledger/Storage2Ledger.h's applyModifiedEntry (codeHash: evmc
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

// `toBcosH256` (evmc::bytes32 -> bcos::h256) moved to OpEngineSeam.h (task-5b): that header is
// included above and needs the same conversion, and two identical inline definitions of one name
// in `bcos::evm::engine::detail` would be a redefinition error. Call sites below are unchanged.

/// bcos::u256 -> uint64_t, explicit bounds-checked narrowing — NOT a raw static_cast/convert_to.
/// This repo has a documented silent-truncation incident with unchecked wide-integer narrowing
/// (MEMORY costofprecompiled-int64-overflow); Storage2Ledger.h's fetchAccount nonce handling
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
/// narrow" discipline as `narrowU256ToU64` above, applied to the wire-decoded gas_limit scalar
/// (coordinator review C4). DepositTx::gas_limit / evmone::state::Transaction::gas_limit are
/// both `int64_t`; a canonical 8-byte RLP scalar (e.g. `0xFFFFFFFFFFFFFFFF`) decodes to a
/// `uint64_t` that silently becomes *negative* under a raw `static_cast<int64_t>`. Traced attack
/// chain for the deposit path specifically (`dep.gas_limit`): `validate_transaction` rejects the
/// negative gas_limit as below the intrinsic-gas floor -> `runDeposit` (OpDepositTx.cpp) only
/// special-cases `GAS_LIMIT_REACHED`, so this falls into the generic failed-deposit branch and
/// sets `receipt.gas_used = dep.gas_limit` (still negative) -> `OpBlockExecute.cpp`'s
/// `blockGasLeft -= gas_used` then *raises* the remaining block gas pool by roughly 2^63,
/// letting later transactions in the same block exceed the real gasLimit, while `gasUsed` itself
/// wraps to a huge-but-plausible `uint64_t` on the `OpExecuteBlockResult` projection — an
/// attacker supplying the same wrapped value on the payload side would pass the six-way
/// comparison surface and get a block VALID that op-geth rejects outright (`GasPool.SubGas`
/// returns `ErrGasLimitReached`, and `state_transition.go`'s failed-deposit branch explicitly
/// excludes that error from the "still charge, still succeed" path — the whole block is invalid).
/// `fieldName` (coordinator review M-2): mirrors `narrowU256ToU64`'s own `fieldName` parameter
/// above — without it, all three call sites (deposit/eip1559/setcode) produced the exact same
/// message ("gas limit exceeds int64_t range"), which is also what I-1's fix-vs-no-fix message
/// assertion in OpSchedulerImplTest.cpp needs to distinguish per call site.
inline int64_t narrowGasLimit(uint64_t v, const char* fieldName)
{
    if (v > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        throw OpConsensusError(
            std::string("OpSchedulerImpl: raw tx decode: gas limit exceeds int64_t range: ") +
            fieldName);
    return static_cast<int64_t>(v);
}

/// EIP-2 signature-malleability guard (r,s in [1, n-1], s <= n/2). `evmmax::secp256k1::ecrecover`
/// implements ECRECOVER-PRECOMPILE semantics, which only require `0 < r,s < n` — real Ethereum
/// signers additionally reject high-s (op-geth's `crypto.ValidateSignatureValues`, invoked with
/// `homestead=true` from `transaction_signing.go`, crypto/crypto.go:244-248). Without this check,
/// an attacker can rewrite a legitimately-signed transaction's `(r,s,yParity)` to
/// `(r, n-s, 1-yParity)`: sender recovery and execution outcome are unchanged, but the raw bytes
/// (hence txRoot/blockHash) differ from the canonical signing — this scheduler would accept the
/// rewritten block as VALID where op-geth rejects the whole block. Mirrors
/// bcos-evm/test/opstack/T8nReplayHarness.h's `structurallyUnrecoverable` predicate (same
/// four-way r/s bound check), applied here to the *outer* transaction's own signature rather
/// than an EIP-7702 authorization tuple — same underlying curve constant
/// (`evmmax::secp256k1::Curve::ORDER`) already used for exactly this bound in
/// bcos-evm/bcos-evm/eth/state/state.cpp:21/117 and OpTransition.cpp:28/71 (both for
/// authorization tuples; this codebase never previously validated the *outer* tx signature this
/// way, because nothing before this task decoded an outer tx signature from raw bytes at all).
inline void requireLowSSignature(const intx::uint256& r, const intx::uint256& s)
{
    constexpr auto kSecpOrder = evmmax::secp256k1::Curve::ORDER;
    constexpr auto kSecpHalfOrder = kSecpOrder / 2;
    if (r == 0 || r >= kSecpOrder || s == 0 || s >= kSecpOrder || s > kSecpHalfOrder)
        throw OpConsensusError(
            "OpSchedulerImpl: raw tx decode: invalid signature (r/s out of [1,n-1], or s exceeds "
            "secp256k1n/2 — EIP-2 malleable signature)");
}

/// BlockHashes answering exactly one query (block number - 1 -> the injected parent hash),
/// mirroring bcos-evm/test/opstack/T8nReplayHarness.h's ParentOnlyBlockHashes: EIP-2935's
/// system call at block 1 only ever queries the immediate parent within this loop's scope; any
/// other query returning the zero hash (rather than a fabricated value) is intentional.
struct ParentOnlyBlockHashes final : evmone::state::BlockHashes
{
    int64_t blockNumber = 0;
    evmone::hash256 parentHash{};

    evmc::bytes32 get_block_hash(int64_t queriedNumber) const noexcept override
    {
        return queriedNumber == blockNumber - 1 ? parentHash : evmc::bytes32{};
    }
};

inline evmone::state::BlockInfo toBlockInfo(const OpBlockEnv& env)
{
    evmone::state::BlockInfo blk;
    blk.number = env.fiscoHeader.number();
    blk.timestamp = env.fiscoHeader.timestamp();
    blk.gas_limit = static_cast<int64_t>(env.gasLimit);
    blk.base_fee = narrowU256ToU64(env.baseFeePerGas, "OpBlockEnv::baseFeePerGas");
    blk.coinbase = toEvmcAddress(env.feeRecipient);
    blk.prev_randao = toEvmcBytes32(env.prevRandao);
    blk.parent_beacon_block_root = toEvmcBytes32(env.parentBeaconBlockRoot);
    blk.extra_data = evmc::bytes(env.extraData.begin(), env.extraData.end());
    blk.blob_gas_used = env.blobGasUsed;
    return blk;
}

// ---- raw tx envelope decode (design §4.3 step 1 "分拣": deposit 0x7E / eip1559 0x02 / setcode
// 0x04 — the same three shapes T8nReplayHarness.h's `_op_type` dispatch and processOpBlock's
// OpBlockTx variant already understand; no legacy/access_list(0x01)/blob(0x03) support, matching
// the t8n corpus). ----
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
        // that fired (C1, final review batch B) rather than a bare "decode failed".
        throw OpConsensusError(
            std::string("OpSchedulerImpl: raw tx decode failed: ") + err->errorMessage());
}

// ---- canonical-encoding strictness (final review B4-2) ----
//
// Go's `rlp` (which produced every byte this decoder will ever legitimately see, via
// `tx.MarshalBinary()`) rejects non-canonical encodings outright: `ErrCanonInt` for a leading zero
// byte or a single `0x00`, "input string too long for uint64", "input string too short/long for
// common.Address|common.Hash", "rlp: invalid boolean value". The primitives in
// `bcos-codec/rlp/RLPDecode.h` are permissive by comparison — `fromBigEndian` silently keeps only
// the low 8 bytes of an over-long uint64 payload, and `FixedBytes`' constructor right-pads a short
// payload and truncates a long one. Those defaults are fine for their other consumers, so the
// strictness is added HERE, on the OP path, rather than by changing shared primitives.
//
// The sharpest consequence of NOT doing this: a deposit envelope carries no signature, so nothing
// else cross-checks its fields. A 19-byte `from`/`to` or a 33-byte `sourceHash` would have been
// silently padded/truncated into a DIFFERENT account and executed as if it were the one the bytes
// named.
//
// It also closes a blockHash-derivation gap: `computeOpTxRoot` (OpEngineSeam.h) hashes the raw
// wire bytes, whereas op-geth's `DeriveSha` re-encodes each transaction canonically from the
// parsed struct. For canonical input the two agree; for non-canonical input they do not, so
// without this check the two implementations could compute different block hashes for the same
// payload. With it, non-canonical input never survives decoding, and the equivalence holds for
// everything that does.

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
/// true=0x01, matching Task 3's own encode side: OpDepositEncode.cpp encodes this field via the
/// generic UnsignedByte scalar path (`isSystemTransactionByte ? 1 : 0`), not
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

/// DepositTx (OpDepositTx.h field order, cross-checked against Task 3's OpDepositFields /
/// encodeDepositEnvelope, bcos-codec/rlp/OpDepositEncode.h): [sourceHash, from, to, mint, value,
/// gas, isSystemTransaction, data] — no signature (`from` is explicit); `signedEnvelope` stays
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
    // encode to the empty string) — OpDepositEncode.h's OpDepositFields comment resolves this
    // same ambiguity the same way (a plain, non-optional scalar defaulting to 0); decoding
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

/// Dispatches on the EIP-2718 type byte (design §4.3 step 1 "分拣"). Deliberately narrow: only
/// the three shapes processOpBlock's OpBlockTx variant understands (matching the t8n corpus's
/// own three `_op_type` values, T8nReplayHarness.h:465-596 — no legacy/access_list/blob).
///
/// `chainId` is threaded straight through to the two typed-tx decoders (C2, coordinator review)
/// — deposit has no chain_id field and does not take the parameter. This function stays a free
/// function in `detail`, not a member of `OpSchedulerImpl`, precisely so this parameter never
/// touches the class template's own member-function signatures (which are eagerly instantiated
/// whenever `OpSchedulerImpl<Storage>` is named, unlike member bodies — see the "engine-facing
/// seam surface" block on the class below, and engine's `EngineServiceImpl.h` c_opMode comment,
/// task-5b, for the "OP dependent name must not leak into a signature the generic composition
/// root also instantiates" failure mode this avoids).
inline bcos::evm::opstack::OpBlockTx decodeOneRawTx(bcos::bytes rawEntry, uint64_t chainId)
{
    if (rawEntry.empty())
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: empty envelope");
    constexpr uint8_t kDepositTypeByte = 0x7e;  // OpDepositFields::kDepositTxType (Task 3)
    constexpr uint8_t kEip1559TypeByte = 0x02;  // evmone::state::Transaction::Type::eip1559
    constexpr uint8_t kSetCodeTypeByte = 0x04;  // evmone::state::Transaction::Type::set_code
    const auto typeByte = rawEntry[0];
    if (typeByte == kDepositTypeByte)
        return decodeDepositTx(std::move(rawEntry));
    if (typeByte == kEip1559TypeByte)
        return decodeEip1559Tx(std::move(rawEntry), chainId);
    if (typeByte == kSetCodeTypeByte)
        return decodeSetCodeTx(std::move(rawEntry), chainId);
    throw OpConsensusError("OpSchedulerImpl: raw tx decode: unsupported tx type byte 0x" +
                           std::to_string(static_cast<unsigned>(typeByte)));
}

}  // namespace detail

/// OP scheduler component (design §2 "组件形态"): dual signature, constructed once per
/// [receiptFactory, chainId, fork-timestamps] combination (composition-root-owned, design §4.2
/// decision D2 — this class never reads chainId/fork thresholds from SystemConfigs itself).
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

    // ---- engine-facing seam surface (task-5b, design §6.1) ----
    //
    // `engine/bcos-engine/EngineServiceImpl.h`'s newPayload OP branch reaches every one of the
    // names below as a **dependent name on its `SchedulerType` template parameter**
    // (`typename SchedulerType::BlockEnv`, `SchedulerType::computeTxRoot(...)`, ...). That is the
    // only channel available: engine must not `#include` anything from bcos-evm (库纯净, see the
    // `c_opMode` comment in EngineServiceImpl.h), and dependent names are looked up at
    // instantiation — inside `if constexpr (c_opMode)`, in a TU that has already included this
    // header. See `OpEngineSeam.h`'s file comment for the full rationale; the definitions live
    // there, this block only re-publishes them under the class scope the engine can reach.

    /// The block-execution environment the engine fills in from the payload (design §4.2).
    using BlockEnv = OpBlockEnv;
    /// What `executeOpBlock` returns (design §4.1).
    using ExecuteResult = OpExecuteBlockResult;
    /// Consensus-level rejection -> engine maps to INVALID (design §4.3 error table).
    using ConsensusError = OpConsensusError;
    /// Storage-layer failure -> engine maps to JSON-RPC -32603, never INVALID (design §4.3).
    using StorageError = OpStorageError;
    /// Table for the ETH/OP header RLP written on VALID (design §6.1 step 6, 裁定 B5).
    static constexpr std::string_view c_ethBlockHeaderTable = SYS_ETH_BLOCK_HEADER;
    /// Table for the raw EIP-2718 transaction envelopes written on VALID (batch 6, decision (B)).
    static constexpr std::string_view c_ethRawTxTable = SYS_ETH_HASH_2_RAWTX;

    /// The six-way comparison surface (+ the two §5.1 seal outputs) in bcos:: types.
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

    /// Isthmus activation predicate for the engine's -38005 timestamp x version gate (design
    /// §6.1 step 1). The threshold comparison deliberately lives on this side of the seam, next
    /// to `configAt`, rather than being reimplemented in the engine.
    ///
    /// It is a *separate* function from `configAt` on purpose (task-4 review item M4):
    /// `configAt` cannot answer this question — it resolves sub-`isthmusTime` timestamps to the
    /// Isthmus config as well (documented in OpForkSchedule.h: "Timestamps below isthmusTime also
    /// resolve to Isthmus"), because the minimal loop has no pre-Isthmus config to fall back to.
    /// The version gate, by contrast, must reject V4 for a pre-Isthmus timestamp, so it needs the
    /// raw threshold. Both read the same injected `m_forkTimestamps.isthmusTime`, so there is
    /// still exactly one source of truth for the value.
    [[nodiscard]] bool isIsthmusActiveAt(uint64_t timestamp) const noexcept
    {
        return timestamp >= m_forkTimestamps.isthmusTime;
    }

    /// Jovian activation predicate. The engine needs it for one fork-dependent static check: the
    /// header's `blobGasUsed` slot must be 0 under Isthmus, but from Jovian on the same slot is
    /// repurposed as the DA footprint and is validated by seal comparison instead (design §5.1 /
    /// OpBlockSeal.h:31-38). Same "threshold comparison stays on this side" reasoning as
    /// `isIsthmusActiveAt`.
    [[nodiscard]] bool isJovianActiveAt(uint64_t timestamp) const noexcept
    {
        return timestamp >= m_forkTimestamps.jovianTime;
    }

    OpSchedulerImpl(const OpSchedulerImpl&) = delete;
    OpSchedulerImpl(OpSchedulerImpl&&) = delete;
    OpSchedulerImpl& operator=(const OpSchedulerImpl&) = delete;
    OpSchedulerImpl& operator=(OpSchedulerImpl&&) = delete;
    ~OpSchedulerImpl() = default;

    /// Dummy signature satisfying scheduler_v1::TransactionScheduler (concept-check purpose
    /// only — the concept is an unconditional compile-time constraint on EngineServiceImpl's
    /// SchedulerType template parameter, independent of runtime reachability; OP mode never
    /// calls this, design §2). Throws immediately, before any co_await/co_return: safe because
    /// bcos::task::Task's promise_type uses std::suspend_always at initial_suspend
    /// (libtask/bcos-task/Task.h:55), so the coroutine body does not run until the coroutine is
    /// actually resumed (syncWait/co_await); unhandled_exception() (Task.h:63-71) is the
    /// standard propagation path for exceptions raised inside a Task<T> coroutine body, and is
    /// already relied upon elsewhere in this codebase for throwing scheduler/executor
    /// implementations.
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

    /// OP-mode entry point (design §4.3 "执行六步"; called from handleNewPayload's OP branch,
    /// T5b — not delivered by this task). Steps:
    ///   1. sort/decode rawTxBytes into OpBlockTx (deposit/eip1559/set_code dispatch,
    ///      detail::decodeOneRawTx);
    ///   2. one Storage2Ledger<Storage> bridge instance for this block ("一块一实例", design
    ///      §4.1);
    ///   3. processOpBlock (bridge doubles as StateView and applyDiff sink);
    ///   4. poison-flag check (bridge.poisoned() -> OpStorageError; any other throw from
    ///      processOpBlock -> OpConsensusError — design §4.3 error-classification table;
    ///      poisoned() is checked *first* because Storage2Ledger's read methods are noexcept and
    ///      swallow storage failures into the poison flag rather than propagating them, so it is
    ///      authoritative over whatever processOpBlock did or threw — Storage2Ledger.h's "毒旗
    ///      错误通道" contract);
    ///   5. sealOpBlock (needs the post-finalize MessagePasser storage snapshot) + stateRootOf,
    ///      both while the bridge is still alive ("桥销毁前算毕", design §4.1);
    ///   6. txRoot over the caller-supplied rawTxBytes (independent of step 1's parsed
    ///      interpretation — txRoot commits to the exact wire bytes) + gasUsed, folded into the
    ///      six-way comparison surface (OpExecuteBlockResult, design §4.1).
    task::Task<OpExecuteBlockResult> executeOpBlock(
        Storage& storage, OpBlockEnv const& env, ::ranges::input_range auto const& rawTxBytes)
    {
        // Step 1: sort/decode. m_chainId is passed as a plain uint64_t argument (not a new
        // parameter on this method, and not an OP-specific type) — see decodeOneRawTx's comment
        // on why the OP dependent-name-in-signature hazard does not apply here (C2, coordinator
        // review).
        std::vector<bcos::evm::opstack::OpBlockTx> txs;
        for (auto const& rawItem : rawTxBytes)
            txs.push_back(detail::decodeOneRawTx(
                bcos::bytes(std::begin(rawItem), std::end(rawItem)), m_chainId));

        // Step 2: one bridge instance for this block.
        bcos::evm::ledger::Storage2Ledger<Storage> bridge(storage);

        const auto blk = detail::toBlockInfo(env);
        detail::ParentOnlyBlockHashes hashes;
        hashes.blockNumber = blk.number;
        hashes.parentHash = detail::toEvmcBytes32(env.parentHash);

        const auto& cfg = bcos::evm::opstack::configAt(
            static_cast<uint64_t>(env.fiscoHeader.timestamp()), m_forkTimestamps);

        const auto applyDiff = [&bridge](const evmone::state::StateDiff& diff) {
            bridge.applyDiff(diff);
        };

        // Step 3+4.
        bcos::evm::opstack::OpBlockResult result;
        try
        {
            result = bcos::evm::opstack::processOpBlock(
                bridge, blk, hashes, txs, cfg, m_vm, m_chainId, applyDiff);
        }
        catch (const std::exception& e)
        {
            if (bridge.poisoned())
                throw OpStorageError(std::string(bridge.firstError()));
            throw OpConsensusError(e.what());
        }
        catch (...)
        {
            // Typed-catch RTTI bypass (the same phenomenon bcos-evm/test/opstack/
            // T8nReplayHarness.h already documents and works around, in its own two `catch (...)`
            // clauses). NOTE: the original investigation write-up is NOT in this branch -- it was
            // written on the unrelated `feat-evm-mb1-block-execution` branch (commit d0937e8a1) at
            // `bcos-evm-ref/docs/audits/2026-07-12-typed-catch-rtti-investigation.md` and was never
            // carried over, so the `docs/audits/...` path this comment used to cite resolved to
            // nothing here. The mechanism is therefore restated in full below rather than
            // delegated to a link: libevmone.a (-fno-rtti) brings in a
            // hidden non-unique typeinfo for std::exception, so `catch (const std::exception&)`
            // above does NOT reliably bind std::runtime_error thrown by evmone/opstack-linked
            // code (or, empirically, by *any* std::runtime_error surfacing through this call —
            // confirmed via a diagnostic build: both processOpBlock's own "first tx is not the
            // L1 attributes deposit" throw and a ThrowingStorage-injected failure propagating
            // through Storage2Ledger::applyDiff's write-back path escaped the typed catch above
            // as an uncaught St13runtime_error). Without this fallback, both of those cases would
            // propagate out of executeOpBlock as raw, unclassified exceptions instead of
            // OpConsensusError/OpStorageError — silently breaking the INVALID vs -32603 dispatch
            // T5b/T6 build on top of this classification (design §4.3). Re-applies the *same*
            // poisoned()-first classification without relying on typeid matching; the original
            // message is unrecoverable here (no typed handle on the caught object).
            if (bridge.poisoned())
                throw OpStorageError(std::string(bridge.firstError()));
            throw OpConsensusError(
                "OpSchedulerImpl: processOpBlock threw a block-level error (typed catch bypassed "
                "by a known RTTI issue across the -fno-rtti evmone library boundary; original "
                "exception message unavailable, see this catch(...) clause's comment)");
        }
        if (bridge.poisoned())
            throw OpStorageError(std::string(bridge.firstError()));

        // Step 5: MessagePasser post-finalize storage snapshot (OpBlockSeal.h contract) + seal +
        // stateRoot, bridge still alive throughout.
        std::map<evmc::bytes32, evmc::bytes32> messagePasserStorage;
        bridge.visitAccounts([&](const auto& accountView) {
            if (accountView.addr == bcos::evm::opstack::OP_L2_TO_L1_MESSAGE_PASSER)
            {
                messagePasserStorage = accountView.storage;
                return false;  // found it; not a poison condition (Storage2Ledger.h contract)
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
        // tx bytes as-is — MPT::insert's value parameter is the leaf payload, not re-wrapped by
        // the caller, same contract stateRootOf<Ledger>'s account leaves rely on via
        // encode_tuple) + gasUsed. NOTE (B4-2 review M-3): this used to be described as "the
        // op-geth DeriveSha convention", which is not accurate — `DeriveSha` re-encodes each
        // transaction canonically from the parsed struct, while this hashes the wire bytes. The
        // two coincide only because the decoders above reject every non-canonical encoding; see
        // `computeOpTxRoot`'s comment in OpEngineSeam.h for the full statement of that
        // dependency (this is the second copy of the same claim — keep them in step). The trie
        // construction itself moved to `OpEngineSeam.h`'s `computeOpTxRoot` (task-5b) because the
        // engine's newPayload OP branch must derive the same value *before* execution, for the
        // header reconstruction the blockHash check depends on — same function, two call sites, no
        // second implementation.
        const auto txRoot = computeOpTxRoot(rawTxBytes);

        std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts;
        receipts.reserve(result.receipts.size());
        for (auto const& receiptVariant : result.receipts)
        {
            const auto& innerReceipt = std::visit(
                [](const auto& r) -> const evmone::state::TransactionReceipt& { return r.receipt; },
                receiptVariant);
            receipts.push_back(mapOpReceipt(innerReceipt, m_receiptFactory));
        }

        co_return OpExecuteBlockResult{
            .receipts = std::move(receipts),
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
    evmc::VM m_vm;  // evmc_create_evmone(), one instance per scheduler (design §4.1).
};

}  // namespace bcos::evm::engine
