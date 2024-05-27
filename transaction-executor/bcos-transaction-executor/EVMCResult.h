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
 * @file Common.h
 * @author: ancelmo
 * @date: 2021-10-08
 */

#pragma once
#include <evmc/instructions.h>
#include <type_traits>

namespace bcos::transaction_executor
{
class EVMCResult : public evmc_result
{
public:
    explicit EVMCResult(evmc_result const& result) : evmc_result(result) {}
    EVMCResult(const EVMCResult&) = delete;
    EVMCResult(EVMCResult&& from) noexcept : evmc_result(from)
    {
        from.release = nullptr;
        from.output_data = nullptr;
        from.output_size = 0;
    }
    EVMCResult& operator=(const EVMCResult&) = delete;
    EVMCResult& operator=(EVMCResult&& from) noexcept
    {
        evmc_result::operator=(from);
        from.release = nullptr;
        from.output_data = nullptr;
        from.output_size = 0;
        return *this;
    }

    ~EVMCResult() noexcept
    {
        if (release != nullptr)
        {
            release(static_cast<evmc_result*>(this));
            release = nullptr;
            output_data = nullptr;
            output_size = 0;
        }
    }
};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;
}  // namespace bcos::transaction_executor