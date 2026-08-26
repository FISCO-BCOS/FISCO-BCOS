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
#include "Nibble.h"
#include <boost/throw_exception.hpp>
#include <cassert>

namespace bcos::ledger::mpt
{

// Header byte bit layout (Yellow Paper Appendix C):
//   bit 5      = leaf flag (1 = leaf / terminator, 0 = extension)
//   bit 4      = odd flag  (1 = odd nibble count, first nibble packed into low 4 bits)
//   bits [3:0] = first nibble (selected with LOW_NIBBLE_MASK from Nibble.h)
inline constexpr uint8_t HP_LEAF_FLAG = 0b0010'0000U;
inline constexpr uint8_t HP_ODD_FLAG = 0b0001'0000U;

bcos::bytes hexPrefixEncode(bcos::bytesConstRef nibbles, bool isLeaf)
{
    bcos::bytes out;
    bool const odd = (nibbles.size() % NIBBLES_PER_BYTE == 1);
    // An empty nibble path is LEGAL for a LEAF on variable-length (raw-key, non-secure)
    // tries: a key that terminates exactly where a branch node's child would continue
    // (or a single-entry leaf whose path is consumed by the time the build reaches
    // depth == nibbles.size()) encodes as the bare header byte (e.g. 0x20 for an even
    // leaf). It is NOT legal for an extension node: an empty extension path encodes as
    // 0x00 and yields a self-consistent root no other client agrees with.
    assert((isLeaf || !nibbles.empty()) && "extension nodes require a non-empty path");
    uint8_t firstByte = (isLeaf ? HP_LEAF_FLAG : 0U) | (odd ? HP_ODD_FLAG : 0U);
    if (odd)
    {
        assert(nibbles[0] < NIBBLE_RANGE && "nibble out of range");
        firstByte |= (nibbles[0] & LOW_NIBBLE_MASK);
    }
    out.push_back(firstByte);
    size_t const start = odd ? 1U : 0U;
    for (size_t i = start; i + 1 < nibbles.size(); i += NIBBLES_PER_BYTE)
    {
        assert(nibbles[i] < NIBBLE_RANGE && nibbles[i + 1] < NIBBLE_RANGE && "nibble out of range");
        out.push_back(static_cast<bcos::byte>(
            ((nibbles[i] & LOW_NIBBLE_MASK) << NIBBLE_BITS) | (nibbles[i + 1] & LOW_NIBBLE_MASK)));
    }
    return out;
}

std::pair<bcos::bytes, bool> hexPrefixDecode(bcos::bytesConstRef encoded)
{
    if (encoded.empty())
    {
        BOOST_THROW_EXCEPTION(
            MPTDecodeError{} << bcos::errinfo_comment("hexPrefixDecode: empty input"));
    }
    uint8_t const first = encoded[0];
    bool const isLeaf = ((first & HP_LEAF_FLAG) != 0U);
    bool const odd = ((first & HP_ODD_FLAG) != 0U);

    bcos::bytes nibbles;
    nibbles.reserve(((encoded.size() - 1) * NIBBLES_PER_BYTE) + (odd ? 1U : 0U));

    if (odd)
    {
        nibbles.push_back(first & LOW_NIBBLE_MASK);
    }
    for (size_t i = 1; i < encoded.size(); ++i)
    {
        nibbles.push_back(static_cast<uint8_t>((encoded[i] >> NIBBLE_BITS) & LOW_NIBBLE_MASK));
        nibbles.push_back(static_cast<uint8_t>(encoded[i] & LOW_NIBBLE_MASK));
    }
    return {std::move(nibbles), isLeaf};
}

}  // namespace bcos::ledger::mpt
