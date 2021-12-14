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
 */

#pragma once
#include "../Common.h"
#include "bcos-framework/libutilities/Common.h"
#include <evmc/evmc.h>

namespace bcos
{
namespace executor
{
class HostContext;

class Result : public evmc_result
{
public:
    explicit Result(evmc_result const& _result) : evmc_result(_result) {}

    ~Result()
    {
        if (release)
            release(this);
    }

    Result(Result&& _other) noexcept : evmc_result(_other) { _other.release = nullptr; }

    Result& operator=(Result&&) = delete;
    Result(Result const&) = delete;
    Result& operator=(Result const&) = delete;

    evmc_status_code status() const { return status_code; }
    int64_t gasLeft() const { return gas_left; }
    bytesConstRef output() const { return {output_data, output_size}; }
};


/// Returns the EVM-C options parsed from command line.
std::vector<std::pair<std::string, std::string>>& evmcOptions() noexcept;

/// Translate the EVMSchedule to VMInstance-C revision.
evmc_revision toRevision(EVMSchedule const& _schedule);

/// The RAII wrapper for an VMInstance-C instance.
class VMInstance
{
public:
    explicit VMInstance(evmc_vm* _instance) noexcept;
    ~VMInstance() { m_instance->destroy(m_instance); }

    VMInstance(VMInstance const&) = delete;
    VMInstance& operator=(VMInstance) = delete;

    Result exec(HostContext& _hostContext, evmc_revision _rev, evmc_message* _msg,
        const uint8_t* _code, size_t _code_size);

    void enableDebugOutput();

private:
    /// The VM instance created with VMInstance-C <prefix>_create() function.
    evmc_vm* m_instance = nullptr;
};

}  // namespace executor
}  // namespace bcos
