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
#include "../RollbackableStorage.h"
#include "EVMHostInterface.h"
#include "VMFactory.h"
#include "bcos-framework/storage2/StringPool.h"
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/Concepts.h>
#include <bcos-task/Wait.h>
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <evmc/instructions.h>
#include <evmone/evmone.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string_view>

namespace bcos::transaction_executor
{

struct NotFoundCode : public bcos::Error
{
};

evmc_bytes32 evm_hash_fn(const uint8_t* data, size_t size)
{
    return toEvmC(GlobalHashImpl::g_hashImpl->hash(bytesConstRef(data, size)));
}

template <StateStorage Storage>
class HostContext : public evmc_host_context
{
public:
    HostContext(Storage& storage, TableNamePool& tableNamePool,
        const protocol::BlockHeader& blockHeader, const evmc_message& message,
        const evmc_address& origin)
      : evmc_host_context(),
        m_rollbackableStorage(storage),
        m_tableNamePool(tableNamePool),
        m_blockHeader(blockHeader),
        m_message(message),
        m_origin(origin),
        m_myContractTable(getTableNameID(message.destination)),
        m_codeTable(storage2::string_pool::makeStringID(m_tableNamePool, ledger::SYS_CODE_BINARY)),
        m_abiTable(storage2::string_pool::makeStringID(m_tableNamePool, ledger::SYS_CONTRACT_ABI))
    {
        interface = getHostInterface<HostContext>();
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

    task::Task<evmc_bytes32> get(const evmc_bytes32* key)
    {
        StateKey stateKey(
            m_myContractTable, std::string_view((char*)key->bytes, sizeof(key->bytes)));
        auto it = co_await m_rollbackableStorage.read(storage2::single(stateKey));
        co_await it.next();

        evmc_bytes32 result;
        if (co_await it.hasValue())
        {
            auto& entry = co_await it.value();
            auto field = entry.getField(0);
            std::uninitialized_copy_n(field.data(), sizeof(result), result.bytes);
        }
        else
        {
            std::uninitialized_fill_n(result.bytes, sizeof(result), 0);
        }

        co_return result;
    }

    task::Task<void> set(const evmc_bytes32* key, const evmc_bytes32* value)
    {
        StateKey stateKey(m_myContractTable, std::string((char*)key->bytes, sizeof(key->bytes)));
        std::string_view valueView((char*)value->bytes, sizeof(value->bytes));

        storage::Entry entry;
        entry.set(valueView);

        co_await m_rollbackableStorage.write(storage2::single(stateKey), storage2::single(entry));
    }

    task::Task<void> setCode(crypto::HashType codeHash, bytes code)
    {
        storage::Entry codeHashEntry;
        codeHashEntry.set(std::string_view((const char*)codeHash.data(), codeHash.size()));

        if (blockVersion() >= uint32_t(bcos::protocol::BlockVersion::V3_1_VERSION))
        {
            // Query the code table first
            if (!co_await storage2::existsOne(
                    m_rollbackableStorage, StateKey{m_codeTable, codeHashEntry.get()}))
            {
                storage::Entry codeEntry;
                codeEntry.importFields({std::move(code)});
                co_await m_rollbackableStorage.write(
                    storage2::single(StateKey{m_codeTable, codeHashEntry.get()}),
                    storage2::single(codeEntry));
            }
            co_await m_rollbackableStorage.write(
                storage2::single(StateKey{m_myContractTable, ACCOUNT_CODE_HASH}),
                storage2::single(codeHashEntry));

            co_return;
        }

        storage::Entry codeEntry;
        codeEntry.importFields({std::move(code)});
        // old logic
        co_await m_rollbackableStorage.write(
            storage2::single(StateKey{m_myContractTable, ACCOUNT_CODE_HASH}),
            storage2::single(codeHashEntry));
        co_await m_rollbackableStorage.write(
            storage2::single(StateKey{m_myContractTable, ACCOUNT_CODE}),
            storage2::single(codeEntry));
        co_return;
    }

    task::Task<void> setCode(bytes code)
    {
        co_await setCode(GlobalHashImpl::g_hashImpl->hash(code), std::move(code));
    }

    task::Task<void> setCodeAndABI(bytes code, std::string abi)
    {
        auto codeHash = GlobalHashImpl::g_hashImpl->hash(code);
        auto codeHashView = std::string_view((char*)codeHash.data(), codeHash.size());
        co_await setCode(codeHash, std::move(code));

        storage::Entry abiEntry;
        abiEntry.importFields({std::move(abi)});
        if (blockVersion() >= uint32_t(bcos::protocol::BlockVersion::V3_1_VERSION))
        {
            auto abiIt = co_await m_rollbackableStorage.read(
                storage2::single(StateKey{m_abiTable, codeHashView}));
            co_await abiIt.next();
            if (!co_await abiIt.hasValue())
            {
                co_await m_rollbackableStorage.write(m_abiTable,
                    storage2::single(StateKey{m_abiTable, codeHashView}),
                    storage2::single(abiEntry));
            }
            co_return;
        }
        co_await m_rollbackableStorage.write(
            storage2::single(StateKey{m_myContractTable, ACCOUNT_ABI}), storage2::single(abiEntry));
    }

    task::Task<size_t> codeSizeAt(const evmc_address& address)
    {
        if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            // TODO: Check is precompiled
            auto contractTableID = getTableNameID(address);
            auto code = co_await storage2::readOne(
                m_rollbackableStorage, StateKey{contractTableID, ACCOUNT_CODE});
            if (code)
            {
                co_return code->get().size();
            }
            co_return 0;
        }
        co_return 1;
    }

    task::Task<h256> codeHashAt(const evmc_address& address)
    {
        if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            // TODO: check is precompiled
            auto contractTableID = getTableNameID(address);
            auto codeHashEntry = co_await storage2::readOne(
                m_rollbackableStorage, StateKey{contractTableID, ACCOUNT_CODE_HASH});
            if (codeHashEntry)
            {
                auto view = codeHashEntry->get().get();
                h256 codeHash((const bcos::byte*)view.data(), view.size());
                co_return codeHash;
            }
        }
        co_return h256{};
    }

    /// Does the account exist?
    task::Task<bool> exists([[maybe_unused]] const std::string_view& address) { co_return true; }

    /// Return the EVM gas-price schedule for this execution context.
    VMSchedule const& vmSchedule() const { return DefaultSchedule; }

    /// Hash of a block if within the last 256 blocks, or h256() otherwise.
    task::Task<h256> blockHash(int64_t number) const { co_return h256{}; }
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
    evmc_address origin() const { return m_origin; }

    /// Revert any changes made (by any of the other calls).
    void log(h256s&& _topics, bytesConstRef _data)
    {
        // TODO:
    }

    void suicide()
    {
        if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            // suicide(m_myContractTable); // TODO: add suicide
        }
    }

    task::Task<evmc_result> execute()
    {
        constexpr static evmc_address EMPTY_DESTINATION = {};
        // TODO:
        // 1: Check auth
        // 2: Check is precompiled

        if (RANGES::equal(m_message.destination.bytes, EMPTY_DESTINATION.bytes))
        {
            co_return co_await create();
        }
        else
        {
            co_return co_await call();
        }
    }

    task::Task<evmc_result> call()
    {
        auto codeEntry = co_await storage2::readOne(
            m_rollbackableStorage, StateKey{m_myContractTable, ACCOUNT_CODE});
        if (!codeEntry)
        {
            BOOST_THROW_EXCEPTION(
                NotFoundCode{} << bcos::Error::ErrorMessage(
                    std::string("Not found contract code: ").append(*m_myContractTable)));
        }
        auto code = codeEntry->get().get();

        auto vmInstance = VMFactory::create();
        auto mode = toRevision(vmSchedule());

        auto savepoint = m_rollbackableStorage.current();
        auto result = vmInstance.execute(
            interface, this, mode, &m_message, (const uint8_t*)code.data(), code.size());
        if (result.status_code != 0)
        {
            m_rollbackableStorage.rollback(savepoint);
        }

        co_return result;
    }

    task::Task<evmc_result> create()
    {
        auto vmInstance = VMFactory::create();
        auto mode = toRevision(vmSchedule());

        auto savepoint = m_rollbackableStorage.current();
        auto result = vmInstance.execute(
            interface, this, mode, &m_message, m_message.input_data, m_message.input_size);
        if (result.status_code != 0)
        {
            m_rollbackableStorage.rollback(savepoint);
        }
        else
        {
            // Write result to table
        }

        co_return result;
    }

    task::Task<evmc_result> callBuiltInPrecompiled(
        const evmc_message* message, bool _isEvmPrecompiled)
    {
        // TODO: to be done
        co_return evmc_result{};
    }

    task::Task<evmc_result> externalCall(const evmc_message& message) { co_return evmc_result{}; }

private:
    TableNameID getTableNameID(const evmc_address& address)
    {
        std::array<char, EVM_CONTRACT_PREFIX.size() + sizeof(address)> tableName;
        std::uninitialized_copy_n(
            EVM_CONTRACT_PREFIX.data(), EVM_CONTRACT_PREFIX.size(), tableName.data());
        std::uninitialized_copy_n((const char*)address.bytes, sizeof(address),
            tableName.data() + EVM_CONTRACT_PREFIX.size());

        return storage2::string_pool::makeStringID(
            m_tableNamePool, std::string_view(tableName.data(), tableName.size()));
    }

    Rollbackable<Storage> m_rollbackableStorage;
    TableNamePool& m_tableNamePool;
    const protocol::BlockHeader& m_blockHeader;
    const evmc_message& m_message;
    const evmc_address& m_origin;

    TableNameID m_myContractTable;
    TableNameID m_codeTable;
    TableNameID m_abiTable;
};

}  // namespace bcos::transaction_executor
