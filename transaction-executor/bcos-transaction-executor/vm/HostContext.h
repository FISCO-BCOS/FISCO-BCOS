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
 * @file HostContext.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#pragma once

#include "../Common.h"
#include "EVMHostInterface.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/storage2/Storage2.h>
#include <bcos-task/Wait.h>
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <evmc/instructions.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string_view>

namespace bcos::transaction_executor
{

evmc_bytes32 evm_hash_fn(const uint8_t* data, size_t size)
{
    return toEvmC(GlobalHashImpl::g_hashImpl->hash(bytesConstRef(data, size)));
}

template <storage2::Storage<storage::Entry> Storage>
class HostContext : public evmc_host_context
{
public:
    /// Full constructor.
    HostContext(const protocol::BlockHeader& blockHeader, const evmc_message& message,
        Storage& storage, std::string tableName,
        std::function<task::Task<evmc_result>(const evmc_message&)> callMethod,
        std::function<task::Task<h256>(int64_t)> getBlockHashMethod)
      : evmc_host_context(),
        m_message(message),
        m_storage(storage),
        m_tableName(std::move(tableName)),
        m_callMethod(std::move(callMethod)),
        m_getBlockHashMethod(std::move(getBlockHashMethod))
    {
        // TODO: Set the interfaces
        interface = getHostInterface();

        hash_fn = evm_hash_fn;
        // version = m_executive->blockContext().blockVersion();
        isSMCrypto = false;
        if (GlobalHashImpl::g_hashImpl &&
            GlobalHashImpl::g_hashImpl->getHashImplType() == crypto::HashImplType::Sm3Hash)
        {
            isSMCrypto = true;
        }

        constexpr static evmc_gas_metrics ethMetrics{32000, 20000, 5000, 200, 9000, 2300, 25000};
        metrics = &ethMetrics;
    }
    ~HostContext() noexcept = default;

    HostContext(HostContext const&) = delete;
    HostContext& operator=(HostContext const&) = delete;
    HostContext(HostContext&&) = delete;
    HostContext& operator=(HostContext&&) = delete;

    // Read storage location.
    task::Task<evmc_bytes32> store(const evmc_bytes32* key)
    {
        auto keyView = std::string_view((char*)key->bytes, sizeof(key->bytes));
        auto entry = co_await m_storage.getRow(m_tableName, keyView);

        evmc_bytes32 result;
        if (entry)
        {
            auto field = entry->getField(0);
            std::uninitialized_copy_n(field.data(), sizeof(result), result.bytes);
        }
        else
        {
            std::uninitialized_fill_n(result.bytes, sizeof(result), 0);
        }
        co_return result;
    }

    // Write a value in storage.
    task::Task<void> setStore(const evmc_bytes32* key, const evmc_bytes32* value)
    {
        std::string_view keyView((char*)key->bytes, sizeof(key->bytes));
        std::string_view valueView((char*)value->bytes, sizeof(value->bytes));

        storage::Entry entry;
        entry.set(valueView);
        co_await m_storage.setRow(m_tableName, keyView, std::move(entry));
    }

    // call
    task::Task<evmc_result> call(const evmc_message& message)
    {
        auto result = co_await m_callMethod(message);

        // TODO: Store the log
        co_return result;
    }

    task::Task<evmc_result> callBuiltInPrecompiled(
        const evmc_message* message, bool _isEvmPrecompiled)
    {
        //
    }

    task::Task<void> setCode(crypto::HashType codeHash, bytes code)
    {
        storage::Entry codeHashEntry;
        codeHashEntry.importFields({codeHash});

        storage::Entry codeEntry;
        codeEntry.importFields({std::move(code)});

        if (blockVersion() >= uint32_t(bcos::protocol::BlockVersion::V3_1_VERSION))
        {
            // Query the code table first
            auto oldCodeEntry =
                co_await m_storage.getRow(bcos::ledger::SYS_CODE_BINARY, codeHashEntry.get());
            if (!oldCodeEntry)
            {
                co_await m_storage.setRow(
                    bcos::ledger::SYS_CODE_BINARY, codeHash, std::move(codeEntry));
            }
            co_await m_storage.setRow(m_tableName, ACCOUNT_CODE_HASH, std::move(codeHashEntry));

            co_return;
        }

        // old logic
        co_await m_storage.setRow(m_tableName, ACCOUNT_CODE_HASH, std::move(codeHashEntry));
        co_await m_storage.setRow(m_tableName, ACCOUNT_CODE, std::move(codeEntry));
    }

    task::Task<void> setCode(bytes code)
    {
        co_await setCode(GlobalHashImpl::g_hashImpl->hash(code), std::move(code));
    }

    task::Task<void> setCodeAndABI(bytes code, std::string abi)
    {
        auto codeHash = GlobalHashImpl::g_hashImpl->hash(code);
        co_await setCode(codeHash, std::move(code));

        storage::Entry abiEntry;
        abiEntry.importFields({std::move(abi)});
        if (blockVersion() >= uint32_t(bcos::protocol::BlockVersion::V3_1_VERSION))
        {
            auto oldABIEntry = co_await m_storage.getRow(bcos::ledger::SYS_CONTRACT_ABI, codeHash);
            if (oldABIEntry)
            {
                co_await m_storage.setRow(
                    bcos::ledger::SYS_CONTRACT_ABI, codeHash, std::move(abiEntry));
            }

            co_return;
        }
        co_await m_storage.setRow(m_tableName, ACCOUNT_ABI, std::move(abiEntry));
    }

    task::Task<size_t> codeSizeAt(std::string_view address)
    {
        if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            // TODO: Check is precompiled
            std::string contractAddress(address);  // TODO: add address prefix
            auto codeEntry =
                co_await m_storage.getRow(contractAddress, bcos::ledger::SYS_CODE_BINARY);
            if (codeEntry)
            {
                co_return codeEntry->length();
            }
            co_return 0;
        }
        co_return 1;
    }

    task::Task<h256> codeHashAt(std::string_view address)
    {
        if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            // TODO: check is precompiled
            std::string contractAddress(address);  // TODO: add address prefix
            auto codeHashEntry = co_await m_storage.getRow(contractAddress, ACCOUNT_CODE_HASH);
            if (codeHashEntry)
            {
                auto view = codeHashEntry->get();
                h256 codeHash(view.begin(), view.end());
                co_return codeHash;
            }
        }
        co_return {};
    }

    /// Does the account exist?
    task::Task<bool> exists([[maybe_unused]] const std::string_view& address) { co_return true; }

    /// Return the EVM gas-price schedule for this execution context.
    VMSchedule const& vmSchedule() const { return DefaultSchedule; }

    /// Hash of a block if within the last 256 blocks, or h256() otherwise.
    task::Task<h256> blockHash(int64_t number) const { co_return m_getBlockHashMethod(number); }
    int64_t blockNumber() const { return m_blockHeader.number(); }
    uint32_t blockVersion() const { return m_blockHeader.version(); }
    uint64_t timestamp() const { return m_blockHeader.timestamp(); }
    int64_t blockGasLimit() const
    {
        // TODO: add version check
        // if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        // {
        //     // FISCO BCOS only has tx Gas limit. We use it as block gas limit
        //     return m_executive->blockContext().txGasLimit();
        // }
        return 3000000000;  // TODO: add config
    }

    /// Revert any changes made (by any of the other calls).
    void log(h256s&& _topics, bytesConstRef _data);

    /// ------ get interfaces related to HostContext------
    std::string_view myAddress() const;
    std::string_view caller() const { return fromEvmC(m_message.sender); }  // no call?
    std::string_view origin() const { return m_origin; }
    std::string_view codeAddress() const { return m_origin; }  // no call?
    bytesConstRef data() const { return {m_message.input_data, m_message.input_size}; }

    h256 codeHash() const;

    u256 salt() const { return fromEvmC(m_message.create2_salt); }
    int64_t gas() const { return m_message.gas; }
    void suicide()
    {
        if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            suicide(m_tableName);
        }
    }

    const evmc_message& getMessage() const { return m_message; }

private:
    const protocol::BlockHeader& m_blockHeader;
    const evmc_message& m_message;
    Storage& m_storage;
    std::string m_tableName;
    std::string m_origin;

    std::function<task::Task<evmc_result>(const evmc_message&)> m_callMethod;
    std::function<task::Task<h256>(int64_t)> m_getBlockHashMethod;
};

}  // namespace bcos::transaction_executor
