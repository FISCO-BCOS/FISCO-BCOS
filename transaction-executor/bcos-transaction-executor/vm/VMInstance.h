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
    explicit VMInstance(evmc_vm* _instance) noexcept : m_instance(_instance)
    {
        assert(m_instance);
        // the abi_version of intepreter is EVMC_ABI_VERSION when callback VMFactory::create()
        assert(m_instance->abi_version == EVMC_ABI_VERSION);

        // Set the options.
        if (m_instance->set_option != nullptr)
        {
            m_instance->set_option(m_instance, "advanced", "");
            m_instance->set_option(m_instance, "trace", "");
            // m_instance->set_option(m_instance, "baseline", "");
        }
    }

    ~VMInstance() noexcept { m_instance->destroy(m_instance); }
    VMInstance(VMInstance const&) = delete;
    VMInstance(VMInstance&&) noexcept = default;
    VMInstance& operator=(VMInstance) = delete;
    VMInstance& operator=(VMInstance&&) noexcept = default;

    evmc_result execute(const struct evmc_host_interface* host, struct evmc_host_context* context,
        evmc_revision rev, const evmc_message* msg, const uint8_t* code, size_t codeSize)
    {
        return m_instance->execute(m_instance, host, context, rev, msg, code, codeSize);
    }

    void enableDebugOutput() {}

private:
    /// The VM instance created with VMInstance-C <prefix>_create() function.
    evmc_vm* m_instance = nullptr;
};

inline void releaseResult(evmc_result& result)
{
    if (result.release)
    {
        result.release(std::addressof(result));
    }
}

}  // namespace bcos::transaction_executor
