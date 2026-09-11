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
 * @file BodySequence.cpp
 * @brief Block-body download implementation.
 * @date 2026/8/18
 */
#include "BodySequence.h"

#include <stdexcept>

namespace bcos::devp2p::sync
{
std::vector<Block> BodySequence::requestBodies(
    rlpx::Session& _session, std::vector<HeaderWithHash> const& _headers)
{
    if (_headers.empty())
    {
        return {};
    }

    eth::GetBlockBodiesMessage request;
    request.requestId = ++m_requestId;
    request.hashes.reserve(_headers.size());
    for (auto const& header : _headers)
    {
        request.hashes.push_back(header.hash);
    }
    _session.sendMessage(
        rlpx::Message{static_cast<uint8_t>(eth::frameId(eth::msg::GetBlockBodies)),
            eth::encodeGetBlockBodies(request)});

    // Peers freely broadcast transactions/new blocks while we wait for the
    // BlockBodies reply — ignore them (and answer Ping) and keep waiting.
    int ignoredBroadcasts = 0;
    std::vector<Block> out;
    while (true)
    {
        auto response = _session.recvMessage();
        if (response.id == rlpx::baseMsg::Ping)
        {
            _session.sendMessage(rlpx::Message{rlpx::baseMsg::Pong, {}});
            continue;
        }
        if (response.id == rlpx::baseMsg::Pong)
        {
            continue;
        }
        if (response.id != eth::frameId(eth::msg::BlockBodies))
        {
            if (response.id == rlpx::baseMsg::Disconnect)
            {
                auto disc = rlpx::decodeDisconnect(
                    bytesConstRef(response.data.data(), response.data.size()));
                throw std::runtime_error(
                    "BodySequence: peer disconnected: reason=" +
                    std::to_string(static_cast<int>(disc.reason)));
            }
            if (response.id == eth::frameId(eth::msg::NewBlockHashes) ||
                response.id == eth::frameId(eth::msg::Transactions) ||
                response.id == eth::frameId(eth::msg::NewBlock) ||
                response.id == eth::frameId(eth::msg::NewPooledTransactionHashes))
            {
                if (++ignoredBroadcasts > 64)
                {
                    throw std::runtime_error(
                        "BodySequence: too many broadcast messages before BlockBodies");
                }
                continue;
            }
            throw std::runtime_error("BodySequence: expected BlockBodies, got message id=" +
                                     std::to_string(response.id));
        }
        auto bodies = eth::decodeBlockBodies(
            bytesConstRef(response.data.data(), response.data.size()));
        if (bodies.requestId != request.requestId)
        {
            throw std::runtime_error("BodySequence: request id mismatch");
        }
        // The eth protocol lets a peer answer GetBlockBodies with FEWER bodies
        // than requested: geth caps the response at its message size limit, so
        // large bodies (blocks with many transactions/uncles) truncate it. A
        // partial response pairs bodies[i] with headers[i]; the caller
        // (BlockExchange) re-requests the remaining headers. An empty response
        // is a hard error — retrying it would just loop forever.
        if (bodies.bodies.size() > _headers.size())
        {
            throw std::runtime_error("BodySequence: body count overflow (got " +
                                     std::to_string(bodies.bodies.size()) + ", want " +
                                     std::to_string(_headers.size()) + ")");
        }
        if (bodies.bodies.empty())
        {
            throw std::runtime_error("BodySequence: peer returned no bodies");
        }

        out.reserve(bodies.bodies.size());
        for (size_t i = 0; i < bodies.bodies.size(); ++i)
        {
            Block block;
            block.header = _headers[i].header;
            block.hash = _headers[i].hash;
            block.headerRlp = _headers[i].rlp;
            block.transactions = bodies.bodies[i].transactions;
            block.uncles = bodies.bodies[i].uncles;
            block.withdrawals = bodies.bodies[i].withdrawals;
            out.push_back(std::move(block));
        }
        return out;
    }
}

}  // namespace bcos::devp2p::sync
