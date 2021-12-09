/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief common functions for scale codec
 * @file Common.h
 */
#pragma once
#include "Exceptions.h"
#include "../../libutilities/Common.h"
namespace bcos
{
namespace codec
{
namespace scale
{
// represents compact integer value
using CompactInteger = boost::multiprecision::cpp_int;

// OptionalBool is internal extended bool type
enum class OptionalBool : uint8_t
{
    NoneValue = 0u,
    TrueValue = 1u,
    FalseValue = 2u
};

/// categories of compact encoding
struct EncodingCategoryLimits
{
    // min integer encoded by 2 bytes
    constexpr static size_t kMinUint16 = (1ul << 6u);
    // min integer encoded by 4 bytes
    constexpr static size_t kMinUint32 = (1ul << 14u);
    // min integer encoded as multibyte
    constexpr static size_t kMinBigInteger = (1ul << 30u);
};
// calculate number of bytes required
inline size_t countBytes(CompactInteger v)
{
    size_t counter = 0;
    do
    {
        ++counter;
    } while ((v >>= 8) != 0);
    return counter;
}
// Returns the compact encoded length for the given value.
template <typename T, typename I = std::decay_t<T>,
    typename = std::enable_if_t<std::is_integral<I>::value>>
uint32_t compactLen(T val)
{
    if (val < EncodingCategoryLimits::kMinUint16)
        return 1;
    if (val < EncodingCategoryLimits::kMinUint32)
        return 2;
    if (val < EncodingCategoryLimits::kMinBigInteger)
        return 4;
    return countBytes(val);
}
}  // namespace scale
}  // namespace codec
}  // namespace bcos