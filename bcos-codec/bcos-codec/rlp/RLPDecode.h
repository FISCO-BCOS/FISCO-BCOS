/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file RLPDecode.h
 * @author: kyonGuo
 * @date 2024/4/7
 */

#pragma once
#include "Common.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <utility>
#include <vector>

// THANKS TO: RLP implement based on silkworm: https://github.com/erigontech/silkworm.git
// Note:https://ethereum.org/en/developers/docs/data-structures-and-encoding/rlp/
namespace bcos::codec::rlp
{

inline std::tuple<bcos::Error::UniquePtr, Header> decodeHeader(bytesRef& from) noexcept
{
    if (from.size() == 0)
    {
        return {BCOS_ERROR_UNIQUE_PTR(DecodingError::InputTooShort, "Input data is too short"),
            Header()};
    }
    Header header{.isList = false};
    const auto byte{from[0]};
    if (byte < BYTES_HEAD_BASE)
    {
        // it means single byte
        header.payloadLength = 1;
    }
    else if (byte <= LONG_BYTES_HEAD_BASE)
    {
        // it means bytes length is less than 56
        // remove first byte
        from = from.getCroppedData(1);
        header.payloadLength = byte - BYTES_HEAD_BASE;
        if (header.payloadLength == 1)
        {
            if (from.empty())
            {
                return {BCOS_ERROR_UNIQUE_PTR(InputTooShort, "Input data is too short"), Header()};
            }
            if (from[0] < 0x80)
            {
                return {BCOS_ERROR_UNIQUE_PTR(NonCanonicalSize, "NonCanonicalSize"), Header()};
            }
        }
    }
    else if (byte < LIST_HEAD_BASE)
    {
        // it means it is a long bytes, length is GE than 56
        from = from.getCroppedData(1);
        const auto lenOfLen{byte - LONG_BYTES_HEAD_BASE};
        if (std::cmp_greater(lenOfLen, from.size()))
        {
            return {BCOS_ERROR_UNIQUE_PTR(InputTooShort, "Input data is too short"), Header()};
        }
        auto payloadSize =
            fromBigEndian<uint64_t, bcos::bytesConstRef>(from.getCroppedData(0, lenOfLen));
        header.payloadLength = payloadSize;
        from = from.getCroppedData(lenOfLen);
        if (header.payloadLength < 56)
        {
            return {BCOS_ERROR_UNIQUE_PTR(
                        NonCanonicalSize, "The length of the payload is less than 56"),
                Header()};
        }
    }
    else if (byte <= LONG_LIST_HEAD_BASE)
    {
        // it means it is a list, length is less than 56
        from = from.getCroppedData(1);
        header.isList = true;
        header.payloadLength = byte - LIST_HEAD_BASE;
    }
    else
    {
        // it means it is a list, length is GE than 56
        from = from.getCroppedData(1);
        header.isList = true;
        const auto lenOfLen{byte - LONG_LIST_HEAD_BASE};
        if (std::cmp_greater(lenOfLen, from.size()))
        {
            return {BCOS_ERROR_UNIQUE_PTR(DecodingError::InputTooShort, "Input data is too short"),
                Header()};
        }
        auto payloadSize =
            fromBigEndian<uint64_t, bcos::bytesConstRef>(from.getCroppedData(0, lenOfLen));
        header.payloadLength = payloadSize;
        from = from.getCroppedData(lenOfLen);
        if (header.payloadLength < 56)
        {
            return {BCOS_ERROR_UNIQUE_PTR(DecodingError::NonCanonicalSize,
                        "The length of the payload is less than 56"),
                Header()};
        }
    }
    if (header.payloadLength > from.size())
    {
        return {BCOS_ERROR_UNIQUE_PTR(DecodingError::InputTooShort, "Input data is too short"),
            Header()};
    }
    return {nullptr, header};
}

inline bcos::Error::UniquePtr decode(bytesRef& from, bcos::concepts::ByteBuffer auto& to) noexcept
{
    auto&& [error, header] = decodeHeader(from);
    if (error)
    {
        return std::move(error);
    }
    if (header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedList, "Unexpected list");
    }
    if constexpr (std::same_as<std::decay_t<decltype(to)>, bcos::bytes>)
    {
        to = from.getCroppedData(0, header.payloadLength).toBytes();
    }
    else if constexpr (std::same_as<std::decay_t<decltype(to)>, bcos::bytesRef>)
    {
        to = from.getCroppedData(0, header.payloadLength);
    }
    else if constexpr (bcos::concepts::StringLike<std::decay_t<decltype(to)>>)
    {
        to =
            from.getCroppedData(0, header.payloadLength).toStringLike<std::decay_t<decltype(to)>>();
    }
    else if constexpr (std::same_as<std::decay_t<decltype(to)>, bcos::FixedBytes<32>>)
    {
        to = FixedBytes<32>{from.getCroppedData(0, header.payloadLength)};
    }
    else if constexpr (std::same_as<std::decay_t<decltype(to)>, bcos::FixedBytes<20>>)
    {
        to = FixedBytes<20>{from.getCroppedData(0, header.payloadLength)};
    }
    else
    {
        static_assert(!sizeof(to), "Unsupported type");
    }
    from = from.getCroppedData(header.payloadLength);
    return nullptr;
}

inline bcos::Error::UniquePtr decode(bytesRef& from, UnsignedIntegral auto& to) noexcept
{
    auto&& [error, header] = decodeHeader(from);
    if (error)
    {
        return std::move(error);
    }
    if (header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedList, "Unexpected list");
    }
    to = fromBigEndian<std::decay_t<decltype(to)>, bcos::bytesRef>(
        from.getCroppedData(0, header.payloadLength));
    from = from.getCroppedData(header.payloadLength);
    return nullptr;
}

inline bcos::Error::UniquePtr decode(bytesRef& from, bool& to) noexcept
{
    auto&& [error, header] = decodeHeader(from);
    if (error)
    {
        return std::move(error);
    }
    if (header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedList, "Unexpected list");
    }
    if (header.payloadLength != 1)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedLength, "Unexpected length");
    }
    to = from[0] != 0;
    from = from.getCroppedData(1);
    return nullptr;
}

template <typename T>
    requires(!std::same_as<std::remove_cvref_t<T>, bcos::byte>)
inline bcos::Error::UniquePtr decode(bytesRef& from, std::vector<T>& to) noexcept
{
    auto&& [error, header] = decodeHeader(from);
    if (error)
    {
        return std::move(error);
    }
    if (!header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedString, "Unexpected string");
    }
    to.clear();
    auto payloadView = from.getCroppedData(0, header.payloadLength);
    while (!payloadView.empty())
    {
        to.emplace_back();
        if (auto decodeError = decode(payloadView, to.back()); decodeError != nullptr)
        {
            return decodeError;
        }
    }
    from = from.getCroppedData(header.payloadLength);
    return nullptr;
}

template <typename Arg1, typename Arg2>
inline bcos::Error::UniquePtr decodeItems(bytesRef& from, Arg1& arg1, Arg2& arg2) noexcept
{
    if (auto error = decode(from, arg1); error != nullptr)
    {
        return error;
    }
    return decode(from, arg2);
}

template <typename Arg1, typename Arg2, typename... Args>
inline bcos::Error::UniquePtr decodeItems(
    bytesRef& from, Arg1& arg1, Arg2& arg2, Args&... args) noexcept
{
    if (auto error = decode(from, arg1); error != nullptr)
    {
        return error;
    }
    return decodeItems(from, arg2, args...);
}

template <typename Arg1, typename Arg2, typename... Args>
inline bcos::Error::UniquePtr decode(bytesRef& from, Arg1& arg1, Arg2& arg2, Args&... args) noexcept
{
    auto&& [error, header] = decodeHeader(from);
    if (error)
    {
        return std::move(error);
    }
    if (!header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedString, "Unexpected string");
    }
    const uint64_t leftover{from.size() - header.payloadLength};

    if (auto decodeError = decodeItems(from, arg1, arg2, args...); decodeError != nullptr)
    {
        return decodeError;
    }

    if (from.size() != leftover)
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedListElements, "Unexpected list elements");
    }
    return {};
}

}  // namespace bcos::codec::rlp