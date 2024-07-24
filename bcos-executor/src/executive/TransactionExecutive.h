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
#include "../vm/VMFactory.h"
#include "BlockContext.h"
#include "SyncStorageWrapper.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-table/src/StateStorage.h"
#include <bcos-codec/abi/ContractABICodec.h>
#include <boost/algorithm/string/case_conv.hpp>
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
class BlockContext;

class TransactionExecutive : public std::enable_shared_from_this<TransactionExecutive>
{
public:
    using Ptr = std::shared_ptr<TransactionExecutive>;
    TransactionExecutive(const BlockContext& blockContext, std::string contractAddress,
        int64_t contextID, int64_t seq, const wasm::GasInjector& gasInjector)
      : m_blockContext(blockContext),
        m_contractAddress(std::move(contractAddress)),
        m_contextID(contextID),
        m_seq(seq),
        m_gasInjector(gasInjector),
        m_recoder(std::make_shared<storage::Recoder>()),
        m_transientRecoder(std::make_shared<storage::Recoder>()),
        m_storageWrapperObj(m_blockContext.storage(), m_recoder),
        m_storageWrapper(&m_storageWrapperObj)
    {
        m_hashImpl = m_blockContext.hashHandler();
        m_storageWrapperObj.setCodeCache(m_blockContext.getCodeCache());
        m_storageWrapperObj.setCodeHashCache(m_blockContext.getCodeHashCache());
    }

    TransactionExecutive(TransactionExecutive const&) = delete;
    TransactionExecutive& operator=(TransactionExecutive) = delete;
    TransactionExecutive(TransactionExecutive&&) = delete;
    TransactionExecutive& operator=(TransactionExecutive&&) = delete;

    virtual ~TransactionExecutive() = default;

    virtual CallParameters::UniquePtr start(CallParameters::UniquePtr input);

    // External call request
    virtual CallParameters::UniquePtr externalCall(CallParameters::UniquePtr input);

    auto& storage()
    {
        assert(m_storageWrapper);
        return *m_storageWrapper;
    }


    const BlockContext& blockContext() { return m_blockContext; }

    int64_t contextID() const { return m_contextID; }
    int64_t seq() const { return m_seq; }

    // in delegatecall this is codeAddress
    std::string_view contractAddress() const { return m_contractAddress; }

    CallParameters::UniquePtr execute(
        CallParameters::UniquePtr callParameters);  // execute parameters in
                                                    // current corouitine

    virtual bool isPrecompiled(const std::string& _address) const;

    std::shared_ptr<precompiled::Precompiled> getPrecompiled(const std::string& _address) const;
    virtual std::shared_ptr<precompiled::Precompiled> getPrecompiled(const std::string& _address,
        uint32_t version, bool isAuth, const ledger::Features& features) const;

    void setStaticPrecompiled(std::shared_ptr<const std::set<std::string>> _staticPrecompiled)
    {
        m_staticPrecompiled = std::move(_staticPrecompiled);
    }

    inline bool isStaticPrecompiled(const std::string& _a) const
    {
        return _a.starts_with(precompiled::SYS_ADDRESS_PREFIX) && m_staticPrecompiled->contains(_a);
    }

    inline bool isEthereumPrecompiled(const std::string& _a) const
    {
        return m_evmPrecompiled != nullptr && _a.starts_with(precompiled::EVM_PRECOMPILED_PREFIX) &&
               m_evmPrecompiled->contains(_a);
    }

    std::pair<bool, bytes> executeOriginPrecompiled(const std::string& _a, bytesConstRef _in) const;

    int64_t costOfPrecompiled(const std::string& _a, bytesConstRef _in) const;

    void setEVMPrecompiled(
        std::shared_ptr<std::map<std::string, std::shared_ptr<PrecompiledContract>>>);

    void setPrecompiled(std::shared_ptr<PrecompiledMap>);

    std::shared_ptr<precompiled::PrecompiledExecResult> execPrecompiled(
        precompiled::PrecompiledExecResult::Ptr const& _precompiledParams);

    using tssMap = bcos::BucketMap<int64_t, std::shared_ptr<storage::StateStorageInterface>>;

    VMSchedule const& vmSchedule() const { return m_blockContext.vmSchedule(); }

    bool isWasm() const { return m_blockContext.isWasm(); }

    bool hasContractTableChanged() const { return m_hasContractTableChanged; }
    void setContractTableChanged() { m_hasContractTableChanged = true; }

    bool checkAuth(const CallParameters::UniquePtr& callParameters);
    void creatAuthTable(std::string_view _tableName, std::string_view _origin,
        std::string_view _sender, uint32_t _version);

    crypto::HashType getCodeHash(const std::string_view& contractTableName);
    std::optional<storage::Entry> getCodeEntryFromContractTable(
        const std::string_view contractTableName);
    std::optional<storage::Entry> getCodeByHash(const std::string_view& codeHash);
    bool setCode(std::string_view contractTableName,
        std::variant<std::string_view, std::string, bcos::bytes> code);
    void setAbiByCodeHash(std::string_view codeHash, std::string_view abi);
    std::tuple<h256, std::optional<storage::Entry>> getCodeByContractTableName(
        const std::string_view& contractTableName, bool needTryFromContractTable = true);

    CallParameters::UniquePtr transferBalance(CallParameters::UniquePtr callParameters,
        int64_t requireGas, std::string_view currentContextAddress);

    std::string getContractTableName(
        const std::string_view& _address, bool isWasm = false, bool isCreate = false);

    std::shared_ptr<storage::StateStorageInterface> getTransientStateStorage(int64_t contextID);

    std::shared_ptr<storage::Recoder> getRecoder() { return m_recoder; }

protected:
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

    //    virtual TransactionExecutive::Ptr buildChildExecutive(const std::string& _contractAddress,
    //        int64_t contextID, int64_t seq, bool useCoroutine = true)
    virtual TransactionExecutive::Ptr buildChildExecutive(
        const std::string& _contractAddress, int64_t contextID, int64_t seq)
    {
        auto executiveFactory = std::make_shared<ExecutiveFactory>(
            m_blockContext, m_evmPrecompiled, m_precompiled, m_staticPrecompiled, m_gasInjector);


        return executiveFactory->build(_contractAddress, contextID, seq, ExecutiveType::common);
    }

    void revert();

    CallParameters::UniquePtr parseEVMCResult(
        CallParameters::UniquePtr callResults, const Result& _result);

    void writeErrInfoToOutput(std::string const& errInfo, CallParameters& _callParameters)
    {
        bcos::codec::abi::ContractABICodec abi(*m_hashImpl);
        auto codecOutput = abi.abiIn("Error(string)", errInfo);
        _callParameters.data = std::move(codecOutput);
    }

    bool checkExecAuth(const CallParameters::UniquePtr& callParameters);
    int32_t checkContractAvailable(const CallParameters::UniquePtr& callParameters);
    uint8_t checkAccountAvailable(const CallParameters::UniquePtr& callParameters);

    bool buildBfsPath(std::string_view _absoluteDir, std::string_view _origin,
        std::string_view _sender, std::string_view _type, int64_t gasLeft);

    const BlockContext& m_blockContext;  ///< Information on the runtime environment.
    std::shared_ptr<std::map<std::string, std::shared_ptr<PrecompiledContract>>> m_evmPrecompiled;
    std::shared_ptr<PrecompiledMap> m_precompiled;
    std::shared_ptr<const std::set<std::string>> m_staticPrecompiled;

    std::string m_contractAddress;
    int64_t m_contextID;
    int64_t m_seq;
    crypto::Hash::Ptr m_hashImpl;

    const wasm::GasInjector& m_gasInjector;

    bcos::storage::Recoder::Ptr m_recoder;
    bcos::storage::Recoder::Ptr m_transientRecoder;
    std::vector<TransactionExecutive::Ptr> m_childExecutives;

    storage::StorageWrapper m_storageWrapperObj;
    storage::StorageWrapper* m_storageWrapper;
    bool m_hasContractTableChanged = false;
};

}  // namespace executor
}  // namespace bcos
