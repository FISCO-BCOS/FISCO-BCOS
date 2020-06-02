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

owning_bytes_ref EVMInstance::exec(u256& io_gas, EVMHostInterface& _ext, const OnOpFunc& _onOp)
{
    // the block number will be larger than 0,
    // can be controlled by the programmers
    assert(_ext.envInfo().number() >= 0);

    constexpr int64_t int64max = std::numeric_limits<int64_t>::max();

    // TODO: The following checks should be removed by changing the types
    //       used for gas, block number and timestamp.
    (void)int64max;

    // gas, gasLimit are u256 outside of EVMInstance
    // and can be passed inside by the outsider-users
    // so we should remove the `assert`, and throw exceptions if this happend
    // *origin code:
    // assert(io_gas <= int64max);
    // assert(_ext.envInfo().gasLimit() <= int64max);
    // *modified code:
    if (io_gas > int64max || _ext.envInfo().gasLimit() > int64max)
    {
        LOG(ERROR) << LOG_DESC("Gas overflow") << LOG_KV("gas", io_gas)
                   << LOG_KV("gasLimit", _ext.envInfo().gasLimit())
                   << LOG_KV("max gas/gasLimit", int64max);
        BOOST_THROW_EXCEPTION(GasOverflow());
    }

    assert(_ext.depth() <= static_cast<size_t>(std::numeric_limits<int32_t>::max()));

    auto gas = static_cast<int64_t>(io_gas);
    auto mode = toRevision(_ext.evmSchedule());
    evmc_call_kind kind = _ext.isCreate() ? EVMC_CREATE : EVMC_CALL;
    uint32_t flags = _ext.staticCall() ? EVMC_STATIC : 0;
    // this is ensured by solidity compiler
    assert(flags != EVMC_STATIC || kind == EVMC_CALL);  // STATIC implies a CALL.

    evmc_message msg = {toEvmC(_ext.myAddress()), toEvmC(_ext.caller()), toEvmC(_ext.value()),
        _ext.data().data(), _ext.data().size(), toEvmC(_ext.codeHash()), toEvmC(0x0_cppui256), gas,
        static_cast<int32_t>(_ext.depth()), kind, flags};
    Result r{
        m_instance->execute(m_instance, &_ext, mode, &msg, _ext.code().data(), _ext.code().size())};

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
        return VMFactory::create(VMKind::Interpreter)->exec(io_gas, _ext, _onOp);

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
