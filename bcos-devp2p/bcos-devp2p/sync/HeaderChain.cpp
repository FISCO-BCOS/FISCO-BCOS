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
 * @file HeaderChain.cpp
 * @brief Header download implementation.
 * @date 2026/8/18
 */
#include "HeaderChain.h"

#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <stdexcept>

namespace bcos::devp2p::sync
{
HeaderChain::HeaderChain(uint64_t _nextNumber, bcos::h256 _anchorHash,
    uint64_t _maxHeadersPerRequest)
  : m_nextNumber(_nextNumber),
    m_anchorHash(_anchorHash),
    m_maxHeadersPerRequest(_maxHeadersPerRequest)
{}

HeaderChain::HeaderChain(uint64_t _nextNumber,
    bcos::protocol::EthBlockHeaderData _anchorHeader, ChainConfig const& _config,
    uint64_t _maxHeadersPerRequest)
  : m_nextNumber(_nextNumber),
    m_anchorHash(headerHash(_anchorHeader)),
    m_anchorHeader(std::move(_anchorHeader)),
    m_config(_config),
    m_maxHeadersPerRequest(_maxHeadersPerRequest)
{}

std::vector<HeaderWithHash> HeaderChain::requestHeaders(
    rlpx::Session& _session, uint64_t _amount)
{
    if (_amount == 0)
    {
        return {};
    }
    _amount = std::min(_amount, m_maxHeadersPerRequest);

    eth::GetBlockHeadersMessage request;
    request.requestId = ++m_requestId;
    request.originNumber = m_nextNumber;
    request.amount = _amount;
    request.skip = 0;
    request.reverse = false;
    auto requestRlp = eth::encodeGetBlockHeaders(request);
    std::cerr << "[HeaderChain] sent GetBlockHeaders id=" << request.requestId
              << " origin=" << request.originNumber << " amount=" << _amount
              << " rlp=" << bcos::toHexStringWithPrefix(requestRlp) << std::endl;
    _session.sendMessage(
        rlpx::Message{static_cast<uint8_t>(eth::frameId(eth::msg::GetBlockHeaders)),
            std::move(requestRlp)});

    // Loop until we get the BlockHeaders response, answering Ping along the way
    // (peers ping us periodically; ignoring a Ping makes geth mark us "useless").
    std::vector<HeaderWithHash> out;
    int ignoredBroadcasts = 0;
    while (true)
    {
        auto response = _session.recvMessage();
        std::cerr << "[HeaderChain] recv msg id=" << static_cast<int>(response.id)
                  << " size=" << response.data.size()
                  << " data=" << bcos::toHexStringWithPrefix(response.data).substr(0, 200)
                  << std::endl;
        if (response.id == rlpx::baseMsg::Ping)
        {
            // RLPx base protocol: Ping (id 0x02) is answered with Pong (id 0x03).
            _session.sendMessage(rlpx::Message{rlpx::baseMsg::Pong, {}});
            std::cerr << "[HeaderChain] answered Ping with Pong" << std::endl;
            continue;
        }
        if (response.id == rlpx::baseMsg::Pong)
        {
            continue;  // answer to a Ping we (currently) never send; ignore
        }
        if (response.id != eth::frameId(eth::msg::BlockHeaders))
        {
            if (response.id == rlpx::baseMsg::Disconnect)
            {
                auto disc = rlpx::decodeDisconnect(
                    bytesConstRef(response.data.data(), response.data.size()));
                throw std::runtime_error(
                    "HeaderChain: peer disconnected: reason=" +
                    std::to_string(static_cast<int>(disc.reason)));
            }
            // Peers freely broadcast transactions/new blocks while we are
            // waiting for the BlockHeaders reply — NewBlockHashes(0x11),
            // Transactions(0x12), NewBlock(0x17), NewPooledTransactionHashes
            // (0x18). Ignore them and keep waiting for the response.
            if (response.id == eth::frameId(eth::msg::NewBlockHashes) ||
                response.id == eth::frameId(eth::msg::Transactions) ||
                response.id == eth::frameId(eth::msg::NewBlock) ||
                response.id == eth::frameId(eth::msg::NewPooledTransactionHashes))
            {
                if (++ignoredBroadcasts > 64)
                {
                    throw std::runtime_error(
                        "HeaderChain: too many broadcast messages before BlockHeaders");
                }
                continue;
            }
            throw std::runtime_error("HeaderChain: expected BlockHeaders, got message id=" +
                                     std::to_string(response.id));
        }
        auto headers = eth::decodeBlockHeaders(
            bytesConstRef(response.data.data(), response.data.size()));
        if (headers.requestId != request.requestId)
        {
            throw std::runtime_error("HeaderChain: request id mismatch");
        }

        out.reserve(headers.headers.size());
        for (size_t i = 0; i < headers.headers.size(); ++i)
        {
            HeaderWithHash header;
            header.rlp = headers.headers[i];
            bcos::protocol::EthBlockHeader ethHeader;
            if (auto err = ethHeader.rlpDecode(bytesConstRef(header.rlp.data(), header.rlp.size())))
        {
            throw std::runtime_error("HeaderChain: header RLP decode failed");
        }
        header.header = ethHeader.data();
        // The header hash is keccak of the received wire encoding.
        header.hash = bcos::crypto::keccak256Hash(
            bytesConstRef(header.rlp.data(), header.rlp.size()));

        // Strictly ascending, contiguous block numbers.
        if (header.number() != m_nextNumber + i)
        {
            throw std::runtime_error("HeaderChain: non-contiguous block numbers (got " +
                                     std::to_string(header.number()) + ", want " +
                                     std::to_string(m_nextNumber + i) + ")");
        }
        // Parent chain: the first header's parent must be our anchor; the rest
        // must chain to the previously received header.
        if (i == 0)
        {
            if (header.parentHash() != m_anchorHash)
            {
                throw std::runtime_error(
                    "HeaderChain: parent hash mismatch (fork or reorg)");
            }
        }
        else if (header.parentHash() != out[i - 1].hash)
        {
            throw std::runtime_error("HeaderChain: broken parent chain");
        }

        // When the anchor header is known, validate the Ethereum PoS field
        // rules against the actual parent header.
        if (m_anchorHeader.has_value())
        {
            auto const& parentHeader =
                i == 0 ? *m_anchorHeader : out[i - 1].header;
            auto result = validateHeaderPoS(header.header, parentHeader, m_config);
            if (!result.valid)
            {
                throw std::runtime_error("HeaderChain: PoS validation failed at block " +
                                         std::to_string(header.number()) + ": " + result.error);
            }
        }
        out.push_back(std::move(header));
        }
        return out;
    }
}

void HeaderChain::advance(uint64_t _count, HeaderWithHash const& _lastHeader)
{
    m_nextNumber += _count;
    m_anchorHash = _lastHeader.hash;
    // Keep the anchor header in sync too: the next batch validates its first
    // header against m_anchorHeader (parent number + 1 check). Stale it and a
    // multi-batch download (peer has more blocks than one batch) fails with
    // "header number must equal the parent number + 1".
    m_anchorHeader = _lastHeader.header;
}

}  // namespace bcos::devp2p::sync
