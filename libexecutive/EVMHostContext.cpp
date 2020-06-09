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
#include "EVMHostInterface.h"
#include <libblockverifier/ExecutiveContext.h>
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

void goOnOffloadedStack(Executive& _e)
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
                _e.go();
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

void go(unsigned _depth, Executive& _e)
{
    // If in the offloading point we need to switch to additional separated stack space.
    // Current stack is too small to handle more CALL/CREATE executions.
    // It needs to be done only once as newly allocated stack space it enough to handle
    // the rest of the calls up to the depth limit (c_depthLimit).

    if (_depth == c_offloadPoint)
    {
        LOG(TRACE) << "Stack offloading (depth: " << c_offloadPoint << ")";
        goOnOffloadedStack(_e);
    }
    else
        _e.go();
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
namespace executive
{
EVMHostContext::EVMHostContext(std::shared_ptr<StateFace> _s,
    dev::executive::EnvInfo const& _envInfo, Address const& _myAddress, Address const& _caller,
    Address const& _origin, u256 const& _value, u256 const& _gasPrice, bytesConstRef _data,
    const bytes& _code, h256 const& _codeHash, unsigned _depth, bool _isCreate, bool _staticCall)
  : evmc_context{getHostInterface(), 0},
    m_envInfo(_envInfo),
    m_myAddress(_myAddress),
    m_caller(_caller),
    m_origin(_origin),
    m_value(_value),
    m_gasPrice(_gasPrice),
    m_data(_data),
    m_code(_code),
    m_codeHash(_codeHash),
    m_depth(_depth),
    m_isCreate(_isCreate),
    m_staticCall(_staticCall),
    m_s(_s)
{
#if 0
    // Contract: processing account must exist. In case of CALL, the EVMHostContext
    // is created only if an account has code (so exist). In case of CREATE
    // the account must be created first.
    assert(m_s->addressInUse(_myAddress));
#endif
}

evmc_result EVMHostContext::call(CallParameters& _p)
{
    Executive e{m_s, envInfo(), depth() + 1};
    // Note: When create initializes Executive, the flags of evmc context must be passed in
    e.setEvmFlags(flags);
    if (!e.call(_p, gasPrice(), origin()))
    {
        go(depth(), e);
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

evmc_result EVMHostContext::create(
    u256 const& _endowment, u256& io_gas, bytesConstRef _code, evmc_opcode _op, u256 _salt)
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
        go(depth(), e);
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
}  // namespace executive
}  // namespace dev