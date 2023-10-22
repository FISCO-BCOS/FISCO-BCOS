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

#include "../precompiled/PrecompiledManager.h"
#include "EVMHostInterface.h"
#include "VMFactory.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-utilities/Common.h"
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
    return toEvmC(executor::GlobalHashImpl::g_hashImpl->hash(bytesConstRef(data, size)));
}

template <class Storage, class PrecompiledManager>
class HostContext : public evmc_host_context
{
private:
    VMFactory& m_vmFactory;
    Storage& m_rollbackableStorage;
    protocol::BlockHeader const& m_blockHeader;
    const evmc_message& m_message;
    const evmc_address& m_origin;
    std::string_view m_abi;
    int m_contextID;
    int64_t& m_seq;
    PrecompiledManager const& m_precompiledManager;

    ContractTable m_myContractTable;
    evmc_address m_newContractAddress;  // Set by getMyContractTable, not need initialize value!
    std::vector<protocol::LogEntry> m_logs;

    ContractTable getTableName(const evmc_address& address)
    {
        ContractTable tableName;
        tableName.reserve(executor::USER_APPS_PREFIX.size() + sizeof(address) * 2);
        tableName.insert(
            tableName.end(), executor::USER_APPS_PREFIX.begin(), executor::USER_APPS_PREFIX.end());
        boost::algorithm::hex_lower((const char*)address.bytes,
            (const char*)address.bytes + sizeof(address), std::back_inserter(tableName));

        return tableName;
    }

    ContractTable getMyContractTable(
        const protocol::BlockHeader& blockHeader, const evmc_message& message)
    {
        switch (message.kind)
        {
        case EVMC_CREATE:
        {
            auto address = fmt::format("{}_{}_{}", blockHeader.number(), m_contextID, m_seq);
            auto hash = executor::GlobalHashImpl::g_hashImpl->hash(address);
            std::uninitialized_copy_n(
                hash.data(), sizeof(m_newContractAddress.bytes), m_newContractAddress.bytes);

            return getTableName(m_newContractAddress);
        }
        case EVMC_CREATE2:
        {
            BOOST_THROW_EXCEPTION(std::runtime_error("Unimplementation!"));
            break;
        }
        default:
        {
            // CALL OR DELEGATECALL
            m_newContractAddress = {};
            return getTableName(message.recipient);
        }
        }
    }

public:
    HostContext(VMFactory& vmFactory, Storage& storage, protocol::BlockHeader const& blockHeader,
        const evmc_message& message, const evmc_address& origin, std::string_view abi,
        int contextID, int64_t& seq, PrecompiledManager const& precompiledManager)
      : evmc_host_context{.interface = getHostInterface<HostContext>(),
            .wasm_interface = nullptr,
            .hash_fn = evm_hash_fn,
            .isSMCrypto = (executor::GlobalHashImpl::g_hashImpl->getHashImplType() ==
                           crypto::HashImplType::Sm3Hash),
            .version = 0,
            .metrics = metrics},
        m_vmFactory(vmFactory),
        m_rollbackableStorage(storage),
        m_blockHeader(blockHeader),
        m_message(message),
        m_origin(origin),
        m_abi(abi),
        m_contextID(contextID),
        m_seq(seq),
        m_precompiledManager(precompiledManager),
        m_myContractTable(getMyContractTable(blockHeader, message))
    {}
    ~HostContext() noexcept = default;

    HostContext(HostContext const&) = delete;
    HostContext& operator=(HostContext const&) = delete;
    HostContext(HostContext&&) = delete;
    HostContext& operator=(HostContext&&) = delete;

    task::Task<evmc_bytes32> get(const evmc_bytes32* key)
    {
        evmc_bytes32 result;
        auto valueEntry = co_await storage2::readOne(m_rollbackableStorage,
            StateKeyView{
                m_myContractTable, std::string_view((const char*)key->bytes, sizeof(key->bytes))});
        if (valueEntry)
        {
            auto field = valueEntry->getField(0);
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

        co_await storage2::writeOne(m_rollbackableStorage,
            StateKey{m_myContractTable, ContractKey{bytesConstRef(key->bytes, sizeof(key->bytes))}},
            std::move(entry));
    }

    task::Task<std::optional<storage::Entry>> code(const evmc_address& address)
    {
        // Need block version >= 3.1
        auto tableName = getTableName(address);
        auto codeHashEntry = co_await storage2::readOne(
            m_rollbackableStorage, StateKeyView{tableName, executor::ACCOUNT_CODE_HASH});
        if (codeHashEntry)
        {
            auto codeEntry = co_await storage2::readOne(
                m_rollbackableStorage, StateKeyView{ledger::SYS_CODE_BINARY, codeHashEntry->get()});
            if (codeEntry)
            {
                co_return codeEntry;
            }
        }
        co_return std::optional<storage::Entry>{};
    }

    task::Task<void> setCode(const crypto::HashType& codeHash, bytesConstRef code)
    {
        storage::Entry codeHashEntry;
        codeHashEntry.set(std::string_view((const char*)codeHash.data(), codeHash.size()));

        // Need block version >= 3.1
        // Query the code table first
        if (!co_await storage2::existsOne(
                m_rollbackableStorage, StateKeyView{ledger::SYS_CODE_BINARY, codeHashEntry.get()}))
        {
            storage::Entry codeEntry;
            codeEntry.set(code.toBytes());
            co_await storage2::writeOne(m_rollbackableStorage,
                StateKey{ledger::SYS_CODE_BINARY, codeHashEntry.get()}, std::move(codeEntry));
        }
        co_await storage2::writeOne(m_rollbackableStorage,
            StateKey{m_myContractTable, executor::ACCOUNT_CODE_HASH}, std::move(codeHashEntry));
    }

    task::Task<void> setCode(bytesConstRef code)
    {
        co_await setCode(executor::GlobalHashImpl::g_hashImpl->hash(code), code);
    }

    task::Task<void> setCodeAndABI(bytesConstRef code, std::string abi)
    {
        auto codeHash = executor::GlobalHashImpl::g_hashImpl->hash(code);
        auto codeHashView = std::string_view((char*)codeHash.data(), codeHash.size());
        co_await setCode(codeHash, code);

        if (!abi.empty())
        {
            storage::Entry abiEntry;
            abiEntry.set(std::move(abi));

            if (!co_await storage2::existsOne(
                    m_rollbackableStorage, StateKeyView{ledger::SYS_CONTRACT_ABI, codeHashView}))
            {
                co_await storage2::writeOne(m_rollbackableStorage,
                    StateKey{ledger::SYS_CONTRACT_ABI, codeHashView}, std::move(abiEntry));
            }
        }
        co_return;
    }

    task::Task<size_t> codeSizeAt(const evmc_address& address)
    {
        // TODO: Check is precompiled
        auto codeEntry = co_await code(address);
        if (codeEntry)
        {
            co_return codeEntry->get().size();
        }
        co_return 0;
    }

    task::Task<h256> codeHashAt(const evmc_address& address)
    {
        auto tableName = getTableName(address);
        auto codeHashEntry = co_await storage2::readOne(
            m_rollbackableStorage, StateKeyView{tableName, executor::ACCOUNT_CODE_HASH});
        if (codeHashEntry)
        {
            auto view = codeHashEntry->get();
            h256 codeHash((const bcos::byte*)view.data(), view.size());
            co_return codeHash;
        }
        co_return h256{};
    }

    task::Task<bool> exists([[maybe_unused]] const std::string_view& address)
    {
        // TODO: impl the full suport for solidity
        co_return true;
    }

    /// Return the EVM gas-price schedule for this execution context.
    executor::VMSchedule const& vmSchedule() const { return executor::FiscoBcosScheduleV320; }

    /// Hash of a block if within the last 256 blocks, or h256() otherwise.
    task::Task<h256> blockHash(int64_t number) const
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unsupported method!"));
        // TODO: return the block hash in multilayer storage
        co_return h256{};
    }
    int64_t blockNumber() const { return m_blockHeader.number(); }
    uint32_t blockVersion() const { return m_blockHeader.version(); }
    int64_t timestamp() const { return m_blockHeader.timestamp(); }
    evmc_address const& origin() const { return m_origin; }
    int64_t blockGasLimit() const
    {
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

    task::Task<EVMCResult> execute()
    {
        // TODO:
        // 1: Check auth

        if (m_message.kind == EVMC_CREATE || m_message.kind == EVMC_CREATE2)
        {
            co_return co_await create();
        }
        else
        {
            co_return co_await call();
        }
    }

    task::Task<EVMCResult> create()
    {
        // Write table to s_tables
        constexpr std::string_view SYS_TABLES{"s_tables"};
        if (co_await storage2::existsOne(
                m_rollbackableStorage, StateKey{SYS_TABLES, m_myContractTable}))
        {
            // Table exists
        }
        storage::Entry tableEntry;
        tableEntry.setField(0, "value");
        co_await storage2::writeOne(
            m_rollbackableStorage, StateKey{SYS_TABLES, m_myContractTable}, std::move(tableEntry));

        std::string_view createCode((const char*)m_message.input_data, m_message.input_size);
        auto createCodeHash = executor::GlobalHashImpl::g_hashImpl->hash(createCode);
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
            co_await setCodeAndABI(
                bytesConstRef(result.output_data, result.output_size), std::string(m_abi));
            result.gas_left -= result.output_size * vmSchedule().createDataGas;
            result.create_address = m_newContractAddress;
        }

        co_return result;
    }

    task::Task<EVMCResult> call()
    {
        constexpr static unsigned long MAX_PRECOMPILED_ADDRESS = 100000;
        auto address = fromBigEndian<u160>(bcos::bytesConstRef(
            m_message.code_address.bytes, sizeof(m_message.code_address.bytes)));
        if (address > 0 && address < MAX_PRECOMPILED_ADDRESS)
        {
            auto addressUL = address.convert_to<unsigned long>();
            auto const* precompiled = m_precompiledManager.getPrecompiled(addressUL);

            if (precompiled)
            {
                co_return precompiled->call(m_rollbackableStorage, m_blockHeader, m_message,
                    m_origin, [this](const evmc_message& message) {
                        return task::syncWait(externalCall(message));
                    });
            }
        }

        auto codeEntry = co_await code(m_message.code_address);
        if (!codeEntry || codeEntry->size() == 0)
        {
            BOOST_THROW_EXCEPTION(NotFoundCodeError{} << bcos::Error::ErrorMessage(
                                      std::string("Not found contract code: ")
                                          .append(toHexStringWithPrefix(
                                              static_cast<std::string_view>(m_myContractTable)))));
        }
        auto code = codeEntry->get();
        auto mode = toRevision(vmSchedule());

        auto codeHash = co_await codeHashAt(m_message.code_address);
        auto vmInstance = m_vmFactory.create(VMKind::evmone, codeHash, code, mode);
        auto savepoint = m_rollbackableStorage.current();
        auto result = vmInstance.execute(
            interface, this, mode, &m_message, (const uint8_t*)code.data(), code.size());
        if (result.status_code != 0)
        {
            HOST_CONTEXT_LOG(DEBUG) << "Execute transaction failed, status: " << result.status_code;
            co_await m_rollbackableStorage.rollback(savepoint);
        }

        co_return result;
    }

    task::Task<EVMCResult> externalCall(const evmc_message& message)
    {
        if (c_fileLogLevel <= LogLevel::TRACE)
        {
            HOST_CONTEXT_LOG(TRACE)
                << "External call, sender:"
                << toHex(bytesConstRef(message.sender.bytes, sizeof(message.sender.bytes)));
        }
        ++m_seq;

        const auto* messagePtr = std::addressof(message);
        std::optional<evmc_message> messageWithSender;
        if (message.kind == EVMC_CREATE &&
            RANGES::equal(message.sender.bytes, executor::EMPTY_EVM_ADDRESS.bytes))
        {
            messageWithSender.emplace(message);
            messageWithSender->sender = m_newContractAddress;
            messagePtr = std::addressof(*messageWithSender);
        }

        HostContext hostcontext(m_vmFactory, m_rollbackableStorage, m_blockHeader, *messagePtr,
            m_origin, {}, m_contextID, m_seq, m_precompiledManager);

        auto result = co_await hostcontext.execute();
        auto& logs = hostcontext.logs();
        if (result.status_code == EVMC_SUCCESS && !logs.empty())
        {
            m_logs.reserve(m_logs.size() + RANGES::size(logs));
            RANGES::move(logs, std::back_inserter(m_logs));
        }

        co_return result;
    }

    std::vector<protocol::LogEntry>& logs() & { return m_logs; }
};

}  // namespace bcos::transaction_executor
