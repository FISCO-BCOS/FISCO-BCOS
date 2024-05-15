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

#include "../precompiled/AuthCheck.h"
#include "../precompiled/Precompiled.h"
#include "../precompiled/PrecompiledManager.h"
#include "EVMHostInterface.h"
#include "VMFactory.h"
#include "bcos-concepts/ByteBuffer.h"
#include "bcos-crypto/hasher/Hasher.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/Account.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-transaction-executor/vm/VMInstance.h"
#include "bcos-utilities/Common.h"
#include <bcos-task/Wait.h>
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <evmc/instructions.h>
#include <evmone/evmone.h>
#include <fmt/format.h>
#include <boost/multiprecision/cpp_int/import_export.hpp>
#include <boost/throw_exception.hpp>
#include <atomic>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>

namespace bcos::transaction_executor
{

#define HOST_CONTEXT_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("HOST_CONTEXT")

// clang-format off
struct NotFoundCodeError : public bcos::Error {};
// clang-format on

evmc_bytes32 evm_hash_fn(const uint8_t* data, size_t size);

template <class Storage>
class HostContext : public evmc_host_context
{
private:
    using Account = ledger::account::EVMAccount<Storage>;
    struct Executable
    {
        Executable(storage::Entry code, evmc_revision revision)
          : m_code(std::make_optional(std::move(code))),
            m_revision(revision),
            m_vmInstance(VMFactory::create(VMKind::evmone,
                bytesConstRef((const uint8_t*)m_code->data(), m_code->size()), m_revision))
        {}
        Executable(bytesConstRef code, evmc_revision mode)
          : m_vmInstance(VMFactory::create(VMKind::evmone, code, mode))
        {}

        std::optional<storage::Entry> m_code;
        evmc_revision m_revision;
        VMInstance m_vmInstance;
    };

    Storage& m_rollbackableStorage;
    Storage& m_rollbackableTransientStorage;
    protocol::BlockHeader const& m_blockHeader;
    const evmc_message& m_message;
    const evmc_address& m_origin;
    std::string_view m_abi;
    int m_contextID;
    int64_t& m_seq;
    PrecompiledManager const& m_precompiledManager;
    ledger::LedgerConfig const& m_ledgerConfig;
    crypto::Hash const& m_hashImpl;

    Account m_myAccount;
    evmc_address m_newContractAddress;  // Set by getMyContractTable, no need initialize value!
    evmc_revision m_mode;
    std::vector<protocol::LogEntry> m_logs;

    std::shared_ptr<Executable> m_executable;
    const bcos::transaction_executor::Precompiled* m_preparedPrecompiled{};

    auto buildLegacyExternalCaller()
    {
        return
            [this](const evmc_message& message) { return task::syncWait(externalCall(message)); };
    }

    auto getMyAccount(const protocol::BlockHeader& blockHeader, const evmc_message& message)
    {
        switch (message.kind)
        {
        case EVMC_CREATE:
        {
            if (concepts::bytebuffer::equalTo(
                    message.code_address.bytes, executor::EMPTY_EVM_ADDRESS.bytes))
            {
                auto address =
                    fmt::format(FMT_COMPILE("{}_{}_{}"), blockHeader.number(), m_contextID, m_seq);
                auto hash = m_hashImpl.hash(address);
                std::uninitialized_copy_n(
                    hash.data(), sizeof(m_newContractAddress.bytes), m_newContractAddress.bytes);
            }
            else
            {
                m_newContractAddress = message.code_address;
            }

            return Account(m_rollbackableStorage, m_newContractAddress);
        }
        case EVMC_CREATE2:
        {
            auto field1 = m_hashImpl.hash(bytes{0xff});
            auto field2 = bytesConstRef(message.sender.bytes, sizeof(message.sender.bytes));
            auto field3 = toBigEndian(fromEvmC(message.create2_salt));
            auto field4 = m_hashImpl.hash(bytesConstRef(message.input_data, message.input_size));
            auto hashView = RANGES::views::concat(field1, field2, field3, field4);

            std::uninitialized_copy_n(hashView.begin() + 12, sizeof(m_newContractAddress.bytes),
                m_newContractAddress.bytes);

            return Account(m_rollbackableStorage, m_newContractAddress);
        }
        default:
        {
            // CALL OR DELEGATECALL
            m_newContractAddress = {};
            return Account(m_rollbackableStorage, message.recipient);
        }
        }
    }
    constexpr static struct InnerConstructor
    {
    } innerConstructor{};

    HostContext(InnerConstructor /*unused*/, Storage& storage, Storage& transientStorage,
        protocol::BlockHeader const& blockHeader, const evmc_message& message,
        const evmc_address& origin, std::string_view abi, int contextID, int64_t& seq,
        PrecompiledManager const& precompiledManager, ledger::LedgerConfig const& ledgerConfig,
        crypto::Hash const& hashImpl, const evmc_host_interface* hostInterface)
      : evmc_host_context{.interface = hostInterface,
            .wasm_interface = nullptr,
            .hash_fn = evm_hash_fn,
            .isSMCrypto = (hashImpl.getHashImplType() == crypto::HashImplType::Sm3Hash),
            .version = 0,
            .metrics = std::addressof(executor::ethMetrics)},
        m_rollbackableStorage(storage),
        m_rollbackableTransientStorage(transientStorage),
        m_blockHeader(blockHeader),
        m_message(message),
        m_origin(origin),
        m_abi(abi),
        m_contextID(contextID),
        m_seq(seq),
        m_precompiledManager(precompiledManager),
        m_ledgerConfig(ledgerConfig),
        m_hashImpl(hashImpl),
        m_myAccount(getMyAccount(blockHeader, message))
    {
        if (m_ledgerConfig.features().get(ledger::Features::Flag::feature_evm_cancun))
        {
            m_mode = evmc_revision::EVMC_CANCUN;
        }
        else
        {
            m_mode = evmc_revision::EVMC_LONDON;
        }
    }

public:
    HostContext(Storage& storage, Storage& transientStorage,
        protocol::BlockHeader const& blockHeader, const evmc_message& message,
        const evmc_address& origin, std::string_view abi, int contextID, int64_t& seq,
        PrecompiledManager const& precompiledManager, ledger::LedgerConfig const& ledgerConfig,
        crypto::Hash const& hashImpl, auto&& waitOperator)
      : HostContext(innerConstructor, storage, transientStorage, blockHeader, message, origin, abi,
            contextID, seq, precompiledManager, ledgerConfig, hashImpl,
            getHostInterface<HostContext>(std::forward<decltype(waitOperator)>(waitOperator)))
    {}

    ~HostContext() noexcept = default;

    HostContext(HostContext const&) = delete;
    HostContext& operator=(HostContext const&) = delete;
    HostContext(HostContext&&) = delete;
    HostContext& operator=(HostContext&&) = delete;

    task::Task<evmc_bytes32> get(const evmc_bytes32* key)
    {
        co_return co_await ledger::account::storage(m_myAccount, *key);
    }

    task::Task<void> set(const evmc_bytes32* key, const evmc_bytes32* value)
    {
        co_await ledger::account::setStorage(m_myAccount, *key, *value);
    }

    task::Task<evmc_bytes32> getTransient(const evmc_bytes32* key)
    {
        auto valueEntry = co_await storage2::readOne(m_rollbackableTransientStorage,
            transaction_executor::StateKeyView{
                concepts::bytebuffer::toView(co_await ledger::account::path(m_myAccount)),
                concepts::bytebuffer::toView(key->bytes)});
        evmc_bytes32 value;
        if (valueEntry)
        {
            auto field = valueEntry->get();
            std::uninitialized_copy_n(field.data(), sizeof(value), value.bytes);
        }
        else
        {
            std::uninitialized_fill_n(value.bytes, sizeof(value), 0);
        }
        co_return value;
    }

    task::Task<void> setTransient(const evmc_bytes32* key, const evmc_bytes32* value)
    {
        storage::Entry valueEntry(concepts::bytebuffer::toView(value->bytes));
        StateKey stateKey =
            StateKey{concepts::bytebuffer::toView(co_await ledger::account::path(m_myAccount)),
                concepts::bytebuffer::toView(key->bytes)};
        co_await storage2::writeOne(
            m_rollbackableTransientStorage, stateKey, std::move(valueEntry));
    }

    task::Task<std::optional<storage::Entry>> code(const evmc_address& address)
    {
        auto executable = co_await getExecutable(m_rollbackableStorage, address);
        if (executable && executable->m_code)
        {
            co_return executable->m_code;
        }
        co_return std::optional<storage::Entry>{};
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
        Account account(m_rollbackableStorage, address);
        co_return co_await ledger::account::codeHash(account);
    }

    task::Task<bool> exists([[maybe_unused]] const evmc_address& address)
    {
        // TODO: impl the full suport for solidity
        co_return true;
    }

    /// Hash of a block if within the last 256 blocks, or h256() otherwise.
    task::Task<h256> blockHash(int64_t number) const
    {
        if (number >= blockNumber() || number < 0)
        {
            co_return h256{};
        }

        BOOST_THROW_EXCEPTION(std::runtime_error("Unsupported method!"));
        co_return h256{};
    }
    int64_t blockNumber() const { return m_blockHeader.number(); }
    uint32_t blockVersion() const { return m_blockHeader.version(); }
    int64_t timestamp() const { return m_blockHeader.timestamp(); }
    evmc_address const& origin() const { return m_origin; }
    int64_t blockGasLimit() const { return std::get<0>(m_ledgerConfig.gasLimit()); }

    /// Revert any changes made (by any of the other calls).
    void log(h256s topics, bytesConstRef data)
    {
        m_logs.emplace_back(bytes{}, std::move(topics), data.toBytes());
    }

    void suicide()
    {
        // suicide(m_myContractTable); // TODO: add suicide
    }

    task::Task<void> prepare()
    {
        if (m_message.kind == EVMC_CREATE || m_message.kind == EVMC_CREATE2)
        {
            prepareCreate();
        }
        else
        {
            co_await prepareCall();
        }
    }

    task::Task<EVMCResult> execute()
    {
        if (m_ledgerConfig.authCheckStatus() != 0U)
        {
            HOST_CONTEXT_LOG(DEBUG) << "Checking auth..." << m_ledgerConfig.authCheckStatus();
            auto [result, param] = checkAuth(m_rollbackableStorage, m_blockHeader, m_message,
                m_origin, buildLegacyExternalCaller(), m_precompiledManager);
            if (!result)
            {
                // FIXME: build EVMCResult and return
            }
        }

        if (m_message.kind == EVMC_CREATE || m_message.kind == EVMC_CREATE2)
        {
            co_return co_await executeCreate();
        }
        else
        {
            co_return co_await executeCall();
        }
    }

    task::Task<EVMCResult> externalCall(const evmc_message& message)
    {
        if (c_fileLogLevel <= LogLevel::TRACE)
        {
            HOST_CONTEXT_LOG(TRACE)
                << "External call, sender:" << address2HexString(message.sender);
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

        HostContext hostcontext(innerConstructor, m_rollbackableStorage,
            m_rollbackableTransientStorage, m_blockHeader, *messagePtr, m_origin, {}, m_contextID,
            m_seq, m_precompiledManager, m_ledgerConfig, m_hashImpl, interface);

        co_await hostcontext.prepare();
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

private:
    task::Task<std::shared_ptr<Executable>> getExecutable(
        Storage& storage, const evmc_address& address)
    {
        static storage2::memory_storage::MemoryStorage<evmc_address, std::shared_ptr<Executable>,
            storage2::memory_storage::Attribute(
                storage2::memory_storage::MRU | storage2::memory_storage::CONCURRENT),
            std::hash<evmc_address>>
            cachedExecutables;

        auto executable = co_await storage2::readOne(cachedExecutables, address);
        if (executable)
        {
            co_return std::move(*executable);
        }

        Account account(m_rollbackableStorage, address);
        auto codeEntry = co_await ledger::account::code(account);
        if (!codeEntry)
        {
            co_return std::shared_ptr<Executable>{};
        }

        executable.emplace(std::make_shared<Executable>(Executable(std::move(*codeEntry), m_mode)));
        co_await storage2::writeOne(cachedExecutables, address, *executable);
        co_return std::move(*executable);
    }

    void prepareCreate()
    {
        bytesConstRef createCode(m_message.input_data, m_message.input_size);
        m_executable = std::make_shared<Executable>(createCode, m_mode);
    }

    task::Task<EVMCResult> executeCreate()
    {
        auto savepoint = m_rollbackableStorage.current();
        auto transientSavepoint = m_rollbackableTransientStorage.current();
        if (m_ledgerConfig.authCheckStatus() != 0U)
        {
            createAuthTable(m_rollbackableStorage, m_blockHeader, m_message, m_origin,
                co_await ledger::account::path(m_myAccount), buildLegacyExternalCaller(),
                m_precompiledManager);
        }
        HOST_CONTEXT_LOG(TRACE) << "Executing create contract" << LOG_KV("EVM mode", m_mode);
        auto result = m_executable->m_vmInstance.execute(
            interface, this, m_mode, &m_message, m_message.input_data, m_message.input_size);
        if (result.status_code == 0)
        {
            auto code = bytesConstRef(result.output_data, result.output_size);
            auto codeHash = m_hashImpl.hash(code);

            co_await ledger::account::create(m_myAccount);
            co_await ledger::account::setCode(
                m_myAccount, code.toBytes(), std::string(m_abi), codeHash);
            auto schedule = bcos::executor::VMSchedule();
            result.gas_left -= result.output_size * schedule.createDataGas;
            result.create_address = m_newContractAddress;
        }
        else
        {
            co_await m_rollbackableStorage.rollback(savepoint);
            co_await m_rollbackableTransientStorage.rollback(transientSavepoint);
        }

        co_return result;
    }

    task::Task<void> prepareCall()
    {
        constexpr static unsigned long MAX_PRECOMPILED_ADDRESS = 100000;
        u160 address;
        boost::multiprecision::import_bits(address, m_message.code_address.bytes,
            m_message.code_address.bytes + sizeof(m_message.code_address.bytes));
        if (address > 0 && address < MAX_PRECOMPILED_ADDRESS)
        {
            auto addressUL = address.convert_to<unsigned long>();
            auto const* precompiled = m_precompiledManager.getPrecompiled(addressUL);

            if (precompiled != nullptr)
            {
                m_preparedPrecompiled = precompiled;
                co_return;
            }
        }
    }

    task::Task<EVMCResult> executeCall()
    {
        auto savepoint = m_rollbackableStorage.current();
        auto transientSavepoint = m_rollbackableTransientStorage.current();
        if (m_preparedPrecompiled != nullptr)
        {
            co_return transaction_executor::callPrecompiled(*m_preparedPrecompiled,
                m_rollbackableStorage, m_blockHeader, m_message, m_origin,
                buildLegacyExternalCaller(), m_precompiledManager);
        }

        if (!m_executable)
        {
            m_executable = co_await getExecutable(m_rollbackableStorage, m_message.code_address);
        }
        auto result = m_executable->m_vmInstance.execute(interface, this, m_mode, &m_message,
            (const uint8_t*)m_executable->m_code->data(), m_executable->m_code->size());
        if (result.status_code != 0)
        {
            HOST_CONTEXT_LOG(DEBUG) << "Execute transaction failed, status: " << result.status_code;
            co_await m_rollbackableStorage.rollback(savepoint);
            co_await m_rollbackableTransientStorage.rollback(transientSavepoint);
        }

        co_return result;
    }
};

}  // namespace bcos::transaction_executor
