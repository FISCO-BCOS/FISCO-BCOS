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
// Ethereum block body (the part of a block not committed to by the header hash).
// RLP form (yellow paper, Appendix B):
//   pre-Shanghai: rlp([header, transactions, ommers])
//   Shanghai+  :  rlp([header, transactions, ommers, withdrawals])   (EIP-4895)
// `transactions` holds the raw EIP-2718 encodings (opaque bytes); `ommers` are uncle headers
// (always empty on PoS chains); `withdrawals` is nullopt for pre-Shanghai bodies and an
// (possibly empty) list for Shanghai+ bodies — the presence of the field itself is fork
// significant, so it is an optional<vector>, not a bare vector.
struct EthBlockBodyData
{
    EthBlockHeaderData header;
    std::vector<bcos::bytes> transactions;           // opaque EIP-2718 encoded transactions
    std::vector<EthBlockHeaderData> ommers;          // uncle headers (empty on PoS)
    std::optional<std::vector<EthWithdrawalData>> withdrawals;  // nullopt = pre-Shanghai

    bool operator==(const EthBlockBodyData& rhs) const
    {
        return header == rhs.header && transactions == rhs.transactions &&
               ommers == rhs.ommers && withdrawals == rhs.withdrawals;
    }
    bool operator!=(const EthBlockBodyData& rhs) const { return !(*this == rhs); }
};

// Class wrapper following the EthBlockHeader pattern: rlpEncode/rlpDecode plus the
// codec::rlp overloads that let EthBlockBodyData work as an item in larger structures.
class EthBlockBody
{
public:
    EthBlockBody() = default;
    explicit EthBlockBody(EthBlockBodyData data) : m_data(std::move(data)) {}

    void rlpEncode(bcos::bytes& out) const;
    // Decodes a single block body (a 3- or 4-element list) from `data`.
    bcos::Error::UniquePtr rlpDecode(bcos::bytesConstRef data);

    const EthBlockBodyData& data() const { return m_data; }
    EthBlockBodyData& data() { return m_data; }

private:
    EthBlockBodyData m_data;
};
}  // namespace bcos::protocol

namespace bcos::codec::rlp
{
namespace
{
// RLP length of one transaction inside a block-body transactions list, with the
// wire semantics geth uses: legacy txs (leading byte >= 0xc0 — already a complete
// RLP list) are spliced raw; typed txs (leading byte 0x01..0x7f) are wrapped in an
// RLP string (geth's Transaction.EncodeRLP). The generic bytes codec would wrap
// EVERY element as a string, which corrupts legacy txs, so the list is built by
// hand.
size_t txWireLength(bcos::bytes const& _tx) noexcept
{
    if (!_tx.empty() && _tx.front() < LIST_HEAD_BASE)
    {
        return length(bytesConstRef(_tx.data(), _tx.size()));
    }
    return _tx.size();
}

// RLP length of the complete transactions list element (header + payload).
size_t txListLength(std::vector<bcos::bytes> const& _txs) noexcept
{
    size_t payload = 0;
    for (auto const& tx : _txs)
    {
        payload += txWireLength(tx);
    }
    return lengthOfLength(payload) + payload;
}

// Encode the complete transactions list element (header + payload), splicing
// legacy txs raw and string-wrapping typed txs.
bcos::bytes encodeTxList(std::vector<bcos::bytes> const& _txs) noexcept
{
    size_t const payload = [&_txs] {
        size_t sum = 0;
        for (auto const& tx : _txs)
        {
            sum += txWireLength(tx);
        }
        return sum;
    }();
    bcos::bytes out;
    encodeHeader(out, {.isList = true, .payloadLength = payload});
    out.reserve(out.size() + payload);
    for (auto const& tx : _txs)
    {
        if (!tx.empty() && tx.front() < LIST_HEAD_BASE)
        {
            encode(out, bytesConstRef(tx.data(), tx.size()));
        }
        else
        {
            out.insert(out.end(), tx.begin(), tx.end());
        }
    }
    return out;
}

// Decode one transaction from a block-body transactions list into its opaque
// EIP-2718 bytes: a typed tx (RLP string) loses its string prefix (0xNN||payload
// is returned, matching what encodeTxList wraps); a legacy tx (RLP list) is taken
// whole. Malformed input (e.g. a bare type byte 0x00) is rejected.
bcos::Error::UniquePtr decodeTx(bcos::bytesRef& _in, bcos::bytes& _out) noexcept
{
    if (_in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::InputTooShort, "empty tx stream");
    }
    if (_in[0] >= BYTES_HEAD_BASE && _in[0] < LIST_HEAD_BASE)
    {
        // Typed tx wrapped as an RLP string — return its content (0xNN||payload)
        // without the string prefix.
        return decode(_in, _out);
    }
    if (_in[0] >= LIST_HEAD_BASE)
    {
        // Legacy tx: a standalone RLP list — take the whole element raw.
        size_t const originalSize = _in.size();
        auto [error, header] = decodeHeader(_in);
        if (error)
        {
            return std::move(error);
        }
        size_t const prefixLen = originalSize - _in.size();
        size_t const payloadLen = header.payloadLength;
        bcos::byte const* begin = _in.data() - static_cast<std::ptrdiff_t>(prefixLen);
        _out.assign(begin, begin + prefixLen + payloadLen);
        _in = bcos::bytesRef(_in.data() + payloadLen, _in.size() - payloadLen);
        return nullptr;
    }
    // A bare type byte 0x00 is not a valid EIP-2718 transaction.
    return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnsupportedTransactionType,
        "invalid EIP-2718 type byte 0x00 in block body");
}
}  // namespace

// Overloads so EthBlockBodyData works as an item inside the generic list/vector codecs.
// The header codec (EthBlockHeaderData) is the single source of truth for the header
// field order; the transactions list is built by hand so legacy txs stay raw lists.
inline size_t length(const protocol::EthBlockBodyData& _body) noexcept
{
    size_t const txsLen = txListLength(_body.transactions);
    size_t payload =
        bcos::codec::rlp::length(_body.header) + txsLen + length(_body.ommers);
    if (_body.withdrawals.has_value())
    {
        payload += length(*_body.withdrawals);
    }
    return lengthOfLength(payload) + payload;
}
inline void encode(bcos::bytes& _out, const protocol::EthBlockBodyData& _body) noexcept
{
    bcos::bytes const txsList = encodeTxList(_body.transactions);
    size_t payload =
        bcos::codec::rlp::length(_body.header) + txsList.size() + length(_body.ommers);
    if (_body.withdrawals.has_value())
    {
        payload += length(*_body.withdrawals);
    }
    encodeHeader(_out, {.isList = true, .payloadLength = payload});
    _out.reserve(_out.size() + payload);
    bcos::codec::rlp::encode(_out, _body.header);
    _out.insert(_out.end(), txsList.begin(), txsList.end());
    encode(_out, _body.ommers);
    if (_body.withdrawals.has_value())
    {
        encode(_out, *_body.withdrawals);
    }
}
inline bcos::Error::UniquePtr decode(bcos::bytesRef& _in, protocol::EthBlockBodyData& _body) noexcept
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
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedString, "block body must be a list");
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
    _body.transactions.reserve(txHeader.payloadLength);
    while (!txPayload.empty())
    {
        bcos::bytes tx;
        if (auto err = decodeTx(txPayload, tx))
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
    return nullptr;
}
}  // namespace bcos::codec::rlp

namespace bcos::protocol
{
// ADL-visible delegators (see EthLog.h): let EthBlockBodyData participate in
// variadic-list encode/decode.
inline size_t length(const EthBlockBodyData& _body) noexcept
{
    return codec::rlp::length(_body);
}
inline void encode(bcos::bytes& _out, const EthBlockBodyData& _body) noexcept
{
    codec::rlp::encode(_out, _body);
}
inline bcos::Error::UniquePtr decode(bcos::bytesRef& _in, EthBlockBodyData& _body) noexcept
{
    return codec::rlp::decode(_in, _body);
}
}  // namespace bcos::protocol
