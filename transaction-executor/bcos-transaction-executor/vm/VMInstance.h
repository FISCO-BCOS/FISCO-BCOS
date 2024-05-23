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
#include "../EVMCResult.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Overloaded.h>
#include <evmc/evmc.h>
#include <evmone/evmone.h>
#include <evmone/advanced_analysis.hpp>
#include <evmone/advanced_execution.hpp>
#include <evmone/baseline.hpp>
#include <evmone/vm.hpp>

namespace bcos::transaction_executor
{

struct ReleaseEVMC
{
    void operator()(evmc_vm* ptr) const noexcept;
};

/// The RAII wrapper for an VMInstance-C instance.
class VMInstance
{
private:
    std::shared_ptr<evmone::baseline::CodeAnalysis const> m_instance;

public:
    explicit VMInstance(std::shared_ptr<evmone::baseline::CodeAnalysis const> instance) noexcept;
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

template <>
struct std::equal_to<evmc_address>
{
    bool operator()(const evmc_address& lhs, const evmc_address& rhs) const noexcept;
};

template <>
struct boost::hash<evmc_address>
{
    size_t operator()(const evmc_address& address) const noexcept;
};

template <>
struct std::hash<evmc_address>
{
    size_t operator()(const evmc_address& address) const noexcept;
};