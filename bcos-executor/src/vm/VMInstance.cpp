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
 * @file VMInstance.cpp
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#include "VMInstance.h"
#include "HostContext.h"
#include "evmone/advanced_execution.hpp"
#include "evmone/execution_state.hpp"
#include <utility>

using namespace std;
namespace bcos::executor
{

VMInstance::VMInstance(evmc_vm* instance, evmc_revision revision, bytes_view code) noexcept
  : m_instance(instance), m_revision(revision), m_code(code)
{
    assert(m_instance != nullptr);
    // the abi_version of intepreter is EVMC_ABI_VERSION when callback VMFactory::create()
    assert(m_instance->abi_version == EVMC_ABI_VERSION);

    // Set the options.
    if (m_instance->set_option != nullptr)
    {  // baseline interpreter could not work with precompiled
       // m_instance->set_option(m_instance, "advanced", "");  // default is baseline interpreter
       // m_instance->set_option(m_instance, "trace", "");
       // m_instance->set_option(m_instance, "cgoto", "no");
    }
}

VMInstance::VMInstance(
    std::shared_ptr<evmoneCodeAnalysis> analysis, evmc_revision revision, bytes_view code) noexcept
  : m_analysis(std::move(analysis)), m_revision(revision), m_code(code)
{
    assert(m_analysis != nullptr);
}

Result VMInstance::execute(HostContext& _hostContext, evmc_message* _msg)
{
    if (m_instance != nullptr)
    {
        return Result(m_instance->execute(m_instance, _hostContext.interface, &_hostContext,
            m_revision, _msg, m_code.data(), m_code.size()));
    }
    auto state = std::unique_ptr<evmone::ExecutionState>(new evmone::ExecutionState(
        *_msg, m_revision, *_hostContext.interface, &_hostContext, m_code, {}));
    {                                             // baseline
        static auto* evm = evmc_create_evmone();  // baseline use the vm to get options
        return Result(evmone::baseline::execute(
            *static_cast<evmone::VM*>(evm), _msg->gas, *state, *m_analysis));
    }
    // advanced, TODO: state also could be reused
    // return Result(evmone::advanced::execute(*state, *m_analysis));
}

evmc_revision toRevision(VMSchedule const& _schedule)
{
    if (_schedule.enablePairs)
    {
        return EVMC_PARIS;
    }
    if (_schedule.enableCanCun)
    {
        return EVMC_CANCUN;
    }
    return EVMC_LONDON;
}
}  // namespace bcos::executor
