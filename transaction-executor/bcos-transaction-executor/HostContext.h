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

#include "Common.h"
#include "RollbackableStorage.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/Concepts.h>
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

template <StateStorage Storage>
class HostContext : public evmc_host_context
{
public:
    /// Full constructor.
    HostContext(const protocol::BlockHeader& blockHeader, const evmc_message& message, Storage& storage,
        TableNameID tableName, std::function<task::Task<h256>(int64_t)> getBlockHashMethod)
      : evmc_host_context(),
        m_message(message),
        m_storage(storage),
        m_myContractTable(tableName),
        m_getBlockHashMethod(std::move(getBlockHashMethod))
    {
        // TODO: Set the interfaces
        interface = getHostInterface<decltype(*this)>();
        wasm_interface = nullptr;

        hash_fn = evm_hash_fn;
        // version = m_executive->blockContext().lock()->blockVersion();
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
        auto it = co_await m_storage.read(storage2::single(StateKey{m_myContractTable, keyView}));
        co_await it.next();

        evmc_bytes32 result;
        if (co_await it.hasValue())
        {
            auto& entry = co_await it.value();
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

        co_await m_storage.write(storage2::single(keyView), storage2::single(entry));
    }

    // call
    task::Task<evmc_result> call(const evmc_message& message)
    {
        // TODO: to be done
        co_return {};
    }

    task::Task<evmc_result> callBuiltInPrecompiled(const evmc_message* message, bool _isEvmPrecompiled)
    {
        // TODO: to be done
        co_return {};
    }

    task::Task<void> setCode(crypto::HashType codeHash, bytes code)
    {
        storage::Entry codeHashEntry;
        codeHashEntry.importFields({codeHash});

        if (blockVersion() >= uint32_t(bcos::protocol::BlockVersion::V3_1_VERSION))
        {
            // Query the code table first
            bool exists = false;
            {
                auto codeIt = co_await m_storage.read(storage2::single(StateKey{m_codeTable, codeHashEntry.get()}));
                co_await codeIt.next();
                exists = co_await codeIt.hasValue();
            }
            if (!exists)
            {
                storage::Entry codeEntry;
                codeEntry.importFields({std::move(code)});
                co_await m_storage.write(
                    storage2::single(StateKey{m_codeTable, codeHashEntry.get()}), storage2::single(codeEntry));
            }
            co_await m_storage.write(
                storage2::single(StateKey{m_myContractTable, ACCOUNT_CODE_HASH}), storage2::single(codeHashEntry));

            co_return;
        }

        storage::Entry codeEntry;
        codeEntry.importFields({std::move(code)});
        // old logic
        co_await m_storage.write(
            storage2::single(StateKey{m_myContractTable, ACCOUNT_CODE_HASH}), storage2::single(codeHashEntry));
        co_await m_storage.write(
            storage2::single(StateKey{m_myContractTable, ACCOUNT_CODE}), storage2::single(codeEntry));
        co_return;
    }

    task::Task<void> setCode(bytes code) { co_await setCode(GlobalHashImpl::g_hashImpl->hash(code), std::move(code)); }

    task::Task<void> setCodeAndABI(bytes code, std::string abi)
    {
        auto codeHash = GlobalHashImpl::g_hashImpl->hash(code);
        auto codeHashView = std::string_view((char*)codeHash.data(), codeHash.size());
        co_await setCode(codeHash, std::move(code));

        storage::Entry abiEntry;
        abiEntry.importFields({std::move(abi)});
        if (blockVersion() >= uint32_t(bcos::protocol::BlockVersion::V3_1_VERSION))
        {
            auto abiIt = co_await m_storage.read(storage2::single(StateKey{m_abiTable, codeHashView}));
            auto exists = co_await abiIt.next();
            if (!exists)
            {
                co_await m_storage.write(
                    m_abiTable, storage2::single(StateKey{m_abiTable, codeHashView}), storage2::single(abiEntry));
            }
            co_return;
        }
        co_await m_storage.write(
            storage2::single(StateKey{m_myContractTable, ACCOUNT_ABI}), storage2::single(abiEntry));
    }

    task::Task<size_t> codeSizeAt(std::string_view address)
    {
        if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            // TODO: Check is precompiled
            std::string contractAddress(address);  // TODO: add address prefix
            TableNameID contractTableID;
            auto codeIt = co_await m_storage.read(storage2::single(StateKey{contractTableID, ACCOUNT_CODE}));
            co_await codeIt.next();
            if (co_await codeIt.hasValue())
            {
                auto& code = codeIt.value();
                co_return code.length();
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
        //     return m_executive->blockContext().lock()->txGasLimit();
        // }
        return 3000000000;  // TODO: add config
    }

    /// Revert any changes made (by any of the other calls).
    void log(h256s&& _topics, bytesConstRef _data)
    {
        // TODO:
    }

    std::string_view origin() const { return m_origin; }
    /// ------ get interfaces related to HostContext------
    // std::string_view myAddress() const { return storage2::string_pool::query(m_myContractTable); }
    // std::string_view caller() const { return fromEvmC(m_message.sender); }  // no call?
    // std::string_view codeAddress() const { return m_origin; }  // no call?
    // bytesConstRef data() const { return {m_message.input_data, m_message.input_size}; }
    // h256 codeHash() const;
    // u256 salt() const { return fromEvmC(m_message.create2_salt); }
    // int64_t gas() const { return m_message.gas; }
    void suicide()
    {
        if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            suicide(m_myContractTable);
        }
    }

    const evmc_message& getMessage() const { return m_message; }

    task::Task<evmc_result> execute(const evmc_message& message)
    {
        // TODO:
        // 1: Check auth
        // 2: Check is precompiled
        // 3. Do something create or call

        // TODO: if revert, rollback the storage and clear the log
    }

    task::Task<EVMCResult> call(const evmc_message& message)
    {
        std::string tableName;  // TODO: calculate the table name
        Rollbackable rollbackableStorage(m_storage);
        auto codeEntry = m_storage.getRow(tableName, ACCOUNT_CODE);
        if (!codeEntry)
        {
            BOOST_THROW_EXCEPTION(NotFoundCode{} << bcos::Error::ErrorMessage("Not found contract code: " + tableName));
        }
        auto code = codeEntry->get();

        HostContext<decltype(rollbackableStorage)> hostContext(m_blockHeader, message, rollbackableStorage,
            std::move(tableName), [this](int64_t) -> task::Task<h256> { co_return {}; });

        auto vmInstance = VMFactory::create();
        auto mode = toRevision(hostContext.vmSchedule());

        auto result = vmInstance.execute(hostContext.interface, &hostContext, mode, &message, code.data(), code.size());
        if (result.m_evmcResult.status_code != 0)
        {
            // TODO: Rollback the state, clear the log
            rollbackableStorage.rollback();
        }

        co_return result;
    }

private:
    const protocol::BlockHeader& m_blockHeader;
    const evmc_message& m_message;
    Storage& m_storage;
    TableNameID m_myContractTable;
    TableNameID m_codeTable;
    TableNameID m_abiTable;
    std::string m_origin;

    // std::function<task::Task<evmc_result>(const evmc_message&)> m_callMethod;
    std::function<task::Task<h256>(int64_t)> m_getBlockHashMethod;
};

}  // namespace bcos::transaction_executor
