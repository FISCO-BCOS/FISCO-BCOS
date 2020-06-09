/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file EVMInstance.cpp
 * @author wheatli
 * @date 2018.8.28
 * @record copy from aleth, this is a vm interface
 */

#include "EVMInstance.h"
#include "VMFactory.h"

using namespace std;
namespace dev
{
namespace eth
{
EVMInstance::EVMInstance(evmc_instance* _instance) noexcept : m_instance(_instance)
{
    assert(m_instance != nullptr);
    // the abi_version of intepreter is EVMC_ABI_VERSION when callback VMFactory::create()
    assert(m_instance->abi_version == EVMC_ABI_VERSION);

    // Set the options.
    if (m_instance->set_option)
        for (auto& pair : evmcOptions())
            m_instance->set_option(m_instance, pair.first.c_str(), pair.second.c_str());
}

std::shared_ptr<Result> EVMInstance::exec(executive::EVMHostContext& _ext, evmc_revision _rev,
    evmc_message* _msg, const uint8_t* _code, size_t _code_size)
{
    auto result = std::make_shared<Result>(
        m_instance->execute(m_instance, &_ext, _rev, _msg, _code, _code_size));

    if (result->status() == EVMC_REJECTED)
    {
        LOG(WARNING) << "Execution rejected by EVMC, executing with default VM implementation";
        return VMFactory::create(VMKind::Interpreter)->exec(_ext, _rev, _msg, _code, _code_size);
    }
    return result;
}

evmc_revision toRevision(EVMSchedule const& _schedule)
{
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
}  // namespace eth
}  // namespace dev
