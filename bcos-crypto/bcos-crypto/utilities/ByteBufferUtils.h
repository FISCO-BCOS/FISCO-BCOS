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
 * @brief small byte-buffer assembly helpers shared by the VRF and ZKP modules
 * @file ByteBufferUtils.h
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace bcos::crypto
{
// concatenates byte ranges into one freshly allocated buffer
inline bcos::bytes bytesConcat(std::initializer_list<bytesConstRef> _parts)
{
    bcos::bytes out;
    size_t total = 0;
    for (auto const& part : _parts)
    {
        total += part.size();
    }
    out.reserve(total);
    for (auto const& part : _parts)
    {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

inline bytesConstRef refOf(bcos::bytes const& _data)
{
    return bytesConstRef(_data.data(), _data.size());
}

template <size_t N>
bytesConstRef refOf(std::array<std::uint8_t, N> const& _data)
{
    return bytesConstRef(_data.data(), N);
}

template <size_t N>
void appendBytes(bcos::bytes& _buffer, std::array<std::uint8_t, N> const& _data)
{
    _buffer.insert(_buffer.end(), _data.begin(), _data.end());
}

inline void appendBytes(bcos::bytes& _buffer, const std::uint8_t* _data, size_t _len)
{
    _buffer.insert(_buffer.end(), _data, _data + _len);
}
}  // namespace bcos::crypto
