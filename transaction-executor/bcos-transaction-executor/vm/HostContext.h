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
 * @author: ancelmo
 * @date: 2022-12-24
 */

#pragma once

#include "../Common.h"
#include "EVMHostInterface.h"
#include "VMFactory.h"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-framework/storage2/StringPool.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "bcos-utilities/Overloaded.h"
#include <bcos-crypto/hasher/Hasher.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-task/Wait.h>
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <evmc/instructions.h>
#include <evmone/evmone.h>
#include <fmt/format.h>
#include <boost/throw_exception.hpp>
#include <atomic>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace bcos::transaction_executor
{

#define HOST_CONTEXT_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("HOST_CONTEXT")

// clang-format off
struct NotFoundCodeError : public bcos::Error {};
// clang-format on

inline evmc_bytes32 evm_hash_fn(const uint8_t* data, size_t size)
{
    return transaction_executor::toEvmC(
        GlobalHashImpl::g_hashImpl->hash(bytesConstRef(data, size)));
}

template <StateStorage Storage, protocol::IsBlockHeader BlockHeader>
class HostContext : public evmc_host_context
{
private:
    VMFactory& m_vmFactory;
    Storage& m_rollbackableStorage;
    TableNamePool& m_tableNamePool;
    BlockHeader const& m_blockHeader;
    const evmc_message& m_message;
    const evmc_address& m_origin;
    int m_contextID;
    int64_t& m_seq;

    TableNameID m_myContractTable;
    TableNameID m_codeTable;
    TableNameID m_abiTable;
    evmc_address m_newContractAddress;
    std::vector<protocol::LogEntry> m_logs;

    TableNameID getTableNameID(const evmc_address& address)
    {
        std::array<char, USER_APPS_PREFIX.size() + sizeof(address)> tableName;
        std::uninitialized_copy_n(
            USER_APPS_PREFIX.data(), USER_APPS_PREFIX.size(), tableName.data());
        std::uninitialized_copy_n((const char*)address.bytes, sizeof(address),
            tableName.data() + USER_APPS_PREFIX.size());

        return storage2::string_pool::makeStringID(
            m_tableNamePool, std::string_view(tableName.data(), tableName.size()));
    }

    TableNameID getMyContractTable(
        const protocol::BlockHeader& blockHeader, const evmc_message& message)
    {
        switch (message.kind)
        {
        case EVMC_CREATE:
        {
            auto address = fmt::format("{}_{}_{}", blockHeader.number(), m_contextID, m_seq);
            auto hash = GlobalHashImpl::g_hashImpl->hash(address);
            std::uninitialized_copy_n(
                hash.data(), sizeof(m_newContractAddress.bytes), m_newContractAddress.bytes);

            return getTableNameID(m_newContractAddress);
        }
        case EVMC_CREATE2:
        {
            BOOST_THROW_EXCEPTION(std::runtime_error("Unimplement"));
            break;
        }
        default:
        {
            // CALL OR DELEGATECALL
            m_newContractAddress = {};
            return getTableNameID(message.recipient);
        }
        }
    }

public:
    HostContext(VMFactory& vmFactory, Storage& storage, TableNamePool& tableNamePool,
        BlockHeader const& blockHeader, const evmc_message& message, const evmc_address& origin,
        int contextID, int64_t& seq)
      : evmc_host_context(),
        m_vmFactory(vmFactory),
        m_rollbackableStorage(storage),
        m_tableNamePool(tableNamePool),
        m_blockHeader(blockHeader),
        m_message(message),
        m_origin(origin),
        m_contextID(contextID),
        m_seq(seq),
        m_myContractTable(getMyContractTable(blockHeader, message)),
        m_codeTable(storage2::string_pool::makeStringID(m_tableNamePool, ledger::SYS_CODE_BINARY)),
        m_abiTable(storage2::string_pool::makeStringID(m_tableNamePool, ledger::SYS_CONTRACT_ABI))
    {
        interface = getHostInterface<HostContext>();
        wasm_interface = nullptr;

        hash_fn = evm_hash_fn;
        // version = m_executive->blockContext().blockVersion();
        isSMCrypto = false;
        if (GlobalHashImpl::g_hashImpl &&
            GlobalHashImpl::g_hashImpl->getHashImplType() == crypto::HashImplType::Sm3Hash)
        {
            isSMCrypto = true;
        }
    }
    ~HostContext() noexcept = default;

    HostContext(HostContext const&) = delete;
    HostContext& operator=(HostContext const&) = delete;
    HostContext(HostContext&&) = delete;
    HostContext& operator=(HostContext&&) = delete;

    task::Task<evmc_bytes32> get(const evmc_bytes32* key)
    {
        auto it =
            co_await m_rollbackableStorage.read(RANGES::single_view<StateKey>(RANGES::in_place,
                m_myContractTable, std::string_view((const char*)key->bytes, sizeof(key->bytes))));
        co_await it.next();

        evmc_bytes32 result;
        if (co_await it.hasValue())
        {
            auto&& entry = co_await it.value();
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
        std::string_view valueView((char*)value->bytes, sizeof(value->bytes));

        storage::Entry entry;
        entry.set(valueView);

        co_await m_rollbackableStorage.write(
            RANGES::single_view<StateKey>(RANGES::in_place, m_myContractTable,
                SmallKey{bytesConstRef(key->bytes, sizeof(key->bytes))}),
            RANGES::single_view<storage::Entry>(std::move(entry)));
    }

    task::Task<std::optional<storage::Entry>> code(const evmc_address& address)
    {
        if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            auto codeHashIt = co_await m_rollbackableStorage.read(RANGES::single_view<StateKey>(
                RANGES::in_place, getTableNameID(address), ACCOUNT_CODE_HASH));
            co_await codeHashIt.next();
            if (co_await codeHashIt.hasValue())
            {
                auto&& codeHashEntry = co_await codeHashIt.value();

                auto codeIt = co_await m_rollbackableStorage.read(
                    RANGES::single_view(StateKey{m_codeTable, codeHashEntry.get()}));

                co_await codeIt.next();
                if (co_await codeIt.hasValue())
                {
                    auto&& codeEntry = co_await codeIt.value();
                    co_return std::make_optional<storage::Entry>(codeEntry);
                }
            }
            co_return std::optional<storage::Entry>{};
        }

        // Old logic
        auto codeIt = co_await m_rollbackableStorage.read(
            RANGES::single_view(StateKey{getTableNameID(address), ACCOUNT_CODE}));
        co_await codeIt.next();
        if (co_await codeIt.hasValue())
        {
            auto&& codeEntry = co_await codeIt.value();
            co_return std::forward<decltype(codeEntry)>(codeEntry);
        }

        co_return std::optional<storage::Entry>{};
    }

    task::Task<void> setCode(const crypto::HashType& codeHash, bytesConstRef code)
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
                codeEntry.set(code.toBytes());
                co_await m_rollbackableStorage.write(RANGES::single_view<StateKey>(RANGES::in_place,
                                                         m_codeTable, codeHashEntry.get()),
                    RANGES::single_view(std::move(codeEntry)));
            }
            co_await m_rollbackableStorage.write(RANGES::single_view<StateKey>(RANGES::in_place,
                                                     m_myContractTable, ACCOUNT_CODE_HASH),
                RANGES::single_view(std::move(codeHashEntry)));

            co_return;
        }

        // old logic
        storage::Entry codeEntry;
        codeEntry.set(code.toBytes());

        co_await m_rollbackableStorage.write(
            RANGES::single_view(StateKey{m_myContractTable, ACCOUNT_CODE_HASH}),
            RANGES::single_view(codeHashEntry));
        co_await m_rollbackableStorage.write(
            RANGES::single_view(StateKey{m_myContractTable, ACCOUNT_CODE}),
            RANGES::single_view(codeEntry));
        co_return;
    }

    task::Task<void> setCode(bytesConstRef code)
    {
        co_await setCode(GlobalHashImpl::g_hashImpl->hash(code), code);
    }

    task::Task<void> setCodeAndABI(bytesConstRef code, std::string abi)
    {
        auto codeHash = GlobalHashImpl::g_hashImpl->hash(code);
        auto codeHashView = std::string_view((char*)codeHash.data(), codeHash.size());
        co_await setCode(codeHash, code);

        storage::Entry abiEntry;
        abiEntry.set(std::move(abi));
        if (blockVersion() >= uint32_t(bcos::protocol::BlockVersion::V3_1_VERSION))
        {
            auto abiIt = co_await m_rollbackableStorage.read(
                RANGES::single_view(StateKey{m_abiTable, codeHashView}));
            co_await abiIt.next();
            if (!co_await abiIt.hasValue())
            {
                co_await m_rollbackableStorage.write(m_abiTable,
                    RANGES::single_view(StateKey{m_abiTable, codeHashView}),
                    RANGES::single_view(abiEntry));
            }
            co_return;
        }
        co_await m_rollbackableStorage.write(
            RANGES::single_view(StateKey{m_myContractTable, ACCOUNT_ABI}),
            RANGES::single_view(std::move(abiEntry)));
    }

    task::Task<size_t> codeSizeAt(const evmc_address& address)
    {
        if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            // TODO: Check is precompiled
            auto codeEntry = co_await code(address);
            if (codeEntry)
            {
                co_return codeEntry->get().size();
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
            auto it = co_await m_rollbackableStorage.read(
                RANGES::single_view(StateKey{getTableNameID(address), ACCOUNT_CODE_HASH}));
            co_await it.next();
            if (co_await it.hasValue())
            {
                auto&& codeHashEntry = co_await it.value();
                auto view = codeHashEntry.get();
                h256 codeHash((const bcos::byte*)view.data(), view.size());
                co_return codeHash;
            }
        }
        co_return h256{};
    }

    task::Task<bool> exists([[maybe_unused]] const std::string_view& address)
    {
        // TODO: impl the full suport for solidity
        co_return true;
    }

    /// Return the EVM gas-price schedule for this execution context.
    VMSchedule const& vmSchedule() const { return DefaultSchedule; }

    /// Hash of a block if within the last 256 blocks, or h256() otherwise.
    task::Task<h256> blockHash(int64_t number) const
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unsupported method!"));
        // TODO: return the block hash in multilayer storage
        co_return h256{};
    }
    int64_t blockNumber() const { return m_blockHeader.number(); }
    uint32_t blockVersion() const { return m_blockHeader.version(); }
    uint64_t timestamp() const { return m_blockHeader.timestamp(); }
    evmc_address const& origin() const { return m_origin; }
    int64_t blockGasLimit() const
    {
        // TODO: add version check
        // if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        // {
        //     // FISCO BCOS only has tx Gas limit. We use it as block gas limit
        //     return m_executive->blockContext().txGasLimit();
        // }
        return 30000 * 10000;  // TODO: add config
    }

    /// Revert any changes made (by any of the other calls).
    void log(h256s topics, bytesConstRef data)
    {
        m_logs.emplace_back(bytes{}, std::move(topics), data.toBytes());
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
        // TODO:
        // 1: Check auth
        // 2: Check is precompiled

        if (m_message.kind == EVMC_CREATE || m_message.kind == EVMC_CREATE2)
        {
            co_return co_await create();
        }
        else
        {
            co_return co_await call();
        }
    }

    task::Task<evmc_result> create()
    {
        std::string_view createCode((const char*)m_message.input_data, m_message.input_size);
        auto createCodeHash = GlobalHashImpl::g_hashImpl->hash(createCode);
        auto mode = toRevision(vmSchedule());
        auto vmInstance = m_vmFactory.create(VMKind::evmone, createCodeHash, createCode, mode);

        auto savepoint = m_rollbackableStorage.current();
        auto result = vmInstance.execute(
            interface, this, mode, &m_message, m_message.input_data, m_message.input_size);
        if (result.status_code != 0)
        {
            co_await m_rollbackableStorage.rollback(savepoint);
        }
        else
        {
            co_await setCode(bytesConstRef(result.output_data, result.output_size));
            result.create_address = m_newContractAddress;
        }

        releaseResult(result);
        co_return result;
    }

    task::Task<evmc_result> call()
    {
        auto codeEntry = co_await code(m_message.code_address);
        if (!codeEntry || codeEntry->size() == 0)
        {
            BOOST_THROW_EXCEPTION(NotFoundCodeError{} << bcos::Error::ErrorMessage(
                                      std::string("Not found contract code: ")
                                          .append(toHexStringWithPrefix(*m_myContractTable))));
        }
        auto code = codeEntry->get();
        auto mode = toRevision(vmSchedule());

        if (blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION)
        {
            auto codeHash = co_await codeHashAt(m_message.code_address);
            auto vmInstance = m_vmFactory.create(VMKind::evmone, codeHash, code, mode);
            auto savepoint = m_rollbackableStorage.current();
            auto result = vmInstance.execute(
                interface, this, mode, &m_message, (const uint8_t*)code.data(), code.size());
            if (result.status_code != 0)
            {
                co_await m_rollbackableStorage.rollback(savepoint);
            }

            co_return result;
        }
        else
        {
            auto vmInstance = VMFactory::create();
            auto savepoint = m_rollbackableStorage.current();
            auto result = vmInstance.execute(
                interface, this, mode, &m_message, (const uint8_t*)code.data(), code.size());
            if (result.status_code != 0)
            {
                co_await m_rollbackableStorage.rollback(savepoint);
            }

            co_return result;
        }
    }

    task::Task<evmc_result> callBuiltInPrecompiled(
        const evmc_message* message, bool _isEvmPrecompiled)
    {
        // TODO: to be done
        BOOST_THROW_EXCEPTION(std::runtime_error("Unsupported method!"));
        co_return evmc_result{};
    }

    task::Task<evmc_result> externalCall(const evmc_message& message)
    {
        ++m_seq;
        HostContext hostcontext(m_vmFactory, m_rollbackableStorage, m_tableNamePool, m_blockHeader,
            message, m_origin, m_contextID, m_seq);

        auto result = co_await hostcontext.execute();
        if (result.status_code == EVMC_SUCCESS && !hostcontext.m_logs.empty())
        {
            auto& logs = hostcontext.logs();
            m_logs.reserve(m_logs.size() + RANGES::size(logs));
            RANGES::move(logs, std::back_inserter(m_logs));
        }

        co_return result;
    }

    const std::vector<protocol::LogEntry>& logs() const& { return m_logs; }
    std::vector<protocol::LogEntry>& logs() & { return m_logs; }
};

}  // namespace bcos::transaction_executor
