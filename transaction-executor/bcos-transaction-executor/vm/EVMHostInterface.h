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
 * @file EVMHostInterface.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#pragma once

#include "../Common.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/protocol/BlockHeader.h>
#include <evmc/evmc.h>
#include <evmc/instructions.h>
#include <boost/optional.hpp>
#include <functional>
#include <set>

namespace bcos::transaction_executor
{
static_assert(sizeof(Address) == sizeof(evmc_address), "Address types size mismatch");
static_assert(alignof(Address) == alignof(evmc_address), "Address types alignment mismatch");
static_assert(sizeof(h256) == sizeof(evmc_bytes32), "Hash types size mismatch");
static_assert(alignof(h256) == alignof(evmc_bytes32), "Hash types alignment mismatch");

template <class HostContextType>
bool accountExists(evmc_host_context* context, const evmc_address* _addr) noexcept
{
    auto& hostContext = static_cast<HostContextType&>(*context);
    auto addr = fromEvmC(*_addr);
    return task::syncWait(hostContext.exists(addr));
}

template <class HostContextType>
evmc_bytes32 getStorage(
    evmc_host_context* context, [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key)
{
    auto& hostContext = static_cast<HostContextType&>(*context);

    // programming assert for debug
    // assert(fromEvmC(*addr) == boost::algorithm::unhex(std::string(hostContext.myAddress())));

    return task::syncWait(hostContext.get(key));
}

template <class HostContextType>
evmc_storage_status setStorage(evmc_host_context* context,
    [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key, const evmc_bytes32* value)
{
    auto& hostContext = static_cast<HostContextType&>(*context);
    // assert(fromEvmC(*addr) == boost::algorithm::unhex(std::string(hostContext.myAddress())));

    auto status = EVMC_STORAGE_MODIFIED;
    if (value == nullptr)  // TODO: Should use 32 bytes 0
    {
        status = EVMC_STORAGE_DELETED;
        // hostContext.sub().refunds += hostContext.vmSchedule().sstoreRefundGas; // TODO: add gas
    }
    task::syncWait(hostContext.set(key, value));  // Interface uses native endianness
    return status;
}

template <class HostContextType>
evmc_bytes32 getBalance([[maybe_unused]] evmc_host_context* _context,
    [[maybe_unused]] const evmc_address* _addr) noexcept
{
    // always return 0
    return toEvmC(h256(0));
}

template <class HostContextType>
size_t getCodeSize(evmc_host_context* _context, const evmc_address* _addr)
{
    auto& hostContext = static_cast<HostContextType&>(*_context);
    return task::syncWait(hostContext.codeSizeAt(*_addr));
}

template <class HostContextType>
evmc_bytes32 getCodeHash(evmc_host_context* _context, const evmc_address* _addr)
{
    auto& hostContext = static_cast<HostContextType&>(*_context);
    return toEvmC(task::syncWait(hostContext.codeHashAt(*_addr)));
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
template <class HostContextType>
size_t copyCode(evmc_host_context* _context, const evmc_address*, size_t, uint8_t* _bufferData,
    size_t _bufferSize)
{
    auto& hostContext = static_cast<HostContextType&>(*_context);

    task::syncWait(hostContext.setCode(
        bytes((bcos::byte*)_bufferData, (bcos::byte*)_bufferData + _bufferSize)));
    return _bufferSize;
}

template <class HostContextType>
void selfdestruct(evmc_host_context* _context, [[maybe_unused]] const evmc_address* _addr,
    [[maybe_unused]] const evmc_address* _beneficiary) noexcept
{
    auto& hostContext = static_cast<HostContextType&>(*_context);

    hostContext.suicide();  // FISCO BCOS has no _beneficiary
}

template <class HostContextType>
void log(evmc_host_context* context, [[maybe_unused]] const evmc_address* addr, uint8_t const* data,
    size_t dataSize, const evmc_bytes32 topics[], size_t numTopics) noexcept
{
    auto& hostContext = static_cast<HostContextType&>(*context);
    h256s hashTopics;
    hashTopics.reserve(numTopics);
    for (auto i : RANGES::iota_view<size_t, size_t>(0, numTopics))
    {
        hashTopics.emplace_back(topics[i].bytes, sizeof(evmc_bytes32));
    }
    hostContext.log(std::move(hashTopics), bytesConstRef{data, dataSize});
}

template <class HostContextType>
evmc_access_status access_account(
    [[maybe_unused]] evmc_host_context* _context, [[maybe_unused]] const evmc_address* _addr)
{
    return EVMC_ACCESS_COLD;
}

template <class HostContextType>
evmc_access_status access_storage([[maybe_unused]] evmc_host_context* _context,
    [[maybe_unused]] const evmc_address* _addr, [[maybe_unused]] const evmc_bytes32* _key)
{
    return EVMC_ACCESS_COLD;
}

template <class HostContextType>
evmc_tx_context getTxContext(evmc_host_context* _context) noexcept
{
    auto& hostContext = static_cast<HostContextType&>(*_context);
    evmc_tx_context result = {};
    result.tx_origin = hostContext.origin();
    result.block_number = hostContext.blockNumber();
    result.block_timestamp = hostContext.timestamp();
    result.block_gas_limit = hostContext.blockGasLimit();

    // RANGES::fill(result.tx_gas_price.bytes, 0);
    // RANGES::fill(result.tx_gas_price.bytes, 0);
    // RANGES::fill(result.block_coinbase.bytes, 0);
    // RANGES::fill(result.block_difficulty.bytes, 0);
    // RANGES::fill(result.chain_id.bytes, 0);
    return result;
}

template <class HostContextType>
evmc_bytes32 getBlockHash(evmc_host_context* _txContextPtr, int64_t _number)
{
    auto& hostContext = static_cast<HostContextType&>(*_txContextPtr);
    return toEvmC(task::syncWait(hostContext.blockHash(_number)));
}

template <class HostContextType>
evmc_result call(evmc_host_context* context, const evmc_message* message) noexcept
{
    // gas maybe smaller than 0 since outside gas is u256 and evmc_message is
    // int64_t so gas maybe smaller than 0 in some extreme cases
    // * origin code: assert(_msg->gas >= 0)
    if (message->gas < 0)
    {
        EXECUTIVE_LOG(INFO) << LOG_DESC("EVM Gas overflow") << LOG_KV("cur gas", message->gas);
        BOOST_THROW_EXCEPTION(protocol::GasOverflow());
    }

    auto& hostContext = static_cast<HostContextType&>(*context);

    return task::syncWait(hostContext.externalCall(*message));
}

template <class HostContextType>
const evmc_host_interface* getHostInterface()
{
    // TODO: Use type
    // return nullptr;
    static evmc_host_interface const fnTable = {
        accountExists<HostContextType>,
        getStorage<HostContextType>,
        setStorage<HostContextType>,
        getBalance<HostContextType>,
        getCodeSize<HostContextType>,
        getCodeHash<HostContextType>,
        copyCode<HostContextType>,
        selfdestruct<HostContextType>,
        call<HostContextType>,
        getTxContext<HostContextType>,
        getBlockHash<HostContextType>,
        log<HostContextType>,
        access_account<HostContextType>,
        access_storage<HostContextType>,
    };
    return &fnTable;
}

}  // namespace bcos::transaction_executor
