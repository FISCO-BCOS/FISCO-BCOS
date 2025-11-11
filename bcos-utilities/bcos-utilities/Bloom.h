/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file Bloom.h
 * @author: kyonGuo
 * @date 2024/4/12
 */

#pragma once
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-utilities/Common.h"

namespace bcos
{

constexpr static size_t BloomBytesSize = 256;
using Bloom = std::array<bcos::byte, BloomBytesSize>;

constexpr static uint8_t LOWER_3_BITS = 0b00000111;

static void bytesToBloom(bcos::concepts::ByteBuffer auto const& _bytes, Bloom& _bloom)
{
    auto hash = crypto::keccak256Hash(RefDataContainer(_bytes.data(), _bytes.size()));
    static_assert(sizeof(crypto::HashType) == 32);
    static_assert(sizeof(bcos::byte) == 1);
    static_assert(sizeof(unsigned short) == 2);
    for (size_t i = 0; i < 6; i += 2)
    {
        unsigned short bitPosition =
            static_cast<unsigned short>((hash[i] & LOWER_3_BITS) << 8) + hash[i + 1];
        size_t positionInBytes = BloomBytesSize - 1 - (bitPosition / 8);
        _bloom[positionInBytes] |= 1 << (bitPosition % 8);
    }
}


template <class Logs>
    requires ::ranges::input_range<Logs> &&
             std::same_as<typename ::ranges::range_value_t<Logs>, protocol::LogEntry>
Bloom getLogsBloom(Logs logs)
{
    Bloom bloom{};
    for (auto const& log : logs)
    {
        // Convert string_view to RefDataContainer<const unsigned char>
        auto addressView = log.address();
        bytesToBloom(
            RefDataContainer(reinterpret_cast<const byte*>(addressView.data()), addressView.size()),
            bloom);
        auto topics = log.topics();
        for (const auto& topic : topics)
        {
            bytesToBloom(topic, bloom);
        }
    }
    return bloom;
}

void orBloom(Bloom& bloom, bcos::concepts::ByteBuffer auto const& other)
{
    for (size_t i = 0; i < BloomBytesSize; i++)
    {
        bloom[i] |= std::bit_cast<bcos::byte>(other[i]);
    }
}

std::string_view toStringView(Bloom const& bloom);

}  // namespace bcos