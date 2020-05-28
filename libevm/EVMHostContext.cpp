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
/**
 * @Legacy EVM context
 *
 * @file EVMHostContext.cpp
 * @author jimmyshi
 * @date 2018-09-22
 */

#include "EVMHostContext.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libethcore/LastBlockHashesFace.h>
#include <boost/thread.hpp>
#include <exception>


using namespace dev;
using namespace dev::eth;
using namespace dev::executive;

namespace  // anonymous
{
static unsigned const c_depthLimit = 1024;

/// Upper bound of stack space needed by single CALL/CREATE execution. Set experimentally.
static size_t const c_singleExecutionStackSize = 100 * 1024;

/// Standard thread stack size.
static size_t const c_defaultStackSize =
#if defined(__linux)
    8 * 1024 * 1024;
#elif defined(_WIN32)
    16 * 1024 * 1024;
#else
    512 * 1024;  // OSX and other OSs
#endif

/// Stack overhead prior to allocation.
static size_t const c_entryOverhead = 128 * 1024;

/// On what depth execution should be offloaded to additional separated stack space.
static unsigned const c_offloadPoint =
    (c_defaultStackSize - c_entryOverhead) / c_singleExecutionStackSize;

void goOnOffloadedStack(Executive& _e, OnOpFunc const& _onOp)
{
    // Set new stack size enouth to handle the rest of the calls up to the limit.
    boost::thread::attributes attrs;
    attrs.set_stack_size((c_depthLimit - c_offloadPoint) * c_singleExecutionStackSize);

    // Create new thread with big stack and join immediately.
    // TODO: It is possible to switch the implementation to Boost.Context or similar when the API is
    // stable.
    boost::exception_ptr exception;
    boost::thread{attrs,
        [&] {
            try
            {
                _e.go(_onOp);
            }
            catch (...)
            {
                exception = boost::current_exception();  // Catch all exceptions to be rethrown in
                                                         // parent thread.
            }
        }}
        .join();
    if (exception)
        boost::rethrow_exception(exception);
}

void go(unsigned _depth, Executive& _e, OnOpFunc const& _onOp)
{
    // If in the offloading point we need to switch to additional separated stack space.
    // Current stack is too small to handle more CALL/CREATE executions.
    // It needs to be done only once as newly allocated stack space it enough to handle
    // the rest of the calls up to the depth limit (c_depthLimit).

    if (_depth == c_offloadPoint)
    {
        LOG(TRACE) << "Stack offloading (depth: " << c_offloadPoint << ")";
        goOnOffloadedStack(_e, _onOp);
    }
    else
        _e.go(_onOp);
}

void generateCallResult(
    evmc_result* o_result, evmc_status_code status, u256 gas, owning_bytes_ref&& output)
{
    o_result->status_code = status;
    o_result->gas_left = static_cast<int64_t>(gas);

    // Pass the output to the EVM without a copy. The EVM will delete it
    // when finished with it.

    // First assign reference. References are not invalidated when vector
    // of bytes is moved. See `.takeBytes()` below.
    o_result->output_data = output.data();
    o_result->output_size = output.size();

    // Place a new vector of bytes containing output in result's reserved memory.
    auto* data = evmc_get_optional_storage(o_result);
    static_assert(sizeof(bytes) <= sizeof(*data), "Vector is too big");
    new (data) bytes(output.takeBytes());
    // Set the destructor to delete the vector.
    o_result->release = [](evmc_result const* _result) {
        // check _result is not null
        if (_result == NULL)
        {
            return;
        }
        auto* data = evmc_get_const_optional_storage(_result);
        auto& output = reinterpret_cast<bytes const&>(*data);
        // Explicitly call vector's destructor to release its data.
        // This is normal pattern when placement new operator is used.
        output.~bytes();
    };
}

void generateCreateResult(evmc_result* o_result, evmc_status_code status, u256 gas,
    owning_bytes_ref&& output, Address const& address)
{
    if (status == EVMC_SUCCESS)
    {
        o_result->status_code = status;
        o_result->gas_left = static_cast<int64_t>(gas);
        o_result->release = nullptr;
        o_result->create_address = toEvmC(address);
        o_result->output_data = nullptr;
        o_result->output_size = 0;
    }
    else
        generateCallResult(o_result, status, gas, std::move(output));
}

evmc_status_code transactionExceptionToEvmcStatusCode(TransactionException ex) noexcept
{
    switch (ex)
    {
    case TransactionException::None:
        return EVMC_SUCCESS;

    case TransactionException::RevertInstruction:
        return EVMC_REVERT;

    case TransactionException::OutOfGas:
        return EVMC_OUT_OF_GAS;

    case TransactionException::BadInstruction:
        return EVMC_UNDEFINED_INSTRUCTION;

    case TransactionException::OutOfStack:
        return EVMC_STACK_OVERFLOW;

    case TransactionException::StackUnderflow:
        return EVMC_STACK_UNDERFLOW;

    case TransactionException ::BadJumpDestination:
        return EVMC_BAD_JUMP_DESTINATION;

    default:
        return EVMC_FAILURE;
    }
}

}  // anonymous namespace

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
    auto& env = static_cast<EVMHostContext&>(*_context);
    Address addr = fromEvmC(*_addr);
    return env.exists(addr) ? 1 : 0;
}

void getStorage(evmc_uint256be* o_result, evmc_context* _context, evmc_address const* _addr,
    evmc_uint256be const* _key)
{
    (void)_addr;
    auto& env = static_cast<EVMHostContext&>(*_context);

    // programming assert for debug
    assert(fromEvmC(*_addr) == env.myAddress());
    u256 key = fromEvmC(*_key);
    *o_result = toEvmC(env.store(key));
}

evmc_storage_status setStorage(evmc_context* _context, evmc_address const* _addr,
    evmc_uint256be const* _key, evmc_uint256be const* _value)
{
    (void)_addr;
    auto& env = static_cast<EVMHostContext&>(*_context);
    if (!env.isPermitted())
    {
        BOOST_THROW_EXCEPTION(PermissionDenied());
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

void getBalance(
    evmc_uint256be* o_result, evmc_context* _context, evmc_address const* _addr) noexcept
{
    auto& env = static_cast<EVMHostContext&>(*_context);
    *o_result = toEvmC(env.balance(fromEvmC(*_addr)));
}

size_t getCodeSize(evmc_context* _context, evmc_address const* _addr)
{
    auto& env = static_cast<EVMHostContext&>(*_context);
    return env.codeSizeAt(fromEvmC(*_addr));
}

void getCodeHash(evmc_uint256be* o_result, evmc_context* _context, evmc_address const* _addr)
{
    auto& env = static_cast<EVMHostContext&>(*_context);
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

void selfdestruct(
    evmc_context* _context, evmc_address const* _addr, evmc_address const* _beneficiary) noexcept
{
    (void)_addr;
    auto& env = static_cast<EVMHostContext&>(*_context);
    assert(fromEvmC(*_addr) == env.myAddress());
    env.suicide(fromEvmC(*_beneficiary));
}


void log(evmc_context* _context, evmc_address const* _addr, uint8_t const* _data, size_t _dataSize,
    evmc_uint256be const _topics[], size_t _numTopics) noexcept
{
    (void)_addr;
    auto& env = static_cast<EVMHostContext&>(*_context);
    assert(fromEvmC(*_addr) == env.myAddress());
    h256 const* pTopics = reinterpret_cast<h256 const*>(_topics);
    env.log(h256s{pTopics, pTopics + _numTopics}, bytesConstRef{_data, _dataSize});
}

void getTxContext(evmc_tx_context* result, evmc_context* _context) noexcept
{
    auto& env = static_cast<EVMHostContext&>(*_context);
    result->tx_gas_price = toEvmC(env.gasPrice());
    result->tx_origin = toEvmC(env.origin());
    result->block_number = env.envInfo().number();
    result->block_timestamp = env.envInfo().timestamp();
    result->block_gas_limit = static_cast<int64_t>(env.envInfo().gasLimit());
}

void getBlockHash(evmc_uint256be* o_hash, evmc_context* _envPtr, int64_t _number)
{
    auto& env = static_cast<EVMHostContext&>(*_envPtr);
    *o_hash = toEvmC(env.blockHash(_number));
}

void create(evmc_result* o_result, EVMHostContext& _env, evmc_message const* _msg) noexcept
{
    u256 gas = _msg->gas;
    u256 value = fromEvmC(_msg->value);
    bytesConstRef init = {_msg->input_data, _msg->input_size};
    u256 salt = fromEvmC(_msg->create2_salt);
    evmc_opcode opcode =
        _msg->kind == EVMC_CREATE ? evmc_opcode::OP_CREATE : evmc_opcode::OP_CREATE2;

    // EVMHostContext::create takes the sender address from .myAddress().
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

    auto& env = static_cast<EVMHostContext&>(*_context);

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

EVMHostContext::EVMHostContext(std::shared_ptr<StateFace> _s, dev::eth::EnvInfo const& _envInfo,
    Address const& _myAddress, Address const& _caller, Address const& _origin, u256 const& _value,
    u256 const& _gasPrice, bytesConstRef _data, bytesConstRef _code, h256 const& _codeHash,
    unsigned _depth, bool _isCreate, bool _staticCall)
  : evmc_context{&fnTable, 0},
    m_envInfo(_envInfo),
    m_myAddress(_myAddress),
    m_caller(_caller),
    m_origin(_origin),
    m_value(_value),
    m_gasPrice(_gasPrice),
    m_data(_data),
    m_code(_code.toBytes()),
    m_codeHash(_codeHash),
    m_depth(_depth),
    m_isCreate(_isCreate),
    m_staticCall(_staticCall),
    m_s(_s)
{
    // Contract: processing account must exist. In case of CALL, the EVMHostContext
    // is created only if an account has code (so exist). In case of CREATE
    // the account must be created first.
    assert(m_s->addressInUse(_myAddress));
}

evmc_result EVMHostContext::call(CallParameters& _p)
{
    Executive e{m_s, envInfo(), depth() + 1};
    // Note: When create initializes Executive, the flags of evmc context must be passed in
    e.setEvmFlags(flags);
    if (!e.call(_p, gasPrice(), origin()))
    {
        go(depth(), e, _p.onOp);
        e.accrueSubState(sub());
    }
    _p.gas = e.gas();

    evmc_result evmcResult;
    generateCallResult(&evmcResult, transactionExceptionToEvmcStatusCode(e.getException()), _p.gas,
        e.takeOutput());
    return evmcResult;
}

size_t EVMHostContext::codeSizeAt(dev::Address const& _a)
{
    if (m_envInfo.precompiledEngine()->isPrecompiled(_a))
    {
        return 1;
    }
    return m_s->codeSize(_a);
}

h256 EVMHostContext::codeHashAt(Address const& _a)
{
    return exists(_a) ? m_s->codeHash(_a) : h256{};
}

bool EVMHostContext::isPermitted()
{
    // check authority by tx.origin
    if (!m_s->checkAuthority(origin(), myAddress()))
    {
        LOG(ERROR) << "EVMHostContext::isPermitted PermissionDenied" << LOG_KV("origin", origin())
                   << LOG_KV("address", myAddress());
        return false;
    }
    return true;
}

void EVMHostContext::setStore(u256 const& _n, u256 const& _v)
{
    m_s->setStorage(myAddress(), _n, _v);
}

evmc_result EVMHostContext::create(u256 const& _endowment, u256& io_gas, bytesConstRef _code,
    evmc_opcode _op, u256 _salt, OnOpFunc const& _onOp)
{
    Executive e{m_s, envInfo(), depth() + 1};
    // Note: When create initializes Executive, the flags of evmc context must be passed in
    e.setEvmFlags(flags);
    bool result = false;
    if (_op == evmc_opcode::OP_CREATE)
        result = e.createOpcode(myAddress(), _endowment, gasPrice(), io_gas, _code, origin());
    else
    {
        // TODO: when new CREATE opcode added, this logic maybe affected
        assert(_op == evmc_opcode::OP_CREATE2);
        result =
            e.create2Opcode(myAddress(), _endowment, gasPrice(), io_gas, _code, origin(), _salt);
    }

    if (!result)
    {
        go(depth(), e, _onOp);
        e.accrueSubState(sub());
    }
    io_gas = e.gas();
    evmc_result evmcResult;
    generateCreateResult(&evmcResult, transactionExceptionToEvmcStatusCode(e.getException()),
        io_gas, e.takeOutput(), e.newAddress());
    return evmcResult;
}

void EVMHostContext::suicide(Address const& _a)
{
    // Why transfer is not used here? That caused a consensus issue before (see Quirk #2 in
    // http://martin.swende.se/blog/Ethereum_quirks_and_vulns.html). There is one test case
    // witnessing the current consensus
    // 'GeneralStateTests/stSystemOperationsTest/suicideSendEtherPostDeath.json'.

    if (g_BCOSConfig.version() >= RC2_VERSION)
    {
        // No balance here in BCOS. Balance has data racing in parallel suicide.
        m_sub.suicides.insert(m_myAddress);
        return;
    }

    m_s->addBalance(_a, m_s->balance(myAddress()));
    m_s->setBalance(myAddress(), 0);
    m_sub.suicides.insert(m_myAddress);
}

h256 EVMHostContext::blockHash(int64_t _number)
{
    return envInfo().numberHash(_number);
}
}  // namespace eth
}