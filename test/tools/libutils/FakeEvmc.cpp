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
using namespace dev::eth;

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

FakeState fakeState;
eth::LogEntries fakeLogs;
int64_t fakeDepth = -1;

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

evmc_storage_status setStorage(evmc_context*, evmc_address const* _addr, evmc_uint256be const* _key,
    evmc_uint256be const* _value) noexcept
{
    u256 oldValue = fromEvmC(fakeState.get(*_addr, *_key));
    u256 value = fromEvmC(*_value);

    if (value == oldValue)
        return EVMC_STORAGE_UNCHANGED;

    auto status = EVMC_STORAGE_MODIFIED;
    if (0 == oldValue)
        status = EVMC_STORAGE_ADDED;
    else if (0 == value)
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
    h256 codeHash = crypto::Hash(code);
    *o_result = reinterpret_cast<evmc_uint256be const&>(codeHash);
}

size_t copyCode(evmc_context*, evmc_address const* _addr, size_t _codeOffset, byte* _bufferData,
    size_t _bufferSize)
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

void call(evmc_result* o_result, evmc_context*, evmc_message const* _msg) noexcept
{
    EVMSchedule const& schedule = DefaultSchedule;
    int64_t gas = _msg->gas;
    u256 value = fromEvmC(_msg->value);
    Address caller = fromEvmC(_msg->sender);
    FakeEvmc sonEvmc(evmc_create_interpreter());

    // Handle CREATE.
    if (_msg->kind == EVMC_CREATE || _msg->kind == EVMC_CREATE2)
    {
        bytes code = bytesConstRef{_msg->input_data, _msg->input_size}.toBytes();
        bytes data = bytes();
        Address destination{KeyPair::create().address()};
        bool isCreate = true;
        bool isStaticCall = false;

        *o_result = sonEvmc.execute(schedule, code, data, destination, caller, value, gas,
            sonEvmc.depth(), isCreate, isStaticCall);
        if (o_result->status_code == EVMC_SUCCESS)
        {
            // We assume that generatedCode is added into state immediately
            bytes generatedCode =
                bytesConstRef{o_result->output_data, o_result->output_size}.toBytes();
            fakeState.accountCode(toEvmC(destination)) = generatedCode;

            o_result->create_address = toEvmC(destination);
            o_result->output_data = nullptr;
            o_result->output_size = 0;
        }
    }
    else  // CALL
    {
        bytes& code = fakeState.accountCode(_msg->destination);
        cout << "get code size:" << code.size() << endl;
        bytes data = bytesConstRef{_msg->input_data, _msg->input_size}.toBytes();
        Address destination = fromEvmC(_msg->destination);
        bool isCreate = false;
        bool isStaticCall = (_msg->flags & EVMC_STATIC) != 0;

        *o_result = sonEvmc.execute(schedule, code, data, destination, caller, value, gas,
            sonEvmc.depth(), isCreate, isStaticCall);
    }
}

void getTxContext(evmc_tx_context* result, evmc_context*) noexcept
{
    result->tx_gas_price = toEvmC(FAKE_GAS_PRICE);
    result->tx_origin = reinterpret_cast<evmc_address const&>(FAKE_ORIGIN);
    result->block_coinbase = reinterpret_cast<evmc_address const&>(FAKE_COINBASE);
    result->block_number = FAKE_BLOCK_NUMBER;
    result->block_timestamp = FAKE_TIMESTAMP;
    result->block_gas_limit = FAKE_GAS_LIMIT;
    result->block_difficulty = toEvmC(FAKE_DIFFICULTY);
}

void getBlockHash(evmc_uint256be* o_hash, evmc_context*, int64_t)
{
    bytes const& fakeBlock = bytes();
    h256 blockHash = crypto::Hash(fakeBlock);
    cout << "block hash: " << blockHash.hex() << endl;
    *o_hash = reinterpret_cast<evmc_uint256be const&>(FAKE_BLOCK_HASH);
}

void log(evmc_context* _context, evmc_address const* _addr, uint8_t const* _data, size_t _dataSize,
    evmc_uint256be const _topics[], size_t _numTopics) noexcept
{
    (void)_context;
    Address myAddress = fromEvmC(*_addr);
    h256 const* pTopics = reinterpret_cast<h256 const*>(_topics);
    h256s topics = h256s{pTopics, pTopics + _numTopics};
    bytes data = bytesConstRef{_data, _dataSize}.toBytes();
    fakeLogs.push_back(eth::LogEntry(myAddress, topics, data));
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
    fakeDepth++;
    m_depth = fakeDepth;
    m_instance = _instance;
    m_context = new evmc_context();
    m_context->fn_table = &fakeFnTable;
}

evmc_result FakeEvmc::execute(EVMSchedule const& schedule, bytes code, bytes data,
    Address destination, Address caller, u256 value, int64_t gas, int32_t depth, bool isCreate,
    bool isStaticCall)
{
    auto mode = toRevision(schedule);
    evmc_call_kind kind = isCreate ? EVMC_CREATE : EVMC_CALL;
    uint32_t flags = isStaticCall ? EVMC_STATIC : 0;
    h256 codeHash = crypto::Hash(code);
    assert(flags != EVMC_STATIC || kind == EVMC_CALL);  // STATIC implies a CALL.
    evmc_message msg = {toEvmC(destination), toEvmC(caller), toEvmC(value), data.data(),
        data.size(), toEvmC(codeHash), toEvmC(0x0_cppui256), gas, depth, kind, flags};

    evmc_result result =
        m_instance->execute(m_instance, m_context, mode, &msg, code.data(), code.size());

    if (isCreate && result.status_code == EVMC_SUCCESS)
    {
        // We assume that generatedCode is added into state immediately
        bytes generatedCode = bytesConstRef{result.output_data, result.output_size}.toBytes();
        fakeState.accountCode(toEvmC(destination)) = generatedCode;
        result.create_address = toEvmC(destination);
    }
    return result;
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
