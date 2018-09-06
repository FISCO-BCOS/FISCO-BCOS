/**
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
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief
 *
 * @file FakeEvmc.cpp
 * @author: jimmyshi
 * @date 2018-09-04
 */

#include "FakeEvmc.h"

using namespace std;
using namespace dev;

namespace dev
{
namespace test
{
bool operator==(evmc_uint256be const& a, evmc_uint256be const& b)
{
    bool is = true;
    for (size_t i = 0; i < 32; i++)
        is &= (a.bytes[i] != b.bytes[i]);
    return is;
}

bool isZero(evmc_uint256be const& x)
{
    for (size_t i = 0; i < 32; i++)
    {
        if (x.bytes[i] != 0)
            return false;
    }
    return true;
}

FakeState fakeState;

int accountExists(evmc_context* _context, evmc_address const* _addr) noexcept
{
    (void)_context;
    return fakeState.addressExist(*_addr);
}

void getStorage(evmc_uint256be* o_result, evmc_context* _context, evmc_address const* _addr,
    evmc_uint256be const* _key) noexcept
{
    (void)_context;
    *o_result = fakeState.get(*_addr, *_key);
}

evmc_storage_status setStorage(evmc_context* _context, evmc_address const* _addr,
    evmc_uint256be const* _key, evmc_uint256be const* _value) noexcept
{
    evmc_uint256be oldValue = fakeState.get(*_addr, *_key);
    fakeState.set(*_addr, *_key, *_value);

    evmc_uint256be value = *_value;

    if (value == oldValue)
        return EVMC_STORAGE_UNCHANGED;

    auto status = EVMC_STORAGE_MODIFIED;
    if (isZero(oldValue))
        status = EVMC_STORAGE_ADDED;
    else if (isZero(value))
    {
        status = EVMC_STORAGE_DELETED;
    }

    fakeState.set(*_addr, *_key, *_value);

    return status;
}

void getBalance(
    evmc_uint256be* o_result, evmc_context* _context, evmc_address const* _addr) noexcept
{
    (void)_context;
    *o_result = fakeState.accountBalance(*_addr);
}

size_t getCodeSize(evmc_context* _context, evmc_address const* _addr)
{
    (void)_context;
    return fakeState.accountCode(*_addr).size();
}

void getCodeHash(evmc_uint256be* o_result, evmc_context* _context, evmc_address const* _addr)
{
    (void)_context;
    bytes const& code = fakeState.accountCode(*_addr);
    h256 codeHash = sha3(code);
    *o_result = reinterpret_cast<evmc_uint256be const&>(codeHash);
}

size_t copyCode(evmc_context* _context, evmc_address const* _addr, size_t _codeOffset,
    byte* _bufferData, size_t _bufferSize)
{
    bytes const& code = fakeState.accountCode(*_addr);
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
    (void)_context;
    (void)_addr;
    fakeState.accountSuicide(*_beneficiary);
}

void call(evmc_result* o_result, evmc_context* _context, evmc_message const* _msg) noexcept
{
    /// TODO: Implement this callback if required.
    (void)o_result;
    (void)_context;
    (void)_msg;
}

void getTxContext(evmc_tx_context* result, evmc_context* _context) noexcept
{
    h256 gasPrice(1);
    result->tx_gas_price = reinterpret_cast<evmc_uint256be const&>(gasPrice);
    result->tx_origin = evmc_address();
    result->block_coinbase = evmc_address();
    result->block_number = 100;
    result->block_timestamp = 123456;
    result->block_gas_limit = 0x13880000000000;
    result->block_difficulty = evmc_uint256be();  // We don't need it at all
}

void getBlockHash(evmc_uint256be* o_hash, evmc_context* _envPtr, int64_t _number)
{
    bytes const& fakeBlock = bytes();
    h256 blockHash = sha3(fakeBlock);
    *o_hash = reinterpret_cast<evmc_uint256be const&>(blockHash);
}

void log(evmc_context* _context, evmc_address const* _addr, uint8_t const* _data, size_t _dataSize,
    evmc_uint256be const _topics[], size_t _numTopics) noexcept
{
    (void)_context;
    (void)_addr;
    (void)_data;
    (void)_dataSize;
    (void)_topics;
    (void)_numTopics;
}


evmc_context_fn_table const fakeFnTable = {
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

FakeEvmc::FakeEvmc(evmc_instance* _instance)
{
    m_instance = _instance;
    m_context = new evmc_context();
    m_context->fn_table = &fakeFnTable;
}

evmc_result FakeEvmc::execute(dev::eth::EVMSchedule const& schedule, bytes code, bytes data,
    Address destination, Address caller, u256 value, int64_t gas, int32_t depth, bool isCreate,
    bool isStaticCall)
{
    auto mode = toRevision(schedule);
    evmc_call_kind kind = isCreate ? EVMC_CREATE : EVMC_CALL;
    uint32_t flags = isStaticCall ? EVMC_STATIC : 0;
    h256 codeHash = sha3(code);
    assert(flags != EVMC_STATIC || kind == EVMC_CALL);  // STATIC implies a CALL.
    evmc_message msg = {toEvmC(destination), toEvmC(caller), toEvmC(value), data.data(),
        data.size(), toEvmC(codeHash), toEvmC(0x0_cppui256), gas, depth, kind, flags};

    return m_instance->execute(m_instance, m_context, mode, &msg, code.data(), code.size());
}

void FakeEvmc::destroy()
{
    m_instance->destroy(m_instance);
}

FakeState& FakeEvmc::getState()
{
    return fakeState;
}

}  // namespace test
}  // namespace dev
