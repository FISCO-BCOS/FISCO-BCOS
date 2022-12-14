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
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>

namespace bcos::transaction_executor
{
class HostContext;

class Result : public evmc_result
{
public:
    explicit Result(evmc_result const& _result) : evmc_result(_result) {}

    ~Result()
    {
        if (release != nullptr)
        {
            release(this);
        }
    }

    Result(Result&& _other) noexcept : evmc_result(_other) { _other.release = nullptr; }

    Result& operator=(Result&&) = delete;
    Result(Result const&) = delete;
    Result& operator=(Result const&) = delete;

    evmc_status_code status() const { return status_code; }
    int64_t gasLeft() const { return gas_left; }
    bytesConstRef output() const { return {output_data, output_size}; }
};


static std::vector<std::pair<std::string, std::string>> s_evmcOptions;
/// Returns the EVM-C options parsed from command line.
std::vector<std::pair<std::string, std::string>>& evmcOptions() noexcept
{
    return s_evmcOptions;
}

/// Translate the VMSchedule to VMInstance-C revision.
evmc_revision toRevision(VMSchedule const& _schedule)
{
    if (_schedule.enableLondon)
        return EVMC_LONDON;
    if (_schedule.enableIstanbul)
        return EVMC_ISTANBUL;
    if (_schedule.haveCreate2)
        return EVMC_CONSTANTINOPLE;
    if (_schedule.haveRevert)
        return EVMC_BYZANTIUM;
    if (_schedule.eip158Mode)
        return EVMC_SPURIOUS_DRAGON;
    if (_schedule.eip150Mode)
        return EVMC_TANGERINE_WHISTLE;
    if (_schedule.haveDelegateCall)
        return EVMC_HOMESTEAD;
    return EVMC_FRONTIER;
}

/// The RAII wrapper for an VMInstance-C instance.
class VMInstance
{
public:
    explicit VMInstance(evmc_vm* _instance) noexcept : m_instance(_instance)
    {
        assert(m_instance != nullptr);
        // the abi_version of intepreter is EVMC_ABI_VERSION when callback VMFactory::create()
        assert(m_instance->abi_version == EVMC_ABI_VERSION);

        // Set the options.
        if (m_instance->set_option != nullptr)
        {
            for (auto& pair : evmcOptions())
            {
                m_instance->set_option(m_instance, pair.first.c_str(), pair.second.c_str());
            }
        }
    }

    ~VMInstance() noexcept { m_instance->destroy(m_instance); }

    VMInstance(VMInstance const&) = delete;
    VMInstance& operator=(VMInstance) = delete;

    Result exec(HostContext& _hostContext, evmc_revision _rev, evmc_message* _msg,
        const uint8_t* _code, size_t _code_size)
    {
        Result result = Result(m_instance->execute(
            m_instance, hostContext.interface, &hostContext, _rev, _msg, _code, _code_size));
        return result;
    }

    void enableDebugOutput() {}

private:
    /// The VM instance created with VMInstance-C <prefix>_create() function.
    evmc_vm* m_instance = nullptr;
};

}  // namespace bcos::transaction_executor
