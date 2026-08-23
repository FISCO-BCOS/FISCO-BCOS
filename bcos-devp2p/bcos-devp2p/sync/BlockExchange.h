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
 * @file BlockExchange.h
 * @brief Block download orchestration: batches of headers + bodies, anchor
 *        advancement, per-block callback (port of silkworm's BlockExchange,
 *        simplified for a single peer / blocking session).
 * @date 2026/8/18
 */
#pragma once

#include "BodySequence.h"
#include "HeaderChain.h"
#include "../rlpx/Session.h"
#include <functional>

namespace bcos::devp2p::sync
{
// Downloads a contiguous range of blocks from a peer by alternating header
// and body requests, and hands each assembled block to a callback in order.
class BlockExchange
{
public:
    using BlockCallback = std::function<void(Block const&)>;

    // `_startNumber`: first block to download (nextNumber of the anchor).
    // `_anchorHash`: hash of the last locally-known block (block startNumber - 1).
    BlockExchange(uint64_t _startNumber, bcos::h256 _anchorHash,
        uint64_t _batchSize = 192)
      : m_headerChain(_startNumber, _anchorHash), m_batchSize(_batchSize)
    {}

    // Same, but with the known anchor header so downloaded headers are also
    // validated against Ethereum PoS field rules.
    BlockExchange(uint64_t _startNumber,
        bcos::protocol::EthBlockHeaderData _anchorHeader,
        ChainConfig const& _config = {}, uint64_t _batchSize = 192)
      : m_headerChain(_startNumber, std::move(_anchorHeader), _config), m_batchSize(_batchSize)
    {}

    // Synchronously download `_count` blocks and invoke `_onBlock` for each in
    // ascending order. Throws on protocol/validation errors.
    void downloadRange(rlpx::Session& _session, uint64_t _count, BlockCallback _onBlock)
    {
        uint64_t remaining = _count;
        while (remaining > 0)
        {
            uint64_t amount = std::min(remaining, m_batchSize);
            auto headers = m_headerChain.requestHeaders(_session, amount);
            if (headers.empty())
            {
                throw std::runtime_error("BlockExchange: peer returned no headers");
            }
            // GetBlockBodies may be answered with FEWER bodies than requested:
            // geth caps the response at its message size limit, so large bodies
            // truncate it. Pair the returned bodies with the leading headers,
            // then re-request the remaining headers until all bodies arrive.
            size_t processed = 0;
            while (processed < headers.size())
            {
                std::vector<HeaderWithHash> pending(headers.begin() +
                        static_cast<std::ptrdiff_t>(processed),
                    headers.end());
                auto blocks = m_bodySequence.requestBodies(_session, pending);
                if (blocks.empty())
                {
                    throw std::runtime_error("BlockExchange: peer returned no bodies");
                }
                for (auto const& block : blocks)
                {
                    _onBlock(block);
                }
                processed += blocks.size();
            }
            // `processed` is >= 1 (the first requestBodies returned at least one
            // body) and <= headers.size().
            m_headerChain.advance(processed, headers[processed - 1]);
            remaining -= processed;
        }
    }

    uint64_t nextNumber() const { return m_headerChain.nextNumber(); }
    bcos::h256 headHash() const { return m_headerChain.anchorHash(); }

private:
    HeaderChain m_headerChain;
    BodySequence m_bodySequence;
    uint64_t m_batchSize;
};
}  // namespace bcos::devp2p::sync
