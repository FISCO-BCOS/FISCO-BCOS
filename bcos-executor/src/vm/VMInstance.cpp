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

using namespace std;
namespace bcos
{
namespace executor
{
namespace
{
/// The list of EVM-C options stored as pairs of (name, value).
std::vector<std::pair<std::string, std::string>> s_evmcOptions;

}  // namespace

std::vector<std::pair<std::string, std::string>>& evmcOptions() noexcept
{
    return s_evmcOptions;
}

VMInstance::VMInstance(evmc_vm* _instance) noexcept : m_instance(_instance)
{
    assert(m_instance != nullptr);
    // the abi_version of intepreter is EVMC_ABI_VERSION when callback VMFactory::create()
    assert(m_instance->abi_version == EVMC_ABI_VERSION);

    // Set the options.
    if (m_instance->set_option)
        for (auto& pair : evmcOptions())
            m_instance->set_option(m_instance, pair.first.c_str(), pair.second.c_str());
}

Result VMInstance::exec(HostContext& _hostContext, evmc_revision _rev, evmc_message* _msg,
    const uint8_t* _code, size_t _code_size)
{
    Result result = Result(m_instance->execute(
        m_instance, _hostContext.interface, &_hostContext, _rev, _msg, _code, _code_size));
    return result;
}

void VMInstance::enableDebugOutput()
{
    // m_instance->set_option(m_instance, "histogram", "1");
}

evmc_revision toRevision(EVMSchedule const& _schedule)
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
}  // namespace executor
}  // namespace bcos
