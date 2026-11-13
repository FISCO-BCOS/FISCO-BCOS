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
 * @file ForkId.h
 * @brief EIP-2124 fork identifier (CRC32 over genesis + fork points).
 * @date 2026/8/18
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace bcos::devp2p::eth
{
// EIP-2124 fork identifier.
struct ForkId
{
    uint32_t hash{0};  // first 4 bytes of the genesis hash, CRC32-chained with fork points
    uint64_t next{0};  // next fork block/time greater than the head (0 if none)

    bool operator==(ForkId const& _rhs) const { return hash == _rhs.hash && next == _rhs.next; }
};

// CRC-32 (IEEE 802.3, identical to zlib's crc32) over `_data`, continuing from `_seed`.
uint32_t crc32(bytesConstRef _data, uint32_t _seed = 0);

// Chain a fork point (block number or timestamp) into the fork-id hash.
uint32_t forkIdAddForkPoint(uint32_t _hash, uint64_t _forkPoint);

// Compute the EIP-2124 fork-id over a timestamp fork ladder, mirroring the
// timestamp half of geth's forkid.NewID: every fork point <= _localHeadTime is
// chained into the checksum and the first not-yet-passed one is announced as
// `next`. `_forks` must be in activation order. Semantics:
//   * fork == 0          -> active from genesis, never a fork-id point (skipped);
//   * fork <= _genesisTime -> active at the genesis block itself: geth's gatherForks
//     drops it, so it never enters the checksum either;
//   * fork == previous fork point -> geth's gatherForks deduplicates adjacent equal
//     time-based fork points; a duplicate chains into the checksum only once;
//   * fork == UINT64_MAX -> unscheduled ("not yet active", the NodeConfig default
//     for an absent key); it is the END of the ladder: nothing after it is chained
//     and `next` falls back to 0 — geth announces next = 0 when no future fork is
//     known, never UINT64_MAX.
inline ForkId forkIdFromTimeLadder(uint32_t _checksum, uint64_t _genesisTime,
    uint64_t _localHeadTime, std::span<const uint64_t> _forks)
{
    uint32_t hash = _checksum;
    uint64_t previousFork = 0;
    for (uint64_t fork : _forks)
    {
        if (fork == std::numeric_limits<uint64_t>::max())
        {
            break;  // unscheduled tail fork: no future fork known -> next = 0
        }
        if (fork == 0 || fork <= _genesisTime || fork == previousFork)
        {
            continue;  // active at/from genesis, or a duplicate of the previous point
        }
        previousFork = fork;
        if (fork <= _localHeadTime)
        {
            hash = forkIdAddForkPoint(hash, fork);
        }
        else
        {
            return {hash, fork};  // first fork the local head has not passed yet
        }
    }
    return {hash, 0};
}

// initializer_list convenience form (fixed ladders); delegates to the span overload so
// callers with a dynamically-compacted ladder (e.g. the OP schedule, whose unscheduled
// rungs must be dropped rather than terminate the ladder) share the one implementation.
inline ForkId forkIdFromTimeLadder(uint32_t _checksum, uint64_t _genesisTime,
    uint64_t _localHeadTime, std::initializer_list<uint64_t> const& _forks)
{
    return forkIdFromTimeLadder(
        _checksum, _genesisTime, _localHeadTime, std::span<const uint64_t>(_forks));
}

// EIP-2124 validation of a REMOTE fork-id against the local chain (port of geth
// forkid.NewFilter / Filter). Built once from the chain genesis and its fork ladder;
// accepts() is evaluated at the local head of the moment. Fork points are gathered the
// way geth's gatherForks does: block forks of 0 and time forks at/before the genesis
// time are dropped, adjacent duplicates collapse, and an unscheduled UINT64_MAX ends
// the ladder.
class ForkFilter
{
public:
    ForkFilter(bcos::h256 const& _genesisHash, uint64_t _genesisTime,
        std::span<const uint64_t> _blockForks, std::span<const uint64_t> _timeForks);

    // Empty = the remote fork-id is compatible with the local chain at the given head;
    // otherwise the EIP-2124 rejection reason (remote stale / local incompatible or
    // stale), for the Disconnect log line.
    std::optional<std::string> reject(
        ForkId const& _remote, uint64_t _headNumber, uint64_t _headTime) const;
    bool accepts(ForkId const& _remote, uint64_t _headNumber, uint64_t _headTime) const
    {
        return !reject(_remote, _headNumber, _headTime).has_value();
    }

private:
    std::vector<uint64_t> m_blockForks;
    std::vector<uint64_t> m_timeForks;
    // m_sums[i] = checksum after the first i fork points (m_sums[0] = crc32(genesis)).
    std::vector<uint32_t> m_sums;
};
}  // namespace bcos::devp2p::eth
