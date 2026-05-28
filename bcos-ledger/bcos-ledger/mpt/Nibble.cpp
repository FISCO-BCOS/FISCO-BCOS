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
    out.reserve(bytes.size() * 2);
    for (bcos::byte b : bytes)
    {
        out.push_back(static_cast<uint8_t>((b >> 4) & 0x0f));
        out.push_back(static_cast<uint8_t>(b & 0x0f));
    }
    return out;
}

bcos::bytes nibblesToBytes(bcos::bytesConstRef nibbles)
{
    if (nibbles.size() % 2 != 0)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                  "nibblesToBytes requires even nibble count"));
    }
    bcos::bytes out;
    out.reserve(nibbles.size() / 2);
    for (size_t i = 0; i < nibbles.size(); i += 2)
    {
        // Strict invariant: each nibble must fit in 4 bits.
        if ((nibbles[i] | nibbles[i + 1]) > 0x0fu)
        {
            BOOST_THROW_EXCEPTION(
                MPTInvariantViolation{} << bcos::errinfo_comment(
                    "nibblesToBytes: nibble value out of range [0, 15] at index " +
                    std::to_string(i) + " or " + std::to_string(i + 1)));
        }
        out.push_back(
            static_cast<bcos::byte>(((nibbles[i] & 0x0fu) << 4) | (nibbles[i + 1] & 0x0fu)));
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
