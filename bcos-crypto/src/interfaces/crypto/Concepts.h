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

namespace bcos::crypto
{

template <class Value>
concept TrivialValue = std::is_trivial_v<std::remove_cvref_t<Value>>;

#if (defined __clang__) && (__clang_major__ < 15)
template <class Range>
concept TrivialRange = std::ranges::range<std::remove_cvref_t<Range>> &&
    std::is_trivial_v<std::remove_cvref_t<std::ranges::range_value_t<Range>>>;
#else
template <class Range>
concept TrivialRange = std::ranges::contiguous_range<std::remove_cvref_t<Range>> &&
    std::is_trivial_v<std::remove_cvref_t<std::ranges::range_value_t<Range>>>;
#endif

template <class Object>
concept TrivialObject = TrivialValue<Object> || TrivialRange<Object>;

}  // namespace bcos::crypto