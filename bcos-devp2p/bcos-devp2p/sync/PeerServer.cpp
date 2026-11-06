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
 * @file PeerServer.cpp
 * @brief eth serving loop implementation.
 * @date 2026/11/3
 */
#include "PeerServer.h"

#include "../rlpx/Socket.h"
#include <bcos-utilities/LogStream.h>
#include <algorithm>
#include <exception>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace bcos::devp2p::sync
{
namespace
{
constexpr auto kPong = rlpx::baseMsg::Pong;
}  // namespace

PeerServer::Outcome PeerServer::reply(uint8_t _messageId, bcos::bytes _payload)
{
    Outcome outcome;
    outcome.kind = Outcome::Kind::Reply;
    outcome.reply = rlpx::Message{_messageId, std::move(_payload)};
    return outcome;
}

PeerServer::Outcome PeerServer::breach(std::string _detail)
{
    Outcome outcome;
    outcome.kind = Outcome::Kind::Disconnect;
    outcome.reason = rlpx::DisconnectReason::ProtocolBreach;
    outcome.detail = std::move(_detail);
    return outcome;
}

PeerServer::Outcome PeerServer::handle(uint8_t _ethVersion, rlpx::Message const& _message)
{
    auto const data = bcos::bytesConstRef(_message.data.data(), _message.data.size());
    // --- devp2p base protocol ---
    if (_message.id == rlpx::baseMsg::Ping)
    {
        return reply(kPong, rlpx::encodePong());
    }
    if (_message.id == rlpx::baseMsg::Disconnect)
    {
        Outcome outcome;
        outcome.kind = Outcome::Kind::PeerClosed;
        auto disconnect = rlpx::decodeDisconnect(data);
        outcome.detail = disconnect ?
                             "reason=" + std::to_string(static_cast<int>(disconnect->reason)) :
                             "reason undecodable";
        return outcome;
    }
    if (_message.id < eth::kBaseProtocolLength)
    {
        return {};  // Pong, Hello again, ...: nothing to answer
    }
    // --- eth ---
    auto const code = static_cast<uint8_t>(_message.id - eth::kBaseProtocolLength);
    switch (code)
    {
    case eth::msg::GetBlockHeaders:
    {
        auto request = eth::decodeGetBlockHeaders(data);
        if (!request)
        {
            return breach("malformed GetBlockHeaders: " + request.error().message);
        }
        return serveHeaders(*request);
    }
    case eth::msg::GetBlockBodies:
    {
        auto request = eth::decodeGetBlockBodies(data);
        if (!request)
        {
            return breach("malformed GetBlockBodies: " + request.error().message);
        }
        return serveBodies(*request);
    }
    case eth::msg::GetReceipts:
    {
        auto request = eth::decodeGetReceipts(data, _ethVersion);
        if (!request)
        {
            return breach("malformed GetReceipts: " + request.error().message);
        }
        return serveReceipts(_ethVersion, *request);
    }
    case eth::msg::GetPooledTransactions:
    {
        // We hold no pooled transactions for peers: answer the empty list so the
        // requester's request id resolves instead of timing out.
        auto request = eth::decodeGetPooledTransactions(data);
        if (!request)
        {
            return breach("malformed GetPooledTransactions: " + request.error().message);
        }
        eth::PooledTransactionsMessage response;
        response.requestId = request->requestId;
        return reply(static_cast<uint8_t>(eth::frameId(eth::msg::PooledTransactions)),
            eth::encodePooledTransactions(response));
    }
    case eth::msg::BlockRangeUpdate:
    {
        if (_ethVersion < 69)
        {
            return breach("BlockRangeUpdate on an eth/68 session");
        }
        auto update = eth::decodeBlockRangeUpdate(data);
        if (!update)
        {
            return breach("malformed BlockRangeUpdate: " + update.error().message);
        }
        if (!update->valid())
        {
            return breach("BlockRangeUpdate with earliest > latest");
        }
        return {};  // informational: nothing to answer
    }
    case eth::msg::Status:
        return breach("Status after the handshake");
    default:
        // Gossip (Transactions, NewPooledTransactionHashes, NewBlockHashes, NewBlock),
        // responses to requests we never sent, and codes this version does not know:
        // ignored.
        return {};
    }
}

PeerServer::Outcome PeerServer::serveHeaders(eth::GetBlockHeadersMessage const& _request)
{
    eth::BlockHeadersMessage response;
    response.requestId = _request.requestId;
    uint64_t const amount = std::min<uint64_t>(_request.amount, m_limits.maxHeaders);
    uint64_t origin = _request.originNumber;
    if (_request.originHash.has_value())
    {
        auto number = m_source.numberByHash(*_request.originHash);
        if (!number)
        {
            return reply(static_cast<uint8_t>(eth::frameId(eth::msg::BlockHeaders)),
                eth::encodeBlockHeaders(response));  // unknown origin: empty answer
        }
        origin = *number;
    }
    uint64_t const step = _request.skip + 1;
    size_t bytes = 0;
    uint64_t number = origin;
    for (uint64_t served = 0; served < amount && bytes < m_limits.softResponseBytes; ++served)
    {
        auto header = m_source.headerRlp(number);
        if (!header)
        {
            break;  // walked past the head (or below genesis)
        }
        bytes += std::max(header->size(), m_limits.estHeaderBytes);
        response.headers.push_back(std::move(*header));
        // Next number; stop on arithmetic over/underflow (geth's "skip overflow" guard).
        // skip = UINT64_MAX wraps the step to 0: the next number is out of range either way.
        if (step == 0)
        {
            break;
        }
        if (_request.reverse)
        {
            if (number < step)
            {
                break;
            }
            number -= step;
        }
        else
        {
            if (std::numeric_limits<uint64_t>::max() - number < step)
            {
                break;
            }
            number += step;
        }
    }
    return reply(static_cast<uint8_t>(eth::frameId(eth::msg::BlockHeaders)),
        eth::encodeBlockHeaders(response));
}

PeerServer::Outcome PeerServer::serveBodies(eth::GetBlockBodiesMessage const& _request)
{
    eth::BlockBodiesMessage response;
    response.requestId = _request.requestId;
    size_t bytes = 0;
    size_t const count = std::min(_request.hashes.size(), m_limits.maxBodies);
    for (size_t i = 0; i < count && bytes < m_limits.softResponseBytes; ++i)
    {
        auto number = m_source.numberByHash(_request.hashes[i]);
        if (!number)
        {
            continue;  // unknown blocks are skipped, not padded (geth)
        }
        auto body = m_source.body(*number);
        if (!body)
        {
            continue;
        }
        for (auto const& tx : body->transactions)
        {
            bytes += tx.size();
        }
        for (auto const& uncle : body->uncles)
        {
            bytes += uncle.size();
        }
        if (body->withdrawals)
        {
            for (auto const& withdrawal : *body->withdrawals)
            {
                bytes += withdrawal.size();
            }
        }
        response.bodies.push_back(std::move(*body));
    }
    return reply(static_cast<uint8_t>(eth::frameId(eth::msg::BlockBodies)),
        eth::encodeBlockBodies(response));
}

PeerServer::Outcome PeerServer::serveReceipts(
    uint8_t _ethVersion, eth::GetReceiptsMessage const& _request)
{
    eth::ReceiptsMessage response;
    response.requestId = _request.requestId;
    size_t bytes = 0;
    size_t const count = std::min(_request.hashes.size(), m_limits.maxReceipts);
    for (size_t i = 0; i < count && bytes < m_limits.softResponseBytes; ++i)
    {
        auto number = m_source.numberByHash(_request.hashes[i]);
        if (!number)
        {
            continue;  // unknown blocks are skipped (geth)
        }
        auto receipts = m_source.receipts(*number);
        if (!receipts)
        {
            continue;
        }
        std::vector<bcos::bytes> encoded;
        encoded.reserve(receipts->size());
        // eth/70: the first REQUESTED block's list starts at firstBlockReceiptIndex (the
        // requester already holds the earlier ones from a partial answer).
        size_t const first = (_ethVersion >= 70 && i == 0) ? _request.firstBlockReceiptIndex : 0;
        bool truncated = false;
        for (size_t k = first; k < receipts->size(); ++k)
        {
            auto element = eth::encodeReceiptElement((*receipts)[k], _ethVersion);
            if (_ethVersion >= 70 && bytes >= m_limits.softResponseBytes &&
                !(response.receipts.empty() && encoded.empty()))
            {
                // EIP-7975: cut the list here and flag it; the requester continues
                // from this index. Never cut before the very first receipt of a
                // response, or a huge receipt could never be served.
                truncated = true;
                break;
            }
            bytes += element.size();
            encoded.push_back(std::move(element));
        }
        response.receipts.push_back(std::move(encoded));
        if (truncated)
        {
            response.lastBlockIncomplete = true;
            break;
        }
    }
    return reply(static_cast<uint8_t>(eth::frameId(eth::msg::Receipts)),
        eth::encodeReceipts(response, _ethVersion));
}

PeerServer::ExitReason PeerServer::serve(
    rlpx::EstablishedSession& _session, std::atomic<bool> const* _running)
{
    auto const ethVersion = static_cast<uint8_t>(_session.peerStatus.protocolVersion);
    auto const& peer = _session.peerHello.clientId;
    // The range the peer last learned (from our Status at handshake time, refreshed from
    // the source now — a redundant first update is harmless).
    BlockRange announced = m_source.range();
    bool pingOutstanding = false;
    auto const keepRunning = [&]() { return _running == nullptr || _running->load(); };
    try
    {
        while (keepRunning())
        {
            std::optional<rlpx::Message> frame;
            try
            {
                auto decoded = _session.session.recvMessage();
                if (!decoded)
                {
                    // The frame itself (RLP id / snappy) was undecodable: protocol breach.
                    _session.session.sendMessage(rlpx::Message{rlpx::baseMsg::Disconnect,
                        rlpx::encodeDisconnect({rlpx::DisconnectReason::ProtocolBreach})});
                    BCOS_LOG(INFO)
                        << LOG_BADGE("peer-server") << "undecodable frame, disconnecting"
                        << LOG_KV("peer", peer) << LOG_KV("error", decoded.error().message);
                    return ExitReason::ProtocolBreach;
                }
                frame = std::move(*decoded);
            }
            catch (rlpx::SocketTimeout const&)
            {
                // Idle peer: ping once, give up when the next idle interval passes too.
                if (!keepRunning())
                {
                    return ExitReason::Shutdown;
                }
                if (pingOutstanding)
                {
                    BCOS_LOG(INFO) << LOG_BADGE("peer-server") << "peer silent, dropping"
                                   << LOG_KV("peer", peer);
                    return ExitReason::PingTimeout;
                }
                _session.session.sendMessage(
                    rlpx::Message{rlpx::baseMsg::Ping, rlpx::encodePing()});
                pingOutstanding = true;
            }
            if (frame)
            {
                pingOutstanding = false;  // any traffic proves the peer alive
                auto outcome = handle(ethVersion, *frame);
                switch (outcome.kind)
                {
                case Outcome::Kind::Reply:
                    _session.session.sendMessage(outcome.reply);
                    break;
                case Outcome::Kind::Ignore:
                    break;
                case Outcome::Kind::Disconnect:
                    _session.session.sendMessage(rlpx::Message{
                        rlpx::baseMsg::Disconnect, rlpx::encodeDisconnect({outcome.reason})});
                    BCOS_LOG(INFO) << LOG_BADGE("peer-server") << "protocol breach, disconnecting"
                                   << LOG_KV("peer", peer) << LOG_KV("detail", outcome.detail);
                    return ExitReason::ProtocolBreach;
                case Outcome::Kind::PeerClosed:
                    BCOS_LOG(INFO) << LOG_BADGE("peer-server") << "peer disconnected"
                                   << LOG_KV("peer", peer) << LOG_KV("detail", outcome.detail);
                    return ExitReason::PeerDisconnected;
                }
            }
            // Head moved since the peer last heard from us: announce the new range
            // (EIP-7642; the spec asks for an update "about every two minutes", this
            // sends one at most per handled frame / idle wake-up and only on change).
            if (ethVersion >= 69)
            {
                auto const current = m_source.range();
                if (current != announced)
                {
                    eth::BlockRangeUpdateMessage update;
                    update.earliestBlock = current.earliest;
                    update.latestBlock = current.latest;
                    update.latestBlockHash = current.latestHash;
                    _session.session.sendMessage(rlpx::Message{
                        static_cast<uint8_t>(eth::frameId(eth::msg::BlockRangeUpdate)),
                        eth::encodeBlockRangeUpdate(update)});
                    announced = current;
                }
            }
        }
        return ExitReason::Shutdown;
    }
    catch (std::exception const& e)
    {
        if (!keepRunning())
        {
            return ExitReason::Shutdown;  // stop() shut the socket down under us
        }
        bool const closed =
            std::string_view(e.what()).find("closed by peer") != std::string_view::npos;
        BCOS_LOG(INFO) << LOG_BADGE("peer-server") << "session ended" << LOG_KV("peer", peer)
                       << LOG_KV("error", e.what());
        return closed ? ExitReason::PeerDisconnected : ExitReason::TransportError;
    }
}
}  // namespace bcos::devp2p::sync
