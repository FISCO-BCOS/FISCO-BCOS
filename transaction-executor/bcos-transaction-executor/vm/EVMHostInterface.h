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
bool accountExists(evmc_host_context* context, const evmc_address* addr) noexcept
{
    auto& hostContext = static_cast<HostContextType&>(*context);
    auto addrView = fromEvmC(*addr);
    return task::syncWait(hostContext.exists(addrView));
}

template <class HostContextType>
evmc_bytes32 getStorage(evmc_host_context* context, [[maybe_unused]] const evmc_address* addr,
    const evmc_bytes32* key) noexcept
{
    auto& hostContext = static_cast<HostContextType&>(*context);
    return task::syncWait(hostContext.get(key));
}

template <class HostContextType>
evmc_storage_status setStorage(evmc_host_context* context,
    [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key,
    const evmc_bytes32* value) noexcept
{
    auto& hostContext = static_cast<HostContextType&>(*context);

    auto status = EVMC_STORAGE_MODIFIED;
    if (value == nullptr)  // TODO: Should use 32 bytes 0
    {
        status = EVMC_STORAGE_DELETED;
    }
    task::syncWait(hostContext.set(key, value));
    return status;
}

template <class HostContextType>
evmc_bytes32 getBalance(
    [[maybe_unused]] evmc_host_context* context, [[maybe_unused]] const evmc_address* addr) noexcept
{
    // always return 0
    return transaction_executor::toEvmC(h256(0));
}

template <class HostContextType>
size_t getCodeSize(evmc_host_context* context, const evmc_address* addr) noexcept
{
    auto& hostContext = static_cast<HostContextType&>(*context);
    return task::syncWait(hostContext.codeSizeAt(*addr));
}

template <class HostContextType>
evmc_bytes32 getCodeHash(evmc_host_context* context, const evmc_address* addr) noexcept
{
    auto& hostContext = static_cast<HostContextType&>(*context);
    return transaction_executor::toEvmC(task::syncWait(hostContext.codeHashAt(*addr)));
}

template <class HostContextType>
size_t copyCode(evmc_host_context* context, const evmc_address*, size_t, uint8_t* bufferData,
    size_t bufferSize) noexcept
{
    auto& hostContext = static_cast<HostContextType&>(*context);
    task::syncWait(hostContext.setCode(bytesConstRef((bcos::byte*)bufferData, bufferSize)));
    return bufferSize;
}

template <class HostContextType>
bool selfdestruct(evmc_host_context* context, [[maybe_unused]] const evmc_address* addr,
    [[maybe_unused]] const evmc_address* beneficiary) noexcept
{
    auto& hostContext = static_cast<HostContextType&>(*context);

    hostContext.suicide();  // FISCO BCOS has no _beneficiary
    return false;
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
    [[maybe_unused]] evmc_host_context* context, [[maybe_unused]] const evmc_address* addr) noexcept
{
    return EVMC_ACCESS_COLD;
}

template <class HostContextType>
evmc_access_status access_storage([[maybe_unused]] evmc_host_context* context,
    [[maybe_unused]] const evmc_address* addr, [[maybe_unused]] const evmc_bytes32* key) noexcept
{
    return EVMC_ACCESS_COLD;
}

template <class HostContextType>
evmc_tx_context getTxContext(evmc_host_context* context) noexcept
{
    auto& hostContext = static_cast<HostContextType&>(*context);
    evmc_tx_context result = {};
    result.tx_origin = hostContext.origin();
    result.block_number = hostContext.blockNumber();
    result.block_timestamp = hostContext.timestamp();
    result.block_gas_limit = hostContext.blockGasLimit();
    return result;
}

template <class HostContextType>
evmc_bytes32 getBlockHash(evmc_host_context* context, int64_t number) noexcept
{
    auto& hostContext = static_cast<HostContextType&>(*context);
    return transaction_executor::toEvmC(task::syncWait(hostContext.blockHash(number)));
}

template <class HostContextType>
evmc_result call(evmc_host_context* context, const evmc_message* message) noexcept
{
    if (message->gas < 0)
    {
        EXECUTIVE_LOG(INFO) << LOG_DESC("EVM Gas overflow") << LOG_KV("message gas:", message->gas);
        BOOST_THROW_EXCEPTION(protocol::GasOverflow());
    }

    auto& hostContext = static_cast<HostContextType&>(*context);
    return task::syncWait(hostContext.externalCall(*message));
}

template <class HostContextType>
const evmc_host_interface* getHostInterface()
{
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
