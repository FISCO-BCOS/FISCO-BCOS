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

namespace bcos::ledger::mpt
{

bcos::bytes hexPrefixEncode(std::span<uint8_t const> nibbles, bool terminator)
{
    bcos::bytes out;
    bool const odd = (nibbles.size() % 2 == 1);
    uint8_t firstByte = static_cast<uint8_t>((terminator ? 0x20u : 0x00u) | (odd ? 0x10u : 0x00u));
    if (odd)
    {
        firstByte |= (nibbles[0] & 0x0fu);
    }
    out.push_back(firstByte);
    size_t const start = odd ? 1u : 0u;
    for (size_t i = start; i + 1 < nibbles.size(); i += 2)
    {
        out.push_back(static_cast<bcos::byte>((nibbles[i] << 4u) | (nibbles[i + 1] & 0x0fu)));
    }
    return out;
}

std::pair<std::vector<uint8_t>, bool> hexPrefixDecode(std::span<bcos::byte const> encoded)
{
    if (encoded.empty())
    {
        BOOST_THROW_EXCEPTION(
            MPTDecodeError{} << bcos::errinfo_comment("hexPrefixDecode: empty input"));
    }
    uint8_t const first = encoded[0];
    bool const terminator = ((first & 0x20u) != 0u);
    bool const odd = ((first & 0x10u) != 0u);

    std::vector<uint8_t> nibbles;
    nibbles.reserve((encoded.size() - 1) * 2 + (odd ? 1u : 0u));

    if (odd)
    {
        nibbles.push_back(first & 0x0fu);
    }
    for (size_t i = 1; i < encoded.size(); ++i)
    {
        nibbles.push_back(static_cast<uint8_t>((encoded[i] >> 4u) & 0x0fu));
        nibbles.push_back(static_cast<uint8_t>(encoded[i] & 0x0fu));
    }
    return {std::move(nibbles), terminator};
}

}  // namespace bcos::ledger::mpt
