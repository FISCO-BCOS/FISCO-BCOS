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
 * @file Protocol.cpp
 * @brief eth/68 message RLP codecs.
 * @date 2026/8/18
 */
#include "Protocol.h"

#include "../RlpTake.h"
#include "../Try.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/Exceptions.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <stdexcept>

namespace bcos::devp2p::eth
{
using bcos::codec::rlp::RlpResult;
using bcos::devp2p::detail::genericError;
using bcos::devp2p::detail::take;
using bcos::devp2p::detail::takeListPayload;

namespace
{
// --- encode helpers (explicit header building to splice nested lists) ---
bcos::bytes rlpItem(uint64_t _value)
{
    bcos::bytes out;
    bcos::codec::rlp::encode(out, _value);
    return out;
}

bcos::bytes rlpItem(bytesConstRef _data)
{
    bcos::bytes out;
    bcos::codec::rlp::encode(out, _data);
    return out;
}

bcos::bytes rlpItem(h256 const& _hash)
{
    bcos::bytes out;
    bcos::codec::rlp::encode(out, _hash);
    return out;
}

bcos::bytes rlpList(std::vector<bcos::bytes> const& _items)
{
    bcos::bytes out;
    size_t payloadLength = 0;
    for (auto const& item : _items)
    {
        payloadLength += item.size();
    }
    bcos::codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = payloadLength});
    for (auto const& item : _items)
    {
        out.insert(out.end(), item.begin(), item.end());
    }
    return out;
}

// --- decode helpers (non-throwing; the shared take* item extractors and
// genericError live in bcos-devp2p/RlpTake.h — these are the eth-specific ones) ---
// Extracts one COMPLETE RLP element (prefix + payload) as opaque bytes. Unlike
// take<bcos::bytes> (which only accepts RLP strings), this handles list elements too —
// required for BlockHeaders entries (headers are RLP lists), BlockBodies
// transactions (legacy txs are RLP lists) and withdrawals (RLP lists).
RlpResult<bcos::bytes> takeRlpItem(bcos::bytesRef& _view)
{
    size_t const originalSize = _view.size();
    RLP_TRY(auto header, bcos::codec::rlp::tryDecodeHeader(_view));
    // tryDecodeHeader consumed the item prefix; rebuild prefix + payload.
    size_t const prefixLen = originalSize - _view.size();
    size_t const payloadLen = header.payloadLength;
    bcos::byte const* begin = _view.data() - static_cast<std::ptrdiff_t>(prefixLen);
    bcos::bytes out(begin, begin + prefixLen + payloadLen);
    _view = bcos::bytesRef(_view.data() + payloadLen, _view.size() - payloadLen);
    return out;
}

// Extracts one complete EIP-2718 transaction as opaque bytes. In an eth
// BlockBodies transactions list each transaction is one RLP element:
//   - legacy tx: an RLP list (0xc0..), taken whole via takeRlpItem
//   - typed tx:  an RLP string (0x80..0xbf) whose CONTENT is 0xNN || rlp(payload)
//     (the RLP serializer wraps the opaque typed bytes in a string); decode the
//     string so only 0xNN||payload is returned
//   - a bare 0xNN (< 0x80, only outside an RLP list) followed by rlp(payload)
// A plain takeRlpItem on a typed tx would keep the string prefix (and treat
// 0xNN as a one-byte integer) and misalign the payload.
RlpResult<bcos::bytes> takeTx(bcos::bytesRef& _view)
{
    if (_view.empty())
    {
        return std::unexpected(genericError("eth: empty transaction stream"));
    }
    if (_view[0] >= 0x80 && _view[0] < 0xc0)
    {
        // Typed transaction wrapped as an RLP string: return its content
        // (0xNN || rlp(payload)) without the string prefix.
        return take<bcos::bytes>(_view);
    }
    if (_view[0] >= 0xc0)
    {
        // Legacy transaction: a standalone RLP list (0xc0..).
        return takeRlpItem(_view);
    }
    // Typed transaction: 0xNN || rlp(payload). Type byte 0x01..0x7f.
    if (_view[0] == 0)
    {
        return std::unexpected(genericError("eth: invalid EIP-2718 type byte 0x00"));
    }
    bcos::byte const type = _view[0];
    _view = bcos::bytesRef(_view.data() + 1, _view.size() - 1);
    RLP_TRY(auto payload, takeRlpItem(_view));
    payload.insert(payload.begin(), type);
    return payload;
}

// forkid = [forkHash(4 bytes), next]
RlpResult<ForkId> takeForkId(bcos::bytesRef& _view)
{
    RLP_TRY(auto items, takeListPayload(_view, "eth: expected an RLP list"));
    RLP_TRY(auto forkHashBytes, take<bcos::bytes>(items));
    if (forkHashBytes.size() != 4)
    {
        return std::unexpected(genericError("decodeStatus: invalid fork hash size"));
    }
    ForkId forkId;
    forkId.hash = (static_cast<uint32_t>(forkHashBytes[0]) << 24) |
                  (static_cast<uint32_t>(forkHashBytes[1]) << 16) |
                  (static_cast<uint32_t>(forkHashBytes[2]) << 8) |
                  static_cast<uint32_t>(forkHashBytes[3]);
    RLP_TRY(forkId.next, take<uint64_t>(items));
    return forkId;
}
}  // namespace

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
bcos::bytes encodeStatus(StatusMessage const& _msg)
{
    bcos::bytes forkHash(4, 0);
    forkHash[0] = static_cast<bcos::byte>((_msg.forkId.hash >> 24) & 0xff);
    forkHash[1] = static_cast<bcos::byte>((_msg.forkId.hash >> 16) & 0xff);
    forkHash[2] = static_cast<bcos::byte>((_msg.forkId.hash >> 8) & 0xff);
    forkHash[3] = static_cast<bcos::byte>(_msg.forkId.hash & 0xff);

    auto forkIdItem = rlpList(
        {rlpItem(bytesConstRef(forkHash.data(), forkHash.size())), rlpItem(_msg.forkId.next)});

    if (_msg.eip7642)
    {
        // eth/69+ (EIP-7642): [version, networkId, genesis, forkid, earliest, latest, latestHash]
        return rlpList({rlpItem(_msg.protocolVersion), rlpItem(_msg.networkId),
            rlpItem(_msg.genesisHash), forkIdItem, rlpItem(_msg.earliestBlock),
            rlpItem(_msg.latestBlock), rlpItem(_msg.latestBlockHash)});
    }
    // eth/68: [version, networkId, td, head, genesis, forkid]
    return rlpList({rlpItem(_msg.protocolVersion), rlpItem(_msg.networkId),
        rlpItem(bytesConstRef(_msg.totalDifficulty.data(), _msg.totalDifficulty.size())),
        rlpItem(_msg.headHash), rlpItem(_msg.genesisHash), forkIdItem});
}

RlpResult<StatusMessage> decodeStatus(bytesConstRef _data, uint8_t _negotiatedVersion)
{
    StatusMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    RLP_TRY(auto items, takeListPayload(view, "eth: expected an RLP list"));

    RLP_TRY(msg.protocolVersion, take<uint64_t>(items));
    RLP_TRY(msg.networkId, take<uint64_t>(items));
    // The wire layout (eth/68 vs eth/69+) is selected from the version NEGOTIATED
    // in the Hello exchange when the caller knows it, never from the
    // peer-supplied field alone; a mismatched embedded version is rejected.
    if (_negotiatedVersion != 0 && msg.protocolVersion != _negotiatedVersion)
    {
        return std::unexpected(genericError(
            "decodeStatus: peer protocol version does not match the version negotiated in Hello"));
    }
    uint64_t const effectiveVersion =
        _negotiatedVersion != 0 ? _negotiatedVersion : msg.protocolVersion;
    if (effectiveVersion >= kMinProtocolVersion + 1)  // eth/69+
    {
        // EIP-7642: [version, networkId, genesis, forkid, earliest, latest, latestHash]
        msg.eip7642 = true;
        RLP_TRY(msg.genesisHash, take<h256>(items));
        RLP_TRY(msg.forkId, takeForkId(items));
        RLP_TRY(msg.earliestBlock, take<uint64_t>(items));
        RLP_TRY(msg.latestBlock, take<uint64_t>(items));
        RLP_TRY(msg.latestBlockHash, take<h256>(items));
        // Consumers still read headHash (e.g. for the peer-head log line); point it
        // at the latest advertised block hash.
        msg.headHash = msg.latestBlockHash;
    }
    else
    {
        // eth/68: [version, networkId, td, head, genesis, forkid]
        RLP_TRY(msg.totalDifficulty, take<bcos::bytes>(items));
        RLP_TRY(msg.headHash, take<h256>(items));
        RLP_TRY(msg.genesisHash, take<h256>(items));
        RLP_TRY(msg.forkId, takeForkId(items));
    }
    return msg;
}

// ---------------------------------------------------------------------------
// GetBlockHeaders
// ---------------------------------------------------------------------------
// The `reverse` flag is a BOOLEAN on the wire. geth RLP-encodes bool as
// 0x80(false)/0x01(true) and its decoder rejects 0x00 as non-canonical; ethrex's
// bool decoder likewise only accepts 0x80(false)/0x01(true) (RLP_NULL = false)
// and rejects 0x00 with MalformedBoolean. Use 0x80/0x01 for cross-client
// compatibility.
bcos::bytes rlpBool(bool _value)
{
    return bcos::bytes{static_cast<bcos::byte>(_value ? 1 : 0x80)};
}

bcos::bytes encodeGetBlockHeaders(GetBlockHeadersMessage const& _msg)
{
    auto origin =
        _msg.originHash.has_value() ? rlpItem(*_msg.originHash) : rlpItem(_msg.originNumber);
    auto inner = rlpList({origin, rlpItem(_msg.amount), rlpItem(_msg.skip), rlpBool(_msg.reverse)});
    return rlpList({rlpItem(_msg.requestId), inner});
}

RlpResult<GetBlockHeadersMessage> decodeGetBlockHeaders(bytesConstRef _data)
{
    GetBlockHeadersMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    RLP_TRY(auto items, takeListPayload(view, "eth: expected an RLP list"));
    RLP_TRY(msg.requestId, take<uint64_t>(items));

    RLP_TRY(auto inner, takeListPayload(items, "eth: expected an RLP list"));
    // origin: either a hash (32 bytes) or a minimal big-endian number.
    RLP_TRY(auto originBytes, take<bcos::bytes>(inner));
    if (originBytes.size() == 32)
    {
        msg.originHash = h256(bytesConstRef(originBytes.data(), originBytes.size()));
    }
    else
    {
        uint64_t originNumber = 0;
        for (auto byte : originBytes)
        {
            originNumber = (originNumber << 8) | byte;
        }
        msg.originNumber = originNumber;
    }
    RLP_TRY(msg.amount, take<uint64_t>(inner));
    RLP_TRY(msg.skip, take<uint64_t>(inner));
    RLP_TRY(auto reverse, take<uint64_t>(inner));
    msg.reverse = (reverse != 0);
    return msg;
}

// ---------------------------------------------------------------------------
// BlockHeaders
// ---------------------------------------------------------------------------
bcos::bytes encodeBlockHeaders(BlockHeadersMessage const& _msg)
{
    // Each header is an already-encoded RLP element (a list); splice them in
    // directly — do NOT re-wrap them as strings (that changes the wire format).
    std::vector<bcos::bytes> headers;
    headers.reserve(_msg.headers.size());
    for (auto const& header : _msg.headers)
    {
        headers.push_back(header);
    }
    auto inner = rlpList(headers);
    return rlpList({rlpItem(_msg.requestId), inner});
}

RlpResult<BlockHeadersMessage> decodeBlockHeaders(bytesConstRef _data)
{
    BlockHeadersMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    RLP_TRY(auto items, takeListPayload(view, "eth: expected an RLP list"));
    RLP_TRY(msg.requestId, take<uint64_t>(items));
    RLP_TRY(auto headers, takeListPayload(items, "eth: expected an RLP list"));
    while (!headers.empty())
    {
        RLP_TRY(auto header, takeRlpItem(headers));
        msg.headers.push_back(std::move(header));
    }
    return msg;
}

// ---------------------------------------------------------------------------
// GetBlockBodies
// ---------------------------------------------------------------------------
bcos::bytes encodeGetBlockBodies(GetBlockBodiesMessage const& _msg)
{
    std::vector<bcos::bytes> hashes;
    hashes.reserve(_msg.hashes.size());
    for (auto const& hash : _msg.hashes)
    {
        hashes.push_back(rlpItem(hash));
    }
    return rlpList({rlpItem(_msg.requestId), rlpList(hashes)});
}

RlpResult<GetBlockBodiesMessage> decodeGetBlockBodies(bytesConstRef _data)
{
    GetBlockBodiesMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    RLP_TRY(auto items, takeListPayload(view, "eth: expected an RLP list"));
    RLP_TRY(msg.requestId, take<uint64_t>(items));
    RLP_TRY(auto hashes, takeListPayload(items, "eth: expected an RLP list"));
    while (!hashes.empty())
    {
        RLP_TRY(auto hash, take<h256>(hashes));
        msg.hashes.push_back(hash);
    }
    return msg;
}

// ---------------------------------------------------------------------------
// BlockBodies
// ---------------------------------------------------------------------------
bcos::bytes encodeBlockBodies(BlockBodiesMessage const& _msg)
{
    std::vector<bcos::bytes> bodyItems;
    bodyItems.reserve(_msg.bodies.size());
    for (auto const& body : _msg.bodies)
    {
        // Transactions: on the wire a legacy tx is a bare RLP list element while
        // a typed (EIP-2718) tx is an RLP byte STRING whose content is
        // 0xNN || rlp(payload). `body.transactions` holds the unwrapped form, so
        // typed txs (first byte < 0xc0) are string-wrapped here; legacy lists are
        // spliced directly. Uncles / withdrawals are already-encoded RLP list
        // elements and are spliced as-is.
        std::vector<bcos::bytes> txs;
        txs.reserve(body.transactions.size());
        for (auto const& tx : body.transactions)
        {
            if (!tx.empty() && tx[0] < 0xc0)
            {
                txs.push_back(rlpItem(bytesConstRef(tx.data(), tx.size())));
            }
            else
            {
                txs.push_back(tx);
            }
        }
        std::vector<bcos::bytes> uncles;
        uncles.reserve(body.uncles.size());
        for (auto const& uncle : body.uncles)
        {
            uncles.push_back(uncle);
        }
        auto txsList = rlpList(txs);
        auto unclesList = rlpList(uncles);
        if (body.withdrawals.has_value())
        {
            std::vector<bcos::bytes> withdrawals;
            withdrawals.reserve(body.withdrawals->size());
            for (auto const& withdrawal : *body.withdrawals)
            {
                withdrawals.push_back(withdrawal);
            }
            bodyItems.push_back(rlpList({txsList, unclesList, rlpList(withdrawals)}));
        }
        else
        {
            bodyItems.push_back(rlpList({txsList, unclesList}));
        }
    }
    return rlpList({rlpItem(_msg.requestId), rlpList(bodyItems)});
}

RlpResult<BlockBodiesMessage> decodeBlockBodies(bytesConstRef _data)
{
    BlockBodiesMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    RLP_TRY(auto items, takeListPayload(view, "eth: expected an RLP list"));
    RLP_TRY(msg.requestId, take<uint64_t>(items));
    RLP_TRY(auto bodies, takeListPayload(items, "eth: expected an RLP list"));
    while (!bodies.empty())
    {
        RLP_TRY(auto bodyItems, takeListPayload(bodies, "eth: expected an RLP list"));
        RLP_TRY(auto txs, takeListPayload(bodyItems, "eth: expected an RLP list"));
        BlockBody body;
        while (!txs.empty())
        {
            RLP_TRY(auto tx, takeTx(txs));
            body.transactions.push_back(std::move(tx));
        }
        RLP_TRY(auto uncles, takeListPayload(bodyItems, "eth: expected an RLP list"));
        while (!uncles.empty())
        {
            RLP_TRY(auto uncle, takeRlpItem(uncles));
            body.uncles.push_back(std::move(uncle));
        }
        // Shanghai+ bodies carry a third list of withdrawals.
        if (!bodyItems.empty())
        {
            RLP_TRY(auto withdrawals, takeListPayload(bodyItems, "eth: expected an RLP list"));
            body.withdrawals = std::vector<bcos::bytes>{};
            while (!withdrawals.empty())
            {
                RLP_TRY(auto withdrawal, takeRlpItem(withdrawals));
                body.withdrawals->push_back(std::move(withdrawal));
            }
        }
        msg.bodies.push_back(std::move(body));
    }
    return msg;
}

// ---------------------------------------------------------------------------
// NewBlockHashes
// ---------------------------------------------------------------------------
bcos::bytes encodeNewBlockHashes(NewBlockHashesMessage const& _msg)
{
    std::vector<bcos::bytes> entries;
    entries.reserve(_msg.entries.size());
    for (auto const& entry : _msg.entries)
    {
        entries.push_back(rlpList({rlpItem(entry.hash), rlpItem(entry.number)}));
    }
    return rlpList(entries);
}

RlpResult<NewBlockHashesMessage> decodeNewBlockHashes(bytesConstRef _data)
{
    NewBlockHashesMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    RLP_TRY(auto items, takeListPayload(view, "eth: expected an RLP list"));
    while (!items.empty())
    {
        RLP_TRY(auto entry, takeListPayload(items, "eth: expected an RLP list"));
        NewBlockHashesMessage::Entry e;
        RLP_TRY(e.hash, take<h256>(entry));
        RLP_TRY(e.number, take<uint64_t>(entry));
        msg.entries.push_back(std::move(e));
    }
    return msg;
}

// ---------------------------------------------------------------------------
// Transactions
// ---------------------------------------------------------------------------
// A gossip Transactions message carries each tx exactly like a BlockBodies
// transactions element: a legacy tx is a bare RLP list, a typed (EIP-2718) tx is
// an RLP string whose content is 0xNN || rlp(payload) — for a blob tx that
// content is the whole EIP-4844 network wrapper, sidecar included.
bcos::bytes encodeTransactions(TransactionsMessage const& _msg)
{
    std::vector<bcos::bytes> txs;
    txs.reserve(_msg.transactions.size());
    for (auto const& tx : _msg.transactions)
    {
        if (!tx.empty() && tx[0] < 0xc0)
        {
            txs.push_back(rlpItem(bytesConstRef(tx.data(), tx.size())));
        }
        else
        {
            txs.push_back(tx);
        }
    }
    return rlpList(txs);
}

RlpResult<TransactionsMessage> decodeTransactions(bytesConstRef _data)
{
    TransactionsMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    RLP_TRY(auto txs, takeListPayload(view, "eth: expected an RLP list"));
    while (!txs.empty())
    {
        if (msg.transactions.size() >= kMaxTxsPerMessage)
        {
            return std::unexpected(genericError("eth: too many transactions in one message"));
        }
        RLP_TRY(auto tx, takeTx(txs));
        msg.transactions.push_back(std::move(tx));
    }
    return msg;
}

// ---------------------------------------------------------------------------
// NewPooledTransactionHashes (eth/68)
// ---------------------------------------------------------------------------
bcos::bytes encodeNewPooledTransactionHashes(NewPooledTransactionHashesMessage const& _msg)
{
    std::vector<bcos::bytes> sizes;
    sizes.reserve(_msg.sizes.size());
    for (auto const size : _msg.sizes)
    {
        sizes.push_back(rlpItem(size));
    }
    std::vector<bcos::bytes> hashes;
    hashes.reserve(_msg.hashes.size());
    for (auto const& hash : _msg.hashes)
    {
        hashes.push_back(rlpItem(hash));
    }
    return rlpList({rlpItem(bytesConstRef(_msg.types.data(), _msg.types.size())), rlpList(sizes),
        rlpList(hashes)});
}

RlpResult<NewPooledTransactionHashesMessage> decodeNewPooledTransactionHashes(bytesConstRef _data)
{
    NewPooledTransactionHashesMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    RLP_TRY(auto items, takeListPayload(view, "eth: expected an RLP list"));
    RLP_TRY(msg.types, take<bcos::bytes>(items));
    RLP_TRY(auto sizes, takeListPayload(items, "eth: expected an RLP list"));
    while (!sizes.empty())
    {
        if (msg.sizes.size() >= kMaxAnnouncedHashes)
        {
            return std::unexpected(genericError("eth: too many announced transaction sizes"));
        }
        RLP_TRY(auto size, take<uint64_t>(sizes));
        msg.sizes.push_back(size);
    }
    RLP_TRY(auto hashes, takeListPayload(items, "eth: expected an RLP list"));
    while (!hashes.empty())
    {
        if (msg.hashes.size() >= kMaxAnnouncedHashes)
        {
            return std::unexpected(genericError("eth: too many announced transaction hashes"));
        }
        RLP_TRY(auto hash, take<h256>(hashes));
        msg.hashes.push_back(hash);
    }
    // eth/68: the three fields name the same transactions, so their counts must
    // agree — a mismatch is a malformed announcement, not a partial one.
    if (msg.types.size() != msg.hashes.size() || msg.sizes.size() != msg.hashes.size())
    {
        return std::unexpected(genericError(
            "eth: NewPooledTransactionHashes types/sizes/hashes count mismatch"));
    }
    return msg;
}

// ---------------------------------------------------------------------------
// GetPooledTransactions / PooledTransactions (eth/66+ request-id forms)
// ---------------------------------------------------------------------------
bcos::bytes encodeGetPooledTransactions(GetPooledTransactionsMessage const& _msg)
{
    std::vector<bcos::bytes> hashes;
    hashes.reserve(_msg.hashes.size());
    for (auto const& hash : _msg.hashes)
    {
        hashes.push_back(rlpItem(hash));
    }
    return rlpList({rlpItem(_msg.requestId), rlpList(hashes)});
}

RlpResult<GetPooledTransactionsMessage> decodeGetPooledTransactions(bytesConstRef _data)
{
    GetPooledTransactionsMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    RLP_TRY(auto items, takeListPayload(view, "eth: expected an RLP list"));
    RLP_TRY(msg.requestId, take<uint64_t>(items));
    RLP_TRY(auto hashes, takeListPayload(items, "eth: expected an RLP list"));
    while (!hashes.empty())
    {
        if (msg.hashes.size() >= kMaxAnnouncedHashes)
        {
            return std::unexpected(genericError("eth: too many pooled transaction hashes"));
        }
        RLP_TRY(auto hash, take<h256>(hashes));
        msg.hashes.push_back(hash);
    }
    return msg;
}

bcos::bytes encodePooledTransactions(PooledTransactionsMessage const& _msg)
{
    TransactionsMessage txs{.transactions = _msg.transactions};
    return rlpList({rlpItem(_msg.requestId), encodeTransactions(txs)});
}

RlpResult<PooledTransactionsMessage> decodePooledTransactions(bytesConstRef _data)
{
    PooledTransactionsMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    RLP_TRY(auto items, takeListPayload(view, "eth: expected an RLP list"));
    RLP_TRY(msg.requestId, take<uint64_t>(items));
    RLP_TRY(auto txs, takeListPayload(items, "eth: expected an RLP list"));
    while (!txs.empty())
    {
        if (msg.transactions.size() >= kMaxTxsPerMessage)
        {
            return std::unexpected(genericError("eth: too many transactions in one message"));
        }
        RLP_TRY(auto tx, takeTx(txs));
        msg.transactions.push_back(std::move(tx));
    }
    return msg;
}

}  // namespace bcos::devp2p::eth
