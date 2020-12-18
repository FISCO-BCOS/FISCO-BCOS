/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2020 fisco-dev contributors.
 */
/** @file EVMHostInterface.cpp
 * @author wheatli
 * @date 2018.8.28
 * @record copy from aleth, this is a vm interface
 */

#include "EVMHostInterface.h"
#include "EVMHostContext.h"
#include "libprotocol/Exceptions.h"
#include <libblockverifier/ExecutiveContext.h>

namespace bcos
{
namespace executive
{
namespace
{
static_assert(sizeof(Address) == sizeof(evmc_address), "Address types size mismatch");
static_assert(alignof(Address) == alignof(evmc_address), "Address types alignment mismatch");
static_assert(sizeof(h256) == sizeof(evmc_bytes32), "Hash types size mismatch");
static_assert(alignof(h256) == alignof(evmc_bytes32), "Hash types alignment mismatch");

bool accountExists(evmc_host_context* _context, const evmc_address* _addr) noexcept
{
    auto& env = static_cast<EVMHostContext&>(*_context);
    Address addr = fromEvmC(*_addr);
    return env.exists(addr);
}

evmc_bytes32 getStorage(
    evmc_host_context* _context, const evmc_address* _addr, const evmc_bytes32* _key)
{
    auto& env = static_cast<EVMHostContext&>(*_context);

    // programming assert for debug
    assert(fromEvmC(*_addr) == env.myAddress());
    u256 key = fromEvmC(*_key);
    return toEvmC(env.store(key));
}

evmc_storage_status setStorage(evmc_host_context* _context, const evmc_address* _addr,
    const evmc_bytes32* _key, const evmc_bytes32* _value)
{
    auto& env = static_cast<EVMHostContext&>(*_context);
    if (!env.isPermitted())
    {
        BOOST_THROW_EXCEPTION(protocol::PermissionDenied());
    }
    assert(fromEvmC(*_addr) == env.myAddress());
    u256 index = fromEvmC(*_key);
    u256 value = fromEvmC(*_value);
    u256 oldValue = env.store(index);

    if (value == oldValue)
        return EVMC_STORAGE_UNCHANGED;

    auto status = EVMC_STORAGE_MODIFIED;
    if (oldValue == 0)
        status = EVMC_STORAGE_ADDED;
    else if (value == 0)
    {
        status = EVMC_STORAGE_DELETED;
        env.sub().refunds += env.evmSchedule().sstoreRefundGas;
    }
    env.setStore(index, value);  // Interface uses native endianness
    return status;
}

evmc_bytes32 getBalance(evmc_host_context* _context, const evmc_address* _addr) noexcept
{
    auto& env = static_cast<EVMHostContext&>(*_context);
    return toEvmC(env.balance(fromEvmC(*_addr)));
}

size_t getCodeSize(evmc_host_context* _context, const evmc_address* _addr)
{
    auto& env = static_cast<EVMHostContext&>(*_context);
    return env.codeSizeAt(fromEvmC(*_addr));
}

evmc_bytes32 getCodeHash(evmc_host_context* _context, const evmc_address* _addr)
{
    auto& env = static_cast<EVMHostContext&>(*_context);
    return toEvmC(env.codeHashAt(fromEvmC(*_addr)));
}

/**
 * @brief : copy code between [_codeOffset, _codeOffset + _bufferSize] to bufferData
 *          if _codeOffset is larger than code length, then return 0;
 *          if _codeOffset + _bufferSize is larger than the end of the code, than only copy
 * [_codeOffset, _codeEnd]
 * @param _context : evm context, including to myAddress, caller, gas, origin, value, etc.
 * @param _addr: the evmc-address of the code
 * @param _codeOffset: the offset begin to copy code
 * @param _bufferData : buffer store the copied code
 * @param _bufferSize : code size to copy
 * @return size_t : return copied code size(in byte)
 */
size_t copyCode(evmc_host_context* _context, const evmc_address* _addr, size_t _codeOffset,
    uint8_t* _bufferData, size_t _bufferSize)
{
    auto& env = static_cast<EVMHostContext&>(*_context);
    Address addr = fromEvmC(*_addr);
    bytes const& code = env.codeAt(addr);

    // Handle "big offset" edge case.
    if (_codeOffset >= code.size())
        return 0;

    size_t maxToCopy = code.size() - _codeOffset;
    size_t numToCopy = std::min(maxToCopy, _bufferSize);
    std::copy_n(&code[_codeOffset], numToCopy, _bufferData);
    return numToCopy;
}

void selfdestruct(evmc_host_context* _context, const evmc_address* _addr,
    const evmc_address* _beneficiary) noexcept
{
    (void)_addr;
    auto& env = static_cast<EVMHostContext&>(*_context);
    assert(fromEvmC(*_addr) == env.myAddress());
    env.suicide(fromEvmC(*_beneficiary));
}


void log(evmc_host_context* _context, const evmc_address* _addr, uint8_t const* _data,
    size_t _dataSize, const evmc_bytes32 _topics[], size_t _numTopics) noexcept
{
    (void)_addr;
    auto& env = static_cast<EVMHostContext&>(*_context);
    assert(fromEvmC(*_addr) == env.myAddress());
    h256 const* pTopics = reinterpret_cast<h256 const*>(_topics);
    env.log(h256s{pTopics, pTopics + _numTopics}, bytesConstRef{_data, _dataSize});
}

evmc_tx_context getTxContext(evmc_host_context* _context) noexcept
{
    auto& env = static_cast<EVMHostContext&>(*_context);
    evmc_tx_context result;
    result.tx_gas_price = toEvmC(env.gasPrice());
    result.tx_origin = toEvmC(env.origin());
    result.block_number = env.envInfo().number();
    result.block_timestamp = env.envInfo().timestamp();
    result.block_gas_limit = static_cast<int64_t>(env.envInfo().gasLimit());
    return result;
}

evmc_bytes32 getBlockHash(evmc_host_context* _envPtr, int64_t _number)
{
    auto& env = static_cast<EVMHostContext&>(*_envPtr);
    return toEvmC(env.blockHash(_number));
}

evmc_result create(EVMHostContext& _env, evmc_message const* _msg) noexcept
{
    u256 gas = _msg->gas;
    u256 value = fromEvmC(_msg->value);
    bytesConstRef init = {_msg->input_data, _msg->input_size};
    u256 salt = fromEvmC(_msg->create2_salt);
    evmc_opcode opcode =
        _msg->kind == EVMC_CREATE ? evmc_opcode::OP_CREATE : evmc_opcode::OP_CREATE2;

    // EVMHostContext::create takes the sender address from .myAddress().
    assert(fromEvmC(_msg->sender) == _env.myAddress());

    return _env.create(value, gas, init, opcode, salt);
}

evmc_result call(evmc_host_context* _context, const evmc_message* _msg) noexcept
{
    // gas maybe smaller than 0 since outside gas is u256 and evmc_message is int64_t
    // so gas maybe smaller than 0 in some extreme cases
    // * origin code: assert(_msg->gas >= 0)
    if (_msg->gas < 0)
    {
        EXECUTIVE_LOG(ERROR) << LOG_DESC("Gas overflow") << LOG_KV("cur gas", _msg->gas);
        BOOST_THROW_EXCEPTION(protocol::GasOverflow());
    }

    auto& env = static_cast<EVMHostContext&>(*_context);

    // Handle CREATE separately.
    if (_msg->kind == EVMC_CREATE || _msg->kind == EVMC_CREATE2)
        return create(env, _msg);

    CallParameters params;
    params.gas = _msg->gas;
    params.apparentValue = fromEvmC(_msg->value);
    params.valueTransfer = _msg->kind == EVMC_DELEGATECALL ? 0 : params.apparentValue;
    params.senderAddress = fromEvmC(_msg->sender);
    params.codeAddress = fromEvmC(_msg->destination);
    params.receiveAddress = _msg->kind == EVMC_CALL ? params.codeAddress : env.myAddress();
    params.data = {_msg->input_data, _msg->input_size};
    params.staticCall = (_msg->flags & EVMC_STATIC) != 0;

    return env.call(params);
}

/// function table
evmc_host_interface const fnTable = {
    accountExists,
    getStorage,
    setStorage,
    getBalance,
    getCodeSize,
    getCodeHash,
    copyCode,
    selfdestruct,
    call,
    getTxContext,
    getBlockHash,
    log,
};

}  // namespace

const evmc_host_interface* getHostInterface()
{
    return &fnTable;
}
}  // namespace executive
}  // namespace bcos
