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
#include "bcos-executor/src/Common.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Overloaded.h>
#include <evmc/evmc.h>
#include <evmone/advanced_analysis.hpp>
#include <evmone/advanced_execution.hpp>
#include <evmone/baseline.hpp>
#include <variant>

namespace bcos::transaction_executor
{

/// The RAII wrapper for an VMInstance-C instance.
class VMInstance
{
private:
    struct ReleaseEVMC
    {
        void operator()(evmc_vm* ptr) const noexcept;
    };
    using EVMC_VM = std::unique_ptr<evmc_vm, ReleaseEVMC>;
    using EVMC_ANALYSIS_RESULT = std::shared_ptr<evmone::baseline::CodeAnalysis const>;
    std::variant<EVMC_VM, EVMC_ANALYSIS_RESULT> m_instance;

public:
    template <class Instance>
    explicit VMInstance(Instance instance) noexcept
        requires std::same_as<Instance, evmc_vm*> ||
                 std::same_as<Instance, std::shared_ptr<evmone::baseline::CodeAnalysis const>>
    {
        if constexpr (std::is_same_v<Instance, evmc_vm*>)
        {
            assert(instance->abi_version == EVMC_ABI_VERSION);
            m_instance.emplace<EVMC_VM>(instance);
        }
        else
        {
            m_instance.emplace<EVMC_ANALYSIS_RESULT>(std::move(instance));
        }
    }
    ~VMInstance() noexcept = default;

    VMInstance(VMInstance const&) = delete;
    VMInstance(VMInstance&&) noexcept = default;
    VMInstance& operator=(VMInstance const&) = delete;
    VMInstance& operator=(VMInstance&&) noexcept = default;

    EVMCResult execute(const struct evmc_host_interface* host, struct evmc_host_context* context,
        evmc_revision rev, const evmc_message* msg, const uint8_t* code, size_t codeSize);

    void enableDebugOutput();
};

}  // namespace bcos::transaction_executor
