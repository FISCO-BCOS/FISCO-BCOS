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
 * @file Nibble.cpp
 * @brief Nibble helper routines implementation
 */

#include "Nibble.h"
#include "Errors.h"
#include <boost/throw_exception.hpp>
#include <string>

namespace bcos::ledger::mpt
{

bcos::bytes bytesToNibbles(bcos::bytesConstRef bytes)
{
    bcos::bytes out;
    out.reserve(bytes.size() * NIBBLES_PER_BYTE);
    for (auto const _byte : bytes)
    {
        out.push_back(static_cast<uint8_t>((_byte >> NIBBLE_BITS) & LOW_NIBBLE_MASK));
        out.push_back(static_cast<uint8_t>(_byte & LOW_NIBBLE_MASK));
    }
    return out;
}

bcos::bytes nibblesToBytes(bcos::bytesConstRef nibbles)
{
    if (nibbles.size() % NIBBLES_PER_BYTE != 0)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                  "nibblesToBytes requires even nibble count"));
    }
    bcos::bytes out;
    out.reserve(nibbles.size() / NIBBLES_PER_BYTE);
    for (size_t i = 0; i < nibbles.size(); i += NIBBLES_PER_BYTE)
    {
        // Strict invariant: each nibble must fit in 4 bits.
        if ((nibbles[i] | nibbles[i + 1]) > LOW_NIBBLE_MASK)
        {
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation{} << bcos::errinfo_comment(
                    "nibblesToBytes: nibble value out of range [0, 15] at index " +
                    std::to_string(i) + " or " + std::to_string(i + 1)));
        }
        out.push_back(static_cast<bcos::byte>(
            ((nibbles[i] & LOW_NIBBLE_MASK) << NIBBLE_BITS) | (nibbles[i + 1] & LOW_NIBBLE_MASK)));
    }
    return out;
}

size_t commonPrefixLen(bcos::bytesConstRef lhs, bcos::bytesConstRef rhs) noexcept
{
    size_t const maxLen = std::min(lhs.size(), rhs.size());
    size_t i = 0;
    for (; i < maxLen; ++i)
    {
        if (lhs[i] != rhs[i])
        {
            break;
        }
    }
    return i;
}

}  // namespace bcos::ledger::mpt
