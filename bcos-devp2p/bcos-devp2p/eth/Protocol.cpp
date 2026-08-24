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

#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <stdexcept>

namespace bcos::devp2p::eth
{
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
    out.reserve(out.size() + payloadLength);
    for (auto const& item : _items)
    {
        out.insert(out.end(), item.begin(), item.end());
    }
    return out;
}

// --- decode helpers ---
// Expects a list at `_view` and returns a view over its payload.
bcos::bytesRef takeListPayload(bcos::bytesRef& _view)
{
    auto [error, header] = bcos::codec::rlp::decodeHeader(_view);
    if (error || !header.isList)
    {
        throw std::runtime_error("eth: expected an RLP list");
    }
    // Precondition: decodeHeader already guarantees header.payloadLength <= the
    // remaining input size (it fails with InputTooShort otherwise), so the two
    // slices below are in-bounds.
    bcos::bytesRef payload(_view.data(), header.payloadLength);
    _view = bcos::bytesRef(
        _view.data() + header.payloadLength, _view.size() - header.payloadLength);
    return payload;
}

bcos::bytes takeBytes(bcos::bytesRef& _view)
{
    bcos::bytes out;
    if (auto err = bcos::codec::rlp::decode(_view, out))
    {
        throw std::runtime_error("eth: bytes item decode failed");
    }
    return out;
}

// Extracts one COMPLETE RLP element (prefix + payload) as opaque bytes. Unlike
// takeBytes (which only accepts RLP strings), this handles list elements too —
// required for BlockHeaders entries (headers are RLP lists), BlockBodies
// transactions (legacy txs are RLP lists) and withdrawals (RLP lists).
bcos::bytes takeRlpItem(bcos::bytesRef& _view)
{
    size_t const originalSize = _view.size();
    auto [error, header] = bcos::codec::rlp::decodeHeader(_view);
    if (error)
    {
        throw std::runtime_error("eth: rlp item decode failed");
    }
    // decodeHeader consumed the item prefix; rebuild prefix + payload.
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
bcos::bytes takeTx(bcos::bytesRef& _view)
{
    if (_view.empty())
    {
        throw std::runtime_error("eth: empty transaction stream");
    }
    if (_view[0] >= 0x80 && _view[0] < 0xc0)
    {
        // Typed transaction wrapped as an RLP string: return its content
        // (0xNN || rlp(payload)) without the string prefix.
        return takeBytes(_view);
    }
    if (_view[0] >= 0xc0)
    {
        // Legacy transaction: a standalone RLP list (0xc0..).
        return takeRlpItem(_view);
    }
    // Typed transaction: 0xNN || rlp(payload). Type byte 0x01..0x7f.
    if (_view[0] == 0)
    {
        throw std::runtime_error("eth: invalid EIP-2718 type byte 0x00");
    }
    bcos::byte const type = _view[0];
    _view = bcos::bytesRef(_view.data() + 1, _view.size() - 1);
    bcos::bytes payload = takeRlpItem(_view);
    // Build type||payload without shifting the (potentially 128KB blob) payload.
    bcos::bytes out;
    out.reserve(payload.size() + 1);
    out.push_back(type);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

uint64_t takeUint(bcos::bytesRef& _view)
{
    auto [error, header] = bcos::codec::rlp::decodeHeader(_view);
    if (error || header.isList)
    {
        throw std::runtime_error("eth: uint item decode failed");
    }
    // Reject over-wide payloads instead of silently keeping only the low bytes
    // (geth disconnects with "input string too long for uint64").
    if (header.payloadLength > sizeof(uint64_t))
    {
        throw std::runtime_error("eth: uint item too large for uint64");
    }
    uint64_t value = 0;
    for (size_t i = 0; i < header.payloadLength; ++i)
    {
        value = (value << 8) | _view[i];
    }
    _view =
        bcos::bytesRef(_view.data() + header.payloadLength, _view.size() - header.payloadLength);
    return value;
}

// Fail-closed helpers: every eth decoder rejects trailing elements rather than
// silently ignoring them (mirrors the bcos-rlp-protocol decoders' strictness and
// gives byte-level canonicality for anything part 2 hashes or forwards).
void ensureExhausted(bcos::bytesRef const& _view, const char* _what)
{
    if (!_view.empty())
    {
        throw std::runtime_error(std::string("eth: ") + _what + ": trailing elements");
    }
}

h256 takeH256(bcos::bytesRef& _view)
{
    h256 out;
    if (auto err = bcos::codec::rlp::decode(_view, out))
    {
        throw std::runtime_error("eth: h256 item decode failed");
    }
    return out;
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

    if (_msg.protocolVersion >= 69)
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

StatusMessage decodeStatus(bytesConstRef _data)
{
    StatusMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    auto items = takeListPayload(view);

    msg.protocolVersion = takeUint(items);
    msg.networkId = takeUint(items);
    if (msg.protocolVersion >= kMinProtocolVersion + 1)  // eth/69+
    {
        // EIP-7642: [version, networkId, genesis, forkid, earliest, latest, latestHash]
        msg.genesisHash = takeH256(items);
        auto forkIdItems = takeListPayload(items);
        auto forkHashBytes = takeBytes(forkIdItems);
        if (forkHashBytes.size() != 4)
        {
            throw std::runtime_error("decodeStatus: invalid fork hash size");
        }
        msg.forkId.hash = (static_cast<uint32_t>(forkHashBytes[0]) << 24) |
                          (static_cast<uint32_t>(forkHashBytes[1]) << 16) |
                          (static_cast<uint32_t>(forkHashBytes[2]) << 8) |
                          static_cast<uint32_t>(forkHashBytes[3]);
        msg.forkId.next = takeUint(forkIdItems);
        ensureExhausted(forkIdItems, "Status forkId");
        msg.earliestBlock = takeUint(items);
        msg.latestBlock = takeUint(items);
        msg.latestBlockHash = takeH256(items);
        ensureExhausted(items, "Status");
        // Consumers still read headHash (e.g. for the peer-head log line); point it
        // at the latest advertised block hash.
        msg.headHash = msg.latestBlockHash;
    }
    else
    {
        // eth/68: [version, networkId, td, head, genesis, forkid]
        msg.totalDifficulty = takeBytes(items);
        msg.headHash = takeH256(items);
        msg.genesisHash = takeH256(items);
        auto forkIdItems = takeListPayload(items);
        auto forkHashBytes = takeBytes(forkIdItems);
        if (forkHashBytes.size() != 4)
        {
            throw std::runtime_error("decodeStatus: invalid fork hash size");
        }
        msg.forkId.hash = (static_cast<uint32_t>(forkHashBytes[0]) << 24) |
                          (static_cast<uint32_t>(forkHashBytes[1]) << 16) |
                          (static_cast<uint32_t>(forkHashBytes[2]) << 8) |
                          static_cast<uint32_t>(forkHashBytes[3]);
        msg.forkId.next = takeUint(forkIdItems);
        ensureExhausted(forkIdItems, "Status forkId");
        ensureExhausted(items, "Status");
    }
    return msg;
}

// ---------------------------------------------------------------------------
// GetBlockHeaders
// ---------------------------------------------------------------------------
// The `reverse` flag is a BOOLEAN on the wire. Wire encodings differ between
// clients: geth RLP-encodes bool as 0x00(false)/0x01(true), while ethrex's bool
// decoder only accepts 0x80(false)/0x01(true) (RLP_NULL = false) and rejects
// 0x00 with MalformedBoolean. geth's decoder reads the field as an integer, so
// it accepts both 0x00 and 0x80. Use 0x80/0x01 for cross-client compatibility.
bcos::bytes rlpBool(bool _value)
{
    return bcos::bytes{static_cast<bcos::byte>(_value ? 1 : 0x80)};
}

bcos::bytes encodeGetBlockHeaders(GetBlockHeadersMessage const& _msg)
{
    auto origin = _msg.originHash.has_value() ? rlpItem(*_msg.originHash) : rlpItem(_msg.originNumber);
    auto inner = rlpList({origin, rlpItem(_msg.amount), rlpItem(_msg.skip),
        rlpBool(_msg.reverse)});
    return rlpList({rlpItem(_msg.requestId), inner});
}

GetBlockHeadersMessage decodeGetBlockHeaders(bytesConstRef _data)
{
    GetBlockHeadersMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    auto items = takeListPayload(view);
    msg.requestId = takeUint(items);

    auto inner = takeListPayload(items);
    // origin: either a hash (32 bytes) or a minimal big-endian number.
    auto originBytes = takeBytes(inner);
    if (originBytes.size() == 32)
    {
        msg.originHash =
            h256(bytesConstRef(originBytes.data(), originBytes.size()));
    }
    else
    {
        // A non-32-byte origin is a block number; geth encodes it minimally, so
        // anything over 8 bytes is malformed and must be rejected rather than
        // silently truncated to the low 8 bytes.
        if (originBytes.size() > sizeof(uint64_t))
        {
            throw std::runtime_error("eth: origin block number too large for uint64");
        }
        uint64_t originNumber = 0;
        for (auto byte : originBytes)
        {
            originNumber = (originNumber << 8) | byte;
        }
        msg.originNumber = originNumber;
    }
    msg.amount = takeUint(inner);
    msg.skip = takeUint(inner);
    msg.reverse = (takeUint(inner) != 0);
    ensureExhausted(inner, "GetBlockHeaders inner");
    ensureExhausted(items, "GetBlockHeaders");
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

BlockHeadersMessage decodeBlockHeaders(bytesConstRef _data)
{
    BlockHeadersMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    auto items = takeListPayload(view);
    msg.requestId = takeUint(items);
    auto headers = takeListPayload(items);
    while (!headers.empty())
    {
        msg.headers.push_back(takeRlpItem(headers));
    }
    ensureExhausted(headers, "BlockHeaders");
    ensureExhausted(items, "BlockHeaders");
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

GetBlockBodiesMessage decodeGetBlockBodies(bytesConstRef _data)
{
    GetBlockBodiesMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    auto items = takeListPayload(view);
    msg.requestId = takeUint(items);
    auto hashes = takeListPayload(items);
    while (!hashes.empty())
    {
        msg.hashes.push_back(takeH256(hashes));
    }
    ensureExhausted(hashes, "GetBlockBodies");
    ensureExhausted(items, "GetBlockBodies");
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
        // Transactions / uncles / withdrawals are already-encoded RLP elements
        // (legacy txs are lists, typed txs are 0x01||payload bytes); splice them
        // in directly — do NOT re-wrap legacy txs as strings.
        std::vector<bcos::bytes> txs;
        txs.reserve(body.transactions.size());
        for (auto const& tx : body.transactions)
        {
            if (!tx.empty() && tx.front() < 0xc0)
            {
                // Typed EIP-2718 tx (0xNN || rlp(payload)): geth's
                // Transaction.EncodeRLP wraps the opaque bytes in an RLP string
                // (rlp.Encode(w, buf.Bytes())), so the canonical wire form inside
                // the BlockBodies transactions list is an RLP string — NOT bare
                // type-byte + payload. Wrap to match geth byte-for-byte.
                txs.push_back(rlpItem(bytesConstRef(tx.data(), tx.size())));
            }
            else
            {
                // Legacy tx: already a standalone RLP list — splice directly.
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

BlockBodiesMessage decodeBlockBodies(bytesConstRef _data)
{
    BlockBodiesMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    auto items = takeListPayload(view);
    msg.requestId = takeUint(items);
    auto bodies = takeListPayload(items);
    while (!bodies.empty())
    {
        auto bodyItems = takeListPayload(bodies);
        auto txs = takeListPayload(bodyItems);
        BlockBody body;
        while (!txs.empty())
        {
            body.transactions.push_back(takeTx(txs));
        }
        auto uncles = takeListPayload(bodyItems);
        while (!uncles.empty())
        {
            body.uncles.push_back(takeRlpItem(uncles));
        }
        // Shanghai+ bodies carry a third list of withdrawals.
        if (!bodyItems.empty())
        {
            auto withdrawals = takeListPayload(bodyItems);
            body.withdrawals = std::vector<bcos::bytes>{};
            while (!withdrawals.empty())
            {
                body.withdrawals->push_back(takeRlpItem(withdrawals));
            }
            ensureExhausted(withdrawals, "body withdrawals");
        }
        ensureExhausted(bodyItems, "BlockBodies body");
        msg.bodies.push_back(std::move(body));
    }
    ensureExhausted(bodies, "BlockBodies");
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

NewBlockHashesMessage decodeNewBlockHashes(bytesConstRef _data)
{
    NewBlockHashesMessage msg;
    bcos::bytesRef view(const_cast<bcos::byte*>(_data.data()), _data.size());
    auto items = takeListPayload(view);
    while (!items.empty())
    {
        auto entry = takeListPayload(items);
        NewBlockHashesMessage::Entry e;
        e.hash = takeH256(entry);
        e.number = takeUint(entry);
        ensureExhausted(entry, "NewBlockHashes entry");
        msg.entries.push_back(std::move(e));
    }
    ensureExhausted(items, "NewBlockHashes");
    return msg;
}

}  // namespace bcos::devp2p::eth
