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
 * @file Bloom.cpp
 * @author: kyonGuo
 * @date 2024/4/12
 */

#include "Bloom.h"

using namespace bcos;
using namespace bcos::rpc;

void rpc::bytesToBloom(concepts::ByteBuffer auto const& _bytes, Bloom& _bloom)
{
    auto hash = crypto::keccak256Hash(RefDataContainer(_bytes.data(), _bytes.size()));
    static_assert(sizeof(crypto::HashType) == 32);
    static_assert(sizeof(bcos::byte) == 1);
    static_assert(sizeof(unsigned short) == 2);
    for (size_t i = 0; i < 6; i += 2)
    {
        // bitPosition = ((first byte) & 0x07 << 8) + (second byte)
        const unsigned short bitPosition =
            static_cast<unsigned short>((hash[i] & LOWER_3_BITS) << 8) + hash[i + 1];
        const size_t positionInBytes = BloomBytesSize - 1 - bitPosition / 8;
        _bloom[positionInBytes] |= 1 << (bitPosition % 8);
    }
}

Bloom rpc::getLogsBloom(Logs const& logs)
{
    Bloom bloom{};
    for (auto const& log : logs)
    {
        bytesToBloom(log.address, bloom);
        for (auto& topic : log.topics)
        {
            bytesToBloom(topic, bloom);
        }
    }
    return bloom;
}