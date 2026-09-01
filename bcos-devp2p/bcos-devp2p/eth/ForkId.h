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
#include <cstdint>

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
}  // namespace bcos::devp2p::eth
