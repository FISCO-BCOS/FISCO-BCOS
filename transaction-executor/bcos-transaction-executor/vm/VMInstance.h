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
 * @brief c++ wrapper of vm
 * @file VMInstance.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 * @author: ancelmo
 * @date: 2023-3-3
 */

#pragma once
#include "../Common.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Overloaded.h>
#include <evmc/evmc.h>
#include <evmone/advanced_analysis.hpp>
#include <evmone/advanced_execution.hpp>
#include <variant>

namespace bcos::transaction_executor
{

/// Translate the VMSchedule to VMInstance-C revision.
inline evmc_revision toRevision(VMSchedule const& _schedule)
{
    if (_schedule.enablePairs)
    {
        return EVMC_PARIS;
    }
    return EVMC_LONDON;
}

/// The RAII wrapper for an VMInstance-C instance.
class VMInstance
{
public:
    explicit VMInstance(auto instance) noexcept : m_instance(std::move(instance))
    {
        if (std::holds_alternative<evmc_vm*>(m_instance))
        {
            auto* instance = std::get<evmc_vm*>(m_instance);
            assert(instance->abi_version == EVMC_ABI_VERSION);
            if (instance->set_option != nullptr)
            {
                instance->set_option(instance, "advanced", "");
                instance->set_option(instance, "trace", "");
                // m_instance->set_option(m_instance, "baseline", "");
            }
        }
    }

    ~VMInstance() noexcept
    {
        if (std::holds_alternative<evmc_vm*>(m_instance))
        {
            auto* instance = std::get<evmc_vm*>(m_instance);
            instance->destroy(instance);
        }
    }
    VMInstance(VMInstance const&) = delete;
    VMInstance(VMInstance&&) noexcept = default;
    VMInstance& operator=(VMInstance) = delete;
    VMInstance& operator=(VMInstance&&) noexcept = default;

    evmc_result execute(const struct evmc_host_interface* host, struct evmc_host_context* context,
        evmc_revision rev, const evmc_message* msg, const uint8_t* code, size_t codeSize)
    {
        return std::visit(
            overloaded{[&](evmc_vm* instance) {
                           return instance->execute(
                               instance, host, context, rev, msg, code, codeSize);
                       },
                [&](std::shared_ptr<evmone::advanced::AdvancedCodeAnalysis const> const& instance) {
                    auto state = evmone::advanced::AdvancedExecutionState(
                        *msg, rev, *host, context, std::basic_string_view<uint8_t>(code, codeSize));
                    return evmone::advanced::execute(state, *instance);
                }},
            m_instance);
    }

    void enableDebugOutput() {}

private:
    std::variant<evmc_vm*, std::shared_ptr<evmone::advanced::AdvancedCodeAnalysis const>>
        m_instance;
};

inline void releaseResult(evmc_result& result)
{
    if (result.release)
    {
        result.release(std::addressof(result));
        result.release = nullptr;
        result.output_data = nullptr;
        result.output_size = 0;
    }
}

}  // namespace bcos::transaction_executor
