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
 * @brief host context
 * @file EVMHostInterface.cpp
 * @author: xingqiangbai
 * @date: 2021-05-24
 * @brief host context
 * @file EVMHostInterface.cpp
 * @author: ancelmo
 * @date: 2021-09-13
 */

#include "EVMHostInterface.h"
#include "../Common.h"
#include "../precompiled/common/Common.h"
#include "HostContext.h"
#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-codec/wrapper/CodecWrapper.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/core/ignore_unused.hpp>
#include <exception>
#include <optional>

using namespace std;

namespace bcos
{
namespace executor
{
namespace
{
static_assert(sizeof(Address) == sizeof(evmc_address), "Address types size mismatch");
static_assert(alignof(Address) == alignof(evmc_address), "Address types alignment mismatch");
static_assert(sizeof(h256) == sizeof(evmc_bytes32), "Hash types size mismatch");
static_assert(alignof(h256) == alignof(evmc_bytes32), "Hash types alignment mismatch");

bool accountExists(evmc_host_context* _context, const evmc_address* _addr) noexcept
{
    auto& hostContext = static_cast<HostContext&>(*_context);
    auto addr = fromEvmC(*_addr);
    return hostContext.exists(addr);
}

evmc_bytes32 getStorage(
    evmc_host_context* context, [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key)
{
    auto& hostContext = static_cast<HostContext&>(*context);

    // programming assert for debug
    assert(fromEvmC(*addr) == boost::algorithm::unhex(std::string(hostContext.myAddress())));

    return hostContext.store(key);
}

evmc_bytes32 getTransientStorage(
    evmc_host_context* context, [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key)
{
    auto& hostContext = static_cast<HostContext&>(*context);

    // programming assert for debug
    assert(fromEvmC(*addr) == boost::algorithm::unhex(std::string(hostContext.myAddress())));

    return hostContext.getTransientStorage(key);
}


evmc_storage_status setStorage(evmc_host_context* context,
    [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key, const evmc_bytes32* value)
{
    auto& hostContext = static_cast<HostContext&>(*context);

    assert(fromEvmC(*addr) == boost::algorithm::unhex(std::string(hostContext.myAddress())));
    // TODO: use evmc_storage_status 5-8
    auto status = EVMC_STORAGE_MODIFIED;
    if (value == 0)  // TODO: Should use 32 bytes 0
    {
        status = EVMC_STORAGE_DELETED;
        hostContext.sub().refunds += hostContext.vmSchedule().sstoreRefundGas;
    }
    hostContext.setStore(key, value);  // Interface uses native endianness
    return status;
}

void setTransientStorage(evmc_host_context* context, [[maybe_unused]] const evmc_address* addr,
    const evmc_bytes32* key, const evmc_bytes32* value)
{
    auto& hostContext = static_cast<HostContext&>(*context);
    hostContext.setTransientStorage(key, value);  // Interface uses native endianness
}


evmc_bytes32 getBalance(evmc_host_context* _context, const evmc_address* _addr) noexcept
{
    auto& hostContext = static_cast<HostContext&>(*_context);
    const auto& blockContext = hostContext.getTransactionExecutive()->blockContext();
    EXECUTIVE_LOG(DEBUG) << "start getBalance in evm";
    if (blockContext.features().get(ledger::Features::Flag::feature_balance))
    {
        return hostContext.getBalance(_addr);
    }
    else
    {
        // always return 0
        (void)_context;
        (void)_addr;
        return toEvmC(h256(0));
    }
}

size_t getCodeSize(evmc_host_context* _context, const evmc_address* _addr)
{
    auto& hostContext = static_cast<HostContext&>(*_context);
    return hostContext.codeSizeAt(fromEvmC(*_addr));
}

evmc_bytes32 getCodeHash(evmc_host_context* _context, const evmc_address* _addr)
{
    auto& hostContext = static_cast<HostContext&>(*_context);
    return toEvmC(hostContext.codeHashAt(fromEvmC(*_addr)));
}

/**
 * @brief : copy code between [_codeOffset, _codeOffset + _bufferSize] to
 * bufferData if _codeOffset is larger than code length, then return 0; if
 * _codeOffset + _bufferSize is larger than the end of the code, than only copy
 * [_codeOffset, _codeEnd]
 * @param _context : evm context, including to myAddress, caller, gas, origin,
 * value, etc.
 * @param _addr: the evmc-address of the code
 * @param _codeOffset: the offset begin to copy code
 * @param _bufferData : buffer store the copied code
 * @param _bufferSize : code size to copy
 * @return size_t : return copied code size(in byte)
 */
size_t copyCode(evmc_host_context* _context, const evmc_address* _addr, size_t _codeOffset,
    uint8_t* _bufferData, size_t _bufferSize)
{
    auto& hostContext = static_cast<HostContext&>(*_context);
    if (hostContext.features().get(
            ledger::Features::Flag::bugfix_evm_create2_delegatecall_staticcall_codecopy))
    {
        auto addr = fromEvmC(*_addr);
        bytes const& code = hostContext.codeAt(addr);

        // Handle "big offset" edge case.
        if (_codeOffset >= code.size())
            return 0;

        size_t maxToCopy = code.size() - _codeOffset;
        size_t numToCopy = std::min(maxToCopy, _bufferSize);
        std::copy_n(&code[_codeOffset], numToCopy, _bufferData);
        return numToCopy;
    }

    hostContext.setCode(bytes((bcos::byte*)_bufferData, (bcos::byte*)_bufferData + _bufferSize));
    return _bufferSize;
}


bool selfdestruct(evmc_host_context* _context, const evmc_address* _addr,
    const evmc_address* _beneficiary) noexcept
{
    (void)_addr;
    (void)_beneficiary;
    auto& hostContext = static_cast<HostContext&>(*_context);
    const auto& blockContext = hostContext.getTransactionExecutive()->blockContext();
    EXECUTIVE_LOG(DEBUG) << "EVM suicide start ";
    if (blockContext.features().get(ledger::Features::Flag::feature_balance))
    {
        hostContext.selfdestruct(_addr, _beneficiary);
    }
    EXECUTIVE_LOG(DEBUG) << "selfdestruct successful";

    hostContext.suicide();  // FISCO BCOS has no _beneficiary
    return false;
}


void log(evmc_host_context* _context, const evmc_address* _addr, uint8_t const* _data,
    size_t _dataSize, const evmc_bytes32 _topics[], size_t _numTopics) noexcept
{
    (void)_addr;
    auto& hostContext = static_cast<HostContext&>(*_context);
    assert(fromEvmC(*_addr) == boost::algorithm::unhex(std::string(hostContext.myAddress())));
    h256 const* pTopics = reinterpret_cast<h256 const*>(_topics);
    hostContext.log(h256s{pTopics, pTopics + _numTopics}, bytesConstRef{_data, _dataSize});
}

evmc_access_status access_account(evmc_host_context* _context, const evmc_address* _addr)
{
    std::ignore = _context;
    std::ignore = _addr;
    return EVMC_ACCESS_COLD;
}


evmc_access_status access_storage(
    evmc_host_context* _context, const evmc_address* _addr, const evmc_bytes32* _key)
{
    std::ignore = _context;
    std::ignore = _addr;
    std::ignore = _key;
    return EVMC_ACCESS_COLD;
}

evmc_tx_context getTxContext(evmc_host_context* _context) noexcept
{
    auto& hostContext = static_cast<HostContext&>(*_context);
    evmc_tx_context result;
    if (hostContext.isWasm())
    {
        result.tx_origin = toEvmC(hostContext.origin());
    }
    else
    {
        auto origin = fromHex(hostContext.origin());
        result.tx_origin = toEvmC(std::string_view((char*)origin.data(), origin.size()));
    }
    result.block_number = hostContext.blockNumber();
    result.block_timestamp = hostContext.timestamp();
    result.block_gas_limit = hostContext.blockGasLimit();
    result.tx_gas_price = toEvmC(hostContext.gasPrice());
    result.chain_id = hostContext.chainId();

    memset(result.block_coinbase.bytes, 0, 20);
    memset(result.block_prev_randao.bytes, 0, 32);
    memset(result.block_base_fee.bytes, 0, 32);


    return result;
}

evmc_bytes32 getBlockHash(evmc_host_context* _txContextPtr, int64_t _number)
{
    auto& hostContext = static_cast<HostContext&>(*_txContextPtr);
    return toEvmC(hostContext.blockHash(_number));
}

// evmc_result create(HostContext& _txContext, evmc_message const* _msg) noexcept
// {
//     return _txContext.externalRequest(_msg);
// int64_t gas = _msg->gas;
// // u256 value = fromEvmC(_msg->value);
// bytesConstRef init = {_msg->input_data, _msg->input_size};
// u256 salt = fromEvmC(_msg->create2_salt);
// evmc_opcode opcode =
//     _msg->kind == EVMC_CREATE ? evmc_opcode::OP_CREATE : evmc_opcode::OP_CREATE2;

// // HostContext::create takes the sender address from .myAddress().
// assert(fromEvmC(_msg->sender) == _txContext.myAddress());

// return _txContext.create(gas, init, opcode, salt);
// }

evmc_result call(evmc_host_context* _context, const evmc_message* _msg) noexcept
{
    // gas maybe smaller than 0 since outside gas is u256 and evmc_message is
    // int64_t so gas maybe smaller than 0 in some extreme cases
    // * origin code: assert(_msg->gas >= 0)
    if (_msg->gas < 0)
    {
        EXECUTIVE_LOG(INFO) << LOG_DESC("EVM Gas overflow") << LOG_KV("cur gas", _msg->gas);
        BOOST_THROW_EXCEPTION(protocol::GasOverflow());
    }

    auto& hostContext = static_cast<HostContext&>(*_context);

    return hostContext.externalRequest(_msg);
}

/// function table
// clang-format off
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
    access_account,
    access_storage,
    getTransientStorage,
    setTransientStorage,
};
// clang-format on

// for wasm

bool wasmAccountExists(
    evmc_host_context* _context, const uint8_t* address, int32_t addressLength) noexcept
{
    auto& hostContext = static_cast<HostContext&>(*_context);
    return hostContext.exists(string_view((char*)address, addressLength));
}

int32_t get(evmc_host_context* _context, const uint8_t* _addr, int32_t _addressLength,
    const uint8_t* _key, int32_t _keyLength, uint8_t* _value, int32_t _valueLength)
{
    boost::ignore_unused(_addr, _addressLength);
    auto& hostContext = static_cast<HostContext&>(*_context);

    // programming assert for debug
    assert(string_view((char*)_addr, _addressLength) == hostContext.myAddress());
    auto value = hostContext.get(std::string_view((char*)_key, _keyLength));
    if (value.size() > (size_t)_valueLength)
    {
        return -1;
    }
    memcpy(_value, value.data(), value.size());
    return value.size();
}

evmc_storage_status set(evmc_host_context* _context, const uint8_t* _addr, int32_t _addressLength,
    const uint8_t* _key, int32_t _keyLength, const uint8_t* _value, int32_t _valueLength)
{
    boost::ignore_unused(_addr, _addressLength);
    auto& hostContext = static_cast<HostContext&>(*_context);

    // IF (!HOSTCONTEXT.ISPERMITTED())
    // {  // FIXME: RETURN STATUS INSTEAD OF THROW EXCEPTION
    //     BOOST_THROW_EXCEPTION(PERMISSIONDENIED());
    // }
    assert(string_view((char*)_addr, _addressLength) == hostContext.myAddress());
    string key((char*)_key, _keyLength);
    string value((char*)_value, _valueLength);

    auto status = EVMC_STORAGE_MODIFIED;
    if (value.empty())  // TODO: should use 32 bytes 0?
    {
        status = EVMC_STORAGE_DELETED;
        hostContext.sub().refunds += hostContext.vmSchedule().sstoreRefundGas;
    }
    hostContext.set(key, value);  // Interface uses native endianness
    return status;
}

size_t wasmGetCodeSize(evmc_host_context* _context, const uint8_t* _addr, int32_t _addressLength)
{
    auto& hostContext = static_cast<HostContext&>(*_context);
    return hostContext.codeSizeAt(string_view((char*)_addr, _addressLength));
}

evmc_bytes32 wasmGetCodeHash(
    evmc_host_context* _context, const uint8_t* _addr, int32_t _addressLength)
{
    auto& hostContext = static_cast<HostContext&>(*_context);
    return toEvmC(hostContext.codeHashAt(string_view((char*)_addr, _addressLength)));
}

size_t wasmCopyCode(evmc_host_context* _context, const uint8_t*, int32_t, size_t,
    uint8_t* _bufferData, size_t _bufferSize)
{
    auto& hostContext = static_cast<HostContext&>(*_context);

    hostContext.setCode(bytes((bcos::byte*)_bufferData, (bcos::byte*)_bufferData + _bufferSize));
    return _bufferSize;

    //   hostContext.setCode(bcos::bytes(_bufferData, _bufferSize));

    // auto code = hostContext.codeAt(string_view((char *)_addr, _addressLength));

    // // Handle "big offset" edge case.
    // if (_codeOffset >= code->size())
    //   return 0;

    // size_t maxToCopy = code->size() - _codeOffset;
    // size_t numToCopy = std::min(maxToCopy, _bufferSize);
    // std::copy_n(code->data() + _codeOffset, numToCopy, _bufferData);
    // return numToCopy;
}

void wasmLog(evmc_host_context* _context, const uint8_t* _addr, int32_t _addressLength,
    uint8_t const* _data, size_t _dataSize, const evmc_bytes32 _topics[],
    size_t _numTopics) noexcept
{
    boost::ignore_unused(_addr, _addressLength);

    auto& hostContext = static_cast<HostContext&>(*_context);
    assert(string_view((char*)_addr, _addressLength) == hostContext.myAddress());
    h256 const* pTopics = reinterpret_cast<h256 const*>(_topics);
    hostContext.log(h256s{pTopics, pTopics + _numTopics}, bytesConstRef{_data, _dataSize});
}

wasm_host_interface const wasmFnTable = {
    wasmAccountExists,
    get,
    set,
    wasmGetCodeSize,
    wasmGetCodeHash,
    wasmCopyCode,
    wasmLog,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

}  // namespace

const evmc_host_interface* getHostInterface()
{
    return &fnTable;
}
const wasm_host_interface* getWasmHostInterface()
{
    return &wasmFnTable;
}
}  // namespace executor
}  // namespace bcos