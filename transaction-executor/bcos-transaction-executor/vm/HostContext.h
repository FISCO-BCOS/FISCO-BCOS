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
#include "VMInstance.h"
#include "bcos-codec/abi/ContractABICodec.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/Account.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Exceptions.h"
#include "bcos-framework/protocol/LogEntry.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-transaction-executor/EVMCResult.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-task/Wait.h>
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <evmc/instructions.h>
#include <evmone/evmone.h>
#include <boost/algorithm/hex.hpp>
#include <boost/concept_archetype.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/multiprecision/cpp_int/import_export.hpp>
#include <boost/throw_exception.hpp>
#include <functional>
#include <intx/intx.hpp>
#include <iterator>
#include <magic_enum.hpp>
#include <memory>
#include <string_view>

namespace bcos::executor_v1::hostcontext
{

#define HOST_CONTEXT_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("HOST_CONTEXT")

// clang-format off
struct NotFoundCodeError : public bcos::Error {};
// clang-format on

evmc_bytes32 evm_hash_fn(const uint8_t* data, size_t size);

evmc_message getMessage(bool web3Tx, const evmc_message& inputMessage,
    protocol::BlockNumber blockNumber, int64_t contextID, int64_t seq, const u256& nonce,
    crypto::Hash const& hashImpl);

struct Executable
{
    Executable(storage::Entry code, evmc_revision revision);
    Executable(bytesConstRef code, evmc_revision revision);

    std::optional<storage::Entry> m_code;
    VMInstance m_vmInstance;
};

template <class Storage>
using Account = ledger::account::EVMAccount<Storage>;

using CacheExecutables =
    storage2::memory_storage::MemoryStorage<evmc_address, std::shared_ptr<Executable>,
        storage2::memory_storage::Attribute(
            storage2::memory_storage::LRU | storage2::memory_storage::CONCURRENT),
        std::hash<evmc_address>>;
CacheExecutables& getCacheExecutables();

task::Task<std::shared_ptr<Executable>> getExecutable(
    auto& storage, const evmc_address& address, const evmc_revision& revision, bool binaryAddress)
{
    if (auto executable = co_await storage2::readOne(getCacheExecutables(), address))
    {
        co_return std::move(*executable);
    }

    if (Account<std::decay_t<decltype(storage)>> account(storage, address, binaryAddress);
        auto codeEntry = co_await ledger::account::code(account))
    {
        auto executable = std::make_shared<Executable>(std::move(*codeEntry), revision);
        co_await storage2::writeOne(getCacheExecutables(), address, executable);
        co_return executable;
    }
    co_return {};
}

template <class Storage, class TransientStorage>
class HostContext : public evmc_host_context
{
private:
    std::reference_wrapper<Storage> m_rollbackableStorage;
    std::reference_wrapper<TransientStorage> m_rollbackableTransientStorage;
    std::reference_wrapper<const protocol::BlockHeader> m_blockHeader;
    std::reference_wrapper<const evmc_address> m_origin;
    std::string_view m_abi;
    int m_contextID;
    std::reference_wrapper<int64_t> m_seq;
    std::reference_wrapper<const PrecompiledManager> m_precompiledManager;
    std::reference_wrapper<const ledger::LedgerConfig> m_ledgerConfig;
    std::reference_wrapper<const crypto::Hash> m_hashImpl;
    evmc_message m_message;
    Account<Storage> m_recipientAccount;

    evmc_revision m_revision;
    std::vector<protocol::LogEntry> m_logs;
    std::shared_ptr<Executable> m_executable;
    const bcos::executor_v1::Precompiled* m_preparedPrecompiled{};
    bcos::bytes m_dynamicPrecompiledInput;
    bool m_enableTransfer = false;
    int64_t m_level;
    bool m_web3Tx;

    constexpr auto buildLegacyExternalCaller()
    {
        return
            [this](const evmc_message& message) { return task::syncWait(externalCall(message)); };
    }

    task::Task<void> transferBalance(const evmc_message& message)
    {
        if (message.code_address == message.sender || message.recipient == message.sender)
        {
            co_return;
        }

        auto value = fromEvmC(message.value);
        auto senderAccount = getAccount(*this, message.sender);
        auto fromBalance = co_await ledger::account::balance(senderAccount);

        if (fromBalance < value)
        {
            HOST_CONTEXT_LOG(DEBUG) << m_blockHeader.get().number() << " "
                                    << LOG_BADGE("AccountPrecompiled, subAccountBalance")
                                    << LOG_DESC("account balance not enough");
            BOOST_THROW_EXCEPTION(protocol::NotEnoughCashError("Account balance is not enough!"));
        }

        if (!co_await ledger::account::exists(m_recipientAccount))
        {
            co_await ledger::account::create(m_recipientAccount);
        }

        auto toBalance = co_await ledger::account::balance(m_recipientAccount);
        co_await ledger::account::setBalance(senderAccount, fromBalance - value);
        co_await ledger::account::setBalance(m_recipientAccount, toBalance + value);
    }

    constexpr static struct InnerConstructor
    {
    } innerConstructor{};

    HostContext(InnerConstructor /*unused*/, Storage& storage, TransientStorage& transientStorage,
        const protocol::BlockHeader& blockHeader, const evmc_message& message,
        const evmc_address& origin, std::string_view abi, int contextID, int64_t& seq,
        PrecompiledManager const& precompiledManager, ledger::LedgerConfig const& ledgerConfig,
        crypto::Hash const& hashImpl, bool web3Tx, const u256& nonce,
        const evmc_host_interface* hostInterface)
      : evmc_host_context{.interface = hostInterface,
            .wasm_interface = nullptr,
            .hash_fn = evm_hash_fn,
            .isSMCrypto = (hashImpl.getHashImplType() == crypto::HashImplType::Sm3Hash),
            .version = 0,
            .metrics = std::addressof(executor::ethMetrics)},
        m_rollbackableStorage(storage),
        m_rollbackableTransientStorage(transientStorage),
        m_blockHeader(blockHeader),
        m_origin(origin),
        m_abi(abi),
        m_contextID(contextID),
        m_seq(seq),
        m_precompiledManager(precompiledManager),
        m_ledgerConfig(ledgerConfig),
        m_hashImpl(hashImpl),
        m_message(getMessage(
            web3Tx, message, m_blockHeader.get().number(), m_contextID, m_seq, nonce, m_hashImpl)),
        m_recipientAccount(getAccount(*this, this->message().recipient)),
        m_revision(EVMC_CANCUN),
        m_level(seq),
        m_web3Tx(web3Tx)
    {}

public:
    HostContext(Storage& storage, TransientStorage& transientStorage,
        protocol::BlockHeader const& blockHeader, const evmc_message& message,
        const evmc_address& origin, std::string_view abi, int contextID, int64_t& seq,
        PrecompiledManager const& precompiledManager, ledger::LedgerConfig const& ledgerConfig,
        crypto::Hash const& hashImpl, bool web3Tx, const u256& nonce, auto&& waitOperator)
      : HostContext(innerConstructor, storage, transientStorage, blockHeader, message, origin, abi,
            contextID, seq, precompiledManager, ledgerConfig, hashImpl, web3Tx, nonce,
            getHostInterface<HostContext>(std::forward<decltype(waitOperator)>(waitOperator)))
    {}

    ~HostContext() noexcept = default;
    HostContext(HostContext const&) = default;
    HostContext& operator=(HostContext const&) = default;
    HostContext(HostContext&&) noexcept = default;
    HostContext& operator=(HostContext&&) noexcept = default;

    constexpr evmc_message const& message() const& { return m_message; }
    constexpr evmc_message& mutableMessage() & { return m_message; }

    friend auto getAccount(HostContext& hostContext, const evmc_address& address)
    {
        return Account<std::decay_t<Storage>>(hostContext.m_rollbackableStorage.get(), address,
            hostContext.m_ledgerConfig.get().features().get(
                ledger::Features::Flag::feature_raw_address));
    }

    task::Task<evmc_bytes32> get(const evmc_bytes32* key, auto&&... /*unused*/)
    {
        co_return co_await ledger::account::storage(m_recipientAccount, *key);
    }

    task::Task<void> set(const evmc_bytes32* key, const evmc_bytes32* value, auto&&... /*unused*/)
    {
        co_await ledger::account::setStorage(m_recipientAccount, *key, *value);
    }

    task::Task<u256> balance(evmc_address addr, auto&&... /*unused*/)
    {
        auto account = getAccount(*this, addr);
        co_return co_await ledger::account::balance(account);
    }

    task::Task<evmc_bytes32> getTransientStorage(const evmc_bytes32* key, auto&&... /*unused*/)
    {
        evmc_bytes32 value;
        if (auto valueEntry = co_await storage2::readOne(m_rollbackableTransientStorage.get(),
                executor_v1::StateKeyView{concepts::bytebuffer::toView(
                                              co_await ledger::account::path(m_recipientAccount)),
                    concepts::bytebuffer::toView(key->bytes)}))
        {
            auto field = valueEntry->get();
            std::uninitialized_copy_n(field.data(), sizeof(value), value.bytes);
        }
        else
        {
            ::ranges::fill(value.bytes, 0);
        }
        HOST_CONTEXT_LOG(TRACE) << "getTransientStorage:"
                                << LOG_KV("key", concepts::bytebuffer::toView(key->bytes))
                                << LOG_KV("value", concepts::bytebuffer::toView(value.bytes));
        co_return value;
    }

    task::Task<void> setTransientStorage(
        const evmc_bytes32* key, const evmc_bytes32* value, auto&&... /*unused*/)
    {
        storage::Entry valueEntry(concepts::bytebuffer::toView(value->bytes));
        StateKey stateKey{
            concepts::bytebuffer::toView(co_await ledger::account::path(m_recipientAccount)),
            concepts::bytebuffer::toView(key->bytes)};
        HOST_CONTEXT_LOG(TRACE) << "setTransientStorage:"
                                << LOG_KV("key", concepts::bytebuffer::toView(key->bytes));
        co_await storage2::writeOne(
            m_rollbackableTransientStorage.get(), std::move(stateKey), std::move(valueEntry));
    }

    task::Task<std::optional<storage::Entry>> code(
        const evmc_address& address, auto&&... /*unused*/)
    {
        if (auto executable = co_await getExecutable(m_rollbackableStorage.get(), address,
                m_revision,
                m_ledgerConfig.get().features().get(ledger::Features::Flag::feature_raw_address));
            executable && executable->m_code)
        {
            co_return executable->m_code;
        }
        co_return {};
    }

    task::Task<size_t> codeSizeAt(const evmc_address& address, auto&&... /*unused*/)
    {
        if (auto const* precompiled = m_precompiledManager.get().getPrecompiled(address))
        {
            co_return executor_v1::size(*precompiled);
        }

        if (auto codeEntry = co_await code(address))
        {
            co_return codeEntry->get().size();
        }
        co_return 0;
    }

    task::Task<h256> codeHashAt(const evmc_address& address, auto&&... /*unused*/)
    {
        Account<Storage> account(m_rollbackableStorage.get(), address,
            m_ledgerConfig.get().features().get(ledger::Features::Flag::feature_raw_address));
        co_return co_await ledger::account::codeHash(account);
    }

    task::Task<bool> exists([[maybe_unused]] const evmc_address& address, auto&&... /*unused*/)
    {
        // TODO: impl the full support for solidity
        co_return true;
    }

    /// Hash of a block if within the last 256 blocks, or h256() otherwise.
    task::Task<h256> blockHash(int64_t number, auto&&... /*unused*/) const
    {
        if (number < blockNumber() && number >= 0)
        {
            if (auto blockHash = co_await ledger::getBlockHash(
                    m_rollbackableStorage.get(), number, ledger::fromStorage))
            {
                co_return *blockHash;
            }
        }
        co_return {};
    }
    int64_t blockNumber() const { return m_blockHeader.get().number(); }
    uint32_t blockVersion() const { return m_blockHeader.get().version(); }
    int64_t timestamp() const { return m_blockHeader.get().timestamp(); }
    evmc_address const& origin() const { return m_origin; }
    int64_t blockGasLimit() const { return std::get<0>(m_ledgerConfig.get().gasLimit()); }
    u256 gasPrice() const { return u256(std::get<0>(m_ledgerConfig.get().gasPrice())); }
    evmc_uint256be chainId() const
    {
        return m_ledgerConfig.get().chainId().value_or(evmc_uint256be{});
    }

    /// Revert any changes made (by any of the other calls).
    void log(const evmc_address& address, h256s topics, bytesConstRef data)
    {
        std::span view(address.bytes);
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
        if (ref.kind == EVMC_CREATE || ref.kind == EVMC_CREATE2)
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
        const auto* ref = std::addressof(message());
        HOST_CONTEXT_LOG(TRACE) << "HostContext execute level: " << m_level << " " << *ref;

        auto savepoint = m_rollbackableStorage.get().current();
        auto transientSavepoint = m_rollbackableTransientStorage.get().current();

        std::optional<EVMCResult> evmResult;
        if (m_ledgerConfig.get().authCheckStatus() != 0U)
        {
            HOST_CONTEXT_LOG(DEBUG) << "Checking auth..." << m_ledgerConfig.get().authCheckStatus()
                                    << " gas: " << ref->gas;

            if (auto result = checkAuth(m_rollbackableStorage.get(), m_blockHeader, *ref, m_origin,
                    buildLegacyExternalCaller(), m_precompiledManager.get(), m_contextID, m_seq,
                    m_hashImpl))
            {
                HOST_CONTEXT_LOG(DEBUG) << "Auth check failed";
                evmResult = std::move(result);
            };
        }

        if (!evmResult)
        {
            try
            {
                // 先转账，再执行
                // Transfer first, then proceed execute
                if (!::ranges::equal(ref->value.bytes, executor::EMPTY_EVM_BYTES32.bytes) &&
                    m_ledgerConfig.get().balanceTransfer())
                {
                    co_await transferBalance(*ref);
                }

                if (ref->kind == EVMC_CREATE || ref->kind == EVMC_CREATE2)
                {
                    evmResult.emplace(co_await executeCreate());
                }
                else
                {
                    evmResult.emplace(co_await executeCall());
                }
            }
            catch (protocol::OutOfGas& e)
            {
                HOST_CONTEXT_LOG(DEBUG)
                    << "OutOfGas exception: " << boost::diagnostic_information(e);
                evmResult.emplace(
                    makeErrorEVMCResult(m_hashImpl, protocol::TransactionStatus::OutOfGas,
                        EVMC_OUT_OF_GAS, evmResult->gas_left, e.what()));
            }
            catch (protocol::NotEnoughCashError& e)

            {
                HOST_CONTEXT_LOG(DEBUG)
                    << "NotEnoughCash exception: " << boost::diagnostic_information(e);
                evmResult.emplace(
                    makeErrorEVMCResult(m_hashImpl, protocol::TransactionStatus::NotEnoughCash,
                        EVMC_INSUFFICIENT_BALANCE, ref->gas, e.what()));
            }
            catch (NotFoundCodeError& e)
            {
                HOST_CONTEXT_LOG(DEBUG)
                    << "Not found code exception: " << boost::diagnostic_information(e);

                // Static call或delegate call时，合约不存在要返回EVMC_SUCCESS
                // STATIC_CALL or DELEGATE_CALL, the EVMC_SUCCESS is returned when the contract does
                // not exist
                using namespace std::string_literals;
                if (ref->flags == EVMC_STATIC || ref->kind == EVMC_DELEGATECALL)
                {
                    evmResult.emplace(makeErrorEVMCResult(
                        m_hashImpl, protocol::TransactionStatus::None, EVMC_SUCCESS, ref->gas, {}));
                }
                else
                {
                    evmResult.emplace(makeErrorEVMCResult(m_hashImpl,
                        protocol::TransactionStatus::RevertInstruction, EVMC_REVERT, ref->gas,
                        "Call address error."s));
                }
            }
            catch (std::exception& e)
            {
                HOST_CONTEXT_LOG(DEBUG)
                    << "Execute exception: " << boost::diagnostic_information(e);
                evmResult.emplace(makeErrorEVMCResult(m_hashImpl,
                    protocol::TransactionStatus::OutOfGas, EVMC_INTERNAL_ERROR, ref->gas, ""));
            }
        }

        if (evmResult->gas_left < 0)
        {
            HOST_CONTEXT_LOG(DEBUG) << "Execute gas < 0: " << evmResult->gas_left;
            evmResult.emplace(makeErrorEVMCResult(
                m_hashImpl, protocol::TransactionStatus::OutOfGas, EVMC_OUT_OF_GAS, ref->gas, ""));
        }

        if (evmResult->status_code != EVMC_SUCCESS)
        {
            co_await m_rollbackableStorage.get().rollback(savepoint);
            co_await m_rollbackableTransientStorage.get().rollback(transientSavepoint);
            m_logs.clear();
        }

        HOST_CONTEXT_LOG(TRACE) << "HostContext execute finished, kind: " << ref->kind
                                << " level: " << m_level << " seq: " << m_seq << " " << *evmResult;
        co_return std::move(*evmResult);
    }

    task::Task<EVMCResult> externalCall(const evmc_message& message, auto&&... /*unused*/)
    {
        ++m_seq;
        HOST_CONTEXT_LOG(TRACE) << "External call, seq: " << m_seq;
        auto senderAccount = getAccount(*this, message.sender);
        auto nonceStr = co_await ledger::account::nonce(senderAccount);
        auto nonce = u256(nonceStr.value_or(std::string("0")));
        HostContext hostcontext(innerConstructor, m_rollbackableStorage.get(),
            m_rollbackableTransientStorage.get(), m_blockHeader, message, m_origin, {}, m_contextID,
            m_seq, m_precompiledManager.get(), m_ledgerConfig, m_hashImpl, m_web3Tx, nonce,
            interface);

        co_await hostcontext.prepare();
        auto result = co_await hostcontext.execute();
        auto& logs = hostcontext.logs();
        if (result.status_code == EVMC_SUCCESS && !logs.empty())
        {
            m_logs.reserve(m_logs.size() + ::ranges::size(logs));
            ::ranges::move(logs, std::back_inserter(m_logs));
        }
        co_return result;
    }

    std::vector<protocol::LogEntry>& logs() & { return m_logs; }

private:
    void prepareCreate()
    {
        auto& ref = message();
        bytesConstRef createCode(ref.input_data, ref.input_size);
        m_executable = std::make_shared<Executable>(createCode, m_revision);
    }

    task::Task<EVMCResult> executeCreate()
    {
        auto& ref = message();
        if (m_blockHeader.get().number() != 0)
        {
            co_await createAuthTable(m_rollbackableStorage.get(), m_blockHeader, ref, m_origin,
                co_await ledger::account::path(m_recipientAccount), buildLegacyExternalCaller(),
                m_precompiledManager.get(), m_contextID, m_seq, m_ledgerConfig);
        }

        if (m_web3Tx && m_level != 0)
        {
            auto senderAccount = getAccount(*this, ref.sender);
            co_await ledger::account::increaseNonce(senderAccount);
        }

        co_await ledger::account::create(m_recipientAccount);
        auto result = m_executable->m_vmInstance.execute(
            interface, this, m_revision, std::addressof(ref), ref.input_data, ref.input_size);
        if (result.status_code == 0)
        {
            auto code = bytesConstRef(result.output_data, result.output_size);
            auto codeHash = m_hashImpl.get().hash(code);
            co_await ledger::account::setCode(
                m_recipientAccount, code.toBytes(), std::string(m_abi), codeHash);
            co_await ledger::account::setNonce(m_recipientAccount, "1");
            result.gas_left -= result.output_size * bcos::executor::VMSchedule().createDataGas;
            result.create_address = ref.code_address;

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

    void consumeTransferGas(evmc_message& ref)
    {
        if (m_level == 0)
        {
            if (ref.gas < executor::BALANCE_TRANSFER_GAS)
            {
                BOOST_THROW_EXCEPTION(protocol::OutOfGas{});
            }
            ref.gas -= executor::BALANCE_TRANSFER_GAS;
        }
    }

    void processDynamicPrecompiled()
    {
        if (hasPrecompiledPrefix(m_executable->m_code->get()))
        {
            auto& message = mutableMessage();
            auto code = m_executable->m_code->get();

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
            m_dynamicPrecompiledInput = codec.abiIn(
                "", codeParameters, bcos::bytesConstRef(message.input_data, message.input_size));

            message.input_data = m_dynamicPrecompiledInput.data();
            message.input_size = m_dynamicPrecompiledInput.size();

            HOST_CONTEXT_LOG(TRACE)
                << LOG_DESC("callDynamicPrecompiled") << LOG_KV("codeAddr", message.code_address)
                << LOG_KV("recvAddr", message.recipient) << LOG_KV("code", code);

            if (m_preparedPrecompiled =
                    m_precompiledManager.get().getPrecompiled(message.recipient);
                m_preparedPrecompiled == nullptr)
            {
                BOOST_THROW_EXCEPTION(NotFoundCodeError());
            }
        }
    }

    task::Task<void> prepareCall()
    {
        auto& ref = message();
        // delegatecall static precompiled is not allowed
        if (ref.kind != EVMC_DELEGATECALL)
        {
            if (auto const* precompiled =
                    m_precompiledManager.get().getPrecompiled(ref.code_address))
            {
                if (auto flag = executor_v1::featureFlag(*precompiled);
                    !flag || m_ledgerConfig.get().features().get(*flag))
                {
                    m_preparedPrecompiled = precompiled;
                    co_return;
                }
            }
        }
    }

    task::Task<EVMCResult> executeCall()
    {
        auto& ref = mutableMessage();
        // 先扣除BALANCE_TRANSFER_GAS
        // First deduct the BALANCE_TRANSFER_GAS.
        consumeTransferGas(ref);

        if (m_preparedPrecompiled != nullptr)
        {
            co_return executor_v1::callPrecompiled(*m_preparedPrecompiled,
                m_rollbackableStorage.get(), m_blockHeader, ref, m_origin,
                buildLegacyExternalCaller(), m_precompiledManager.get(), m_contextID, m_seq,
                m_ledgerConfig.get().authCheckStatus());
        }

        if (m_executable = co_await getExecutable(m_rollbackableStorage.get(), ref.code_address,
                m_revision,
                m_ledgerConfig.get().features().get(ledger::Features::Flag::feature_raw_address));
            !m_executable)
        {
            if (ref.input_size > 0)
            {
                BOOST_THROW_EXCEPTION(NotFoundCodeError());
            }

            co_return EVMCResult{evmc_result{.status_code = EVMC_SUCCESS,
                                     .gas_left = ref.gas,
                                     .gas_refund = 0,
                                     .output_data = nullptr,
                                     .output_size = 0,
                                     .release = nullptr,
                                     .create_address = {},
                                     .padding = {}},
                protocol::TransactionStatus::None};
        }
        processDynamicPrecompiled();

        if (m_preparedPrecompiled != nullptr)
        {
            co_return executor_v1::callPrecompiled(*m_preparedPrecompiled,
                m_rollbackableStorage.get(), m_blockHeader, ref, m_origin,
                buildLegacyExternalCaller(), m_precompiledManager.get(), m_contextID, m_seq,
                m_ledgerConfig.get().authCheckStatus());
        }

        co_return m_executable->m_vmInstance.execute(interface, this, m_revision,
            std::addressof(ref), (const uint8_t*)m_executable->m_code->data(),
            m_executable->m_code->size());
    }
};

}  // namespace bcos::executor_v1::hostcontext
