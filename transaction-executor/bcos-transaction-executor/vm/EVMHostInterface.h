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

// NOTE: evmc_host_context is upstream-opaque; every context pointer entering these
// shims is the executor's own HostContextType (see VMInstance/HostContext::execute),
// so the reinterpret_casts below are the inverse of the cast made at the execute()
// call sites.

#pragma once

#include "bcos-concepts/ByteBuffer.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/protocol/Exceptions.h"
#include <evmc/evmc.h>
#include <boost/exception/diagnostic_information.hpp>
#include <limits>

namespace bcos::executor_v1
{
static_assert(sizeof(Address) == sizeof(evmc_address), "Address types size mismatch");
static_assert(alignof(Address) == alignof(evmc_address), "Address types alignment mismatch");
static_assert(sizeof(h256) == sizeof(evmc_bytes32), "Hash types size mismatch");
static_assert(alignof(h256) == alignof(evmc_bytes32), "Hash types alignment mismatch");

template <class HostContextType, auto syncWait>
struct EVMHostInterface
{
    // Callback exceptions cannot cross EVMC's noexcept host adapter. Record the
    // failure and use a temporary fail-closed answer; execute() subsequently throws
    // HostContextFault through the executor error channel, before receipt creation.
    // Deterministic VM results (including a negative gas budget) retain their status.

    template <typename Result, typename Function>
    static Result translateHostFault(
        HostContextType& hostContext, Result fallback, Function&& function) noexcept
    {
        if (hostContext.hasHostFault())
        {
            return fallback;
        }
        try
        {
            return function();
        }
        catch (...)
        {
            hostContext.recordHostFault(boost::current_exception_diagnostic_information());
            return fallback;
        }
    }

    template <typename Function>
    static void translateHostFault(HostContextType& hostContext, Function&& function) noexcept
    {
        translateHostFault(hostContext, false, [&]() {
            function();
            return true;
        });
    }

    static bool accountExists(evmc_host_context* context, const evmc_address* addr) noexcept
    {
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        return translateHostFault(
            hostContext, false, [&]() -> bool { return syncWait(hostContext.exists(*addr)); });
    }

    static evmc_bytes32 getStorage(evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key) noexcept
    {
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        return translateHostFault(hostContext, evmc_bytes32{},
            [&]() -> evmc_bytes32 { return syncWait(hostContext.get(key)); });
    }

    static evmc_bytes32 getTransientStorage(evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key) noexcept
    {
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        return translateHostFault(hostContext, evmc_bytes32{},
            [&]() -> evmc_bytes32 { return syncWait(hostContext.getTransientStorage(key)); });
    }

    static evmc_storage_status setStorage(evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key,
        const evmc_bytes32* value) noexcept
    {
        assert(!concepts::bytebuffer::equalTo(addr->bytes, executor::EMPTY_EVM_ADDRESS.bytes));
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        return translateHostFault(hostContext, EVMC_STORAGE_ASSIGNED, [&]() -> evmc_storage_status {
            bool newIsZero =
                concepts::bytebuffer::equalTo(value->bytes, executor::EMPTY_EVM_BYTES32.bytes);

            evmc_storage_status status;
            if (hostContext.ledgerConfig().features().get(
                    ledger::Features::Flag::bugfix_evm_storage_status))
            {
                // TODO: full EIP-2200 support — also report the 5 dirty-slot statuses
                // (DELETED_ADDED, MODIFIED_DELETED, DELETED_RESTORED, ADDED_DELETED,
                // MODIFIED_RESTORED). Requires tracking the transaction-original value
                // per slot to distinguish clean (o == c) from dirty (o != c) writes.
                //
                // bypass-read-set read: this lookup is for status metadata only and must
                // not be registered in the parallel scheduler's read set, otherwise pure
                // SSTORE writes would create false RAW edges against any earlier writer
                // of the same slot.
                auto existingValue = syncWait(
                    hostContext.get(key, storage2::BYPASS_READ_SET, storage2::BYPASS_MULTILAYER));
                const bool existingIsZero = concepts::bytebuffer::equalTo(
                    existingValue.bytes, executor::EMPTY_EVM_BYTES32.bytes);
                if (newIsZero)
                {
                    status = existingIsZero ? EVMC_STORAGE_ASSIGNED : EVMC_STORAGE_DELETED;
                }
                else
                {
                    status = existingIsZero ? EVMC_STORAGE_ADDED : EVMC_STORAGE_MODIFIED;
                }
            }
            else
            {
                status = newIsZero ? EVMC_STORAGE_DELETED : EVMC_STORAGE_MODIFIED;
            }

            syncWait(hostContext.set(key, value));
            return status;
        });
    }

    static void setTransientStorage(evmc_host_context* context,
        [[maybe_unused]] const evmc_address* addr, const evmc_bytes32* key,
        const evmc_bytes32* value) noexcept
    {
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        translateHostFault(
            hostContext, [&]() { syncWait(hostContext.setTransientStorage(key, value)); });
    }

    static evmc_bytes32 getBalance(evmc_host_context* context, const evmc_address* addr) noexcept
    {
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        return translateHostFault(hostContext, evmc_bytes32{}, [&]() -> evmc_bytes32 {
            auto balance = syncWait(hostContext.balance(*addr));
            return toEvmC(balance);
        });
    }

    static uint64_t getNonce(evmc_host_context* context, const evmc_address* addr)
    {
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        return translateHostFault(hostContext, std::numeric_limits<uint64_t>::max(),
            [&]() -> uint64_t { return syncWait(hostContext.nonce(*addr)); });
    }

    static size_t getCodeSize(evmc_host_context* context, const evmc_address* addr) noexcept
    {
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        return translateHostFault(hostContext, size_t{0},
            [&]() -> size_t { return syncWait(hostContext.codeSizeAt(*addr)); });
    }

    static evmc_bytes32 getCodeHash(evmc_host_context* context, const evmc_address* addr) noexcept
    {
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        return translateHostFault(hostContext, evmc_bytes32{},
            [&]() -> evmc_bytes32 { return toEvmC(syncWait(hostContext.codeHashAt(*addr))); });
    }

    static size_t copyCode(evmc_host_context* context, const evmc_address* address,
        size_t codeOffset, uint8_t* bufferData, size_t bufferSize) noexcept
    {
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        return translateHostFault(hostContext, size_t{0}, [&]() -> size_t {
            auto codeEntry = syncWait(hostContext.code(*address));

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
        });
    }

    static bool selfdestruct(evmc_host_context* context, [[maybe_unused]] const evmc_address* addr,
        [[maybe_unused]] const evmc_address* beneficiary) noexcept
    {
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        translateHostFault(hostContext, [&]() {
            hostContext.suicide();  // FISCO BCOS has no _beneficiary
        });
        // EIP-3529 (London): SELFDESTRUCT gas refund is removed entirely.
        // EIP-6780 (Cancun+): account deletion only when created in same tx;
        //   no gas refund in either case ("Note that no refund is given since EIP-3529").
        // Returning false (no refund) is the correct behavior for all cases.
        // TODO(evmone-eip6780): implement same-tx creation tracking for account deletion.
        return false;
    }

    static void log(evmc_host_context* context, const evmc_address* addr, uint8_t const* data,
        size_t dataSize, const evmc_bytes32 topics[], size_t numTopics) noexcept
    {
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        translateHostFault(hostContext, [&]() {
            h256s hashTopics;
            hashTopics.reserve(numTopics);
            for (auto i : ::ranges::views::iota(0LU, numTopics))
            {
                hashTopics.emplace_back(topics[i].bytes, sizeof(evmc_bytes32));
            }
            hostContext.log(*addr, std::move(hashTopics), bytesConstRef{data, dataSize});
        });
    }

    static evmc_access_status accessAccount(
        evmc_host_context* context, const evmc_address* addr) noexcept
    {
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        return translateHostFault(hostContext, EVMC_ACCESS_WARM,
            [&]() -> evmc_access_status { return hostContext.accessAccount(*addr); });
    }

    static evmc_access_status accessStorage(
        evmc_host_context* context, const evmc_address* addr, const evmc_bytes32* key) noexcept
    {
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        return translateHostFault(hostContext, EVMC_ACCESS_WARM,
            [&]() -> evmc_access_status { return hostContext.accessStorage(*addr, *key); });
    }

    static evmc_tx_context getTxContext(evmc_host_context* context) noexcept
    {
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        return translateHostFault(hostContext, evmc_tx_context{}, [&]() -> evmc_tx_context {
            evmc_tx_context result = {
                .tx_gas_price = toEvmC(hostContext.gasPrice()),
                .tx_origin = hostContext.origin(),
                // TODO(EIP-3651): block_coinbase from HostContext (sealer-derived); must
                // match W1 warm set.
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
                .block_slot_number = 0,  // EIP-7843 (evmc ABI 19): no beacon slots on
                                         // this chain.
            };
            return result;
        });
    }

    static evmc_bytes32 getBlockHash(evmc_host_context* context, int64_t number) noexcept
    {
        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        return translateHostFault(hostContext, evmc_bytes32{},
            [&]() -> evmc_bytes32 { return toEvmC(syncWait(hostContext.blockHash(number))); });
    }

    static evmc_result call(evmc_host_context* context, const evmc_message* message)
    {
        if (message->gas < 0)
        {
            // A wrapped negative gas budget is refused before entering the VM. Answer
            // deterministically (the v1 callback's arm) instead of throwing out of a
            // host callback — a raw throw cannot cross evmc::VM::execute's noexcept
            // boundary.
            EXECUTIVE_LOG(INFO) << LOG_DESC("EVM Gas overflow")
                                << LOG_KV("message gas:", message->gas);
            evmc_result rejected{};
            rejected.status_code = EVMC_OUT_OF_GAS;
            return rejected;
        }

        auto& hostContext = *reinterpret_cast<HostContextType*>(context);
        // R6 F3: a frame that already recorded a host fault is about to fail — stop
        // driving the host (and the scheduler via externalCall) at the first fault.
        // Mirrors the v1 translateHostFault pre-check.
        if (hostContext.hasHostFault())
        {
            evmc_result rejected{};
            rejected.status_code = EVMC_INTERNAL_ERROR;
            return rejected;
        }
        try
        {
            auto result = syncWait(hostContext.externalCall(*message));
            evmc_result evmcResult = result;
            result.release = nullptr;
            return evmcResult;
        }
        catch (...)
        {
            // Only the externalCall dispatch is translated; the gas<0 refusal above
            // never reaches here (it returns EVMC_OUT_OF_GAS before entering the VM).
            hostContext.recordHostFault(boost::current_exception_diagnostic_information());
            return evmc_result{.status_code = EVMC_INTERNAL_ERROR,
                .gas_left = 0,
                .gas_refund = 0,
                .state_gas = {},
                .output_data = nullptr,
                .output_size = 0,
                .release = nullptr};
        }
    }
};

template <class HostContextType>
const evmc_host_interface* getHostInterface(auto&& syncWait)
{
    constexpr static std::decay_t<decltype(syncWait)> localWaitOperator{};
    using HostContextImpl = EVMHostInterface<HostContextType, localWaitOperator>;
    // Designated initializers (C++20) keep each function pointer bound to its named
    // slot: evmc ABI 13..19 inserted get_nonce mid-struct, which would silently
    // shift every positional initializer that follows it.
    static evmc_host_interface const fnTable = {
        .account_exists = HostContextImpl::accountExists,
        .get_storage = HostContextImpl::getStorage,
        .set_storage = HostContextImpl::setStorage,
        .get_balance = HostContextImpl::getBalance,
        .get_nonce = HostContextImpl::getNonce,
        .get_code_size = HostContextImpl::getCodeSize,
        .get_code_hash = HostContextImpl::getCodeHash,
        .copy_code = HostContextImpl::copyCode,
        .selfdestruct = HostContextImpl::selfdestruct,
        .call = HostContextImpl::call,
        .get_tx_context = HostContextImpl::getTxContext,
        .get_block_hash = HostContextImpl::getBlockHash,
        .emit_log = HostContextImpl::log,
        .access_account = HostContextImpl::accessAccount,
        .access_storage = HostContextImpl::accessStorage,
        .get_transient_storage = HostContextImpl::getTransientStorage,
        .set_transient_storage = HostContextImpl::setTransientStorage,
    };
    return &fnTable;
}

}  // namespace bcos::executor_v1
