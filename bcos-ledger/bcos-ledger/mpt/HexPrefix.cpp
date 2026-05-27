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
 * @file HexPrefix.cpp
 * @brief Hex-Prefix (compact) encoding implementation per Yellow Paper Appendix C
 */

#include "HexPrefix.h"
#include "Errors.h"
#include <boost/throw_exception.hpp>
#include <cassert>

namespace bcos::ledger::mpt
{

// Header byte bit layout (Yellow Paper Appendix C):
//   bit 5 = leaf flag (1 = leaf / terminator, 0 = extension)
//   bit 4 = odd flag  (1 = odd nibble count, first nibble packed into low 4 bits)
inline constexpr uint8_t HP_LEAF_FLAG = 0b0010'0000u;
inline constexpr uint8_t HP_ODD_FLAG = 0b0001'0000u;
inline constexpr uint8_t HP_NIBBLE_MASK = 0b0000'1111u;

bcos::bytes hexPrefixEncode(std::span<uint8_t const> nibbles, bool isLeaf)
{
    bcos::bytes out;
    bool const odd = (nibbles.size() % 2 == 1);
    uint8_t firstByte = (isLeaf ? HP_LEAF_FLAG : 0u) | (odd ? HP_ODD_FLAG : 0u);
    if (odd)
    {
        assert(nibbles[0] < 16 && "nibble out of range");
        firstByte |= (nibbles[0] & HP_NIBBLE_MASK);
    }
    out.push_back(firstByte);
    size_t const start = odd ? 1u : 0u;
    for (size_t i = start; i + 1 < nibbles.size(); i += 2)
    {
        assert(nibbles[i] < 16 && nibbles[i + 1] < 16 && "nibble out of range");
        out.push_back(
            static_cast<bcos::byte>(((nibbles[i] & 0x0fu) << 4u) | (nibbles[i + 1] & 0x0fu)));
    }
    return out;
}

std::pair<bcos::bytes, bool> hexPrefixDecode(std::span<bcos::byte const> encoded)
{
    if (encoded.empty())
    {
        BOOST_THROW_EXCEPTION(
            MPTDecodeError{} << bcos::errinfo_comment("hexPrefixDecode: empty input"));
    }
    uint8_t const first = encoded[0];
    bool const isLeaf = ((first & HP_LEAF_FLAG) != 0u);
    bool const odd = ((first & HP_ODD_FLAG) != 0u);

    bcos::bytes nibbles;
    nibbles.reserve((encoded.size() - 1) * 2 + (odd ? 1u : 0u));

    if (odd)
    {
        nibbles.push_back(first & HP_NIBBLE_MASK);
    }
    for (size_t i = 1; i < encoded.size(); ++i)
    {
        nibbles.push_back(static_cast<uint8_t>((encoded[i] >> 4u) & 0x0fu));
        nibbles.push_back(static_cast<uint8_t>(encoded[i] & 0x0fu));
    }
    return {std::move(nibbles), isLeaf};
}

}  // namespace bcos::ledger::mpt
