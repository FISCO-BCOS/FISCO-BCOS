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
 * @file ForkId.cpp
 * @brief EIP-2124 forkid CRC32 implementation.
 * @date 2026/8/18
 */
#include "ForkId.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace bcos::devp2p::eth
{
namespace
{
std::array<uint32_t, 256> const& crc32Table()
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
            {
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    return table;
}
}  // namespace

uint32_t crc32(bytesConstRef _data, uint32_t _seed)
{
    uint32_t c = _seed ^ 0xFFFFFFFFu;
    for (auto byte : _data)
    {
        c = crc32Table()[(c ^ byte) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

uint32_t forkIdAddForkPoint(uint32_t _hash, uint64_t _forkPoint)
{
    // The fork point is encoded as a fixed 8-byte big-endian u64 (matches geth
    // forkid.go).
    std::array<uint8_t, 8> forkBytes{};
    for (int i = 0; i < 8; ++i)
    {
        forkBytes[7 - i] = static_cast<uint8_t>(_forkPoint >> (8 * i));
    }
    // geth seeds crc32 with the previous hash value.
    return crc32(bytesConstRef(forkBytes.data(), forkBytes.size()), _hash);
}


namespace
{
// geth forkid.timestampThreshold: a `next` value above the mainnet genesis timestamp
// is a fork TIME, anything below a fork BLOCK (the two namespaces never overlap in
// practice).
constexpr uint64_t c_timestampThreshold = 1438269973;
}  // namespace

ForkFilter::ForkFilter(bcos::h256 const& _genesisHash, uint64_t _genesisTime,
    std::span<const uint64_t> _blockForks, std::span<const uint64_t> _timeForks)
{
    uint64_t previous = 0;
    for (uint64_t fork : _blockForks)
    {
        if (fork == std::numeric_limits<uint64_t>::max())
        {
            break;
        }
        if (fork == 0 || fork == previous)
        {
            continue;
        }
        previous = fork;
        m_blockForks.push_back(fork);
    }
    previous = 0;
    for (uint64_t fork : _timeForks)
    {
        if (fork == std::numeric_limits<uint64_t>::max())
        {
            break;
        }
        if (fork == 0 || fork <= _genesisTime || fork == previous)
        {
            continue;
        }
        previous = fork;
        m_timeForks.push_back(fork);
    }
    uint32_t sum = crc32(bytesConstRef(_genesisHash.data(), _genesisHash.size()));
    m_sums.push_back(sum);
    for (uint64_t fork : m_blockForks)
    {
        sum = forkIdAddForkPoint(sum, fork);
        m_sums.push_back(sum);
    }
    for (uint64_t fork : m_timeForks)
    {
        sum = forkIdAddForkPoint(sum, fork);
        m_sums.push_back(sum);
    }
}

std::optional<std::string> ForkFilter::reject(
    ForkId const& _remote, uint64_t _headNumber, uint64_t _headTime) const
{
    // Walk the combined ladder (block forks, then time forks) with a trailing sentinel
    // that is never passed, exactly like geth's Filter closure.
    size_t const count = m_blockForks.size() + m_timeForks.size();
    for (size_t i = 0; i <= count; ++i)
    {
        bool passed = false;
        if (i < m_blockForks.size())
        {
            passed = _headNumber >= m_blockForks[i];
        }
        else if (i < count)
        {
            passed = _headTime >= m_timeForks[i - m_blockForks.size()];
        }
        if (passed)
        {
            continue;  // our head is beyond this fork, keep walking
        }
        // First fork the local head has NOT passed. Rule #1: the checksums match.
        if (m_sums[i] == _remote.hash)
        {
            // #1a: the remote announces a next fork that already passed locally without
            // us knowing it -> we are the stale one.
            if (_remote.next > 0 &&
                ((_remote.next <= c_timestampThreshold && _headNumber >= _remote.next) ||
                    (_remote.next > c_timestampThreshold && _headTime >= _remote.next)))
            {
                return "local node is incompatible or stale (remote announces a passed fork)";
            }
            return std::nullopt;  // #1b
        }
        // Rule #2: the remote checksum is a strict subset of ours (the remote is behind);
        // it must announce exactly the fork we passed next.
        for (size_t j = 0; j < i; ++j)
        {
            if (m_sums[j] == _remote.hash)
            {
                uint64_t const expectedNext = j < m_blockForks.size() ?
                                                  m_blockForks[j] :
                                                  m_timeForks[j - m_blockForks.size()];
                if (expectedNext != _remote.next)
                {
                    return "remote node is stale (does not announce the next fork)";
                }
                return std::nullopt;
            }
        }
        // Rule #3: the remote checksum is a superset of ours (we are behind): accept and
        // ignore the upcoming forks.
        for (size_t j = i + 1; j < m_sums.size(); ++j)
        {
            if (m_sums[j] == _remote.hash)
            {
                return std::nullopt;
            }
        }
        // Rule #4: no exact, subset or superset match — different chains.
        return "local node is incompatible or stale (remote fork checksum unknown)";
    }
    return "local node is incompatible or stale";
}

}  // namespace bcos::devp2p::eth
