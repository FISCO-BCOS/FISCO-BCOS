// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OP raw-tx envelope decoders (split out of OpSchedulerImpl.h): the per-type
// tx decoders (deposit / eip1559 / setcode / access-list / legacy) plus the canonical
// whole-envelope round-trip. Canonical re-encoding uses the production bcos-codec RLP encoder
// (encodeTuple in bcos-evm/eth/RlpEncodeTuple.h — a byte-for-byte equivalent of evmone's test-only
// rlp::encode_tuple), so this layer carries no evmone test-header dependency.

#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpRlpDecode.h>
#include <opstack-executor/OpErrors.h>

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-evm/eth/RlpEncodeTuple.h>
#include <bcos-evm/eth/state/transaction.hpp>
#include <bcos-utilities/Common.h>
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <string>
#include <utility>
#include <variant>

namespace bcos::evm::engine::detail
{
// ---- canonical re-encode helpers (bcos-codec RLP) ----
//
// Byte-for-byte equivalents of the evmone *test-only* RLP encoder (test/utils/rlp.hpp +
// test/utils/rlp_encode.hpp), rebuilt on the production bcos-codec encoder so this production
// layer carries no evmone test-header dependency. The helpers now live in the shared header
// bcos-evm/eth/RlpEncodeTuple.h (bcos::evm::eth::detail); they are imported here so the decoders
// below keep calling them unqualified. Equivalence to evmone is asserted by the 125-vector
// golden corpus plus the whole-envelope `assertCanonicalRoundTrip` below.

using bcos::evm::eth::detail::encodeRlp;
using bcos::evm::eth::detail::encodeTuple;

/// DepositTx (OpDepositTx.h field order, cross-checked against the encode side,
/// bcos-rpc TxHandler.cpp DepositTxHandler::encode): [sourceHash, from, to, mint, value, gas,
/// isSystemTransaction, data] — no signature (`from` is explicit); `signedEnvelope` stays
/// empty per OpBlockExecute.h's OpBlockTx contract ("empty for deposit", OpBlockExecute.h:18).
inline bcos::evm::opstack::OpBlockTx decodeDepositTx(bcos::bytes rawEntry)
{
    // envelope shape: 0x7E || rlp([sourceHash, from, to, mint, value, gas, isSystemTransaction,
    // data]) — `body` starts at the LIST HEADER, not at the first field (golden bytes are
    // literally `0x7e f9…`); enterList() consumes that header and
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
        narrowGasLimit(decodeU64Scalar(listBody), "deposit.gas");
    dep.is_system_tx = decodeBoolField(listBody);
    dep.data = decodeBytesField(listBody);
    expectExhausted(listBody, "deposit envelope fields");
    expectExhausted(body, "deposit envelope (trailing bytes after the field list)");
    return bcos::evm::opstack::OpBlockTx{.tx = std::move(dep), .signedEnvelope = {}};
}

/// EIP-1559 (type 0x02): [chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to,
/// value, data, accessList, yParity, r, s] — field order per rlp_encode.cpp:43-47.
///
/// `chainId` (the scheduler's own chain id, threaded in from its `m_chainId` via
/// `decodeOneRawTx`) is compared against the decoded `tx.chain_id` immediately below: nothing
/// downstream (`OpValidate.cpp` -> `validate_transaction`,
/// eth/state/state.cpp:420-529) checks chain id at all — it is used only for the CHAINID opcode
/// and EIP-7702 authorization matching — so a transaction signed for a *different* chain replays
/// unmodified here (ecrecover succeeds, sender is correct, execution is normal, block is VALID),
/// while op-geth's own signer rejects it at decode time (`ErrInvalidChainId`,
/// transaction_signing.go:284-285) and the whole block never reaches execution.
inline bcos::evm::opstack::OpBlockTx decodeEip1559Tx(bcos::bytes rawEntry, uint64_t chainId)
{
    const evmc::bytes fullEnvelope(rawEntry.begin(), rawEntry.end());
    // envelope shape: 0x02 || rlp([...]) — same list-header fix as decodeDepositTx.
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
        narrowGasLimit(decodeU64Scalar(listBody), "eip1559.gasLimit");
    tx.to = decodeOptionalAddressField(listBody);
    tx.value = decodeU256Scalar(listBody);
    tx.data = decodeBytesField(listBody);
    tx.access_list = decodeAccessList(listBody);
    const auto yParity = decodeU256Scalar(listBody);
    // op-geth rejects yParity > 1 as "invalid y parity" before it ever reaches sender recovery;
    // without this check a >1 value gets silently truncated by
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
        evmc::bytes{0x02} + encodeTuple(tx.chain_id, tx.nonce, tx.max_priority_gas_price,
                                tx.max_gas_price, static_cast<uint64_t>(tx.gas_limit),
                                toAddressView(tx.to), tx.value, tx.data, tx.access_list);
    tx.sender = recoverTxSender(signingPreimage, yParity, tx.r, tx.s);

    return bcos::evm::opstack::OpBlockTx{.tx = std::move(tx), .signedEnvelope = fullEnvelope};
}

/// EIP-7702 set-code (type 0x04): [chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit,
/// to, value, data, accessList, authorizationList, yParity, r, s] — field order per
/// rlp_encode.cpp:66-70. `chainId` cross-check: see decodeEip1559Tx's comment (same rationale,
/// same outer-tx-only scope — EIP-7702 authorization tuples carry their own independent chain_id
/// field, already checked inside evmone::state::process_authorization_list, and are unaffected).
inline bcos::evm::opstack::OpBlockTx decodeSetCodeTx(bcos::bytes rawEntry, uint64_t chainId)
{
    const evmc::bytes fullEnvelope(rawEntry.begin(), rawEntry.end());
    // envelope shape: 0x04 || rlp([...]) — same list-header fix as decodeDepositTx.
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
        narrowGasLimit(decodeU64Scalar(listBody), "setcode.gasLimit");
    tx.to = decodeOptionalAddressField(listBody);
    tx.value = decodeU256Scalar(listBody);
    tx.data = decodeBytesField(listBody);
    tx.access_list = decodeAccessList(listBody);
    tx.authorization_list = decodeAuthorizationList(listBody);
    const auto yParity = decodeU256Scalar(listBody);
    // See the matching check in decodeEip1559Tx — same op-geth "invalid y parity" rejection,
    // same truncation/mod-256 hazard if left unchecked.
    if (yParity > 1)
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: invalid y parity (setcode)");
    tx.r = decodeU256Scalar(listBody);
    tx.s = decodeU256Scalar(listBody);
    expectExhausted(listBody, "setcode envelope fields");
    expectExhausted(body, "setcode envelope (trailing bytes after the field list)");
    tx.v = static_cast<uint8_t>(yParity);

    const auto signingPreimage =
        evmc::bytes{0x04} + encodeTuple(tx.chain_id, tx.nonce, tx.max_priority_gas_price,
                                tx.max_gas_price, static_cast<uint64_t>(tx.gas_limit),
                                toAddressView(tx.to), tx.value, tx.data, tx.access_list,
                                tx.authorization_list);
    tx.sender = recoverTxSender(signingPreimage, yParity, tx.r, tx.s);

    return bcos::evm::opstack::OpBlockTx{.tx = std::move(tx), .signedEnvelope = fullEnvelope};
}

/// EIP-2930 access-list tx (type 0x01): [chainId, nonce, gasPrice, gasLimit, to, value, data,
/// accessList, yParity, r, s] — field order per rlp_encode.cpp:29-37. Legacy/access-list carry a
/// single `gasPrice`, which evmone's Transaction models as `max_gas_price` with
/// `max_priority_gas_price` set equal (validate_transaction asserts priority<=cap; state.cpp:466).
/// `chainId` cross-check: same rationale/scope as decodeEip1559Tx.
inline bcos::evm::opstack::OpBlockTx decodeAccessListTx(bcos::bytes rawEntry, uint64_t chainId)
{
    const evmc::bytes fullEnvelope(rawEntry.begin(), rawEntry.end());
    // envelope shape: 0x01 || rlp([...]) — same list-header handling as decodeEip1559Tx.
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
    if (yParity > 1)  // Same op-geth "invalid y parity" rejection as the typed decoders.
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: invalid y parity (access_list)");
    tx.r = decodeU256Scalar(listBody);
    tx.s = decodeU256Scalar(listBody);
    expectExhausted(listBody, "access_list envelope fields");
    expectExhausted(body, "access_list envelope (trailing bytes after the field list)");
    tx.v = static_cast<uint8_t>(yParity);

    const auto signingPreimage =
        evmc::bytes{0x01} + encodeTuple(tx.chain_id, tx.nonce, tx.max_gas_price,
                                static_cast<uint64_t>(tx.gas_limit), toAddressView(tx.to),
                                tx.value, tx.data, tx.access_list);
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
        signingPreimage = encodeTuple(tx.nonce, tx.max_gas_price,
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
        signingPreimage = encodeTuple(tx.nonce, tx.max_gas_price,
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
/// byte-for-byte with the raw envelope. Uses the production bcos-codec RLP encoder (encodeTuple —
/// byte-for-byte equivalent of evmone's canonical RLP) for every type: deposits are rebuilt as
/// `0x7e || rlp([...8 fields])` (the isSystemTransaction bool re-emits via the `uint64 0/1` shape,
/// whose RLP — empty string / 0x01 — matches the wire bool exactly); legacy reconstructs the full
/// v (pre-155 27+parity, EIP-155 chainId*2+35+parity) that `tx.v`'s uint8 cannot hold; the three
/// signed typed forms (0x01/0x02/0x04) round-trip through the type byte + encodeTuple field tuple
/// (field order identical to evmone::state::rlp_encode(const Transaction&)).
inline bcos::bytes canonicalEnvelopeBytes(const bcos::evm::opstack::OpBlockTx& btx)
{
    if (std::holds_alternative<bcos::evm::opstack::DepositTx>(btx.tx))
    {
        const auto& d = std::get<bcos::evm::opstack::DepositTx>(btx.tx);
        auto body = encodeTuple(evmc::bytes_view(d.source_hash), evmc::bytes_view(d.from),
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
        auto e = encodeTuple(tx.nonce, tx.max_gas_price,
            static_cast<uint64_t>(tx.gas_limit), toAddressView(tx.to), tx.value, tx.data, fullV,
            tx.r, tx.s);
        return {e.begin(), e.end()};
    }
    // 0x01/0x02/0x04 typed forms: EIP-2718 type byte + the full field tuple (incl. v,r,s) —
    // mirrors evmone::state::rlp_encode(const Transaction&). Blob (0x03) never reaches here:
    // decodeOneRawTx rejects it before decoding.
    evmc::bytes e;
    switch (tx.type)
    {
    case evmone::state::Transaction::Type::access_list:
        e = evmc::bytes{0x01} + encodeTuple(tx.chain_id, tx.nonce, tx.max_gas_price,
                                     static_cast<uint64_t>(tx.gas_limit), toAddressView(tx.to),
                                     tx.value, tx.data, tx.access_list, tx.v, tx.r, tx.s);
        break;
    case evmone::state::Transaction::Type::eip1559:
        e = evmc::bytes{0x02} + encodeTuple(tx.chain_id, tx.nonce, tx.max_priority_gas_price,
                                     tx.max_gas_price, static_cast<uint64_t>(tx.gas_limit),
                                     toAddressView(tx.to), tx.value, tx.data, tx.access_list, tx.v,
                                     tx.r, tx.s);
        break;
    case evmone::state::Transaction::Type::set_code:
        e = evmc::bytes{0x04} + encodeTuple(tx.chain_id, tx.nonce, tx.max_priority_gas_price,
                                     tx.max_gas_price, static_cast<uint64_t>(tx.gas_limit),
                                     toAddressView(tx.to), tx.value, tx.data, tx.access_list,
                                     tx.authorization_list, tx.v, tx.r, tx.s);
        break;
    default:
        throw OpConsensusError(
            "OpSchedulerImpl: canonical re-encode: unsupported typed transaction");
    }
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
    constexpr uint8_t kDepositTypeByte = 0x7e;     // DepositTxHandler::encode type byte
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
}  // namespace bcos::evm::engine::detail
