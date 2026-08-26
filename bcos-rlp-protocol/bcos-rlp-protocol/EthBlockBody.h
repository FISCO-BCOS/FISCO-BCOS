/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file EthBlockBody.h
 * @brief Ethereum block-body RLP codec
 *        (rlp([header, transactions, ommers/uncles, withdrawals?]) — EIP-4895 for withdrawals)
 * @date 2026/8/18
 */
#pragma once

#include "EthBlockHeader.h"
#include "EthWithdrawal.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <optional>
#include <vector>

namespace bcos::protocol
{
// Ethereum block (geth's types.Block): rlp([header, transactions, ommers]) — the
// ENTIRE block, header included. It is NOT the header-less devp2p BlockBodies element
// ([transactions, ommers, withdrawals?]); a body codec for that wire form belongs with
// the devp2p eth protocol.
// RLP form (yellow paper, Appendix B):
//   pre-Shanghai: rlp([header, transactions, ommers])
//   Shanghai+  :  rlp([header, transactions, ommers, withdrawals])   (EIP-4895)
// `transactions` holds the raw EIP-2718 encodings (opaque bytes); `ommers` are uncle headers
// (always empty on PoS chains); `withdrawals` is nullopt for pre-Shanghai bodies and an
// (possibly empty) list for Shanghai+ bodies — the presence of the field itself is fork
// significant, so it is an optional<vector>, not a bare vector.
struct EthBlockData
{
    EthBlockHeaderData header;
    std::vector<bcos::bytes> transactions;   // opaque EIP-2718 encoded transactions
    std::vector<EthBlockHeaderData> ommers;  // uncle headers (empty on PoS)
    std::optional<std::vector<EthWithdrawalData>> withdrawals;  // nullopt = pre-Shanghai

    bool operator==(const EthBlockData& rhs) const
    {
        return header == rhs.header && transactions == rhs.transactions && ommers == rhs.ommers &&
               withdrawals == rhs.withdrawals;
    }
    bool operator!=(const EthBlockData& rhs) const { return !(*this == rhs); }
};

// Class wrapper following the EthBlockHeader pattern: rlpEncode/rlpDecode plus the
// codec::rlp overloads that let EthBlockData work as an item in larger structures.
class EthBlock
{
public:
    EthBlock() = default;
    explicit EthBlock(EthBlockData data) : m_data(std::move(data)) {}

    // Encode the block via EthBlock::rlpEncode — that entry point enforces the decoder's
    // invariants (empty / too-short / invalid-type elements rejected) so no element is
    // silently dropped into a hash input. The codec::rlp::encode overload below performs no
    // such check.
    bcos::Error::UniquePtr rlpEncode(bcos::bytes& out) const;
    // Decodes a single block body (a 3- or 4-element list) from `data`.
    bcos::Error::UniquePtr rlpDecode(bcos::bytesConstRef data);

    const EthBlockData& data() const { return m_data; }
    EthBlockData& data() { return m_data; }

private:
    EthBlockData m_data;
};
}  // namespace bcos::protocol

namespace bcos::codec::rlp
{
namespace detail
{
// RLP length of one transaction inside a block-body transactions list, with the
// wire semantics geth uses: legacy txs (leading byte >= 0xc0 — already a complete
// RLP list) are spliced raw; typed txs (leading byte 0x01..0x7f) are wrapped in an
// RLP string (geth's Transaction.EncodeRLP). The generic bytes codec would wrap
// EVERY element as a string, which corrupts legacy txs, so the list is built by
// hand. (detail + inline: this is an installed public header, so an anonymous
// namespace here would give every TU its own copy and risk ODR violations.)
inline size_t txWireLength(bcos::bytes const& _tx) noexcept
{
    if (!_tx.empty() && _tx.front() < LIST_HEAD_BASE)
    {
        return length(bytesConstRef(_tx.data(), _tx.size()));
    }
    return _tx.size();
}

// RLP length of the complete transactions list element (header + payload).
inline size_t txListLength(std::vector<bcos::bytes> const& _txs) noexcept
{
    size_t payload = 0;
    for (auto const& tx : _txs)
    {
        payload += txWireLength(tx);
    }
    return lengthOfLength(payload) + payload;
}

// Encode the complete transactions list element (header + payload) directly into
// _out, splicing legacy txs raw and string-wrapping typed txs. txListLength above
// is the single length source so length()/encode() stay in agreement.
inline void encodeTxList(bcos::bytes& _out, std::vector<bcos::bytes> const& _txs) noexcept
{
    size_t payload = 0;
    for (auto const& tx : _txs)
    {
        payload += txWireLength(tx);
    }
    encodeHeader(_out, {.isList = true, .payloadLength = payload});
    _out.reserve(_out.size() + payload);
    for (auto const& tx : _txs)
    {
        if (!tx.empty() && tx.front() < LIST_HEAD_BASE)
        {
            encode(_out, bytesConstRef(tx.data(), tx.size()));
        }
        else
        {
            _out.insert(_out.end(), tx.begin(), tx.end());
        }
    }
}

// Decode one transaction from a block-body transactions list into its opaque
// EIP-2718 bytes: a typed tx (RLP string) loses its string prefix (0xNN||payload
// is returned, matching what encode wraps); a legacy tx (RLP list) is taken
// whole. Malformed input (e.g. a bare type byte 0x00) is rejected.
inline bcos::Error::UniquePtr decodeTx(bcos::bytesRef& _in, bcos::bytes& _out) noexcept
{
    if (_in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::InputTooShort, "empty tx stream");
    }
    if (_in[0] >= BYTES_HEAD_BASE && _in[0] < LIST_HEAD_BASE)
    {
        // Typed tx wrapped as an RLP string — return its content (0xNN||payload)
        // without the string prefix. The content must be non-empty and its first
        // byte a valid EIP-2718 type (0x00 is reserved; geth rejects both with
        // errShortTypedTx / ErrTxTypeNotSupported).
        if (auto err = decode(_in, _out))
        {
            return err;
        }
        // A real EIP-2718 typed transaction is `type || rlp(list of 8+ fields)`, so it is
        // never shorter than ~10 bytes; geth's decodeTyped rejects len(b) <= 1
        // (errShortTypedTx). This floor also caps the one-element-per-input-byte
        // amplification (a bare 0xc0 empty-list element is handled by the legacy arm).
        if (_out.size() < 10)
        {
            return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnsupportedTransactionType,
                "typed transaction too short in block body (errShortTypedTx)");
        }
        if (_out[0] == 0)
        {
            return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnsupportedTransactionType,
                "invalid EIP-2718 type byte 0x00 in block body");
        }
        if (_out[0] >= BYTES_HEAD_BASE)
        {
            // A content byte >= 0x80 would be classified as a legacy list on re-encode
            // (the encoder's discriminator is tx.front() < LIST_HEAD_BASE), so decode and
            // encode would not be the identity. EIP-2718 types are confined to 0x01..0x7f.
            return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnsupportedTransactionType,
                "EIP-2718 transaction type byte out of range in block body");
        }
        return nullptr;
    }
    if (_in[0] >= LIST_HEAD_BASE)
    {
        // Legacy tx: a standalone RLP list — take the whole element raw. Give it the
        // lower bound geth gets from its struct: a legacy transaction list has nine
        // fields, so a payload shorter than 9 bytes cannot be one (this also rejects the
        // bare 0xc0 empty-list element that would otherwise cause ~50x memory
        // amplification via one-element-per-input-byte).
        size_t const originalSize = _in.size();
        auto [error, header] = decodeHeader(_in);
        if (error)
        {
            return std::move(error);
        }
        if (header.payloadLength < 9)
        {
            return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedListElements,
                "legacy transaction element too short in block body");
        }
        size_t const prefixLen = originalSize - _in.size();
        size_t const payloadLen = header.payloadLength;
        bcos::byte const* begin = _in.data() - static_cast<std::ptrdiff_t>(prefixLen);
        _out.assign(begin, begin + prefixLen + payloadLen);
        _in = bcos::bytesRef(_in.data() + payloadLen, _in.size() - payloadLen);
        return nullptr;
    }
    // A bare single-byte element (0x00..0x7f) is not a valid EIP-2718 transaction.
    return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnsupportedTransactionType,
        "unsupported bare single-byte transaction element in block body");
}
}  // namespace detail

// Overloads so EthBlockData works as an item inside the generic list/vector codecs.
// The header codec (EthBlockHeaderData) is the single source of truth for the header
// field order; the transactions list is built by hand so legacy txs stay raw lists.
inline size_t length(const protocol::EthBlockData& _body) noexcept
{
    size_t const txsLen = detail::txListLength(_body.transactions);
    size_t payload = bcos::codec::rlp::length(_body.header) + txsLen + length(_body.ommers);
    if (_body.withdrawals.has_value())
    {
        payload += length(*_body.withdrawals);
    }
    return lengthOfLength(payload) + payload;
}
inline void encode(bcos::bytes& _out, const protocol::EthBlockData& _body) noexcept
{
    size_t const txsLen = detail::txListLength(_body.transactions);
    size_t payload = bcos::codec::rlp::length(_body.header) + txsLen + length(_body.ommers);
    if (_body.withdrawals.has_value())
    {
        payload += length(*_body.withdrawals);
    }
    encodeHeader(_out, {.isList = true, .payloadLength = payload});
    _out.reserve(_out.size() + payload);
    bcos::codec::rlp::encode(_out, _body.header);
    // Splice the transactions list in a single pass (header + elements).
    detail::encodeTxList(_out, _body.transactions);
    encode(_out, _body.ommers);
    if (_body.withdrawals.has_value())
    {
        encode(_out, *_body.withdrawals);
    }
}
inline bcos::Error::UniquePtr decode(bcos::bytesRef& _in, protocol::EthBlockData& _body) noexcept
{
    // The body is a list: [header, transactions, ommers, withdrawals?]. Consume
    // the body list header first; the header element is itself a list decoded by
    // the EthBlockHeaderData codec.
    auto [bodyErr, bodyHeader] = decodeHeader(_in);
    if (bodyErr)
    {
        return std::move(bodyErr);
    }
    if (!bodyHeader.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedString, "block body must be a list");
    }
    bytesRef items(_in.data(), bodyHeader.payloadLength);
    _in = bytesRef(_in.data() + bodyHeader.payloadLength, _in.size() - bodyHeader.payloadLength);

    if (auto err = bcos::codec::rlp::decode(items, _body.header))
    {
        return err;
    }
    // Transactions list: decode each element with legacy/typed semantics.
    auto [txErr, txHeader] = decodeHeader(items);
    if (txErr)
    {
        return std::move(txErr);
    }
    if (!txHeader.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedString, "block body txs must be a list");
    }
    bytesRef txPayload(items.data(), txHeader.payloadLength);
    _body.transactions.clear();
    // No reserve(payloadLength): payloadLength counts bytes, not elements (a
    // bcos::bytes element is 24 bytes on LP64), so it would be a 24x memory
    // amplification driven purely by the list header. push_back's amortised
    // growth is fine at these sizes.
    while (!txPayload.empty())
    {
        bcos::bytes tx;
        if (auto err = detail::decodeTx(txPayload, tx))
        {
            return err;
        }
        _body.transactions.push_back(std::move(tx));
    }
    items = bytesRef(items.data() + txHeader.payloadLength, items.size() - txHeader.payloadLength);

    if (auto err = decode(items, _body.ommers))
    {
        return err;
    }
    // The optional-withdrawals slot doubles as the pre-Shanghai marker: when the body
    // list is exhausted after header/transactions/ommers, withdrawals stays nullopt;
    // otherwise the fourth item is decoded as the withdrawals list.
    if (!items.empty())
    {
        std::vector<bcos::protocol::EthWithdrawalData> withdrawals;
        if (auto err = decode(items, withdrawals))
        {
            return err;
        }
        _body.withdrawals = std::move(withdrawals);
    }
    else
    {
        _body.withdrawals.reset();
    }
    // Fail closed on any trailing element: a 5-element body is malformed (geth's
    // Block.DecodeRLP rejects it with ListEnd), and silently dropping the extra
    // element would make the re-encoded form unfaithful to the wire.
    if (!items.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedListElements,
            "block body has trailing elements after withdrawals");
    }
    return nullptr;
}
}  // namespace bcos::codec::rlp

namespace bcos::protocol
{
// ADL-visible delegators (see EthLog.h): let EthBlockData participate in
// variadic-list encode/decode.
inline size_t length(const EthBlockData& _body) noexcept
{
    return codec::rlp::length(_body);
}
inline void encode(bcos::bytes& _out, const EthBlockData& _body) noexcept
{
    codec::rlp::encode(_out, _body);
}
inline bcos::Error::UniquePtr decode(bcos::bytesRef& _in, EthBlockData& _body) noexcept
{
    return codec::rlp::decode(_in, _body);
}
}  // namespace bcos::protocol
