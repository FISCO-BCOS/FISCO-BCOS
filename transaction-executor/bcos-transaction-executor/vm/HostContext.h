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
#include "../precompiled/PrecompiledImpl.h"
#include "../precompiled/PrecompiledManager.h"
#include "EVMHostInterface.h"
#include "VMFactory.h"
#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/ledger/Account.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-transaction-executor/EVMCResult.h"
#include "bcos-transaction-executor/vm/VMInstance.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-task/Wait.h>
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <evmc/instructions.h>
#include <evmone/evmone.h>
#include <boost/algorithm/hex.hpp>
#include <boost/multiprecision/cpp_int/import_export.hpp>
#include <boost/throw_exception.hpp>
#include <functional>
#include <intx/intx.hpp>
#include <iterator>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace bcos::transaction_executor
{

#define HOST_CONTEXT_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("HOST_CONTEXT")

// clang-format off
struct NotFoundCodeError : public bcos::Error {};
// clang-format on

evmc_bytes32 evm_hash_fn(const uint8_t* data, size_t size);
executor::VMSchedule const& vmSchedule();
static const auto mode = toRevision(vmSchedule());

std::variant<const evmc_message*, evmc_message> getMessage(const evmc_message& inputMessage,
    protocol::BlockNumber blockNumber, int64_t contextID, int64_t seq,
    crypto::Hash const& hashImpl);

struct Executable
{
    explicit Executable(storage::Entry code)
      : m_code(std::make_optional(std::move(code))),
        m_vmInstance(VMFactory::create(VMKind::evmone,
            bytesConstRef(reinterpret_cast<const uint8_t*>(m_code->data()), m_code->size()), mode))
    {}
    explicit Executable(bytesConstRef code)
      : m_vmInstance(VMFactory::create(VMKind::evmone, code, mode))
    {}

    std::optional<storage::Entry> m_code;
    VMInstance m_vmInstance;
};

template <class Storage>
using Account = ledger::account::EVMAccount<Storage>;

inline task::Task<std::shared_ptr<Executable>> getExecutable(
    auto& storage, const evmc_address& address)
{
    static storage2::memory_storage::MemoryStorage<evmc_address, std::shared_ptr<Executable>,
        storage2::memory_storage::Attribute(
            storage2::memory_storage::LRU | storage2::memory_storage::CONCURRENT),
        std::hash<evmc_address>>
        cachedExecutables;

    if (auto executable = co_await storage2::readOne(cachedExecutables, address))
    {
        co_return std::move(*executable);
    }

    Account<std::decay_t<decltype(storage)>> account(storage, address);
    if (auto codeEntry = co_await ledger::account::code(account))
    {
        auto executable = std::make_shared<Executable>(Executable(std::move(*codeEntry)));
        co_await storage2::writeOne(cachedExecutables, address, executable);
        co_return executable;
    }
    co_return std::shared_ptr<Executable>{};
}

template <class Storage>
class HostContext : public evmc_host_context
{
private:
    Storage& m_rollbackableStorage;
    protocol::BlockHeader const& m_blockHeader;
    const evmc_address& m_origin;
    std::string_view m_abi;
    int m_contextID;
    int64_t& m_seq;
    PrecompiledManager const& m_precompiledManager;
    ledger::LedgerConfig const& m_ledgerConfig;
    crypto::Hash const& m_hashImpl;
    std::variant<const evmc_message*, evmc_message> m_message;
    Account<Storage> m_myAccount;

    std::vector<protocol::LogEntry> m_logs;
    std::shared_ptr<Executable> m_executable;
    const bcos::transaction_executor::Precompiled* m_preparedPrecompiled{};
    std::optional<bcos::bytes> m_dynamicPrecompiledInput;

    auto buildLegacyExternalCaller()
    {
        return
            [this](const evmc_message& message) { return task::syncWait(externalCall(message)); };
    }

    evmc_message const& message() const&
    {
        return std::visit(
            bcos::overloaded{
                [](const evmc_message* message) -> evmc_message const& { return *message; },
                [](const evmc_message& message) -> evmc_message const& { return message; }},
            m_message);
    }

    auto getMyAccount()
    {
        return Account<std::decay_t<Storage>>(m_rollbackableStorage, message().recipient);
    }

    inline constexpr static struct InnerConstructor
    {
    } innerConstructor{};

    HostContext(InnerConstructor /*unused*/, Storage& storage,
        const protocol::BlockHeader& blockHeader, const evmc_message& message,
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
        m_blockHeader(blockHeader),
        m_origin(origin),
        m_abi(abi),
        m_contextID(contextID),
        m_seq(seq),
        m_precompiledManager(precompiledManager),
        m_ledgerConfig(ledgerConfig),
        m_hashImpl(hashImpl),
        m_message(getMessage(message, m_blockHeader.number(), m_contextID, m_seq, m_hashImpl)),
        m_myAccount(getMyAccount())
    {}

public:
    HostContext(Storage& storage, protocol::BlockHeader const& blockHeader,
        const evmc_message& message, const evmc_address& origin, std::string_view abi,
        int contextID, int64_t& seq, PrecompiledManager const& precompiledManager,
        ledger::LedgerConfig const& ledgerConfig, crypto::Hash const& hashImpl, auto&& waitOperator)
      : HostContext(innerConstructor, storage, blockHeader, message, origin, abi, contextID, seq,
            precompiledManager, ledgerConfig, hashImpl,
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

    task::Task<std::optional<storage::Entry>> code(const evmc_address& address)
    {
        if (auto executable = co_await getExecutable(m_rollbackableStorage, address);
            executable && executable->m_code)
        {
            co_return executable->m_code;
        }
        co_return std::optional<storage::Entry>{};
    }

    task::Task<size_t> codeSizeAt(const evmc_address& address)
    {
        if (auto const* precompiled = m_precompiledManager.getPrecompiled(address))
        {
            co_return transaction_executor::size(*precompiled);
        }

        if (auto codeEntry = co_await code(address))
        {
            co_return codeEntry->get().size();
        }
        co_return 0;
    }

    task::Task<h256> codeHashAt(const evmc_address& address)
    {
        Account<Storage> account(m_rollbackableStorage, address);
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
    void log(const evmc_address& address, h256s topics, bytesConstRef data)
    {
        std::span<const uint8_t> view(address.bytes);
        m_logs.emplace_back(
            toHex<decltype(view), bcos::bytes>(view), std::move(topics), data.toBytes());
    }

    void suicide()
    {
        // suicide(m_myContractTable); // TODO: add suicide
    }

    task::Task<void> prepare()
    {
        auto const& ref = message();
        assert(!concepts::bytebuffer::equalTo(
            message().code_address.bytes, executor::EMPTY_EVM_ADDRESS.bytes));
        assert(!concepts::bytebuffer::equalTo(
            message().recipient.bytes, executor::EMPTY_EVM_ADDRESS.bytes));
        if (message().kind == EVMC_CREATE || message().kind == EVMC_CREATE2)
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
        auto const& ref = message();
        if (c_fileLogLevel <= LogLevel::TRACE) [[unlikely]]
        {
            HOST_CONTEXT_LOG(TRACE)
                << "HostContext execute, kind: " << ref.kind << " seq:" << m_seq
                << " sender:" << address2HexString(ref.sender)
                << " recipient:" << address2HexString(ref.recipient) << " gas:" << ref.gas;
        }

        auto savepoint = m_rollbackableStorage.current();
        std::optional<EVMCResult> evmResult;
        if (m_ledgerConfig.authCheckStatus() != 0U)
        {
            HOST_CONTEXT_LOG(DEBUG)
                << "Checking auth..." << m_ledgerConfig.authCheckStatus() << " gas: " << ref.gas;

            if (auto result = checkAuth(m_rollbackableStorage, m_blockHeader, ref, m_origin,
                    buildLegacyExternalCaller(), m_precompiledManager, m_contextID, m_seq,
                    m_hashImpl))
            {
                HOST_CONTEXT_LOG(DEBUG) << "Auth check failed";
                evmResult = std::move(result);
            };
        }

        if (!evmResult)
        {
            if (ref.kind == EVMC_CREATE || ref.kind == EVMC_CREATE2)
            {
                evmResult.emplace(co_await executeCreate());
            }
            else
            {
                evmResult.emplace(co_await executeCall());
            }
        }

        // 如果本次调用系统合约失败，不消耗gas
        // If the call to system contract failed, the gasUsed is cleared to zero
        if (evmResult->status_code != EVMC_SUCCESS)
        {
            co_await m_rollbackableStorage.rollback(savepoint);

            if (auto hexAddress = address2FixedArray(ref.code_address);
                bcos::precompiled::c_systemTxsAddress.find(concepts::bytebuffer::toView(
                    hexAddress)) != bcos::precompiled::c_systemTxsAddress.end())
            {
                evmResult->gas_left = message().gas;
                HOST_CONTEXT_LOG(TRACE) << "System contract call failed, clear gasUsed, gas_left: "
                                        << evmResult->gas_left;
            }
        }

        // 如果本次调用的sender或recipient是系统合约，不消耗gas
        // If the sender or recipient of this call is a system contract, gas is not consumed
        auto senderAddress = address2FixedArray(ref.sender);
        auto recipientAddress = address2FixedArray(ref.recipient);
        if (bcos::precompiled::c_systemTxsAddress.contains(
                concepts::bytebuffer::toView(senderAddress)) ||
            bcos::precompiled::c_systemTxsAddress.contains(
                concepts::bytebuffer::toView(recipientAddress)))
        {
            evmResult->gas_left = ref.gas;
            HOST_CONTEXT_LOG(TRACE)
                << "System contract sender call, clear gasUsed, gas_left: " << evmResult->gas_left;
        }

        if (c_fileLogLevel <= LogLevel::TRACE) [[unlikely]]
        {
            HOST_CONTEXT_LOG(TRACE)
                << "HostContext execute finished, kind: " << ref.kind
                << " status: " << evmResult->status_code << " gas: " << evmResult->gas_left
                << " output: " << bytesConstRef(evmResult->output_data, evmResult->output_size);
        }
        co_return std::move(*evmResult);
    }

    task::Task<EVMCResult> externalCall(const evmc_message& message)
    {
        ++m_seq;
        if (c_fileLogLevel <= LogLevel::TRACE) [[unlikely]]
        {
            HOST_CONTEXT_LOG(TRACE)
                << "External call, kind: " << message.kind << " seq:" << m_seq
                << " sender:" << address2HexString(message.sender)
                << " recipient:" << address2HexString(message.recipient) << " gas:" << message.gas;
        }

        HostContext hostcontext(innerConstructor, m_rollbackableStorage, m_blockHeader, message,
            m_origin, {}, m_contextID, m_seq, m_precompiledManager, m_ledgerConfig, m_hashImpl,
            interface);

        try
        {
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
        catch (NotFoundCodeError& e)
        {
            // Static call或delegate call时，合约不存在要返回EVMC_SUCCESS
            // STATIC_CALL or DELEGATE_CALL, the EVMC_SUCCESS is returned when the contract does not
            // exist
            co_return EVMCResult{evmc_result{
                .status_code =
                    ((message.flags == EVMC_STATIC || message.kind == EVMC_DELEGATECALL) ?
                            EVMC_SUCCESS :
                            EVMC_REVERT),
                .gas_left = message.gas,
                .gas_refund = 0,
                .output_data = nullptr,
                .output_size = 0,
                .release = nullptr,
                .create_address = {},
                .padding = {}}};
        }
    }

    std::vector<protocol::LogEntry>& logs() & { return m_logs; }

private:
    void prepareCreate()
    {
        bytesConstRef createCode(message().input_data, message().input_size);
        m_executable = std::make_shared<Executable>(createCode);
    }

    task::Task<EVMCResult> executeCreate()
    {
        if (m_blockHeader.number() != 0)
        {
            createAuthTable(m_rollbackableStorage, m_blockHeader, message(), m_origin,
                co_await ledger::account::path(m_myAccount), buildLegacyExternalCaller(),
                m_precompiledManager, m_contextID, m_seq);
        }

        auto& ref = message();
        co_await ledger::account::create(m_myAccount);
        auto result = m_executable->m_vmInstance.execute(
            interface, this, mode, std::addressof(ref), message().input_data, message().input_size);
        if (result.status_code == 0)
        {
            auto code = bytesConstRef(result.output_data, result.output_size);
            auto codeHash = m_hashImpl.hash(code);
            co_await ledger::account::setCode(
                m_myAccount, code.toBytes(), std::string(m_abi), codeHash);

            result.gas_left -= result.output_size * vmSchedule().createDataGas;
            result.create_address = message().code_address;

            // Clear the output
            if (result.release)
            {
                result.release(std::addressof(result));
                result.release = nullptr;
            }
            result.output_data = nullptr;
            result.output_size = 0;
        }

        co_return result;
    }

    task::Task<void> prepareCall()
    {
        // 不允许delegatecall static precompiled
        // delegatecall static precompiled is not allowed
        if (message().kind != EVMC_DELEGATECALL)
        {
            if (auto const* precompiled =
                    m_precompiledManager.getPrecompiled(message().code_address))
            {
                if (auto flag = transaction_executor::featureFlag(*precompiled);
                    !flag || m_ledgerConfig.features().get(*flag))
                {
                    m_preparedPrecompiled = precompiled;
                    co_return;
                }
            }
        }

        m_executable = co_await getExecutable(m_rollbackableStorage, message().code_address);
        if (m_executable && hasPrecompiledPrefix(m_executable->m_code->data()))
        {
            if (std::holds_alternative<const evmc_message*>(m_message))
            {
                m_message.emplace<evmc_message>(*std::get<const evmc_message*>(m_message));
            }

            auto& message = std::get<evmc_message>(m_message);
            const auto* code = m_executable->m_code->data();

            std::vector<std::string> codeParameters{};
            boost::split(codeParameters, code, boost::is_any_of(","));
            if (codeParameters.size() < 3)
            {
                BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "CallDynamicPrecompiled error code field."));
            }
            message.code_address = message.recipient;
            // precompiled的地址，是不是写到code_address里更合理？考虑delegate call
            // Is it more reasonable to write the address of precompiled in the code_address?
            // Consider Delegate Call
            message.recipient = unhexAddress(codeParameters[1]);
            codeParameters.erase(codeParameters.begin(), codeParameters.begin() + 2);

            codec::abi::ContractABICodec codec(m_hashImpl);
            m_dynamicPrecompiledInput.emplace(codec.abiIn(
                "", codeParameters, bcos::bytesConstRef(message.input_data, message.input_size)));

            message.input_data = m_dynamicPrecompiledInput->data();
            message.input_size = m_dynamicPrecompiledInput->size();
            if (c_fileLogLevel <= LogLevel::TRACE) [[unlikely]]
            {
                HOST_CONTEXT_LOG(TRACE)
                    << LOG_DESC("callDynamicPrecompiled")
                    << LOG_KV("codeAddr", address2HexString(message.code_address))
                    << LOG_KV("recvAddr", address2HexString(message.recipient))
                    << LOG_KV("code", code);
            }

            if (auto const* precompiled = m_precompiledManager.getPrecompiled(message.recipient))
            {
                m_preparedPrecompiled = precompiled;
            }
            else
            {
                BOOST_THROW_EXCEPTION(NotFoundCodeError());
            }
            co_return;
        }
    }

    task::Task<EVMCResult> executeCall()
    {
        auto& ref = message();
        if (m_preparedPrecompiled != nullptr)
        {
            co_return transaction_executor::callPrecompiled(*m_preparedPrecompiled,
                m_rollbackableStorage, m_blockHeader, ref, m_origin, buildLegacyExternalCaller(),
                m_precompiledManager, m_contextID, m_seq, m_ledgerConfig.authCheckStatus());
        }
        else
        {
            if (!m_executable)
            {
                m_executable = co_await getExecutable(m_rollbackableStorage, ref.code_address);
            }

            if (!m_executable)
            {
                BOOST_THROW_EXCEPTION(NotFoundCodeError());
            }
            co_return m_executable->m_vmInstance.execute(interface, this, mode, std::addressof(ref),
                (const uint8_t*)m_executable->m_code->data(), m_executable->m_code->size());
        }
    }
};

}  // namespace bcos::transaction_executor
