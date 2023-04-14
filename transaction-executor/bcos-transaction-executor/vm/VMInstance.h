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

struct EVMCResult
{
public:
    EVMCResult() { m_evmcResult.release = nullptr; }
    ~EVMCResult() noexcept
    {
        if (m_evmcResult.release != nullptr)
        {
            m_evmcResult.release(&m_evmcResult);
        }
    }

    EVMCResult(EVMCResult&& evmcResult) noexcept : m_evmcResult(evmcResult.m_evmcResult)
    {
        evmcResult.m_evmcResult.release = nullptr;
    }
    EVMCResult& operator=(EVMCResult&& evmcResult) noexcept
    {
        m_evmcResult = evmcResult.m_evmcResult;
        evmcResult.m_evmcResult.release = nullptr;
        return *this;
    }
    EVMCResult(EVMCResult const&) = delete;
    EVMCResult& operator=(EVMCResult const&) = delete;

    evmc_result m_evmcResult;
};

struct EVMCMessage : public evmc_message
{
};

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
        assert(m_instance);
        // the abi_version of intepreter is EVMC_ABI_VERSION when callback VMFactory::create()
        assert(m_instance->abi_version == EVMC_ABI_VERSION);

        // Set the options.
        if (m_instance->set_option != nullptr)
        {
            // TODO: set some options
        }
    }

    ~VMInstance() noexcept { m_instance->destroy(m_instance); }
    VMInstance(VMInstance const&) = delete;
    VMInstance& operator=(VMInstance) = delete;

    EVMCResult execute(const struct evmc_host_interface* host, struct evmc_host_context* context,
        evmc_revision rev, evmc_message* msg, const uint8_t* code, size_t codeSize)
    {
        EVMCResult evmcResult;
        evmcResult.m_evmcResult =
            m_instance->execute(m_instance, host, context, rev, msg, code, codeSize);
        return evmcResult;
    }

    void enableDebugOutput() {}

private:
    /// The VM instance created with VMInstance-C <prefix>_create() function.
    evmc_vm* m_instance = nullptr;
};

}  // namespace bcos::transaction_executor
