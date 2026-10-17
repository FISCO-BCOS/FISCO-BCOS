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
// A value that is ALREADY THERE: await_ready() is always true, so `co_await` never suspends and the
// awaiting frame is never left parked. This is the synchronous end of the library's awaitables —
// the counterpart of task::OneShot, which delivers its values LATER (and may never deliver them at all).
//
// The name is load-bearing, not decorative: code in the tree asserts on this exact type to prove a
// backend can be driven without ever blocking a worker thread (MPTPruner's "never suspends" check).
// "Immediate" — not merely "awaitable", which every awaitable in this library already is — is the
// property those traits and callers depend on.
template <class Value>
class [[nodiscard]] Immediate
{
public:
    Immediate()
        requires std::default_initializable<Value>
    = default;
    Immediate(Value value) : m_value(std::move(value)) {}
    Immediate(std::in_place_t /*unused*/, auto&&... args)
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
struct [[nodiscard]] Immediate<void>
{
    Immediate() = default;
    constexpr static bool await_ready() noexcept { return true; }
    constexpr static bool await_suspend([[maybe_unused]] std::coroutine_handle<> handle) noexcept
    {
        return false;
    }
    constexpr void await_resume() noexcept {}
};
}  // namespace bcos::task
