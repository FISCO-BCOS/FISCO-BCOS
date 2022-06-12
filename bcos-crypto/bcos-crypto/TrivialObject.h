/**
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
 * @brief concepts for crypto
 * @file Concepts.h
 * @author: ancelmo
 * @date 2022-05-06
 */
#pragma once

#include <ranges>
#include <span>

namespace bcos::crypto::trivial
{

template <class Object>
concept Value = std::is_trivial_v<std::remove_cvref_t<Object>> &&
    !std::is_pointer_v<std::remove_cvref_t<Object>>;

#if (defined __clang__) && (__clang_major__ < 15)
template <class Object>
concept Range = std::ranges::range<std::remove_cvref_t<Object>> &&
    std::is_trivial_v<std::remove_cvref_t<std::ranges::range_value_t<Object>>>;
#else
template <class Object>
concept Range = std::ranges::contiguous_range<std::remove_cvref_t<Object>> &&
    std::is_trivial_v<std::remove_cvref_t<std::ranges::range_value_t<Object>>>;
#endif

template <class Input>
concept Object = Value<Input> || Range<Input>;

constexpr size_t size(Object auto&& obj)
{
    auto view = toView(std::forward<decltype(obj)>(obj));
    return view.size();
}

constexpr auto toView(trivial::Object auto&& object)
{
    using RawType = std::remove_cvref_t<decltype(object)>;

    if constexpr (trivial::Value<RawType>)
    {
        using ByteType =
            std::conditional_t<std::is_const_v<std::remove_reference_t<decltype(object)>>,
                std::byte const, std::byte>;
        std::span<ByteType> view{(ByteType*)&object, sizeof(object)};

        return view;
    }
    else if constexpr (trivial::Range<RawType>)
    {
        using ByteType = std::conditional_t<
            std::is_const_v<std::remove_reference_t<std::ranges::range_value_t<RawType>>>,
            std::byte const, std::byte>;
        std::span<ByteType> view{(ByteType*)std::data(object),
            sizeof(std::remove_cvref_t<std::ranges::range_value_t<RawType>>) * std::size(object)};

        return view;
    }
    else
    {
        static_assert(!sizeof(object), "Unsupported type!");
    }
}

}  // namespace bcos::crypto::trivial