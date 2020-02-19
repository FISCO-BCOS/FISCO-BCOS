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
/** @file ExtVMFace.cpp
 * @author wheatli
 * @date 2018.8.28
 * @record copy from aleth, this is a vm interface
 */

#include "ExtVMFace.h"
#include <evmc/helpers.h>
#include <libblockverifier/ExecutiveContext.h>
namespace dev
{
namespace eth
{
namespace
{
static_assert(sizeof(Address) == sizeof(evmc_address), "Address types size mismatch");
static_assert(alignof(Address) == alignof(evmc_address), "Address types alignment mismatch");
static_assert(sizeof(h256) == sizeof(evmc_uint256be), "Hash types size mismatch");
static_assert(alignof(h256) == alignof(evmc_uint256be), "Hash types alignment mismatch");

int accountExists(evmc_context* _context, evmc_address const* _addr) noexcept
{
    auto& env = static_cast<ExtVMFace&>(*_context);
    Address addr = fromEvmC(*_addr);
    return env.exists(addr) ? 1 : 0;
}

void getStorage(evmc_uint256be* o_result, evmc_context* _context, evmc_address const* _addr,
    evmc_uint256be const* _key)
{
    (void)_addr;
    auto& env = static_cast<ExtVMFace&>(*_context);

    // programming assert for debug
    assert(fromEvmC(*_addr) == env.myAddress());
    u256 key = fromEvmC(*_key);
    *o_result = toEvmC(env.store(key));
}

evmc_storage_status setStorage(evmc_context* _context, evmc_address const* _addr,
    evmc_uint256be const* _key, evmc_uint256be const* _value)
{
    (void)_addr;
    auto& env = static_cast<ExtVMFace&>(*_context);
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

void getBalance(
    evmc_uint256be* o_result, evmc_context* _context, evmc_address const* _addr) noexcept
{
    auto& env = static_cast<ExtVMFace&>(*_context);
    *o_result = toEvmC(env.balance(fromEvmC(*_addr)));
}

size_t getCodeSize(evmc_context* _context, evmc_address const* _addr)
{
    auto& env = static_cast<ExtVMFace&>(*_context);
    return env.codeSizeAt(fromEvmC(*_addr));
}

void getCodeHash(evmc_uint256be* o_result, evmc_context* _context, evmc_address const* _addr)
{
    auto& env = static_cast<ExtVMFace&>(*_context);
    *o_result = toEvmC(env.codeHashAt(fromEvmC(*_addr)));
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
size_t copyCode(evmc_context* _context, evmc_address const* _addr, size_t _codeOffset,
    byte* _bufferData, size_t _bufferSize)
{
    auto& env = static_cast<ExtVMFace&>(*_context);
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

void selfdestruct(
    evmc_context* _context, evmc_address const* _addr, evmc_address const* _beneficiary) noexcept
{
    (void)_addr;
    auto& env = static_cast<ExtVMFace&>(*_context);
    assert(fromEvmC(*_addr) == env.myAddress());
    env.suicide(fromEvmC(*_beneficiary));
}


void log(evmc_context* _context, evmc_address const* _addr, uint8_t const* _data, size_t _dataSize,
    evmc_uint256be const _topics[], size_t _numTopics) noexcept
{
    (void)_addr;
    auto& env = static_cast<ExtVMFace&>(*_context);
    assert(fromEvmC(*_addr) == env.myAddress());
    h256 const* pTopics = reinterpret_cast<h256 const*>(_topics);
    env.log(h256s{pTopics, pTopics + _numTopics}, bytesConstRef{_data, _dataSize});
}

void getTxContext(evmc_tx_context* result, evmc_context* _context) noexcept
{
    auto& env = static_cast<ExtVMFace&>(*_context);
    result->tx_gas_price = toEvmC(env.gasPrice());
    result->tx_origin = toEvmC(env.origin());
    result->block_number = env.envInfo().number();
    result->block_timestamp = env.envInfo().timestamp();
    result->block_gas_limit = static_cast<int64_t>(env.envInfo().gasLimit());
}

void getBlockHash(evmc_uint256be* o_hash, evmc_context* _envPtr, int64_t _number)
{
    auto& env = static_cast<ExtVMFace&>(*_envPtr);
    *o_hash = toEvmC(env.blockHash(_number));
}

void create(evmc_result* o_result, ExtVMFace& _env, evmc_message const* _msg) noexcept
{
    u256 gas = _msg->gas;
    u256 value = fromEvmC(_msg->value);
    bytesConstRef init = {_msg->input_data, _msg->input_size};
    u256 salt = fromEvmC(_msg->create2_salt);
    Instruction opcode = _msg->kind == EVMC_CREATE ? Instruction::CREATE : Instruction::CREATE2;

    // ExtVM::create takes the sender address from .myAddress().
    assert(fromEvmC(_msg->sender) == _env.myAddress());

    *o_result = _env.create(value, gas, init, opcode, salt, {});
}

void call(evmc_result* o_result, evmc_context* _context, evmc_message const* _msg) noexcept
{
    // gas maybe smaller than 0 since outside gas is u256 and evmc_message is int64_t
    // so gas maybe smaller than 0 in some extreme cases
    // * orgin code: assert(_msg->gas >= 0)
    // * modified：
    if (_msg->gas < 0)
    {
        LOG(ERROR) << LOG_DESC("Gas overflow") << LOG_KV("cur gas", _msg->gas);
        BOOST_THROW_EXCEPTION(GasOverflow());
    }

    auto& env = static_cast<ExtVMFace&>(*_context);

    // Handle CREATE separately.
    if (_msg->kind == EVMC_CREATE || _msg->kind == EVMC_CREATE2)
        return create(o_result, env, _msg);

    CallParameters params;
    params.gas = _msg->gas;
    params.apparentValue = fromEvmC(_msg->value);
    params.valueTransfer = _msg->kind == EVMC_DELEGATECALL ? 0 : params.apparentValue;
    params.senderAddress = fromEvmC(_msg->sender);
    params.codeAddress = fromEvmC(_msg->destination);
    params.receiveAddress = _msg->kind == EVMC_CALL ? params.codeAddress : env.myAddress();
    params.data = {_msg->input_data, _msg->input_size};
    params.staticCall = (_msg->flags & EVMC_STATIC) != 0;
    params.onOp = {};

    *o_result = env.call(params);
}

/// function table
evmc_context_fn_table const fnTable = {
    accountExists,
    getStorage,
    setStorage,
    getBalance,
    getCodeSize,
    getCodeHash,
    copyCode,
    selfdestruct,
    eth::call,
    getTxContext,
    getBlockHash,
    eth::log,
};
}  // namespace

ExtVMFace::ExtVMFace(EnvInfo const& _envInfo, Address const& _myAddress, Address const& _caller,
    Address const& _origin, u256 const& _value, u256 const& _gasPrice, bytesConstRef _data,
    bytes _code, h256 const& _codeHash, unsigned _depth, bool _isCreate, bool _staticCall)
  : evmc_context{&fnTable},
    m_envInfo(_envInfo),
    m_myAddress(_myAddress),
    m_caller(_caller),
    m_origin(_origin),
    m_value(_value),
    m_gasPrice(_gasPrice),
    m_data(_data),
    m_code(std::move(_code)),
    m_codeHash(_codeHash),
    m_depth(_depth),
    m_isCreate(_isCreate),
    m_staticCall(_staticCall)
{}
std::shared_ptr<dev::blockverifier::ExecutiveContext> EnvInfo::precompiledEngine() const
{
    return m_executiveEngine;
}
void EnvInfo::setPrecompiledEngine(
    std::shared_ptr<dev::blockverifier::ExecutiveContext> executiveEngine)
{
    m_executiveEngine = executiveEngine;
}

}  // namespace eth
}  // namespace dev
