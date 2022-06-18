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
 * @brief executive of vm
 * @file TransactionExecutive.h
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#pragma once

#include "../Common.h"
#include "../executor/TransactionExecutor.h"
#include "BlockContext.h"
#include "SyncStorageWrapper.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-framework//executor/ExecutionMessage.h"
#include "bcos-framework//executor/PrecompiledTypeDef.h"
#include "bcos-framework//protocol/BlockHeader.h"
#include "bcos-framework//protocol/Transaction.h"
#include "bcos-protocol/TransactionStatus.h"
#include <bcos-codec/abi/ContractABICodec.h>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/coroutine2/coroutine.hpp>
#include <functional>
#include <variant>

namespace bcos
{
namespace executor
{
class Result;

}  // namespace executor
namespace precompiled
{
struct PrecompiledExecResult;
}

namespace executor
{
class HostContext;

class TransactionExecutive : public std::enable_shared_from_this<TransactionExecutive>
{
public:
    using Ptr = std::shared_ptr<TransactionExecutive>;

    class ResumeHandler;

    using CoroutineMessage = std::function<void(ResumeHandler resume)>;
    using Coroutine = boost::coroutines2::coroutine<CoroutineMessage>;

    class ResumeHandler
    {
    public:
        ResumeHandler(TransactionExecutive& executive) : m_executive(executive) {}

        void operator()()
        {
            COROUTINE_TRACE_LOG(TRACE, m_executive.contextID(), m_executive.seq())
                << "Context switch to executive coroutine, from ResumeHandler";
            (*m_executive.m_pullMessage)();
        }

    private:
        TransactionExecutive& m_executive;
    };

    TransactionExecutive(std::weak_ptr<BlockContext> blockContext, std::string contractAddress,
        int64_t contextID, int64_t seq, std::shared_ptr<wasm::GasInjector>& gasInjector)
      : m_blockContext(std::move(blockContext)),
        m_contractAddress(std::move(contractAddress)),
        m_contextID(contextID),
        m_seq(seq),
        m_gasInjector(gasInjector)
    {
        m_recoder = std::make_shared<storage::Recoder>();
        m_hashImpl = m_blockContext.lock()->hashHandler();
    }

    TransactionExecutive(TransactionExecutive const&) = delete;
    TransactionExecutive& operator=(TransactionExecutive) = delete;
    TransactionExecutive(TransactionExecutive&&) = delete;
    TransactionExecutive& operator=(TransactionExecutive&&) = delete;

    virtual ~TransactionExecutive() = default;

    CallParameters::UniquePtr start(CallParameters::UniquePtr input);  // start a new coroutine to
                                                                       // execute

    // External call request
    CallParameters::UniquePtr externalCall(CallParameters::UniquePtr input);  // call by
                                                                              // hostContext

    // External request key locks, throw exception if dead lock detected
    void externalAcquireKeyLocks(std::string acquireKeyLock);

    auto& storage()
    {
        assert(m_storageWrapper);
        return *m_storageWrapper;
    }

    std::shared_ptr<SyncStorageWrapper> lastStorage() { return m_lastStorageWrapper; }

    std::weak_ptr<BlockContext> blockContext() { return m_blockContext; }

    int64_t contextID() const { return m_contextID; }
    int64_t seq() const { return m_seq; }

    std::string_view contractAddress() { return m_contractAddress; }

    CallParameters::UniquePtr execute(
        CallParameters::UniquePtr callParameters);  // execute parameters in
                                                    // current corouitine

    bool isPrecompiled(const std::string& _address) const;

    std::shared_ptr<precompiled::Precompiled> getPrecompiled(const std::string& _address) const;

    void setConstantPrecompiled(
        const std::string& _address, std::shared_ptr<precompiled::Precompiled> precompiled);

    void setBuiltInPrecompiled(std::shared_ptr<const std::set<std::string>> _builtInPrecompiled)
    {
        m_builtInPrecompiled = std::move(_builtInPrecompiled);
    }

    inline bool isBuiltInPrecompiled(const std::string& _a) const
    {
        std::stringstream prefix;
        prefix << std::setfill('0') << std::setw(36) << "0";
        if (_a.find(prefix.str()) != 0)
            return false;
        return m_builtInPrecompiled->find(_a) != m_builtInPrecompiled->end();
    }

    inline bool isEthereumPrecompiled(const std::string& _a) const
    {
        std::stringstream prefix;
        prefix << std::setfill('0') << std::setw(39) << "0";
        if (!m_evmPrecompiled || _a.find(prefix.str()) != 0)
            return false;
        return m_evmPrecompiled->find(_a) != m_evmPrecompiled->end();
    }

    std::pair<bool, bytes> executeOriginPrecompiled(const std::string& _a, bytesConstRef _in) const;

    int64_t costOfPrecompiled(const std::string& _a, bytesConstRef _in) const;

    void setEVMPrecompiled(
        std::shared_ptr<const std::map<std::string, std::shared_ptr<PrecompiledContract>>>
            precompiledContract);

    void setConstantPrecompiled(
        std::shared_ptr<std::map<std::string, std::shared_ptr<precompiled::Precompiled>>>
            _constantPrecompiled);

    std::shared_ptr<precompiled::PrecompiledExecResult> execPrecompiled(
        precompiled::PrecompiledExecResult::Ptr const& _precompiledParams);

    void setExchangeMessage(CallParameters::UniquePtr callParameters)
    {
        m_exchangeMessage = std::move(callParameters);
    }

    void appendResumeKeyLocks(std::vector<std::string> keyLocks)
    {
        std::copy(
            keyLocks.begin(), keyLocks.end(), std::back_inserter(m_exchangeMessage->keyLocks));
    }

    CallParameters::UniquePtr resume()
    {
        EXECUTOR_LOG(TRACE) << "Context switch to executive coroutine, from resume";
        (*m_pullMessage)();

        return dispatcher();
    }
    VMSchedule const& vmSchedule() const { return m_blockContext.lock()->vmSchedule(); }

private:
    CallParameters::UniquePtr dispatcher();

    std::tuple<std::unique_ptr<HostContext>, CallParameters::UniquePtr> call(
        CallParameters::UniquePtr callParameters);
    CallParameters::UniquePtr callPrecompiled(CallParameters::UniquePtr callParameters);
    std::tuple<std::unique_ptr<HostContext>, CallParameters::UniquePtr> create(
        CallParameters::UniquePtr callParameters);
    CallParameters::UniquePtr internalCreate(CallParameters::UniquePtr callParameters);
    CallParameters::UniquePtr go(
        HostContext& hostContext, CallParameters::UniquePtr extraData = nullptr);
    CallParameters::UniquePtr callDynamicPrecompiled(
        CallParameters::UniquePtr callParameters, const std::string& code);
    void spawnAndCall(std::function<void(ResumeHandler)> function);

    void revert();

    CallParameters::UniquePtr parseEVMCResult(
        CallParameters::UniquePtr callResults, const Result& _result);

    void writeErrInfoToOutput(std::string const& errInfo, CallParameters& _callParameters)
    {
        bcos::codec::abi::ContractABICodec abi(m_hashImpl);
        auto codecOutput = abi.abiIn("Error(string)", errInfo);
        _callParameters.data = std::move(codecOutput);
    }

    inline std::string getContractTableName(const std::string_view& _address, bool isWasm = false)
    {
        auto blockContext = m_blockContext.lock();

        if (blockContext->isAuthCheck())
        {
            if (_address.find(precompiled::SYS_ADDRESS_PREFIX) == 0)
            {
                return std::string(USER_SYS_PREFIX).append(_address);
            }
        }


        std::string formatAddress(_address);
        if (!isWasm)
        {
            // evm address needs to be lower
            boost::algorithm::to_lower(formatAddress);
        }
        else
        {
            if (_address.find(USER_TABLE_PREFIX) == 0)
            {
                return formatAddress;
            }
            formatAddress = (_address[0] == '/') ? formatAddress.substr(1) : formatAddress;
        }

        return std::string(USER_APPS_PREFIX).append(formatAddress);
    }

    bool checkAuth(const CallParameters::UniquePtr& callParameters, bool _isCreate);
    bool checkContractAvailable(const CallParameters::UniquePtr& callParameters);

    void creatAuthTable(
        std::string_view _tableName, std::string_view _origin, std::string_view _sender);

    bool buildBfsPath(std::string_view _absoluteDir, std::string_view _origin,
        std::string_view _sender, std::string_view _type, int64_t gasLeft);

    std::weak_ptr<BlockContext> m_blockContext;  ///< Information on the runtime environment.
    std::shared_ptr<std::map<std::string, std::shared_ptr<precompiled::Precompiled>>>
        m_constantPrecompiled;
    std::shared_ptr<const std::map<std::string, std::shared_ptr<PrecompiledContract>>>
        m_evmPrecompiled;
    std::shared_ptr<const std::set<std::string>> m_builtInPrecompiled;

    std::string m_contractAddress;
    int64_t m_contextID;
    int64_t m_seq;
    crypto::Hash::Ptr m_hashImpl;

    std::shared_ptr<wasm::GasInjector> m_gasInjector = nullptr;

    bcos::storage::Recoder::Ptr m_recoder;
    std::unique_ptr<SyncStorageWrapper> m_storageWrapper;
    std::shared_ptr<SyncStorageWrapper> m_lastStorageWrapper;
    CallParameters::UniquePtr m_exchangeMessage = nullptr;

    std::optional<Coroutine::pull_type> m_pullMessage;
    std::optional<Coroutine::push_type> m_pushMessage;
};

}  // namespace executor
}  // namespace bcos
