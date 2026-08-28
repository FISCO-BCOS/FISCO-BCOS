/*
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
 * @brief Bridge the callback-style ASIOInterface operations into task::Task coroutines
 * @file AsioAwaitable.h
 */
#pragma once
#include "bcos-gateway/libnetwork/Common.h"
#include <boost/exception/diagnostic_information.hpp>
#include <coroutine>
#include <tuple>
#include <utility>

namespace bcos::gateway
{
// Awaitable bridging one callback-style asio operation into a task::Task coroutine.
//
// Threading contract: the wrapped asio operation never invokes its completion handler inline
// from the initiating call, so the suspended coroutine is always resumed on the io_context
// thread that completes the operation — the same thread the old hand-written callback chain ran
// on. resume() is wrapped in try/catch: an exception escaping the resumed coroutine must not
// unwind the asio handler into io_context::run() (the same containment the hand-written
// callbacks applied). The loop coroutines additionally catch everything internally, so this is
// the second line of defence.
//
// Lifetime contract: the awaitable object lives inside the coroutine frame, and asio invokes
// the completion handler exactly once before the frame can unwind, so the `this` capture is
// valid for as long as the handler may run.
template <typename Initiate, typename... Results>
struct AsioAwaitable
{
    Initiate m_initiate;
    std::tuple<Results...> m_result{};

    constexpr bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle)
    {
        m_initiate([this, handle](Results... results) mutable {
            m_result = std::make_tuple(std::move(results)...);
            try
            {
                handle.resume();
            }
            catch (std::exception const& e)
            {
                ASIO_LOG(WARNING) << LOG_DESC("asio awaitable resume exception")
                                  << LOG_KV("what", boost::diagnostic_information(e));
            }
        });
    }
    std::tuple<Results...> await_resume() { return std::move(m_result); }
};

template <typename... Results, typename Initiate>
auto makeAsioAwaitable(Initiate&& initiate)
{
    return AsioAwaitable<std::decay_t<Initiate>, Results...>{
        std::forward<Initiate>(initiate), {}};
}
}  // namespace bcos::gateway
