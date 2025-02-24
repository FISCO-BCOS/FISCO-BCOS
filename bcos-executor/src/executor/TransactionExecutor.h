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
 * @brief TransactionExecutor
 * @file TransactionExecutor.h
 * @author: xingqiangbai
 * @date: 2021-05-27
 * @brief TransactionExecutor
 * @file TransactionExecutor.h
 * @author: ancelmo
 * @date: 2021-10-16
 */
#pragma once

#include "../Common.h"
#include "../dag/CriticalFields.h"
#include "bcos-executor/src/vm/VMFactory.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/executor/ParallelTransactionExecutorInterface.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-table/src/StateStorageFactory.h"
#include "tbb/concurrent_unordered_map.h"
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-executor/src/executive/LedgerCache.h>
#include <bcos-utilities/ThreadPool.h>
#include <tbb/concurrent_hash_map.h>
#include <boost/function.hpp>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stack>
#include <thread>


namespace bcos
{
namespace precompiled
{
class Precompiled;
struct PrecompiledExecResult;
}  // namespace precompiled
namespace wasm
{
class GasInjector;
}
namespace executor
{
enum ExecutorVersion : int32_t
{
    Version_3_0_0 = 1,
};

class TransactionExecutive;
class ExecutiveFlowInterface;
class BlockContext;
class PrecompiledContract;
template <typename T, typename V>
class ClockCache;
class StateStorageFactory;
struct FunctionAbi;
struct CallParameters;
class PrecompiledMap;

using executionCallback = std::function<void(
    const Error::ConstPtr&, std::vector<protocol::ExecutionMessage::UniquePtr>&)>;

class TransactionExecutor : public ParallelTransactionExecutorInterface,
                            public std::enable_shared_from_this<TransactionExecutor>
{
public:
    using Ptr = std::shared_ptr<TransactionExecutor>;
    using ConstPtr = std::shared_ptr<const TransactionExecutor>;

    TransactionExecutor(bcos::ledger::LedgerInterface::Ptr ledger,
        txpool::TxPoolInterface::Ptr txpool, storage::MergeableStorageInterface::Ptr cachedStorage,
        storage::TransactionalStorageInterface::Ptr backendStorage,
        protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
        storage::StateStorageFactory::Ptr stateStorageFactory, bcos::crypto::Hash::Ptr hashImpl,
        bool isWasm, bool isAuthCheck, std::shared_ptr<VMFactory> vmFactory,
        std::shared_ptr<std::set<std::string, std::less<>>> keyPageIgnoreTables, std::string name);

    ~TransactionExecutor() override = default;

    void nextBlockHeader(int64_t schedulerTermId,
        const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
        std::function<void(bcos::Error::UniquePtr)> callback) override;

    void executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override;

    void call(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override;

    void executeTransactions(std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override;

    void preExecuteTransactions(int64_t schedulerTermId,
        const bcos::protocol::BlockHeader::ConstPtr& blockHeader, std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(bcos::Error::UniquePtr)> callback) override {
        // do nothing
    };

    void dmcExecuteTransactions(std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override;

    void dmcExecuteTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override;

    void dmcCall(bcos::protocol::ExecutionMessage::UniquePtr input,
        std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
            callback) override;

    void getHash(bcos::protocol::BlockNumber number,
        std::function<void(bcos::Error::UniquePtr, crypto::HashType)> callback) override;

    void dagExecuteTransactions(gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback) override;

    /* ----- XA Transaction interface Start ----- */

    // Write data to storage uncommitted
    void prepare(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override;

    // Commit uncommitted data
    void commit(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override;

    // Rollback the changes
    void rollback(const bcos::protocol::TwoPCParams& params,
        std::function<void(bcos::Error::Ptr)> callback) override;

    /* ----- XA Transaction interface End ----- */

    // drop all status
    void reset(std::function<void(bcos::Error::Ptr)> callback) override;
    void getCode(std::string_view contract,
        std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) override;
    void getABI(std::string_view contract,
        std::function<void(bcos::Error::Ptr, std::string)> callback) override;

    void updateEoaNonce(std::unordered_map<std::string, u256> const& nonceMap) override;

    void start() override { m_isRunning = true; }
    void stop() override;

    void registerNeedSwitchEvent(std::function<void()> event) { f_onNeedSwitchEvent = event; }

    // only for test, do not use in formal environment
    void setKeyPageIgnoreTable(auto ignoreTable) { m_keyPageIgnoreTables = std::move(ignoreTable); }

protected:
    void executeTransactionsInternal(std::string contractAddress,
        gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs, bool useCoroutine,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback);

    virtual void dagExecuteTransactionsInternal(gsl::span<std::unique_ptr<CallParameters>> inputs,
        std::function<void(
            bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
            callback);
    virtual std::shared_ptr<std::vector<bytes>> extractConflictFields(
        const FunctionAbi& functionAbi, const CallParameters& params,
        std::shared_ptr<BlockContext> _blockContext);

    virtual std::shared_ptr<BlockContext> createBlockContext(
        const protocol::BlockHeader::ConstPtr& currentHeader,
        storage::StateStorageInterface::Ptr tableFactory);

    virtual std::shared_ptr<BlockContext> createBlockContextForCall(
        bcos::protocol::BlockNumber blockNumber, h256 blockHash, uint64_t timestamp,
        int32_t blockVersion, storage::StateStorageInterface::Ptr tableFactory);

    void asyncExecute(std::shared_ptr<BlockContext> blockContext,
        bcos::protocol::ExecutionMessage::UniquePtr input, bool useCoroutine,
        std::function<void(bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>
            callback);

    std::unique_ptr<protocol::ExecutionMessage> toExecutionResult(
        const TransactionExecutive& executive, std::unique_ptr<CallParameters> params);

    std::unique_ptr<protocol::ExecutionMessage> toExecutionResult(
        std::unique_ptr<CallParameters> params);

    std::unique_ptr<CallParameters> createCallParameters(
        bcos::protocol::ExecutionMessage& inputs, bool staticCall);

    std::unique_ptr<CallParameters> createCallParameters(
        bcos::protocol::ExecutionMessage& input, const bcos::protocol::Transaction& tx);

    std::function<void(
        const TransactionExecutive& executive, std::unique_ptr<CallParameters> input)>
    createExternalFunctionCall(std::function<void(
            bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>& callback);


    void removeCommittedState();

    // execute transactions with criticals and return in executionResults
    void executeTransactionsWithCriticals(critical::CriticalFieldsInterface::Ptr criticals,
        gsl::span<std::unique_ptr<CallParameters>> inputs,
        std::vector<protocol::ExecutionMessage::UniquePtr>& executionResults);

    virtual std::shared_ptr<ExecutiveFlowInterface> getExecutiveFlow(
        std::shared_ptr<BlockContext> blockContext, std::string codeAddress, bool useCoroutine,
        bool isStaticCall = false);


    void asyncExecuteExecutiveFlow(std::shared_ptr<ExecutiveFlowInterface> executiveFlow,
        std::function<void(
            bcos::Error::UniquePtr&&, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>&&)>
            callback);


    bcos::storage::StateStorageInterface::Ptr createStateStorage(
        bcos::storage::StorageInterface::Ptr storage, bool ignoreNotExist,
        bool setRowWithDirtyFlag);

    protocol::BlockNumber getBlockNumberInStorage();
    protocol::BlockHeader::Ptr getBlockHeaderInStorage(protocol::BlockNumber number);
    std::string getCodeHash(
        std::string_view tableName, storage::StateStorageInterface::Ptr const& stateStorage);

    std::string m_name;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    txpool::TxPoolInterface::Ptr m_txpool;
    storage::MergeableStorageInterface::Ptr m_cachedStorage;
    std::shared_ptr<storage::TransactionalStorageInterface> m_backendStorage;
    protocol::ExecutionMessageFactory::Ptr m_executionMessageFactory;
    storage::StateStorageFactory::Ptr m_stateStorageFactory;
    std::shared_ptr<BlockContext> m_blockContext;
    crypto::Hash::Ptr m_hashImpl;
    std::shared_ptr<ClockCache<bcos::bytes, FunctionAbi>> m_abiCache;

    struct State
    {
        State(
            bcos::protocol::BlockNumber _number, bcos::storage::StateStorageInterface::Ptr _storage)
          : number(_number), storage(std::move(_storage))
        {}
        State(const State&) = delete;
        State& operator=(const State&) = delete;

        bcos::protocol::BlockNumber number;
        bcos::storage::StateStorageInterface::Ptr storage;
    };
    std::list<State> m_stateStorages;

    bcos::protocol::BlockNumber m_lastCommittedBlockNumber = getBlockNumberInStorage();
    std::atomic_uint64_t m_lastCommittedBlockTimestamp = utcTime();

    struct HashCombine
    {
        size_t hash(const std::tuple<int64_t, int64_t>& val) const
        {
            size_t seed = hashInt64(std::get<0>(val));
            boost::hash_combine(seed, hashInt64(std::get<1>(val)));

            return seed;
        }

        bool equal(
            const std::tuple<int64_t, int64_t>& lhs, const std::tuple<int64_t, int64_t>& rhs) const
        {
            return std::get<0>(lhs) == std::get<0>(rhs) && std::get<1>(lhs) == std::get<1>(rhs);
        }

        std::hash<int64_t> hashInt64;
    };

    struct CallState
    {
        std::shared_ptr<BlockContext> blockContext;
    };
    std::shared_ptr<tbb::concurrent_hash_map<std::tuple<int64_t, int64_t>, CallState, HashCombine>>
        m_calledContext = std::make_shared<
            tbb::concurrent_hash_map<std::tuple<int64_t, int64_t>, CallState, HashCombine>>();
    std::shared_mutex m_stateStoragesMutex;

    std::shared_ptr<std::map<std::string, std::shared_ptr<PrecompiledContract>>> m_evmPrecompiled;
    std::shared_ptr<executor::PrecompiledMap> m_precompiled;
    std::shared_ptr<const std::set<std::string>> m_staticPrecompiled;

    unsigned int m_DAGThreadNum = std::max(std::thread::hardware_concurrency(), (unsigned int)1);
    std::shared_ptr<wasm::GasInjector> m_gasInjector = nullptr;
    mutable bcos::RecursiveMutex x_executiveFlowLock;
    bool m_isAuthCheck = false;
    bool m_isWasm = false;
    bool m_isRunning = false;
    uint32_t m_blockVersion = 0;
    int64_t m_schedulerTermId = -1;
    std::shared_ptr<std::set<std::string, std::less<>>> m_keyPageIgnoreTables;

    bcos::ThreadPool::Ptr m_threadPool;
    mutable RecursiveMutex x_resetEnvironmentLock;

    void setBlockVersion(uint32_t blockVersion);
    void initEvmEnvironment();
    void initWasmEnvironment();
    void resetEnvironment();
    void initTestPrecompiledTable(storage::StorageInterface::Ptr storage);
    std::function<void()> f_onNeedSwitchEvent;

    LedgerCache::Ptr m_ledgerCache;
    std::shared_ptr<VMFactory> m_vmFactory;
};

}  // namespace executor
}  // namespace bcos
