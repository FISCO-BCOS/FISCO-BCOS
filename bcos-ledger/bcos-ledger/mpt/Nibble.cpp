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

namespace bcos::ledger::mpt
{

std::vector<uint8_t> bytesToNibbles(std::span<bcos::byte const> in)
{
    std::vector<uint8_t> out;
    out.reserve(in.size() * 2);
    for (bcos::byte b : in)
    {
        out.push_back(static_cast<uint8_t>((b >> 4) & 0x0f));
        out.push_back(static_cast<uint8_t>(b & 0x0f));
    }
    return out;
}

bcos::bytes nibblesToBytes(std::span<uint8_t const> nibbles)
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
        out.push_back(static_cast<bcos::byte>((nibbles[i] << 4) | (nibbles[i + 1] & 0x0f)));
    }
    return out;
}

size_t commonPrefixLen(std::span<uint8_t const> a, std::span<uint8_t const> b) noexcept
{
    size_t const maxLen = std::min(a.size(), b.size());
    size_t i = 0;
    for (; i < maxLen; ++i)
    {
        if (a[i] != b[i])
        {
            break;
        }
    }
    return i;
}

}  // namespace bcos::ledger::mpt
