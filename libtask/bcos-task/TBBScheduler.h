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
#include <oneapi/tbb/task_group.h>
#include <coroutine>

namespace bcos::task::tbb
{

class TBBScheduler
{
private:
    oneapi::tbb::task_group& m_taskGroup;

public:
    TBBScheduler(oneapi::tbb::task_group& taskGroup) : m_taskGroup(taskGroup) {}

    constexpr static bool await_ready() noexcept { return false; }
    void await_suspend([[maybe_unused]] std::coroutine_handle<> handle) noexcept
    {
        m_taskGroup.run([handle]() { const_cast<std::coroutine_handle<>&>(handle).resume(); });
    }
    constexpr static void await_resume() noexcept {}
};

}  // namespace bcos::task::tbb