/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief tars implementation for Transaction
 * @file TransactionImpl.cpp
 * @author: ancelmo
 * @date 2021-04-20
 */

#include "TransactionImpl.h"
#include "../impl/TarsHashable.h"
#include "../impl/TarsSerializable.h"
#include "Web3RawTransaction.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-concepts/Hash.h>
#include <bcos-concepts/Serialize.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-rlp-protocol/Web3TxEnvelope.h>
#include <bcos-utilities/BoostLog.h>
#include <boost/throw_exception.hpp>
#include <cstring>
#include <exception>
#include <set>

DERIVE_BCOS_EXCEPTION(EmptyTransactionHash);

// EIP-2718 deposit transaction type byte (OP Stack). Matches
// rpc::TransactionType::Deposit in bcos-rpc; defined here as a local literal because
// bcos-tars-protocol sits below bcos-rpc and must not depend on it.
constexpr uint8_t kDepositTxType = 0x7e;

#define WEB3_ACCESS_LIST_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("WEB3_ACCESS_LIST")

bcostars::protocol::TransactionImpl::TransactionImpl(std::function<bcostars::Transaction*()> inner)
  : m_inner(std::move(inner))
{}
bcostars::protocol::TransactionImpl::TransactionImpl()
  : m_inner([m_transaction = bcostars::Transaction()]() mutable {
        return std::addressof(m_transaction);
    })
{}

bool bcostars::protocol::TransactionImpl::operator==(const Transaction& rhs) const
{
    return this->hash() == rhs.hash();
}

void bcostars::protocol::TransactionImpl::decode(bcos::bytesConstRef _txData)
{
    bcos::concepts::serialize::decode(_txData, *m_inner());
}

void bcostars::protocol::TransactionImpl::encode(bcos::bytes& txData) const
{
    bcos::concepts::serialize::encode(*m_inner(), txData);
}

bcos::crypto::HashType bcostars::protocol::TransactionImpl::hash() const
{
    if (m_inner()->dataHash.empty() && m_inner()->extraTransactionHash.empty())
    {
        throwTrace(EmptyTransactionHash{});
    }

    if (type() == static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        bcos::crypto::HashType hashResult((bcos::byte*)m_inner()->extraTransactionHash.data(),
            m_inner()->extraTransactionHash.size());
        return hashResult;
    }
    bcos::crypto::HashType hashResult(
        (bcos::byte*)m_inner()->dataHash.data(), m_inner()->dataHash.size());

    return hashResult;
}

bcos::bytes bcostars::protocol::reassembleWeb3RawTransaction(
    bcos::bytesConstRef payload, bcos::bytesConstRef signature)
{
    // The byte-splice logic mirrors Web3Transaction::encode() / txHash() in
    // bcos-rpc/bcos-rpc/web3jsonrpc/model/Web3Transaction.h (the reference implementation used
    // on the RPC ingress path). Keep the two in sync when adding new transaction types.
    //
    // NB: rlp free functions are fully qualified below -- TransactionImpl has member encode()/
    // decode() that would otherwise shadow bcos::codec::rlp::encode()/decode() in this scope.

    // Signature wire format (tars): r(32) || s(32) || yParity(1).
    if (signature.size() != 65) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument(
            "invalid Web3 signature length, expect 65, got " + std::to_string(signature.size())));
    }
    // RLP encodes integers with no leading zeros, so trim r/s before re-emitting them (this is
    // exactly what Web3Transaction::encode() does via getSignatureRef()).
    auto trimLeadingZeros = [](bcos::bytesConstRef in) {
        size_t offset = 0;
        while (offset < in.size() && in[offset] == 0)
        {
            ++offset;
        }
        return in.getCroppedData(offset);
    };
    auto const r = trimLeadingZeros(signature.getCroppedData(0, 32));
    auto const s = trimLeadingZeros(signature.getCroppedData(32, 32));
    auto const yParity = static_cast<uint64_t>(signature[64]);

    auto throwDecode = [](std::string_view stage) {
        BCOS_LOG(INFO) << LOG_DESC("reassemble raw Web3 transaction: decode failed")
                       << LOG_KV("stage", stage);
        BOOST_THROW_EXCEPTION(std::invalid_argument(
            std::string("reassemble raw Web3 transaction: decode failed at ").append(stage)));
    };
    if (payload.empty()) [[unlikely]]
    {
        throwDecode("empty payload");
    }

    // decodeHeader crops the header off the cursor as it parses, so work over a mutable copy of
    // the preimage bytes -- the underlying bytes are only read, never written.
    bcos::bytes buffer(payload.begin(), payload.end());
    bcos::bytesRef cursor(buffer.data(), buffer.size());

    bcos::bytes full;
    auto const firstByte = buffer[0];
    if (firstByte > 0 && firstByte < bcos::codec::rlp::BYTES_HEAD_BASE)
    {
        // Typed transaction (EIP-2718: EIP-2930 / EIP-1559 / EIP-4844).
        //   preimage = type || rlp([...fields])
        //   full     = type || rlp([...fields, yParity, r, s])
        // Every field byte is identical between the two; only the outer list header grows (it now
        // covers three extra items) and the signature items are appended. So we reuse the field
        // bytes verbatim and re-emit just the header + signature.
        auto const txType = firstByte;
        cursor = cursor.getCroppedData(1);  // drop the EIP-2718 type byte
        auto [error, header] = bcos::codec::rlp::decodeHeader(cursor);
        if (error || !header.isList || header.payloadLength > cursor.size()) [[unlikely]]
        {
            throwDecode("typed body");
        }
        bcos::bytesConstRef fields(cursor.data(), header.payloadLength);

        bcos::bytes sig;
        bcos::codec::rlp::encode(sig, yParity);  // typed txs carry the raw yParity (0/1), not an
                                                 // EIP-155 v
        bcos::codec::rlp::encode(sig, r);
        bcos::codec::rlp::encode(sig, s);

        full.push_back(txType);
        bcos::codec::rlp::encodeHeader(full,
            bcos::codec::rlp::Header{.isList = true, .payloadLength = fields.size() + sig.size()});
        full.insert(full.end(), fields.begin(), fields.end());
        full.insert(full.end(), sig.begin(), sig.end());
    }
    else
    {
        // Legacy transaction.
        //   pre-EIP-155 preimage = rlp([nonce,gasPrice,gasLimit,to,value,data])            (6
        //   items) EIP-155     preimage = rlp([nonce,gasPrice,gasLimit,to,value,data,chainId,0,0])
        //   (9 items) full                 = rlp([nonce,gasPrice,gasLimit,to,value,data,v,r,s])
        // The 6 leading field items are identical between preimage and full. EIP-155 replaces its
        // trailing chainId,0,0 with v,r,s (v = chainId*2+35+yParity); pre-155 simply appends v,r,s
        // (v = yParity+27). We locate the end of the 6 field items, reuse those bytes, and emit a
        // fresh list header + v,r,s.
        auto [error, header] = bcos::codec::rlp::decodeHeader(cursor);
        if (error || !header.isList || header.payloadLength > cursor.size()) [[unlikely]]
        {
            throwDecode("legacy header");
        }
        auto const* fieldsStart = cursor.data();
        bcos::bytesRef walker(cursor.data(), header.payloadLength);
        for (int i = 0; i < 6; ++i)
        {
            auto [fieldError, fieldHeader] = bcos::codec::rlp::decodeHeader(walker);
            if (fieldError || fieldHeader.payloadLength > walker.size()) [[unlikely]]
            {
                throwDecode("legacy field");
            }
            walker = walker.getCroppedData(fieldHeader.payloadLength);
        }
        auto const fieldsLength = static_cast<size_t>(walker.data() - fieldsStart);
        bcos::bytesConstRef fields(fieldsStart, fieldsLength);

        uint64_t v = 0;
        if (fieldsLength == header.payloadLength)
        {
            // pre-EIP-155: exactly 6 items, nothing trailing
            v = yParity + 27;
        }
        else
        {
            // EIP-155: item 7 is the signed chainId (items 8,9 are the 0,0 placeholders). Read
            // chainId from the preimage itself -- that is the value the sender actually signed, so
            // it is authoritative even though the whole tx arrived from an untrusted peer.
            uint64_t chainId = 0;
            if (auto chainIdError = bcos::codec::rlp::decode(walker, chainId)) [[unlikely]]
            {
                throwDecode("legacy chainId");
            }
            // The preimage must end with exactly chainId,0,0 -- reject 7/8-item lists and
            // non-zero trailers rather than misreading item 7 of some other shape as a chainId.
            for (int i = 0; i < 2; ++i)
            {
                uint64_t zero = 0;
                if (auto zeroError = bcos::codec::rlp::decode(walker, zero); zeroError || zero != 0)
                    [[unlikely]]
                {
                    throwDecode("legacy trailing zeros");
                }
            }
            if (!walker.empty()) [[unlikely]]
            {
                throwDecode("legacy trailing garbage");
            }
            v = chainId * 2 + 35 + yParity;
        }

        bcos::bytes sig;
        bcos::codec::rlp::encode(sig, v);
        bcos::codec::rlp::encode(sig, r);
        bcos::codec::rlp::encode(sig, s);

        bcos::codec::rlp::encodeHeader(full,
            bcos::codec::rlp::Header{.isList = true, .payloadLength = fields.size() + sig.size()});
        full.insert(full.end(), fields.begin(), fields.end());
        full.insert(full.end(), sig.begin(), sig.end());
    }
    return full;
}

void bcostars::protocol::TransactionImpl::calculateHash(const bcos::crypto::Hash& hashImpl)
{
    // Web3: the hash is the canonical txHash = keccak256(rlp(signed tx)), stored in
    // extraTransactionHash (which hash() returns). Recompute it from the signed payload
    // unconditionally -- a wire-supplied value is never believed, even when a caller reaches
    // verify() without clearing it first (e.g. TransactionFactoryImpl::createTransaction skips
    // the hash-match check for non-BCOS types), so no caller discipline is required (FIB-New1).
    // The recompute is a byte splice plus one keccak, cheap enough to always run.
    if (type() == static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        // Deposit (0x7e): unsigned — extraTransactionBytes already IS the full 0x7e envelope
        // (stored by takeToTarsTransaction as encode()), so the hash is keccak of it verbatim;
        // reassembleWeb3RawTransaction cannot be used (it needs a 65-byte signature).
        // The deposit determination comes from the SIGNED envelope's first byte, NEVER the
        // forgeable web3TypedTxKind mirror (tars field 12): a peer can rewrite that mirror to
        // 0x7e on a signed Web3 tx, which would route the hash to this unsigned-deposit form
        // and skip reassembleWeb3RawTransaction — defeating the "never believe the wire hash"
        // defense (kyonRay R4 #1). The mirror is display-only; security decisions key on the
        // envelope.
        auto const extraBytes = extraTransactionBytes();
        bool const isDepositEnvelope =
            (!extraBytes.empty() && extraBytes[0] == static_cast<bcos::byte>(0x7e));
        if (isDepositEnvelope)
        {
            auto const depositHash = bcos::crypto::keccak256Hash(extraBytes);
            m_inner()->extraTransactionHash.assign(depositHash.begin(), depositHash.end());
            return;
        }
        auto const canonicalTxHash = bcos::crypto::keccak256Hash(
            bcos::ref(reassembleWeb3RawTransaction(extraTransactionBytes(), signatureData())));
        m_inner()->extraTransactionHash.assign(canonicalTxHash.begin(), canonicalTxHash.end());
        return;
    }
    bcos::concepts::hash::calculate(*m_inner(), hashImpl.hasher(), m_inner()->dataHash);
}

std::string_view bcostars::protocol::TransactionImpl::nonce() const
{
    return m_inner()->data.nonce;
}

bcos::bytesConstRef bcostars::protocol::TransactionImpl::input() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->data.input.data()),
        m_inner()->data.input.size()};
}
int32_t bcostars::protocol::TransactionImpl::version() const
{
    return m_inner()->data.version;
}
std::string_view bcostars::protocol::TransactionImpl::chainId() const
{
    return m_inner()->data.chainID;
}
std::string_view bcostars::protocol::TransactionImpl::groupId() const
{
    return m_inner()->data.groupID;
}
int64_t bcostars::protocol::TransactionImpl::blockLimit() const
{
    return m_inner()->data.blockLimit;
}
void bcostars::protocol::TransactionImpl::setNonce(std::string nonce)
{
    m_inner()->data.nonce = std::move(nonce);
}
std::string_view bcostars::protocol::TransactionImpl::to() const
{
    return m_inner()->data.to;
}
std::string_view bcostars::protocol::TransactionImpl::abi() const
{
    return m_inner()->data.abi;
}

bcos::u256 bcostars::protocol::TransactionImpl::value() const
{
    return bcos::hex2u(m_inner()->data.value);
}

std::optional<bcos::u256> bcostars::protocol::TransactionImpl::gasPrice() const
{
    if (m_inner()->data.gasPrice.empty())
    {
        return std::nullopt;
    }
    return bcos::hex2u(m_inner()->data.gasPrice);
}

int64_t bcostars::protocol::TransactionImpl::gasLimit() const
{
    return m_inner()->data.gasLimit;
}

std::optional<bcos::u256> bcostars::protocol::TransactionImpl::maxFeePerGas() const
{
    if (m_inner()->data.maxFeePerGas.empty())
    {
        return std::nullopt;
    }
    return bcos::hex2u(m_inner()->data.maxFeePerGas);
}

std::optional<bcos::u256> bcostars::protocol::TransactionImpl::maxPriorityFeePerGas() const
{
    if (m_inner()->data.maxPriorityFeePerGas.empty())
    {
        return std::nullopt;
    }
    return bcos::hex2u(m_inner()->data.maxPriorityFeePerGas);
}

bcos::bytesConstRef bcostars::protocol::TransactionImpl::extension() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->data.extension.data()),
        m_inner()->data.extension.size()};
}

int64_t bcostars::protocol::TransactionImpl::importTime() const
{
    return m_inner()->importTime;
}
void bcostars::protocol::TransactionImpl::setImportTime(int64_t _importTime)
{
    m_inner()->importTime = _importTime;
}
bcos::bytesConstRef bcostars::protocol::TransactionImpl::signatureData() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->signature.data()),
        m_inner()->signature.size()};
}
std::string_view bcostars::protocol::TransactionImpl::sender() const
{
    return {m_inner()->sender.data(), m_inner()->sender.size()};
}
void bcostars::protocol::TransactionImpl::forceSender(const bcos::bytes& _sender)
{
    if (!tainted())
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument("sender of clean transaction is immutable"));
    }
    m_inner()->sender.assign(_sender.begin(), _sender.end());
}
void bcostars::protocol::TransactionImpl::clearSenderAndHash()
{
    m_inner()->sender.clear();
    m_inner()->dataHash.clear();
    // FIB-New1: also drop the wire-supplied canonical Web3 txHash (extraTransactionHash) so
    // verify() recomputes it from the signed payload. Both re-verification call sites are
    // untrusted enough to warrant this: the P2P import path (TransactionSync) receives it from an
    // untrusted peer, and the RPC submit path (TxValidator::verify) only pre-wrote a value it can
    // cheaply recompute anyway. Harmless for BCOS transactions (the field is never populated).
    m_inner()->extraTransactionHash.clear();
    setTainted(true);
}

void bcostars::protocol::TransactionImpl::setSignatureData(bcos::bytes& signature)
{
    m_inner()->signature.assign(signature.begin(), signature.end());
}
int32_t bcostars::protocol::TransactionImpl::attribute() const
{
    return m_inner()->attribute;
}
void bcostars::protocol::TransactionImpl::setAttribute(int32_t attribute)
{
    m_inner()->attribute |= attribute;
}
std::string_view bcostars::protocol::TransactionImpl::extraData() const
{
    return m_inner()->extraData;
}
uint8_t bcostars::protocol::TransactionImpl::type() const
{
    return static_cast<uint8_t>(m_inner()->type);
}
bcos::bytesConstRef bcostars::protocol::TransactionImpl::extraTransactionBytes() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner()->extraTransactionBytes.data()),
        m_inner()->extraTransactionBytes.size()};
}

uint8_t bcostars::protocol::TransactionImpl::web3TypedTxKind() const
{
    if (type() != static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        return 0;
    }
    return static_cast<uint8_t>(m_inner()->web3TypedTxKind);
}

std::optional<uint64_t> bcostars::protocol::TransactionImpl::web3ChainIdFromEnvelope() const
{
    // chainId admission must come from the SIGNED envelope, not the tars mirror (data.chainID):
    // the signature binds only extraTransactionBytes + signatureData. extraTransactionBytes is
    // the signing preimage: typed = type byte || rlp([chainId, ...]); legacy = 6 fields or
    // [...6 fields, chainId, 0, 0]. nullopt means a pre-EIP-155 legacy preimage (no chainId
    // tail) — a malformed tail (unparseable field 7) also yields nullopt, but is unreachable
    // through TxValidator: verify() rejects the same bytes first via reassembleWeb3RawTransaction
    // (the walker is shared with the block path — see Web3TxEnvelope.h — keep one home).
    if (type() != static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
    {
        return std::nullopt;
    }
    return bcos::rlp::protocol::web3ChainIdFromEnvelope(extraTransactionBytes());
}

std::string_view bcostars::protocol::TransactionImpl::sourceHash() const
{
    // Unprefixed hex (the asymmetry with mint()'s "0x"+hex is by design: sourceHash is a hash
    // string, mint is a numeric value). Consumers output it directly or parse with fromHex; do
    // not assume a prefix.
    return m_inner()->sourceHash;
}

bcos::u256 bcostars::protocol::TransactionImpl::mint() const
{
    if (m_inner()->mint.empty())
    {
        return 0;
    }
    // Written as "0x"+hex by takeToTarsTransaction, but corrupted data or external writes
    // may lack the prefix. bcos::u256("100") without 0x-parses as decimal 100, not 0x100=256
    // (a silent value error for a value-bearing field). Always force a 0x prefix so the
    // identity "mint stored = mint parsed" holds regardless of input form.
    // Invalid hex from corrupt data must not throw through the const getter — the length
    // guard handles over-wide values (u256 uses boost unchecked backend which silently
    // truncates >256 bits), and try/catch handles remaining corrupt/non-hex input; both
    // fall back to 0, consistent with the empty-string case.
    // IMPORTANT: the tars mirror is display-only and unauthenticated — the signature binds
    // only extraTransactionBytes; execution MUST re-derive mint from the envelope, never
    // trust this value from an untrusted peer (see Transaction.tars field 14).
    try
    {
        auto const& s = m_inner()->mint;
        // bcos::u256 (boost unchecked backend) silently truncates >256-bit values rather
        // than throwing — catch over-wide hex explicitly before the parse: with a 0x/0X
        // prefix, valid is at most 66 chars (2 prefix + 64 hex digits); without, at most 64.
        auto const hasPrefix = s.starts_with("0x") || s.starts_with("0X");
        auto const hexLen = hasPrefix ? s.size() - 2 : s.size();
        if (hexLen > 64)
        {
            return 0;
        }
        return bcos::u256(hasPrefix ? s : ("0x" + s));
    }
    catch (std::exception const&)
    {
        return 0;
    }
}

bool bcostars::protocol::TransactionImpl::isDepositTx() const
{
    // Use web3TypedTxKind() == 0x7e, NOT isSystemTransaction: isSystemTransaction is a
    // per-transaction flag, so a non-system deposit (isSystemTx=false, the vast majority) would
    // be misclassified.
    // Also use the accessor (not the raw tars field): it returns 0 unless type()==Web3Transaction,
    // so a forged BCOS tx (type=0, web3TypedTxKind=0x7e) is never treated as a deposit.
    return web3TypedTxKind() == kDepositTxType;
}

bool bcostars::protocol::TransactionImpl::depositIsSystemTransaction() const
{
    // tars field 15 (optional byte) generates as tars::Char (0 when unset). Distinct from
    // Transaction::systemTx() (m_systemTx).
    return m_inner()->isSystemTransaction != 0;
}

bcos::protocol::Web3AccessList bcostars::protocol::TransactionImpl::web3AccessList() const
{
    if (type() != static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
        return {};
    auto const& entries = m_inner()->data.accessList;
    bcos::protocol::Web3AccessList result;
    result.reserve(entries.size());
    for (auto const& entry : entries)
    {
        bcos::protocol::Web3AccessListEntry out;
        try
        {
            out.account = bcos::toAddress(entry.account);
        }
        catch (std::exception const&)
        {
            WEB3_ACCESS_LIST_LOG(WARNING)
                << LOG_DESC("Skip access list entry with invalid account address")
                << LOG_KV("account", entry.account);
            continue;
        }
        out.storageKeys.reserve(entry.storageKeys.size());
        for (auto const& keyBytes : entry.storageKeys)
        {
            if (keyBytes.size() != bcos::h256::SIZE)
            {
                WEB3_ACCESS_LIST_LOG(WARNING)
                    << LOG_DESC("Skip access list storage key with invalid length")
                    << LOG_KV("account", entry.account) << LOG_KV("keySize", keyBytes.size())
                    << LOG_KV("expected", bcos::h256::SIZE);
                continue;
            }
            bcos::h256 key;
            std::memcpy(key.data(), keyBytes.data(), bcos::h256::SIZE);
            out.storageKeys.emplace_back(key);
        }
        result.emplace_back(std::move(out));
    }
    return result;
}

bcos::protocol::AuthorizationList bcostars::protocol::TransactionImpl::authorizationList() const
{
    if (type() != static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
        return {};
    auto const& entries = m_inner()->data.authorizationList;
    bcos::protocol::AuthorizationList result;
    result.reserve(entries.size());
    for (auto const& entry : entries)
    {
        bcos::protocol::Authorization auth;
        auth.chainId = entry.chainID;
        auth.nonce = entry.nonce;
        auth.v = entry.v;
        // EIP-7702 authorization entries are consensus-critical.
        // Fail-loud on malformed data instead of silently skipping
        // (which produces wrong state root vs Ethereum reference).
        // However, empty signer/address (from unrecoverable signatures in
        // EEST fixtures) must be allowed: use zero address, evmone will skip.
        auth.address = entry.address.empty() ? bcos::Address() : bcos::toAddress(entry.address);
        auth.signer = entry.signer.empty() ? bcos::Address() : bcos::toAddress(entry.signer);
        auth.r = bcos::hex2u(entry.r);
        auth.s = bcos::hex2u(entry.s);
        result.emplace_back(std::move(auth));
    }
    return result;
}

bcos::protocol::VersionedHashes bcostars::protocol::TransactionImpl::blobVersionedHashes() const
{
    if (type() != static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction))
        return {};
    auto const& entries = m_inner()->data.blobVersionedHashes;
    bcos::protocol::VersionedHashes result;
    result.reserve(entries.size());
    for (auto const& entry : entries)
    {
        if (entry.size() != 32)
        {
            BOOST_THROW_EXCEPTION(std::runtime_error(
                "blobVersionedHash must be exactly 32 bytes, got " + std::to_string(entry.size())));
        }
        bcos::h256 h{};
        std::copy_n(entry.begin(), 32, h.begin());
        result.emplace_back(std::move(h));
    }
    return result;
}

std::optional<bcos::u256> bcostars::protocol::TransactionImpl::maxFeePerBlobGas() const
{
    if (m_inner()->data.maxFeePerBlobGas.empty())
        return std::nullopt;
    auto val = bcos::hex2u(m_inner()->data.maxFeePerBlobGas);
    // Defend against hex2u silently returning 0 for malformed input
    if (val == 0)
    {
        auto const& s = m_inner()->data.maxFeePerBlobGas;
        static const std::set<std::string_view> validZeros = {"0", "0x0", "0x00", "0x", "00"};
        if (!validZeros.count(s))
        {
            BOOST_THROW_EXCEPTION(std::runtime_error("Invalid maxFeePerBlobGas: " + s));
        }
    }
    return val;
}

const bcostars::Transaction& bcostars::protocol::TransactionImpl::inner() const
{
    return *m_inner();
}
bcostars::Transaction& bcostars::protocol::TransactionImpl::mutableInner()
{
    return *m_inner();
}
void bcostars::protocol::TransactionImpl::setInner(bcostars::Transaction inner)
{
    *m_inner() = std::move(inner);
}

size_t bcostars::protocol::TransactionImpl::size() const
{
    size_t size = 0;
    size += m_inner()->data.nonce.size();
    size += m_inner()->data.to.size();
    size += m_inner()->data.input.size();
    size += m_inner()->data.abi.size();
    size += m_inner()->data.value.size();
    size += m_inner()->data.gasPrice.size();
    size += m_inner()->data.maxFeePerGas.size();
    size += m_inner()->data.maxPriorityFeePerGas.size();
    size += m_inner()->data.extension.size();
    for (auto const& entry : m_inner()->data.accessList)
    {
        size += entry.account.size();
        for (auto const& key : entry.storageKeys)
        {
            size += key.size();
        }
    }
    size += m_inner()->signature.size();
    size += m_inner()->sender.size();
    size += m_inner()->extraData.size();
    size += m_inner()->extraTransactionBytes.size();
    size += m_inner()->extraTransactionHash.size();
    size += m_inner()->sourceHash.size();
    size += m_inner()->mint.size();
    // isSystemTransaction (optional byte) is a fixed-length scalar — excluded from
    // size() like other fixed scalars (type, version, blockLimit).
    return size;
}
