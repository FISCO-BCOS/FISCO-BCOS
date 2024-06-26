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

#include "bcos-concepts/ByteBuffer.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/protocol/Exceptions.h"
#include <evmc/evmc.h>
#include <evmc/instructions.h>
#include <boost/core/pointer_traits.hpp>

namespace bcos::transaction_executor
{
static_assert(sizeof(Address) == sizeof(evmc_address), "Address types size mismatch");
static_assert(alignof(Address) == alignof(evmc_address), "Address types alignment mismatch");
static_assert(sizeof(h256) == sizeof(evmc_bytes32), "Hash types size mismatch");
static_assert(alignof(h256) == alignof(evmc_bytes32), "Hash types alignment mismatch");

template <class HostContextType, auto waitOperator>
struct EVMHostInterface
{
    static bool accountExists(evmc_host_context* context, const evmc_address* addr) noexcept
    {
        auto& hostContext = static_cast<HostContextType&>(*context);
        return waitOperator(hostContext.exists(*addr));
    }

    static evmc_bytes32 getStorage(evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key) noexcept
    {
        auto& hostContext = static_cast<HostContextType&>(*context);
        return waitOperator(hostContext.get(key));
    }

    static evmc_bytes32 getTransientStorage(evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key) noexcept
    {
        auto& hostContext = static_cast<HostContextType&>(*context);
        return waitOperator(hostContext.getTransientStorage(key));
    }

    static evmc_storage_status setStorage(evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key,
        const evmc_bytes32* value) noexcept
    {
        assert(!concepts::bytebuffer::equalTo(addr->bytes, executor::EMPTY_EVM_ADDRESS.bytes));
        auto& hostContext = static_cast<HostContextType&>(*context);

        auto status = EVMC_STORAGE_MODIFIED;
        if (concepts::bytebuffer::equalTo(value->bytes, executor::EMPTY_EVM_BYTES32.bytes))
        {
            status = EVMC_STORAGE_DELETED;
        }
        waitOperator(hostContext.set(key, value));
        return status;
    }

    static void setTransientStorage(evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key,
        const evmc_bytes32* value) noexcept
    {
        auto& hostContext = static_cast<HostContextType&>(*context);
        waitOperator(hostContext.setTransientStorage(key, value));
    }

    static evmc_bytes32 getBalance([[maybe_unused]] evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr) noexcept
    {
        // always return 0
        return toEvmC(h256(0));
    }

    static size_t getCodeSize(evmc_host_context* context, const evmc_address* addr) noexcept
    {
        auto& hostContext = static_cast<HostContextType&>(*context);
        return waitOperator(hostContext.codeSizeAt(*addr));
    }

    static evmc_bytes32 getCodeHash(evmc_host_context* context, const evmc_address* addr) noexcept
    {
        auto& hostContext = static_cast<HostContextType&>(*context);
        return toEvmC(waitOperator(hostContext.codeHashAt(*addr)));
    }

    static size_t copyCode(evmc_host_context* context, const evmc_address* address,
        size_t codeOffset, uint8_t* bufferData, size_t bufferSize) noexcept
    {
        auto& hostContext = static_cast<HostContextType&>(*context);
        auto codeEntry = waitOperator(hostContext.code(*address));

        // Handle "big offset" edge case.
        if (!codeEntry || codeOffset >= (size_t)codeEntry->size())
        {
            return 0;
        }
        auto code = codeEntry->get();

        size_t maxToCopy = code.size() - codeOffset;
        size_t numToCopy = std::min(maxToCopy, bufferSize);
        std::copy_n(&(code[codeOffset]), numToCopy, bufferData);
        return numToCopy;
    }

    static bool selfdestruct(evmc_host_context* context, [[maybe_unused]] const evmc_address* addr,
        [[maybe_unused]] const evmc_address* beneficiary) noexcept
    {
        auto& hostContext = static_cast<HostContextType&>(*context);

        hostContext.suicide();  // FISCO BCOS has no _beneficiary
        return false;
    }

    static void log(evmc_host_context* context, const evmc_address* addr, uint8_t const* data,
        size_t dataSize, const evmc_bytes32 topics[], size_t numTopics) noexcept
    {
        auto& hostContext = static_cast<HostContextType&>(*context);
        h256s hashTopics;
        hashTopics.reserve(numTopics);
        for (auto i : RANGES::iota_view<size_t, size_t>(0, numTopics))
        {
            hashTopics.emplace_back(topics[i].bytes, sizeof(evmc_bytes32));
        }
        hostContext.log(*addr, std::move(hashTopics), bytesConstRef{data, dataSize});
    }

    static evmc_access_status accessAccount([[maybe_unused]] evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr) noexcept
    {
        return EVMC_ACCESS_COLD;
    }

    static evmc_access_status accessStorage([[maybe_unused]] evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr,
        [[maybe_unused]] const evmc_bytes32* key) noexcept
    {
        return EVMC_ACCESS_COLD;
    }

    static evmc_tx_context getTxContext(evmc_host_context* context) noexcept
    {
        auto& hostContext = static_cast<HostContextType&>(*context);
        evmc_tx_context result = {
            .tx_gas_price = {},
            .tx_origin = hostContext.origin(),
            .block_coinbase = {},
            .block_number = hostContext.blockNumber(),
            .block_timestamp = hostContext.timestamp(),
            .block_gas_limit = hostContext.blockGasLimit(),
            .block_prev_randao = {},
            .chain_id = hostContext.chainId(),
            .block_base_fee = {},
            .blob_base_fee = {},
            .blob_hashes = {},
            .blob_hashes_count = 0,
        };
        return result;
    }

    static evmc_bytes32 getBlockHash(evmc_host_context* context, int64_t number) noexcept
    {
        auto& hostContext = static_cast<HostContextType&>(*context);
        return toEvmC(waitOperator(hostContext.blockHash(number)));
    }

    static evmc_result call(evmc_host_context* context, const evmc_message* message) noexcept
    {
        if (message->gas < 0)
        {
            EXECUTIVE_LOG(INFO) << LOG_DESC("EVM Gas overflow")
                                << LOG_KV("message gas:", message->gas);
            BOOST_THROW_EXCEPTION(protocol::GasOverflow());
        }

        auto& hostContext = static_cast<HostContextType&>(*context);
        auto result = waitOperator(hostContext.externalCall(*message));
        evmc_result evmcResult = result;
        result.release = nullptr;
        return evmcResult;
    }
};

template <class HostContextType>
const evmc_host_interface* getHostInterface(auto&& waitOperator)
{
    constexpr static std::decay_t<decltype(waitOperator)> localWaitOperator{};
    using HostContextImpl = EVMHostInterface<HostContextType, localWaitOperator>;
    static evmc_host_interface const fnTable = {
        HostContextImpl::accountExists,
        HostContextImpl::getStorage,
        HostContextImpl::setStorage,
        HostContextImpl::getBalance,
        HostContextImpl::getCodeSize,
        HostContextImpl::getCodeHash,
        HostContextImpl::copyCode,
        HostContextImpl::selfdestruct,
        HostContextImpl::call,
        HostContextImpl::getTxContext,
        HostContextImpl::getBlockHash,
        HostContextImpl::log,
        HostContextImpl::accessAccount,
        HostContextImpl::accessStorage,
        HostContextImpl::getTransientStorage,
        HostContextImpl::setTransientStorage,
    };
    return &fnTable;
}

}  // namespace bcos::transaction_executor
