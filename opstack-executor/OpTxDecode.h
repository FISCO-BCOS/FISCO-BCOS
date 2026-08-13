// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OP raw-tx envelope decoders: the per-type tx decoders plus the canonical whole-envelope
// round-trip. Canonical re-encoding uses the production bcos-codec RLP encoder (encodeTuple in
// bcos-evm/eth/RlpEncodeTuple.h), so this layer carries no evmone test-header dependency.

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
// Byte-for-byte equivalents of the evmone test-only RLP encoder, rebuilt on the production
// bcos-codec encoder (helpers in bcos-evm/eth/RlpEncodeTuple.h). Equivalence is asserted by the
// golden corpus plus the whole-envelope assertCanonicalRoundTrip.

using bcos::evm::eth::detail::encodeRlp;
using bcos::evm::eth::detail::encodeTuple;

/// DepositTx field order: [sourceHash, from, to, mint, value, gas, isSystemTransaction, data] —
/// no signature (`from` is explicit).
inline bcos::evm::opstack::DepositTx decodeDepositTx(bcos::bytes rawEntry)
{
    // Envelope 0x7E || rlp([...]); body starts at the list header.
    bcos::bytesRef body(rawEntry.data() + 1, rawEntry.size() - 1);
    auto listBody = enterList(body);
    bcos::evm::opstack::DepositTx dep;
    dep.source_hash = decodeHashField(listBody);
    dep.from = decodeAddressField(listBody);
    dep.to = decodeOptionalAddressField(listBody);
    // mint/value nilability: nil and a present-but-zero big.Int are RLP-indistinguishable (both
    // encode to the empty string), so decode as a plain scalar defaulting to 0.
    dep.mint = decodeU256Scalar(listBody);
    dep.value = decodeU256Scalar(listBody);
    dep.gas_limit =
        narrowGasLimit(decodeU64Scalar(listBody), "deposit.gas");
    dep.is_system_tx = decodeBoolField(listBody);
    dep.data = decodeBytesField(listBody);
    expectExhausted(listBody, "deposit envelope fields");
    expectExhausted(body, "deposit envelope (trailing bytes after the field list)");
    return dep;
}

/// EIP-1559 (type 0x02): [chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to,
/// value, data, accessList, yParity, r, s].
///
/// `chainId` cross-check: nothing downstream uses chain id except the CHAINID opcode and EIP-7702
/// auth matching, so a tx signed for a different chain would replay unmodified here while op-geth
/// rejects it at decode time (ErrInvalidChainId).
inline bcos::evm::opstack::OpBlockTx decodeEip1559Tx(bcos::bytes rawEntry, uint64_t chainId)
{
    const evmc::bytes fullEnvelope(rawEntry.begin(), rawEntry.end());
    // Envelope 0x02 || rlp([...]).
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
    // op-geth rejects yParity > 1 ("invalid y parity"); without this it truncates to a
    // bogus-but-plausible 0/1 and any nonzero value would be treated as parity=1.
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
/// to, value, data, accessList, authorizationList, yParity, r, s]. `chainId` cross-check: same
/// rationale as decodeEip1559Tx (authorization tuples carry their own chain_id, checked inside
/// evmone, and are unaffected).
inline bcos::evm::opstack::OpBlockTx decodeSetCodeTx(bcos::bytes rawEntry, uint64_t chainId)
{
    const evmc::bytes fullEnvelope(rawEntry.begin(), rawEntry.end());
    // Envelope 0x04 || rlp([...]).
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
/// accessList, yParity, r, s]. Single `gasPrice` is modeled as max_gas_price with
/// max_priority_gas_price equal (priority<=cap). `chainId` cross-check: same as decodeEip1559Tx.
inline bcos::evm::opstack::OpBlockTx decodeAccessListTx(bcos::bytes rawEntry, uint64_t chainId)
{
    const evmc::bytes fullEnvelope(rawEntry.begin(), rawEntry.end());
    // Envelope 0x01 || rlp([...]).
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
/// value, data, v, r, s]. Handles both forms:
///   * pre-EIP-155: v ∈ {27,28}, parity = v-27, preimage = rlp([...6 fields]);
///   * EIP-155: v = chainId*2 + 35 + parity, preimage = rlp([...6 fields, chainId, 0, 0]).
/// The derived chainId must equal the scheduler's chainId. `v` is read as a canonical scalar so
/// v-canonicality matches the other types; the full v lives in signedEnvelope (txRoot hashes the
/// raw bytes), only parity/chainId are extracted here.
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

/// Re-encode a decoded OpBlockTx canonically (production bcos-codec RLP) so the caller can
/// byte-compare with the raw envelope: deposits as 0x7e || rlp([...8 fields]); legacy reconstructs
/// the full v (pre-155 27+parity, EIP-155 chainId*2+35+parity) that tx.v's uint8 cannot hold; the
/// typed forms round-trip through the type byte + field tuple.
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
/// original wire bytes, rejecting any mismatch. Fails closed on residual non-canonicality,
/// keeping the txRoot equivalence a runtime-checked invariant; one cheap re-encode per tx.
inline void assertCanonicalRoundTrip(
    const bcos::bytes& rawEntry, const bcos::evm::opstack::OpBlockTx& decoded)
{
    auto reencoded = canonicalEnvelopeBytes(decoded);
    if (reencoded.size() != rawEntry.size() ||
        !std::equal(reencoded.begin(), reencoded.end(), rawEntry.begin()))
        throw OpConsensusError(
            "OpSchedulerImpl: raw tx decode: non-canonical envelope (re-encode mismatch)");
}

/// Dispatch on the EIP-2718 type byte: deposit (0x7e), access-list (0x01), eip1559 (0x02),
/// set-code (0x04), legacy (first byte >= 0xc0). Blob (0x03) stays out — L2 blocks carry no blob
/// txs. Free function in `detail` so the `chainId` param never touches OpSchedulerImpl's
/// member-function signatures (which the generic composition root instantiates).
inline bcos::evm::opstack::OpBlockTx decodeOneRawTx(bcos::bytes rawEntry, uint64_t chainId)
{
    if (rawEntry.empty())
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: empty envelope");
    constexpr uint8_t kDepositTypeByte = 0x7e;     // DepositTxHandler::encode type byte
    constexpr uint8_t kAccessListTypeByte = 0x01;  // evmone::state::Transaction::Type::access_list
    constexpr uint8_t kEip1559TypeByte = 0x02;     // evmone::state::Transaction::Type::eip1559
    constexpr uint8_t kSetCodeTypeByte = 0x04;     // evmone::state::Transaction::Type::set_code
    constexpr uint8_t kRlpListBase = 0xc0;  // first byte >= 0xc0 is an untyped RLP list
    // Keep a copy of the wire bytes for the whole-envelope round-trip invariant; the sub-decoders
    // consume rawEntry as they parse.
    const bcos::bytes original = rawEntry;
    const auto typeByte = rawEntry[0];
    auto decoded = [&]() -> bcos::evm::opstack::OpBlockTx {
        if (typeByte >= kRlpListBase)  // legacy: no type byte, the envelope is the RLP list itself
            return decodeLegacyTx(std::move(rawEntry), chainId);
        if (typeByte == kDepositTypeByte)
            return bcos::evm::opstack::OpBlockTx{
                .tx = decodeDepositTx(std::move(rawEntry)), .signedEnvelope = {}};
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
