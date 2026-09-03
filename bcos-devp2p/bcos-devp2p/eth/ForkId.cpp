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

}  // namespace bcos::devp2p::eth
