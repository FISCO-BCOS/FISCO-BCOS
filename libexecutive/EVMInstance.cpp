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

namespace dev
{
namespace eth
{
/// Error info for EVMInstance status code.
using errinfo_evmcStatusCode = boost::error_info<struct tag_evmcStatusCode, evmc_status_code>;

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

owning_bytes_ref EVMInstance::exec(EVMHostInterface& _ext, evmc_revision _rev, evmc_message* _msg,
    const uint8_t* _code, size_t _code_size)
{
    Result r{m_instance->execute(m_instance, &_ext, _rev, _msg, _code, _code_size)};
    auto io_gas = _msg->gas;
    switch (r.status())
    {
    case EVMC_SUCCESS:
        io_gas = r.gasLeft();
        // FIXME: Copy the output for now, but copyless version possible.
        return {r.output().toVector(), 0, r.output().size()};

    case EVMC_REVERT:
        io_gas = r.gasLeft();
        // FIXME: Copy the output for now, but copyless version possible.
        throw RevertInstruction{{r.output().toVector(), 0, r.output().size()}};

    case EVMC_OUT_OF_GAS:
    case EVMC_FAILURE:
        BOOST_THROW_EXCEPTION(OutOfGas());

    case EVMC_INVALID_INSTRUCTION:  // NOTE: this could have its own exception
    case EVMC_UNDEFINED_INSTRUCTION:
        BOOST_THROW_EXCEPTION(BadInstruction());

    case EVMC_BAD_JUMP_DESTINATION:
        BOOST_THROW_EXCEPTION(BadJumpDestination());

    case EVMC_STACK_OVERFLOW:
        BOOST_THROW_EXCEPTION(OutOfStack());

    case EVMC_STACK_UNDERFLOW:
        BOOST_THROW_EXCEPTION(StackUnderflow());

    case EVMC_INVALID_MEMORY_ACCESS:
        BOOST_THROW_EXCEPTION(BufferOverrun());

    case EVMC_STATIC_MODE_VIOLATION:
        BOOST_THROW_EXCEPTION(DisallowedStateChange());

    case EVMC_REJECTED:
        LOG(WARNING)
            << "Execution rejected by EVMInstance, executing with default VM implementation";
        return VMFactory::create(VMKind::Interpreter)->exec(_ext, _rev, _msg, _code, _code_size);

    case EVMC_INTERNAL_ERROR:
    default:
        if (r.status() <= EVMC_INTERNAL_ERROR)
            BOOST_THROW_EXCEPTION(InternalVMError{} << errinfo_evmcStatusCode(r.status()));
        else
            // These cases aren't really internal errors, just more specific
            // error codes returned by the VM. Map all of them to OOG.
            BOOST_THROW_EXCEPTION(OutOfGas());
    }
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
