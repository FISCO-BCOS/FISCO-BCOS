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
#include <memory>
#include <memory_resource>

namespace bcos::transaction_executor
{
static_assert(sizeof(Address) == sizeof(evmc_address), "Address types size mismatch");
static_assert(alignof(Address) == alignof(evmc_address), "Address types alignment mismatch");
static_assert(sizeof(h256) == sizeof(evmc_bytes32), "Hash types size mismatch");
static_assert(alignof(h256) == alignof(evmc_bytes32), "Hash types alignment mismatch");

constexpr static auto SMALL_STACK = 1024;
template <int stackSize>
struct StackAllocator
{
    StackAllocator()  // NOLINT
      : m_resource(m_stack.data(), m_stack.size(), std::pmr::get_default_resource()),
        m_allocator(std::addressof(m_resource))
    {}
    std::array<std::byte, stackSize> m_stack;
    std::pmr::monotonic_buffer_resource m_resource;
    std::pmr::polymorphic_allocator<> m_allocator;

    std::pmr::polymorphic_allocator<>& getAllocator() { return m_allocator; }
};

template <class HostContextType, auto syncWait>
struct EVMHostInterface
{
    static bool accountExists(evmc_host_context* context, const evmc_address* addr) noexcept
    {
        StackAllocator<SMALL_STACK> stackAllocator;
        auto& hostContext = static_cast<HostContextType&>(*context);
        return syncWait(
            hostContext.exists(*addr, std::allocator_arg, stackAllocator.getAllocator()),
            std::allocator_arg, stackAllocator.getAllocator());
    }

    static evmc_bytes32 getStorage(evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key) noexcept
    {
        StackAllocator<SMALL_STACK> stackAllocator;
        auto& hostContext = static_cast<HostContextType&>(*context);
        return syncWait(hostContext.get(key, std::allocator_arg, stackAllocator.getAllocator()),
            std::allocator_arg, stackAllocator.getAllocator());
    }

    static evmc_bytes32 getTransientStorage(evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key) noexcept
    {
        StackAllocator<SMALL_STACK> stackAllocator;
        auto& hostContext = static_cast<HostContextType&>(*context);
        return syncWait(
            hostContext.getTransientStorage(key, std::allocator_arg, stackAllocator.getAllocator()),
            std::allocator_arg, stackAllocator.getAllocator());
    }

    static evmc_storage_status setStorage(evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key,
        const evmc_bytes32* value) noexcept
    {
        StackAllocator<SMALL_STACK> stackAllocator;
        assert(!concepts::bytebuffer::equalTo(addr->bytes, executor::EMPTY_EVM_ADDRESS.bytes));
        auto& hostContext = static_cast<HostContextType&>(*context);

        auto status = EVMC_STORAGE_MODIFIED;
        if (concepts::bytebuffer::equalTo(value->bytes, executor::EMPTY_EVM_BYTES32.bytes))
        {
            status = EVMC_STORAGE_DELETED;
        }
        syncWait(hostContext.set(key, value, std::allocator_arg, stackAllocator.getAllocator()),
            std::allocator_arg, stackAllocator.getAllocator());
        return status;
    }

    static void setTransientStorage(evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key,
        const evmc_bytes32* value) noexcept
    {
        StackAllocator<SMALL_STACK> stackAllocator;
        auto& hostContext = static_cast<HostContextType&>(*context);
        syncWait(hostContext.setTransientStorage(
                     key, value, std::allocator_arg, stackAllocator.getAllocator()),
            std::allocator_arg, stackAllocator.getAllocator());
    }

    static evmc_bytes32 getBalance([[maybe_unused]] evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr) noexcept
    {
        // always return 0
        auto& hostContext = static_cast<HostContextType&>(*context);
        
        return toEvmC(h256(0));
    }

    static size_t getCodeSize(evmc_host_context* context, const evmc_address* addr) noexcept
    {
        StackAllocator<SMALL_STACK> stackAllocator;
        auto& hostContext = static_cast<HostContextType&>(*context);
        return syncWait(
            hostContext.codeSizeAt(*addr, std::allocator_arg, stackAllocator.getAllocator()),
            std::allocator_arg, stackAllocator.getAllocator());
    }

    static evmc_bytes32 getCodeHash(evmc_host_context* context, const evmc_address* addr) noexcept
    {
        StackAllocator<SMALL_STACK> stackAllocator;
        auto& hostContext = static_cast<HostContextType&>(*context);
        return toEvmC(syncWait(
            hostContext.codeHashAt(*addr, std::allocator_arg, stackAllocator.getAllocator()),
            std::allocator_arg, stackAllocator.getAllocator()));
    }

    static size_t copyCode(evmc_host_context* context, const evmc_address* address,
        size_t codeOffset, uint8_t* bufferData, size_t bufferSize) noexcept
    {
        StackAllocator<SMALL_STACK> stackAllocator;
        auto& hostContext = static_cast<HostContextType&>(*context);
        auto codeEntry =
            syncWait(hostContext.code(*address, std::allocator_arg, stackAllocator.getAllocator()),
                std::allocator_arg, stackAllocator.getAllocator());

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
        StackAllocator<SMALL_STACK> stackAllocator;
        auto& hostContext = static_cast<HostContextType&>(*context);
        return toEvmC(syncWait(
            hostContext.blockHash(number, std::allocator_arg, stackAllocator.getAllocator()),
            std::allocator_arg, stackAllocator.getAllocator()));
    }

    static evmc_result call(evmc_host_context* context, const evmc_message* message)
    {
        if (message->gas < 0)
        {
            EXECUTIVE_LOG(INFO) << LOG_DESC("EVM Gas overflow")
                                << LOG_KV("message gas:", message->gas);
            BOOST_THROW_EXCEPTION(protocol::GasOverflow());
        }

        StackAllocator<SMALL_STACK> stackAllocator;
        auto& hostContext = static_cast<HostContextType&>(*context);
        return syncWait(
            hostContext.externalCall(*message, std::allocator_arg, stackAllocator.getAllocator()),
            std::allocator_arg, stackAllocator.getAllocator());
    }
};

template <class HostContextType>
const evmc_host_interface* getHostInterface(auto&& syncWait)
{
    constexpr static std::decay_t<decltype(syncWait)> localWaitOperator{};
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
