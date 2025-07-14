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
 */

#pragma once

#include <concepts>
#include <coroutine>
#include <utility>

namespace bcos::task
{

template <class Value>
class [[nodiscard]] AwaitableValue
{
public:
    AwaitableValue()
        requires std::default_initializable<Value>
    = default;
    AwaitableValue(Value value) : m_value(std::move(value)) {}
    AwaitableValue(std::in_place_t /*unused*/, auto&&... args)
      : m_value(std::forward<decltype(args)>(args)...)
    {}
    constexpr static bool await_ready() noexcept { return true; }
    constexpr static bool await_suspend([[maybe_unused]] std::coroutine_handle<> handle) noexcept
    {
        return false;
    }
    constexpr Value await_resume() noexcept { return std::move(m_value); }
    const Value& value() const& noexcept { return m_value; }
    Value& value() & noexcept { return m_value; }

private:
    Value m_value;
};
template <>
struct [[nodiscard]] AwaitableValue<void>
{
    AwaitableValue() = default;
    constexpr static bool await_ready() noexcept { return true; }
    constexpr static bool await_suspend([[maybe_unused]] std::coroutine_handle<> handle) noexcept
    {
        return false;
    }
    constexpr static void await_resume() noexcept {}
};
}  // namespace bcos::task