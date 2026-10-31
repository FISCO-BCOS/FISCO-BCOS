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
 * @file EthBlockBody.cpp
 * @brief EthBlock — Ethereum block RLP codec implementation
 * @date 2026/8/18
 */
#include "EthBlockBody.h"

using namespace bcos;
using namespace bcos::codec::rlp;

namespace bcos::codec::rlp
{
namespace
{
// RLP payload length of all transaction elements, with the wire semantics geth uses:
// legacy txs (leading byte >= 0xc0 — already a complete RLP list) are spliced raw; typed
// txs (leading byte 0x01..0x7f) are wrapped in an RLP string (geth's Transaction.EncodeRLP).
// The generic bytes codec would wrap EVERY element as a string, which corrupts legacy txs,
// so the list is built by hand.
size_t txPayloadLength(std::vector<bcos::bytes> const& _txs) noexcept
{
    size_t payload = 0;
    for (auto const& tx : _txs)
    {
        payload += (!tx.empty() && tx.front() < LIST_HEAD_BASE) ?
                       length(bytesConstRef(tx.data(), tx.size())) :
                       tx.size();
    }
    return payload;
}

// Encode the complete transactions list element (header + payload) directly into
// _out, splicing legacy txs raw and string-wrapping typed txs. txPayloadLength is the
// single length source so length()/encode() stay in agreement.
void encodeTxList(bcos::bytes& _out, std::vector<bcos::bytes> const& _txs) noexcept
{
    size_t const payload = txPayloadLength(_txs);
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

// RLP payload length of the whole block body list (header + transactions + ommers +
// withdrawals?); shared by length() and encode() so the two cannot drift apart.
size_t bodyPayloadLength(protocol::EthBlockData const& _body) noexcept
{
    size_t const txPayload = txPayloadLength(_body.transactions);
    size_t payload = bcos::codec::rlp::length(_body.header) + lengthOfLength(txPayload) +
                     txPayload + length(_body.ommers);
    if (_body.withdrawals.has_value())
    {
        payload += length(*_body.withdrawals);
    }
    return payload;
}
}  // namespace

namespace detail
{
void decodeTx(bcos::bytesRef& _in, bcos::bytes& _out)
{
    if (_in.empty())
    {
        throwRlpDecodeError(DecodingError::InputTooShort, "empty tx stream");
    }
    if (_in[0] >= BYTES_HEAD_BASE && _in[0] < LIST_HEAD_BASE)
    {
        // Typed tx wrapped as an RLP string — return its content (0xNN||payload)
        // without the string prefix. The content must be non-empty and its first
        // byte a valid EIP-2718 type (0x00 is reserved; geth rejects both with
        // errShortTypedTx / ErrTxTypeNotSupported).
        decode(_in, _out);
        // A real EIP-2718 typed transaction is `type || rlp(list of 8+ fields)`, so it is
        // never shorter than ~10 bytes; geth's decodeTyped rejects len(b) <= 1
        // (errShortTypedTx). This floor also caps the one-element-per-input-byte
        // amplification (a bare 0xc0 empty-list element is handled by the legacy arm).
        if (_out.size() < 10)
        {
            throwRlpDecodeError(DecodingError::UnsupportedTransactionType,
                "typed transaction too short in block body (errShortTypedTx)");
        }
        // EIP-2718 types are confined to 0x01..0x7f: 0x00 is the reserved type byte, and
        // a content byte >= 0x80 would be classified as a legacy list on re-encode (the
        // encoder's discriminator is tx.front() < LIST_HEAD_BASE), so decode and encode
        // would not be the identity.
        if (_out[0] == 0 || _out[0] >= BYTES_HEAD_BASE)
        {
            throwRlpDecodeError(DecodingError::UnsupportedTransactionType,
                "EIP-2718 transaction type byte out of range 0x01..0x7f in block body");
        }
        return;
    }
    if (_in[0] >= LIST_HEAD_BASE)
    {
        // Legacy tx: a standalone RLP list — take the whole element raw. Give it the
        // lower bound geth gets from its struct: a legacy transaction list has nine
        // fields, so a payload shorter than 9 bytes cannot be one (this also rejects the
        // bare 0xc0 empty-list element that would otherwise cause ~50x memory
        // amplification via one-element-per-input-byte).
        auto* begin = _in.data();
        auto header = decodeHeader(_in);
        if (header.payloadLength < 9)
        {
            throwRlpDecodeError(DecodingError::UnexpectedListElements,
                "legacy transaction element too short in block body");
        }
        _out.assign(begin, _in.data() + header.payloadLength);
        _in = _in.getCroppedData(header.payloadLength);
        return;
    }
    // A bare single-byte element (0x00..0x7f) is not a valid EIP-2718 transaction.
    throwRlpDecodeError(DecodingError::UnsupportedTransactionType,
        "unsupported bare single-byte transaction element in block body");
}
}  // namespace detail

size_t length(const protocol::EthBlockData& _body) noexcept
{
    size_t const payload = bodyPayloadLength(_body);
    return lengthOfLength(payload) + payload;
}
void encode(bcos::bytes& _out, const protocol::EthBlockData& _body) noexcept
{
    size_t const payload = bodyPayloadLength(_body);
    encodeHeader(_out, {.isList = true, .payloadLength = payload});
    _out.reserve(_out.size() + payload);
    bcos::codec::rlp::encode(_out, _body.header);
    // Splice the transactions list in a single pass (header + elements).
    encodeTxList(_out, _body.transactions);
    encode(_out, _body.ommers);
    if (_body.withdrawals.has_value())
    {
        encode(_out, *_body.withdrawals);
    }
}
void decode(bcos::bytesRef& _in, protocol::EthBlockData& _body)
{
    // The body is a list: [header, transactions, ommers, withdrawals?]. Consume
    // the body list header first; the header element is itself a list decoded by
    // the EthBlockHeaderData codec.
    auto bodyHeader = decodeHeader(_in);
    if (!bodyHeader.isList)
    {
        throwRlpDecodeError(DecodingError::UnexpectedString, "block body must be a list");
    }
    bytesRef items(_in.data(), bodyHeader.payloadLength);
    _in = bytesRef(_in.data() + bodyHeader.payloadLength, _in.size() - bodyHeader.payloadLength);

    bcos::codec::rlp::decode(items, _body.header);
    // Transactions list: decode each element with legacy/typed semantics.
    auto txHeader = decodeHeader(items);
    if (!txHeader.isList)
    {
        throwRlpDecodeError(DecodingError::UnexpectedString, "block body txs must be a list");
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
        detail::decodeTx(txPayload, tx);
        _body.transactions.push_back(std::move(tx));
    }
    items = bytesRef(items.data() + txHeader.payloadLength, items.size() - txHeader.payloadLength);

    decode(items, _body.ommers);
    // The optional-withdrawals slot doubles as the pre-Shanghai marker: when the body
    // list is exhausted after header/transactions/ommers, withdrawals stays nullopt;
    // otherwise the fourth item is decoded as the withdrawals list.
    if (!items.empty())
    {
        _body.withdrawals.emplace();
        decode(items, *_body.withdrawals);
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
        throwRlpDecodeError(DecodingError::UnexpectedListElements,
            "block body has trailing elements after withdrawals");
    }
}
}  // namespace bcos::codec::rlp

namespace bcos::protocol
{
void EthBlock::rlpEncode(bcos::bytes& out) const
{
    // Enforce the same invariants the decoder does (empty or too-short transaction
    // elements) so encode and decode accept the same set — a silently dropped element
    // would produce a wire form that does not represent the object it was given.
    for (auto const& tx : m_data.transactions)
    {
        if (tx.empty())
        {
            codec::rlp::throwRlpEncodeError(codec::rlp::DecodingError::UnsupportedTransactionType,
                "EthBlock::rlpEncode: empty transaction element");
        }
        if (tx.front() >= codec::rlp::LIST_HEAD_BASE)
        {
            // Mirror detail::decodeTx's lower bound: a legacy transaction list has nine
            // fields, so a declared payload shorter than 9 bytes cannot be one. Also
            // require the declared list to span the WHOLE element (payloadLength ==
            // view.size() after the prefix) so two concatenated minimal lists cannot
            // pass as one element — encode-then-decode would then yield a different set.
            bytesRef view(const_cast<bcos::byte*>(tx.data()), tx.size());
            auto headerResult = codec::rlp::tryDecodeHeader(view);
            if (!headerResult.has_value() || !headerResult->isList ||
                headerResult->payloadLength < 9 || headerResult->payloadLength != view.size())
            {
                codec::rlp::throwRlpEncodeError(codec::rlp::DecodingError::UnexpectedListElements,
                    "EthBlock::rlpEncode: invalid legacy transaction element");
            }
        }
        else
        {
            // Typed arm: mirror detail::decodeTx — the type byte must be 0x01..0x7f and the
            // payload must be long enough to be a real EIP-2718 typed transaction.
            if (tx.size() < 10 || tx.front() == 0 || tx.front() >= codec::rlp::BYTES_HEAD_BASE)
            {
                codec::rlp::throwRlpEncodeError(
                    codec::rlp::DecodingError::UnsupportedTransactionType,
                    "EthBlock::rlpEncode: invalid typed transaction element");
            }
        }
    }
    // Mirror the sibling guard in EthBlockHeader::rlpEncode: the header rides inside
    // m_data here, so EthBlockHeader's own rlpEncode is bypassed — reject a negative
    // number/timestamp before the shared codec would encode it as 2^64-1.
    if (m_data.header.number < 0 || m_data.header.timestamp < 0)
    {
        codec::rlp::throwRlpEncodeError(codec::rlp::DecodingError::InvalidFieldset,
            "EthBlock::rlpEncode: header number/timestamp must be non-negative");
    }
    codec::rlp::encode(out, m_data);
}

void EthBlock::rlpDecode(bcos::bytesConstRef data)
{
    codec::rlp::decodeExact(data, m_data);
}

void decode(bcos::bytesRef& _in, EthBlockData& _body)
{
    codec::rlp::decode(_in, _body);
}
}  // namespace bcos::protocol
