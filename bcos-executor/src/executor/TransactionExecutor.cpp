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
 * @file TransactionExecutor.cpp
 * @author: xingqiangbai
 * @date: 2021-09-01
 */

#include "TransactionExecutor.h"
#include "../Common.h"
#include "../dag/Abi.h"
#include "../dag/ClockCache.h"
#include "../dag/CriticalFields.h"
#include "../dag/ScaleUtils.h"
#include "../dag/TxDAG2.h"
#include "../executive/BlockContext.h"
#include "../executive/ExecutiveFactory.h"
#include "../executive/ExecutiveSerialFlow.h"
#include "../executive/ExecutiveStackFlow.h"
#include "../executive/TransactionExecutive.h"
#include "../precompiled/BFSPrecompiled.h"
#include "../precompiled/ConsensusPrecompiled.h"
#include "../precompiled/CryptoPrecompiled.h"
#include "../precompiled/KVTablePrecompiled.h"
#include "../precompiled/SystemConfigPrecompiled.h"
#include "../precompiled/TableManagerPrecompiled.h"
#include "../precompiled/TablePrecompiled.h"
#include "../precompiled/extension/AuthManagerPrecompiled.h"
#include "../precompiled/extension/ContractAuthMgrPrecompiled.h"
#include "../precompiled/extension/DagTransferPrecompiled.h"
#include "../precompiled/extension/GroupSigPrecompiled.h"
#include "../precompiled/extension/RingSigPrecompiled.h"
#include "../precompiled/extension/UserPrecompiled.h"
#include "../precompiled/extension/ZkpPrecompiled.h"
#include "../vm/Precompiled.h"
#include "../vm/gas_meter/GasInjector.h"
#include "ExecuteOutputs.h"
#include "bcos-codec/abi/ContractABIType.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include "bcos-framework/dispatcher/SchedulerInterface.h"
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/storage/Table.h"
#include "bcos-table/src/KeyPageStorage.h"
#include "bcos-table/src/StateStorage.h"
#include "tbb/flow_graph.h"
#include <bcos-framework/executor/ExecuteError.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/ThreadPool.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/detail/exception_ptr.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/format.hpp>
#include <boost/format/format_fwd.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread/latch.hpp>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <cassert>
#include <exception>
#include <functional>
#include <gsl/util>
#include <iterator>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>


using namespace bcos;
using namespace std;
using namespace bcos::executor;
using namespace bcos::executor::critical;
using namespace bcos::wasm;
using namespace bcos::protocol;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace tbb::flow;

crypto::Hash::Ptr GlobalHashImpl::g_hashImpl;


TransactionExecutor::TransactionExecutor(bcos::ledger::LedgerInterface::Ptr ledger,
    txpool::TxPoolInterface::Ptr txpool, storage::MergeableStorageInterface::Ptr cachedStorage,
    storage::TransactionalStorageInterface::Ptr backendStorage,
    protocol::ExecutionMessageFactory::Ptr executionMessageFactory,
    bcos::crypto::Hash::Ptr hashImpl, bool isWasm, bool isAuthCheck, size_t keyPageSize = 0,
    std::shared_ptr<const std::set<std::string, std::less<>>> keyPageIgnoreTables = nullptr,
    std::string name = "default-executor-name")
  : m_name(std::move(name)),
    m_ledger(ledger),
    m_txpool(std::move(txpool)),
    m_cachedStorage(std::move(cachedStorage)),
    m_backendStorage(std::move(backendStorage)),
    m_executionMessageFactory(std::move(executionMessageFactory)),
    m_hashImpl(std::move(hashImpl)),
    m_isAuthCheck(isAuthCheck),
    m_isWasm(isWasm),
    m_keyPageSize(keyPageSize),
    m_keyPageIgnoreTables(keyPageIgnoreTables)
{
    assert(m_backendStorage);

    GlobalHashImpl::g_hashImpl = m_hashImpl;
    m_abiCache = make_shared<ClockCache<bcos::bytes, FunctionAbi>>(32);
    m_gasInjector = std::make_shared<wasm::GasInjector>(wasm::GetInstructionTable());

    m_threadPool = std::make_shared<bcos::ThreadPool>(name, std::thread::hardware_concurrency());
    if (m_isWasm)
    {
        initWasmEnvironment();
    }
    else
    {
        initEvmEnvironment();
    }
    assert(!m_constantPrecompiled->empty());
    assert(m_builtInPrecompiled);
    start();
}

void TransactionExecutor::initEvmEnvironment()
{
    m_schedule = FiscoBcosScheduleV4;

    auto fillZero = [](int _num) -> std::string {
        std::stringstream stream;
        stream << std::setfill('0') << std::setw(40) << std::hex << _num;
        return stream.str();
    };
    m_precompiledContract =
        std::make_shared<std::map<std::string, std::shared_ptr<PrecompiledContract>>>();
    m_builtInPrecompiled = std::make_shared<std::set<std::string>>();
    m_constantPrecompiled =
        std::make_shared<std::map<std::string, std::shared_ptr<precompiled::Precompiled>>>();

    m_precompiledContract->insert(std::make_pair(fillZero(1),
        make_shared<PrecompiledContract>(3000, 0, PrecompiledRegistrar::executor("ecrecover"))));
    m_precompiledContract->insert(std::make_pair(fillZero(2),
        make_shared<PrecompiledContract>(60, 12, PrecompiledRegistrar::executor("sha256"))));
    m_precompiledContract->insert(std::make_pair(fillZero(3),
        make_shared<PrecompiledContract>(600, 120, PrecompiledRegistrar::executor("ripemd160"))));
    m_precompiledContract->insert(std::make_pair(fillZero(4),
        make_shared<PrecompiledContract>(15, 3, PrecompiledRegistrar::executor("identity"))));
    m_precompiledContract->insert(
        {fillZero(5), make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("modexp"),
                          PrecompiledRegistrar::executor("modexp"))});
    m_precompiledContract->insert(
        {fillZero(6), make_shared<PrecompiledContract>(
                          150, 0, PrecompiledRegistrar::executor("alt_bn128_G1_add"))});
    m_precompiledContract->insert(
        {fillZero(7), make_shared<PrecompiledContract>(
                          6000, 0, PrecompiledRegistrar::executor("alt_bn128_G1_mul"))});
    m_precompiledContract->insert({fillZero(8),
        make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("alt_bn128_pairing_product"),
            PrecompiledRegistrar::executor("alt_bn128_pairing_product"))});
    m_precompiledContract->insert({fillZero(9),
        make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("blake2_compression"),
            PrecompiledRegistrar::executor("blake2_compression"))});
    assert(m_precompiledContract);

    auto sysConfig = std::make_shared<precompiled::SystemConfigPrecompiled>(m_hashImpl);
    auto consensusPrecompiled = std::make_shared<precompiled::ConsensusPrecompiled>(m_hashImpl);
    auto tableManagerPrecompiled =
        std::make_shared<precompiled::TableManagerPrecompiled>(m_hashImpl);
    auto kvTablePrecompiled = std::make_shared<precompiled::KVTablePrecompiled>(m_hashImpl);
    auto tablePrecompiled = std::make_shared<precompiled::TablePrecompiled>(m_hashImpl);

    // in EVM
    m_constantPrecompiled->insert({SYS_CONFIG_ADDRESS, sysConfig});
    m_constantPrecompiled->insert({CONSENSUS_ADDRESS, consensusPrecompiled});
    m_constantPrecompiled->insert({TABLE_MANAGER_ADDRESS, tableManagerPrecompiled});
    m_constantPrecompiled->insert({KV_TABLE_ADDRESS, kvTablePrecompiled});
    m_constantPrecompiled->insert({TABLE_ADDRESS, tablePrecompiled});
    m_constantPrecompiled->insert(
        {DAG_TRANSFER_ADDRESS, std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl)});
    m_constantPrecompiled->insert(
        {CRYPTO_ADDRESS, std::make_shared<CryptoPrecompiled>(m_hashImpl)});
    m_constantPrecompiled->insert(
        {BFS_ADDRESS, std::make_shared<precompiled::BFSPrecompiled>(m_hashImpl)});
    /// auth precompiled
    if (m_isAuthCheck)
    {
        m_constantPrecompiled->insert({AUTH_MANAGER_ADDRESS,
            std::make_shared<precompiled::AuthManagerPrecompiled>(m_hashImpl)});
        m_constantPrecompiled->insert({AUTH_CONTRACT_MGR_ADDRESS,
            std::make_shared<precompiled::ContractAuthMgrPrecompiled>(m_hashImpl)});
    }

    // TODO: enable it
    //  m_constantPrecompiled->insert(
    //      {GROUP_SIG_ADDRESS, std::make_shared<precompiled::GroupSigPrecompiled>(m_hashImpl)});
    // m_constantPrecompiled->insert(
    //     {RING_SIG_ADDRESS, std::make_shared<precompiled::RingSigPrecompiled>(m_hashImpl)});

    CpuHeavyPrecompiled::registerPrecompiled(m_constantPrecompiled, m_hashImpl);
    storage::StorageInterface::Ptr storage = m_backendStorage;
    SmallBankPrecompiled::registerPrecompiled(storage, m_constantPrecompiled, m_hashImpl);
    DagTransferPrecompiled::createDagTable(storage);

    set<string> builtIn = {CRYPTO_ADDRESS, GROUP_SIG_ADDRESS, RING_SIG_ADDRESS};
    m_builtInPrecompiled = make_shared<set<string>>(builtIn);
}

void TransactionExecutor::initWasmEnvironment()
{
    m_schedule = BCOSWASMSchedule;

    m_builtInPrecompiled = std::make_shared<std::set<std::string>>();
    m_constantPrecompiled =
        std::make_shared<std::map<std::string, std::shared_ptr<precompiled::Precompiled>>>();

    auto sysConfig = std::make_shared<precompiled::SystemConfigPrecompiled>(m_hashImpl);
    auto consensusPrecompiled = std::make_shared<precompiled::ConsensusPrecompiled>(m_hashImpl);
    auto tableManagerPrecompiled =
        std::make_shared<precompiled::TableManagerPrecompiled>(m_hashImpl);
    auto kvTablePrecompiled = std::make_shared<precompiled::KVTablePrecompiled>(m_hashImpl);
    auto tablePrecompiled = std::make_shared<precompiled::TablePrecompiled>(m_hashImpl);

    // in WASM
    m_constantPrecompiled->insert({SYS_CONFIG_NAME, sysConfig});
    m_constantPrecompiled->insert({CONSENSUS_NAME, consensusPrecompiled});
    m_constantPrecompiled->insert({TABLE_MANAGER_NAME, tableManagerPrecompiled});
    m_constantPrecompiled->insert({KV_TABLE_NAME, kvTablePrecompiled});
    m_constantPrecompiled->insert({TABLE_NAME, tablePrecompiled});
    m_constantPrecompiled->insert(
        {DAG_TRANSFER_NAME, std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl)});
    m_constantPrecompiled->insert({CRYPTO_NAME, std::make_shared<CryptoPrecompiled>(m_hashImpl)});
    m_constantPrecompiled->insert(
        {BFS_NAME, std::make_shared<precompiled::BFSPrecompiled>(m_hashImpl)});

    // TODO: enable it after update wedpr
    // m_constantPrecompiled->insert(
    //     {GROUP_SIG_NAME, std::make_shared<precompiled::GroupSigPrecompiled>(m_hashImpl)});
    // m_constantPrecompiled->insert(
    //     {RING_SIG_NAME, std::make_shared<precompiled::RingSigPrecompiled>(m_hashImpl)});
    if (m_isAuthCheck)
    {
        m_constantPrecompiled->insert(
            {AUTH_MANAGER_NAME, std::make_shared<precompiled::AuthManagerPrecompiled>(m_hashImpl)});
        m_constantPrecompiled->insert({AUTH_CONTRACT_MGR_ADDRESS,
            std::make_shared<precompiled::ContractAuthMgrPrecompiled>(m_hashImpl)});
    }
    CpuHeavyPrecompiled::registerPrecompiled(m_constantPrecompiled, m_hashImpl);
    storage::StorageInterface::Ptr storage = m_backendStorage;
    SmallBankPrecompiled::registerPrecompiled(storage, m_constantPrecompiled, m_hashImpl);
    DagTransferPrecompiled::createDagTable(storage);

    set<string> builtIn = {CRYPTO_NAME, GROUP_SIG_NAME, RING_SIG_NAME};
    m_builtInPrecompiled = make_shared<set<string>>(builtIn);
    // create the zkp-precompiled
    m_constantPrecompiled->insert(
        {DISCRETE_ZKP_ADDRESS, std::make_shared<bcos::precompiled::ZkpPrecompiled>(m_hashImpl)});
}

void TransactionExecutor::initPrecompiledByBlockContext(std::shared_ptr<BlockContext> _blockContext)
{
    // TODO: use blockHash, version when call
    // call
    UpgradableGuard l(x_constantPrecompiled);
    if (_blockContext->hash() == bcos::crypto::HashType())
    {
        if (!m_constantPrecompiled->count(DISCRETE_ZKP_ADDRESS))
        {
            UpgradeGuard ul(l);
            m_constantPrecompiled->insert({DISCRETE_ZKP_ADDRESS,
                std::make_shared<bcos::precompiled::ZkpPrecompiled>(m_hashImpl)});
        }
        return;
    }
    // transaction: with version smaller than 3.0
    if (_blockContext->blockVersion() < (uint32_t)bcos::protocol::Version::V3_0_VERSION)
    {
        if (m_constantPrecompiled->count(DISCRETE_ZKP_ADDRESS))
        {
            UpgradeGuard ul(l);
            m_constantPrecompiled->erase(DISCRETE_ZKP_ADDRESS);
        }
        return;
    }
    // transaction: with version no more than 3.0
    if (!m_constantPrecompiled->count(DISCRETE_ZKP_ADDRESS))
    {
        UpgradeGuard ul(l);
        m_constantPrecompiled->insert({DISCRETE_ZKP_ADDRESS,
            std::make_shared<bcos::precompiled::ZkpPrecompiled>(m_hashImpl)});
    }
}

BlockContext::Ptr TransactionExecutor::createBlockContext(
    const protocol::BlockHeader::ConstPtr& currentHeader,
    storage::StateStorageInterface::Ptr storage)
{
    BlockContext::Ptr context = make_shared<BlockContext>(
        storage, m_hashImpl, currentHeader, m_schedule, m_isWasm, m_isAuthCheck);
    return context;
}

std::shared_ptr<BlockContext> TransactionExecutor::createBlockContext(
    bcos::protocol::BlockNumber blockNumber, h256 blockHash, uint64_t timestamp,
    int32_t blockVersion, storage::StateStorageInterface::Ptr storage)
{
    BlockContext::Ptr context = make_shared<BlockContext>(storage, m_hashImpl, blockNumber,
        blockHash, timestamp, blockVersion, m_schedule, m_isWasm, m_isAuthCheck);

    return context;
}


void TransactionExecutor::nextBlockHeader(int64_t schedulerTermId,
    const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
    std::function<void(bcos::Error::UniquePtr)> callback)
{
    m_schedulerTermId = schedulerTermId;

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(ERROR) << "TransactionExecutor is not running";
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"));
        return;
    }

    try
    {
        EXECUTOR_NAME_LOG(INFO) << BLOCK_NUMBER(blockHeader->number())
                                << "NextBlockHeader request: "
                                << LOG_KV("schedulerTermId", schedulerTermId);

        {
            std::unique_lock<std::shared_mutex> lock(m_stateStoragesMutex);
            bcos::storage::StateStorageInterface::Ptr stateStorage;
            if (m_stateStorages.empty())
            {
                if (m_cachedStorage)
                {
                    stateStorage = createStateStorage(m_cachedStorage);
                }
                else
                {
                    stateStorage = createStateStorage(m_backendStorage);
                }

                // check storage block Number
                auto storageBlockNumber = getBlockNumberInStorage();
                EXECUTOR_NAME_LOG(DEBUG) << "NextBlockHeader, executor load from backend storage, "
                                            "check storage blockNumber"
                                         << LOG_KV("storageBlockNumber", storageBlockNumber)
                                         << LOG_KV("requestBlockNumber", blockHeader->number());
                // Note: skip check for sys contract deploy
                if (blockHeader->number() - storageBlockNumber != 1 &&
                    !isSysContractDeploy(blockHeader->number()))
                {
                    auto fmt = boost::format(
                                   "[%s] Block number mismatch in storage! request: %d, current in "
                                   "storage: %d, trigger switch") %
                               m_name % blockHeader->number() % storageBlockNumber;
                    EXECUTOR_NAME_LOG(ERROR) << fmt;
                    // to trigger switch operation
                    callback(
                        BCOS_ERROR_UNIQUE_PTR(ExecuteError::SCHEDULER_TERM_ID_ERROR, fmt.str()));
                    return;
                }
            }
            else
            {
                auto& prev = m_stateStorages.back();

                // check number continuity
                if (blockHeader->number() - prev.number != 1)
                {
                    // m_stateStorages.pop_back();
                    auto fmt = boost::format(
                                   "[%s] Block number mismatch! request: %d, current: %d. trigger "
                                   "switch.") %
                               m_name % blockHeader->number() % prev.number;
                    EXECUTOR_NAME_LOG(ERROR) << fmt;
                    callback(
                        BCOS_ERROR_UNIQUE_PTR(ExecuteError::SCHEDULER_TERM_ID_ERROR, fmt.str()));
                    return;
                }

                prev.storage->setReadOnly(true);
                stateStorage = createStateStorage(prev.storage);
            }

            if (m_blockContext)
            {
                m_blockContext->clear();
            }

            // set last commit state storage to blockContext, to auth read last block state
            m_blockContext = createBlockContext(blockHeader, stateStorage);
            initPrecompiledByBlockContext(m_blockContext);
            m_stateStorages.emplace_back(blockHeader->number(), stateStorage);
        }

        EXECUTOR_NAME_LOG(INFO) <<BLOCK_NUMBER(blockHeader->number()) << "NextBlockHeader success"
                                << LOG_KV("number", blockHeader->number());
        callback(nullptr);
    }
    catch (std::exception& e)
    {
        EXECUTOR_NAME_LOG(ERROR) << "NextBlockHeader error: " << boost::diagnostic_information(e);

        callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "nextBlockHeader unknown error", e));
    }
}

void TransactionExecutor::dmcExecuteTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
        callback)
{
    EXECUTOR_NAME_LOG(TRACE) << "ExecuteTransaction request"
                             << LOG_KV("ContextID", input->contextID())
                             << LOG_KV("seq", input->seq()) << LOG_KV("message type", input->type())
                             << LOG_KV("to", input->to()) << LOG_KV("create", input->create());

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(ERROR) << "TransactionExecutor is not running";
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"),
            nullptr);
        return;
    }

    if (!m_blockContext)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(
                     ExecuteError::EXECUTE_ERROR, "Execute failed with empty blockContext!"),
            nullptr);
        return;
    }

    asyncExecute(m_blockContext, std::move(input), true,
        [this, callback = std::move(callback)](
            Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            if (error)
            {
                std::string errorMessage = "ExecuteTransaction failed: " + error->errorMessage();
                EXECUTOR_NAME_LOG(ERROR) << errorMessage;
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, errorMessage, *error), nullptr);
                return;
            }

            callback(std::move(error), std::move(result));
        });
}

void TransactionExecutor::dmcCall(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
        callback)
{
    EXECUTOR_NAME_LOG(TRACE) << "dmcCall request" << LOG_KV("ContextID", input->contextID())
                             << LOG_KV("seq", input->seq()) << LOG_KV("Message type", input->type())
                             << LOG_KV("To", input->to()) << LOG_KV("Create", input->create());

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(ERROR) << "TransactionExecutor is not running";
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"),
            nullptr);
        return;
    }

    BlockContext::Ptr blockContext;
    switch (input->type())
    {
    case protocol::ExecutionMessage::MESSAGE:
    {
        bcos::protocol::BlockNumber number = m_lastCommittedBlockNumber;
        storage::StorageInterface::Ptr prev;

        if (m_cachedStorage)
        {
            prev = m_cachedStorage;
        }
        else
        {
            prev = m_backendStorage;
        }

        // Create a temp storage
        auto storage = createStateStorage(std::move(prev));

        // Create a temp block context
        // TODO: pass blockHash, version here
        blockContext = createBlockContext(
            number, h256(), 0, 0, std::move(storage));  // TODO: complete the block info
        initPrecompiledByBlockContext(blockContext);

        auto inserted = m_calledContext->emplace(
            std::tuple{input->contextID(), input->seq()}, CallState{blockContext});

        if (!inserted)
        {
            auto message = "dmcCall error, contextID: " +
                           boost::lexical_cast<std::string>(input->contextID()) +
                           " seq: " + boost::lexical_cast<std::string>(input->seq()) + " exists";
            EXECUTOR_NAME_LOG(ERROR) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
            return;
        }

        break;
    }
    case protocol::ExecutionMessage::FINISHED:
    case protocol::ExecutionMessage::REVERT:
    {
        tbb::concurrent_hash_map<std::tuple<int64_t, int64_t>, CallState, HashCombine>::accessor it;
        m_calledContext->find(it, std::tuple{input->contextID(), input->seq()});

        if (it.empty())
        {
            auto message = "dmcCall error, contextID: " +
                           boost::lexical_cast<std::string>(input->contextID()) +
                           " seq: " + boost::lexical_cast<std::string>(input->seq()) +
                           " does not exists";
            EXECUTOR_NAME_LOG(ERROR) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
            return;
        }

        blockContext = it->second.blockContext;

        break;
    }
    default:
    {
        auto message =
            "dmcCall error, Unknown call type: " + boost::lexical_cast<std::string>(input->type());
        EXECUTOR_NAME_LOG(ERROR) << message;
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
        return;

        break;
    }
    }

    asyncExecute(std::move(blockContext), std::move(input), true,
        [this, callback = std::move(callback)](
            Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            if (error)
            {
                std::string errorMessage = "Call failed: " + error->errorMessage();
                EXECUTOR_NAME_LOG(WARNING)
                    << LOG_DESC("Call error") << LOG_KV("code", error->errorCode())
                    << LOG_KV("msg", error->errorMessage());
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, errorMessage, *error), nullptr);
                return;
            }

            if (result->type() == protocol::ExecutionMessage::FINISHED ||
                result->type() == protocol::ExecutionMessage::REVERT)
            {
                auto erased =
                    m_calledContext->erase(std::tuple{result->contextID(), result->seq()});

                if (!erased)
                {
                    auto message = "dmcCall error, erase contextID: " +
                                   boost::lexical_cast<std::string>(result->contextID()) +
                                   " seq: " + boost::lexical_cast<std::string>(result->seq()) +
                                   " does not exists";
                    EXECUTOR_NAME_LOG(ERROR) << message;

                    callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
                    return;
                }
            }

            EXECUTOR_NAME_LOG(TRACE)
                << "dmcCall success" << LOG_KV("staticCall", result->staticCall())
                << LOG_KV("from", result->from()) << LOG_KV("to", result->to())
                << LOG_KV("context", result->contextID());
            callback(std::move(error), std::move(result));
        });
}

void TransactionExecutor::executeTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
        callback)
{
    EXECUTOR_NAME_LOG(TRACE) << "ExecuteTransaction request"
                             << LOG_KV("ContextID", input->contextID())
                             << LOG_KV("seq", input->seq()) << LOG_KV("message type", input->type())
                             << LOG_KV("to", input->to()) << LOG_KV("create", input->create());

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(ERROR) << "TransactionExecutor is not running";
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"),
            nullptr);
        return;
    }

    if (!m_blockContext)
    {
        callback(BCOS_ERROR_UNIQUE_PTR(
                     ExecuteError::EXECUTE_ERROR, "Execute failed with empty blockContext!"),
            nullptr);
        return;
    }

    asyncExecute(m_blockContext, std::move(input), false,
        [this, callback = std::move(callback)](
            Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            if (error)
            {
                std::string errorMessage = "ExecuteTransaction failed: " + error->errorMessage();
                EXECUTOR_NAME_LOG(ERROR) << errorMessage;
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, errorMessage, *error), nullptr);
                return;
            }

            callback(std::move(error), std::move(result));
        });
}

void TransactionExecutor::call(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
        callback)
{
    EXECUTOR_NAME_LOG(TRACE) << "Call request" << LOG_KV("ContextID", input->contextID())
                             << LOG_KV("seq", input->seq()) << LOG_KV("Message type", input->type())
                             << LOG_KV("To", input->to()) << LOG_KV("Create", input->create());

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(ERROR) << "TransactionExecutor is not running";
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"),
            nullptr);
        return;
    }

    BlockContext::Ptr blockContext;
    switch (input->type())
    {
    case protocol::ExecutionMessage::MESSAGE:
    {
        bcos::protocol::BlockNumber number = m_lastCommittedBlockNumber;
        storage::StorageInterface::Ptr prev;

        if (m_cachedStorage)
        {
            prev = m_cachedStorage;
        }
        else
        {
            prev = m_backendStorage;
        }

        // Create a temp storage
        auto storage = createStateStorage(std::move(prev), true);

        // Create a temp block context
        // TODO: pass blockHash, version here
        blockContext = createBlockContext(
            number, h256(), 0, 0, std::move(storage));  // TODO: complete the block info
        initPrecompiledByBlockContext(blockContext);

        auto inserted = m_calledContext->emplace(
            std::tuple{input->contextID(), input->seq()}, CallState{blockContext});

        if (!inserted)
        {
            auto message =
                "Call error, contextID: " + boost::lexical_cast<std::string>(input->contextID()) +
                " seq: " + boost::lexical_cast<std::string>(input->seq()) + " exists";
            EXECUTOR_NAME_LOG(ERROR) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
            return;
        }

        break;
    }
    case protocol::ExecutionMessage::FINISHED:
    case protocol::ExecutionMessage::REVERT:
    {
        tbb::concurrent_hash_map<std::tuple<int64_t, int64_t>, CallState, HashCombine>::accessor it;
        m_calledContext->find(it, std::tuple{input->contextID(), input->seq()});

        if (it.empty())
        {
            auto message =
                "Call error, contextID: " + boost::lexical_cast<std::string>(input->contextID()) +
                " seq: " + boost::lexical_cast<std::string>(input->seq()) + " does not exists";
            EXECUTOR_NAME_LOG(ERROR) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
            return;
        }

        blockContext = it->second.blockContext;

        break;
    }
    default:
    {
        auto message =
            "Call error, Unknown call type: " + boost::lexical_cast<std::string>(input->type());
        EXECUTOR_NAME_LOG(ERROR) << message;
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
        return;

        break;
    }
    }

    asyncExecute(std::move(blockContext), std::move(input), false,
        [this, callback = std::move(callback)](
            Error::UniquePtr&& error, bcos::protocol::ExecutionMessage::UniquePtr&& result) {
            if (error)
            {
                std::string errorMessage = "Call failed: " + error->errorMessage();
                EXECUTOR_NAME_LOG(WARNING)
                    << LOG_DESC("Call error") << LOG_KV("code", error->errorCode())
                    << LOG_KV("msg", error->errorMessage());
                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, errorMessage, *error), nullptr);
                return;
            }

            if (result->type() == protocol::ExecutionMessage::FINISHED ||
                result->type() == protocol::ExecutionMessage::REVERT)
            {
                auto erased =
                    m_calledContext->erase(std::tuple{result->contextID(), result->seq()});

                if (!erased)
                {
                    auto message = "Call error, erase contextID: " +
                                   boost::lexical_cast<std::string>(result->contextID()) +
                                   " seq: " + boost::lexical_cast<std::string>(result->seq()) +
                                   " does not exists";
                    EXECUTOR_NAME_LOG(ERROR) << message;

                    callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
                    return;
                }
            }

            EXECUTOR_NAME_LOG(TRACE) << "Call success" << LOG_KV("staticCall", result->staticCall())
                                     << LOG_KV("from", result->from()) << LOG_KV("to", result->to())
                                     << LOG_KV("context", result->contextID());
            callback(std::move(error), std::move(result));
        });
}

void TransactionExecutor::executeTransactionsInternal(std::string contractAddress,
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs, bool useCoroutine,
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        _callback)
{
    if (!m_blockContext)
    {
        _callback(BCOS_ERROR_UNIQUE_PTR(
                      ExecuteError::EXECUTE_ERROR, "Execute failed with empty blockContext!"),
            {});
        return;
    }

    auto requestTimestamp = utcTime();
    auto txNum = inputs.size();
    auto blockNumber = m_blockContext->number();
    EXECUTOR_NAME_LOG(INFO) << BLOCK_NUMBER(blockNumber) << "executeTransactionsInternal request"
                            << LOG_KV("useCoroutine", useCoroutine) << LOG_KV("txNum", txNum)
                            << LOG_KV("contractAddress", contractAddress)
                            << LOG_KV("requestTimestamp", requestTimestamp);

    auto callback = [this, useCoroutine, _callback = _callback, requestTimestamp, blockNumber,
                        txNum, contractAddress](bcos::Error::UniquePtr error,
                        std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs) {
        EXECUTOR_NAME_LOG(DEBUG) << BLOCK_NUMBER(blockNumber)
                                 << "executeTransactionsInternal response"
                                 << LOG_KV("useCoroutine", useCoroutine) << LOG_KV("txNum", txNum)
                                 << LOG_KV("outputNum", outputs.size())
                                 << LOG_KV("contractAddress", contractAddress)
                                 << LOG_KV("requestTimestamp", requestTimestamp)
                                 << LOG_KV("msg", error ? error->errorMessage() : "ok")
                                 << LOG_KV("timeCost", utcTime() - requestTimestamp);
        _callback(std::move(error), std::move(outputs));
    };

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(ERROR) << "TransactionExecutor is not running";
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"), {});
        return;
    }

    auto recoredT = utcTime();
    auto startT = utcTime();
    // for fill block
    auto txHashes = make_shared<HashList>();
    std::vector<decltype(inputs)::size_type> indexes;
    auto fillInputs = std::make_shared<std::vector<bcos::protocol::ExecutionMessage::UniquePtr>>();

    // final result
    auto callParametersList =
        std::make_shared<std::vector<CallParameters::UniquePtr>>(inputs.size());

    bool isStaticCall = true;
#pragma omp parallel for
    for (auto i = 0u; i < inputs.size(); ++i)
    {
        auto& params = inputs[i];

        if (!params->staticCall())
        {
            isStaticCall = false;
        }

        switch (params->type())
        {
        case ExecutionMessage::TXHASH:
        {
#pragma omp critical
            {
                txHashes->emplace_back(params->transactionHash());
                indexes.emplace_back(i);
                fillInputs->emplace_back(std::move(params));
            }

            break;
        }
        case ExecutionMessage::MESSAGE:
        case bcos::protocol::ExecutionMessage::REVERT:
        case bcos::protocol::ExecutionMessage::FINISHED:
        case bcos::protocol::ExecutionMessage::KEY_LOCK:
        {
            callParametersList->at(i) = createCallParameters(*params, params->staticCall());
            break;
        }
        default:
        {
            auto message = (boost::format("Unsupported message type: %d") % params->type()).str();
            EXECUTOR_NAME_LOG(ERROR)
                << BLOCK_NUMBER(blockNumber) << "DAG Execute error, " << message;
            // callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::DAG_ERROR, message), {});
            break;
        }
        }
    }

    if (isStaticCall)
    {
        EXECUTOR_NAME_LOG(FATAL)
            << "executeTransactionsInternal() only handle non static transactions but "
               "receive static call";
        assert(false);
    }

    auto prepareT = utcTime() - startT;
    startT = utcTime();

    if (!txHashes->empty())
    {
        m_txpool->asyncFillBlock(txHashes,
            [this, startT, useCoroutine, contractAddress, indexes = std::move(indexes),
                fillInputs = std::move(fillInputs),
                callParametersList = std::move(callParametersList), callback = std::move(callback),
                txHashes,
                blockNumber](Error::Ptr error, protocol::TransactionsPtr transactions) mutable {
                auto fillTxsT = (utcTime() - startT);

                if (!m_isRunning)
                {
                    callback(BCOS_ERROR_UNIQUE_PTR(
                                 ExecuteError::STOPPED, "TransactionExecutor is not running"),
                        {});
                    return;
                }


                if (error)
                {
                    auto errorMessage = "[" + m_name + "] asyncFillBlock failed";
                    EXECUTOR_NAME_LOG(ERROR)
                        << BLOCK_NUMBER(blockNumber) << errorMessage << error->errorMessage();
                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                 ExecuteError::DAG_ERROR, errorMessage, *error),
                        {});
                    return;
                }
                auto recordT = utcTime();
#pragma omp parallel for
                for (size_t i = 0; i < transactions->size(); ++i)
                {
                    assert(transactions->at(i));
                    callParametersList->at(indexes[i]) =
                        createCallParameters(*fillInputs->at(i), *transactions->at(i));
                }
                auto prepareT = utcTime() - recordT;
                recordT = utcTime();

                auto executiveFlow =
                    getExecutiveFlow(m_blockContext, contractAddress, useCoroutine);
                executiveFlow->submit(callParametersList);

                asyncExecuteExecutiveFlow(executiveFlow,
                    [callback = std::move(callback)](bcos::Error::UniquePtr&& error,
                        std::vector<bcos::protocol::ExecutionMessage::UniquePtr>&& messages) {
                        callback(std::move(error), std::move(messages));
                    });

                EXECUTOR_NAME_LOG(INFO)
                    << BLOCK_NUMBER(blockNumber)
                    << LOG_DESC("executeTransactionsInternal after fillblock")
                    << LOG_KV("useCoroutine", useCoroutine) << LOG_KV("fillTxsT", fillTxsT)
                    << LOG_KV("prepareT", prepareT) << LOG_KV("dmcT", (utcTime() - recordT));
            });
    }
    else
    {
        auto executiveFlow = getExecutiveFlow(m_blockContext, contractAddress, useCoroutine);
        executiveFlow->submit(callParametersList);

        asyncExecuteExecutiveFlow(executiveFlow,
            [callback = std::move(callback)](bcos::Error::UniquePtr&& error,
                std::vector<bcos::protocol::ExecutionMessage::UniquePtr>&& messages) {
                callback(std::move(error), std::move(messages));
            });
    }

    EXECUTOR_NAME_LOG(TRACE) << LOG_DESC("executeTransactionsInternal")
                             << LOG_KV("useCoroutine", useCoroutine) << LOG_KV("prepareT", prepareT)
                             << LOG_KV("total", (utcTime() - recoredT));
}

void TransactionExecutor::executeTransactions(std::string contractAddress,
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        _callback)
{
    executeTransactionsInternal(
        std::move(contractAddress), std::move(inputs), false, std::move(_callback));
}

void TransactionExecutor::dmcExecuteTransactions(std::string contractAddress,
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        _callback)
{
    executeTransactionsInternal(
        std::move(contractAddress), std::move(inputs), true, std::move(_callback));
}

void TransactionExecutor::getHash(bcos::protocol::BlockNumber number,
    std::function<void(bcos::Error::UniquePtr, crypto::HashType)> callback)
{
    EXECUTOR_NAME_LOG(INFO) << BLOCK_NUMBER(number) << "GetTableHashes";

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(ERROR) << "TransactionExecutor is not running";
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"), {});
        return;
    }

    if (m_stateStorages.empty())
    {
        EXECUTOR_NAME_LOG(ERROR) << "GetTableHashes error: No uncommitted state";
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::GETHASH_ERROR, "No uncommitted state"),
            crypto::HashType());
        return;
    }

    auto& last = m_stateStorages.back();
    if (last.number != number)
    {
        auto errorMessage =
            "GetTableHashes error: Request blockNumber: " +
            boost::lexical_cast<std::string>(number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(last.number);

        EXECUTOR_NAME_LOG(ERROR) << errorMessage;
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::GETHASH_ERROR, errorMessage), crypto::HashType());

        return;
    }

    auto hash = last.storage->hash(m_hashImpl);
    EXECUTOR_NAME_LOG(INFO) << BLOCK_NUMBER(number) << "GetTableHashes success"
                            << LOG_KV("hash", hash.hex());

    callback(nullptr, std::move(hash));
}

void TransactionExecutor::dagExecuteTransactions(
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        _callback)
{
    if (!m_blockContext)
    {
        _callback(BCOS_ERROR_UNIQUE_PTR(
                      ExecuteError::EXECUTE_ERROR, "Execute failed with empty blockContext!"),
            {});
        return;
    }

    auto requestTimestamp = utcTime();
    auto txNum = inputs.size();
    auto blockNumber = m_blockContext->number();
    EXECUTOR_NAME_LOG(INFO) << "dagExecuteTransactions request"
                            << LOG_KV("blockNumber", blockNumber) << LOG_KV("txNum", txNum)
                            << LOG_KV("requestTimestamp", requestTimestamp);

    auto callback = [this, _callback = _callback, requestTimestamp, blockNumber, txNum](
                        bcos::Error::UniquePtr error,
                        std::vector<bcos::protocol::ExecutionMessage::UniquePtr> outputs) {
        EXECUTOR_NAME_LOG(DEBUG) << "dagExecuteTransactions response"
                                 << LOG_KV("blockNumber", blockNumber) << LOG_KV("txNum", txNum)
                                 << LOG_KV("outputNum", outputs.size())
                                 << LOG_KV("requestTimestamp", requestTimestamp)
                                 << LOG_KV("errorMessage", error ? error->errorMessage() : "ok")
                                 << LOG_KV("timeCost", utcTime() - requestTimestamp);
        _callback(std::move(error), std::move(outputs));
    };


    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(ERROR) << "TransactionExecutor is not running";
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"), {});
        return;
    }

    auto recoredT = utcTime();
    auto startT = utcTime();
    // for fill block
    auto txHashes = make_shared<HashList>();
    std::vector<size_t> indexes;
    auto fillInputs = std::make_shared<std::vector<bcos::protocol::ExecutionMessage::UniquePtr>>();

    // final result
    auto callParametersList =
        std::make_shared<std::vector<CallParameters::UniquePtr>>(inputs.size());

#pragma omp parallel for
    for (auto i = 0u; i < inputs.size(); ++i)
    {
        auto& params = inputs[i];
        switch (params->type())
        {
        case ExecutionMessage::TXHASH:
        {
#pragma omp critical
            {
                txHashes->emplace_back(params->transactionHash());
                indexes.emplace_back(i);
                fillInputs->emplace_back(std::move(params));
            }

            break;
        }
        case ExecutionMessage::MESSAGE:
        {
            callParametersList->at(i) = createCallParameters(*params, false);
            break;
        }
        default:
        {
            auto message = (boost::format("Unsupported message type: %d") % params->type()).str();
            EXECUTOR_NAME_LOG(ERROR) << "DAG Execute error, " << message;
            // callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::DAG_ERROR, message), {});
            break;
        }
        }
    }
    auto prepareT = utcTime() - startT;
    startT = utcTime();
    if (!txHashes->empty())
    {
        m_txpool->asyncFillBlock(txHashes,
            [this, startT, indexes = std::move(indexes), fillInputs = std::move(fillInputs),
                callParametersList = std::move(callParametersList), callback = std::move(callback),
                txHashes,
                blockNumber](Error::Ptr error, protocol::TransactionsPtr transactions) mutable {
                auto fillTxsT = utcTime() - startT;

                if (!m_isRunning)
                {
                    callback(BCOS_ERROR_UNIQUE_PTR(
                                 ExecuteError::STOPPED, "TransactionExecutor is not running"),
                        {});
                    return;
                }

                if (error)
                {
                    auto errorMessage = "[" + m_name + "] asyncFillBlock failed";
                    EXECUTOR_NAME_LOG(ERROR)
                        << BLOCK_NUMBER(blockNumber) << errorMessage << error->errorMessage();
                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                 ExecuteError::DAG_ERROR, errorMessage, *error),
                        {});
                    return;
                }
                auto recordT = utcTime();
#pragma omp parallel for
                for (size_t i = 0; i < transactions->size(); ++i)
                {
                    assert(transactions->at(i));
                    callParametersList->at(indexes[i]) =
                        createCallParameters(*fillInputs->at(i), *transactions->at(i));
                }
                auto prepareT = utcTime() - recordT;
                recordT = utcTime();
                dagExecuteTransactionsInternal(*callParametersList, std::move(callback));

                EXECUTOR_NAME_LOG(INFO)
                    << LOG_DESC("dagExecuteTransactionsInternal after fillblock")
                    << LOG_KV("fillTxsT", fillTxsT) << LOG_KV("prepareT", prepareT)
                    << LOG_KV("dagT", (utcTime() - recordT));
            });
    }
    else
    {
        dagExecuteTransactionsInternal(*callParametersList, std::move(callback));
    }

    EXECUTOR_NAME_LOG(INFO) << LOG_DESC("dagExecuteTransactions") << LOG_KV("prepareT", prepareT)
                            << LOG_KV("total", (utcTime() - recoredT))
                            << LOG_KV("inputSize", inputs.size());
}

bytes getComponentBytes(size_t index, const std::string& typeName, const bytesConstRef& data)
{
    size_t indexOffset = index * 32;
    auto header = bytes(data.begin() + indexOffset, data.begin() + indexOffset + 32);
    if (typeName == "string" || typeName == "bytes")
    {
        u256 u = fromBigEndian<u256>(header);
        auto offset = static_cast<std::size_t>(u);
        auto rawData = data.getCroppedData(offset);
        auto len = static_cast<std::size_t>(
            fromBigEndian<u256>(bytes(rawData.begin(), rawData.begin() + 32)));
        return bytes(rawData.begin() + 32, rawData.begin() + 32 + static_cast<std::size_t>(len));
    }
    return header;
}

std::shared_ptr<std::vector<bytes>> TransactionExecutor::extractConflictFields(
    const FunctionAbi& functionAbi, const CallParameters& params,
    std::shared_ptr<BlockContext> _blockContext)
{
    if (functionAbi.conflictFields.empty())
    {
        EXECUTOR_NAME_LOG(TRACE) << LOG_BADGE("extractConflictFields")
                                 << LOG_DESC("conflictFields is empty")
                                 << LOG_KV("address", params.senderAddress)
                                 << LOG_KV("functionName", functionAbi.name);
        return nullptr;
    }

    const auto& to = params.receiveAddress;
    auto hasher = boost::hash<string_view>();
    auto toHash = hasher(to);

    auto conflictFields = make_shared<vector<bytes>>();

    for (auto& conflictField : functionAbi.conflictFields)
    {
        auto criticalKey = bytes();

        size_t slot = toHash;
        if (conflictField.slot.has_value())
        {
            slot += static_cast<size_t>(conflictField.slot.value());
        }
        criticalKey.insert(criticalKey.end(), (uint8_t*)&slot, (uint8_t*)&slot + sizeof(slot));
        EXECUTOR_NAME_LOG(TRACE) << LOG_BADGE("extractConflictFields") << LOG_KV("to", to)
                                 << LOG_KV("functionName", functionAbi.name)
                                 << LOG_KV("addressHash", toHash) << LOG_KV("slot", slot);

        switch (conflictField.kind)
        {
        case All:
        {
            EXECUTOR_NAME_LOG(TRACE) << LOG_BADGE("extractConflictFields") << LOG_DESC("use `All`");
            return nullptr;
        }
        case Len:
        {
            EXECUTOR_NAME_LOG(TRACE) << LOG_BADGE("extractConflictFields") << LOG_DESC("use `Len`");
            break;
        }
        case Env:
        {
            assert(conflictField.value.size() == 1);

            auto envKind = conflictField.value[0];
            switch (envKind)
            {
            case EnvKind::Caller:
            {
                const auto& sender = params.senderAddress;
                criticalKey.insert(criticalKey.end(), sender.begin(), sender.end());

                EXECUTOR_NAME_LOG(TRACE) << LOG_BADGE("extractConflictFields")
                                         << LOG_DESC("use `Caller`") << LOG_KV("caller", sender);
                break;
            }
            case EnvKind::Origin:
            {
                const auto& sender = params.origin;
                criticalKey.insert(criticalKey.end(), sender.begin(), sender.end());

                EXECUTOR_NAME_LOG(TRACE) << LOG_BADGE("extractConflictFields")
                                         << LOG_DESC("use `Origin`") << LOG_KV("origin", sender);
                break;
            }
            case EnvKind::Now:
            {
                auto now = _blockContext->timestamp();
                auto bytes = static_cast<bcos::byte*>(static_cast<void*>(&now));
                criticalKey.insert(criticalKey.end(), bytes, bytes + sizeof(now));

                EXECUTOR_NAME_LOG(TRACE) << LOG_BADGE("extractConflictFields")
                                         << LOG_DESC("use `Now`") << LOG_KV("now", now);
                break;
            }
            case EnvKind::BlockNumber:
            {
                auto blockNumber = _blockContext->number();
                auto bytes = static_cast<bcos::byte*>(static_cast<void*>(&blockNumber));
                criticalKey.insert(criticalKey.end(), bytes, bytes + sizeof(blockNumber));

                EXECUTOR_NAME_LOG(DEBUG)
                    << LOG_BADGE("extractConflictFields") << LOG_DESC("use `BlockNumber`")
                    << LOG_KV("functionName", functionAbi.name)
                    << LOG_KV("blockNumber", blockNumber);
                break;
            }
            case EnvKind::Addr:
            {
                criticalKey.insert(criticalKey.end(), to.begin(), to.end());

                EXECUTOR_NAME_LOG(DEBUG) << LOG_BADGE("extractConflictFields")
                                         << LOG_DESC("use `Addr`") << LOG_KV("addr", to);
                break;
            }
            default:
            {
                EXECUTOR_NAME_LOG(ERROR) << LOG_BADGE("unknown env kind in conflict field")
                                         << LOG_KV("envKind", envKind);
                return nullptr;
            }
            }
            break;
        }
        case Params:
        {
            assert(!conflictField.value.empty());
            const ParameterAbi* paramAbi = nullptr;
            auto components = &functionAbi.inputs;
            auto inputData = ref(params.data).getCroppedData(4).toBytes();
            if (_blockContext->isWasm())
            {
                auto startPos = 0u;
                for (auto segment : conflictField.value)
                {
                    if (segment >= components->size())
                    {
                        return nullptr;
                    }

                    for (auto i = 0u; i < segment; ++i)
                    {
                        auto length = scaleEncodingLength(components->at(i), inputData, startPos);
                        if (!length.has_value())
                        {
                            return nullptr;
                        }
                        startPos += length.value();
                    }
                    paramAbi = &components->at(segment);
                    components = &paramAbi->components;
                }
                auto length = scaleEncodingLength(*paramAbi, inputData, startPos);
                if (!length.has_value())
                {
                    return nullptr;
                }
                assert(startPos + length.value() <= inputData.size());
                bytes var(
                    inputData.begin() + startPos, inputData.begin() + startPos + length.value());
                criticalKey.insert(criticalKey.end(), var.begin(), var.end());
            }
            else
            {  // evm
                auto index = conflictField.value[0];
                auto typeName = functionAbi.flatInputs[index];
                if (typeName.empty())
                {
                    return nullptr;
                }
                auto out = getComponentBytes(index, typeName, ref(params.data).getCroppedData(4));
                criticalKey.insert(criticalKey.end(), out.begin(), out.end());
            }

            EXECUTOR_NAME_LOG(DEBUG)
                << LOG_BADGE("extractConflictFields") << LOG_DESC("use `Params`")
                << LOG_KV("functionName", functionAbi.name)
                << LOG_KV("criticalKey", toHexStringWithPrefix(criticalKey));
            break;
        }
        case Const:
        {
            criticalKey.insert(
                criticalKey.end(), conflictField.value.begin(), conflictField.value.end());
            EXECUTOR_NAME_LOG(DEBUG)
                << LOG_BADGE("extractConflictFields") << LOG_DESC("use `Const`")
                << LOG_KV("functionName", functionAbi.name)
                << LOG_KV("criticalKey", toHexStringWithPrefix(criticalKey));
            break;
        }
        case None:
        {
            EXECUTOR_NAME_LOG(DEBUG) << LOG_BADGE("extractConflictFields") << LOG_DESC("use `None`")
                                     << LOG_KV("functionName", functionAbi.name)
                                     << LOG_KV("criticalKey", toHexStringWithPrefix(criticalKey));
            break;
        }
        default:
        {
            EXECUTOR_NAME_LOG(ERROR) << LOG_BADGE("unknown conflict field kind")
                                     << LOG_KV("conflictFieldKind", conflictField.kind);
            return nullptr;
        }
        }

        conflictFields->emplace_back(std::move(criticalKey));
    }
    return conflictFields;
}

void TransactionExecutor::dagExecuteTransactionsInternal(
    gsl::span<std::unique_ptr<CallParameters>> inputs,
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        callback)
{
    auto recordT = utcTime();
    auto startT = utcTime();

    auto transactionsNum = inputs.size();
    auto executionResults = vector<ExecutionMessage::UniquePtr>(transactionsNum);

    CriticalFields::Ptr txsCriticals = make_shared<CriticalFields>(transactionsNum);

    mutex tableMutex;

    // parallel to extract critical fields
    tbb::parallel_for(tbb::blocked_range<uint64_t>(0, transactionsNum),
        [&](const tbb::blocked_range<uint64_t>& range) {
            try
            {
                for (auto i = range.begin(); i != range.end(); ++i)
                {
                    auto defaultExecutionResult =
                        m_executionMessageFactory->createExecutionMessage();
                    executionResults[i].swap(defaultExecutionResult);

                    const auto& params = inputs[i];

                    auto to = params->receiveAddress;
                    const auto& input = params->data;

                    if (params->create)
                    {
                        executionResults[i] = toExecutionResult(std::move(inputs[i]));
                        executionResults[i]->setType(ExecutionMessage::SEND_BACK);
                        continue;
                    }
                    CriticalFields::CriticalFieldPtr conflictFields = nullptr;
                    auto selector = ref(input).getCroppedData(0, 4);
                    auto abiKey = bytes(to.cbegin(), to.cend());
                    abiKey.insert(abiKey.end(), selector.begin(), selector.end());
                    // if precompiled
                    auto executive = createExecutive(
                        m_blockContext, params->codeAddress, params->contextID, params->seq);
                    auto p = executive->getPrecompiled(params->receiveAddress);
                    if (p)
                    {
                        // Precompile transaction
                        if (p->isParallelPrecompiled())
                        {
                            auto criticals =
                                vector<string>(p->getParallelTag(ref(params->data), m_isWasm));
                            conflictFields = make_shared<vector<bytes>>();
                            for (string& critical : criticals)
                            {
                                critical += params->receiveAddress;
                                conflictFields->push_back(bytes((uint8_t*)critical.data(),
                                    (uint8_t*)critical.data() + critical.size()));
                            }
                        }
                        else
                        {
                            // Note: must be sure that the log accessed data should be valid always
                            EXECUTOR_NAME_LOG(DEBUG)
                                << LOG_BADGE("dagExecuteTransactionsInternal")
                                << LOG_DESC("the precompiled can't be parallel")
                                << LOG_KV("address", to);
                            executionResults[i] = toExecutionResult(std::move(inputs[i]));
                            executionResults[i]->setType(ExecutionMessage::SEND_BACK);
                            continue;
                        }
                    }
                    else
                    {
                        auto cacheHandle = m_abiCache->lookup(abiKey);
                        // find FunctionAbi in cache first
                        if (!cacheHandle.isValid())
                        {
                            EXECUTOR_NAME_LOG(TRACE)
                                << LOG_BADGE("dagExecuteTransactionsInternal")
                                << LOG_DESC("No ABI found in cache, try to load")
                                << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));

                            std::lock_guard guard(tableMutex);

                            cacheHandle = m_abiCache->lookup(abiKey);
                            if (cacheHandle.isValid())
                            {
                                EXECUTOR_NAME_LOG(TRACE)
                                    << LOG_BADGE("dagExecuteTransactionsInternal")
                                    << LOG_DESC("ABI had been loaded by other workers")
                                    << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));
                                auto& functionAbi = cacheHandle.value();
                                conflictFields =
                                    extractConflictFields(functionAbi, *params, m_blockContext);
                            }
                            else
                            {
                                auto storage = m_blockContext->storage();

                                auto tableName = "/apps/" + string(to);

                                auto table = storage->openTable(tableName);
                                if (!table.has_value())
                                {
                                    executionResults[i] = toExecutionResult(std::move(inputs[i]));
                                    executionResults[i]->setType(ExecutionMessage::REVERT);
                                    EXECUTOR_NAME_LOG(INFO)
                                        << LOG_BADGE("dagExecuteTransactionsInternal")
                                        << LOG_DESC("No ABI found, please deploy first")
                                        << LOG_KV("tableName", tableName);
                                    continue;
                                }
                                // get abi json
                                auto entry = table->getRow(ACCOUNT_ABI);
                                auto abiStr = entry->getField(0);
                                bool isSmCrypto =
                                    m_hashImpl->getHashImplType() == crypto::HashImplType::Sm3Hash;

                                EXECUTOR_NAME_LOG(TRACE)
                                    << LOG_BADGE("dagExecuteTransactionsInternal")
                                    << LOG_DESC("ABI loaded") << LOG_KV("address", to)
                                    << LOG_KV("selector", toHexString(selector))
                                    << LOG_KV("ABI", abiStr);
                                auto functionAbi = FunctionAbi::deserialize(
                                    abiStr, selector.toBytes(), isSmCrypto);
                                if (!functionAbi)
                                {
                                    EXECUTOR_NAME_LOG(DEBUG)
                                        << LOG_BADGE("dagExecuteTransactionsInternal")
                                        << LOG_DESC("ABI deserialize failed")
                                        << LOG_KV("address", to) << LOG_KV("ABI", abiStr);
                                    executionResults[i] = toExecutionResult(std::move(inputs[i]));
                                    executionResults[i]->setType(ExecutionMessage::SEND_BACK);
                                    // If abi is not valid, we don't impact the cache. In such a
                                    // situation, if the caller invokes this method over and
                                    // over again, executor will read the contract table
                                    // repeatedly, which may cause performance loss. But we
                                    // think occurrence of invalid abi is impossible in actual
                                    // situations.
                                    continue;
                                }

                                auto abiPtr = functionAbi.get();
                                if (m_abiCache->insert(abiKey, abiPtr, &cacheHandle))
                                {
                                    // If abi object had been inserted into the cache
                                    // successfully, the cache will take charge of life time
                                    // management of the object. After this object being
                                    // eliminated, the cache will delete its memory storage.
                                    std::ignore = functionAbi.release();
                                }
                                conflictFields =
                                    extractConflictFields(*abiPtr, *params, m_blockContext);
                            }
                        }
                        else
                        {
                            EXECUTOR_NAME_LOG(DEBUG)
                                << LOG_BADGE("dagExecuteTransactionsInternal")
                                << LOG_DESC("Found ABI in cache") << LOG_KV("address", to)
                                << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));
                            auto& functionAbi = cacheHandle.value();
                            conflictFields =
                                extractConflictFields(functionAbi, *params, m_blockContext);
                        }
                    }
                    if (conflictFields == nullptr)
                    {
                        EXECUTOR_NAME_LOG(DEBUG)
                            << LOG_BADGE("dagExecuteTransactionsInternal")
                            << LOG_DESC("The transaction can't be executed concurrently")
                            << LOG_KV("address", to)
                            << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));
                        executionResults[i] = toExecutionResult(std::move(inputs[i]));
                        executionResults[i]->setType(ExecutionMessage::SEND_BACK);
                        continue;
                    }
                    txsCriticals->put(i, std::move(conflictFields));
                }
            }
            catch (exception& e)
            {
                EXECUTOR_NAME_LOG(ERROR) << LOG_BADGE("dagExecuteTransactionsInternal")
                                         << LOG_DESC("Error during parallel extractConflictFields")
                                         << LOG_KV("EINFO", boost::diagnostic_information(e));
                BOOST_THROW_EXCEPTION(
                    BCOS_ERROR_WITH_PREV(-1, "Error while extractConflictFields", e));
            }
        });
    auto dagInitT = utcTime() - startT;
    startT = utcTime();
    // DAG run
    try
    {
        // DAG run
        EXECUTOR_NAME_LOG(INFO) << BLOCK_NUMBER(m_blockContext->number())
                                << LOG_DESC("begin executeTransactionsWithCriticals")
                                << LOG_KV("txsCriticalsSize", txsCriticals->size());
        executeTransactionsWithCriticals(txsCriticals, inputs, executionResults);
    }
    catch (exception& e)
    {
        EXECUTOR_NAME_LOG(ERROR) << LOG_BADGE("executeBlock")
                                 << LOG_DESC("Error during dag execution")
                                 << LOG_KV("EINFO", boost::diagnostic_information(e));
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, boost::diagnostic_information(e)),
            vector<ExecutionMessage::UniquePtr>{});
        return;
    }
    EXECUTOR_NAME_LOG(INFO) << LOG_DESC("dagExecuteTransactions") << LOG_KV("dagInitT", dagInitT)
                            << LOG_KV("dagRunT", (utcTime() - startT))
                            << LOG_KV("totalCost", (utcTime() - recordT));
    callback(nullptr, std::move(executionResults));
}

void TransactionExecutor::prepare(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback)
{
    EXECUTOR_NAME_LOG(INFO) << BLOCK_NUMBER(params.number) << "Prepare request";

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(ERROR) << "TransactionExecutor is not running";
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"));
        return;
    }

    auto first = m_stateStorages.begin();
    if (first == m_stateStorages.end())
    {
        auto errorMessage = "Prepare error: empty stateStorages";
        EXECUTOR_NAME_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(-1, errorMessage));

        return;
    }

    if (first->number != params.number)
    {
        auto errorMessage =
            "Prepare error: Request blockNumber: " +
            boost::lexical_cast<std::string>(params.number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(first->number);

        EXECUTOR_NAME_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(ExecuteError::PREPARE_ERROR, errorMessage));

        return;
    }

    bcos::protocol::TwoPCParams storageParams{
        params.number, params.primaryTableName, params.primaryTableKey, params.timestamp};

    m_backendStorage->asyncPrepare(storageParams, *(first->storage),
        [this, callback = std::move(callback), blockNumber = params.number](
            auto&& error, uint64_t) {
            if (!m_isRunning)
            {
                callback(BCOS_ERROR_UNIQUE_PTR(
                    ExecuteError::STOPPED, "TransactionExecutor is not running"));
                return;
            }

            if (error)
            {
                auto errorMessage = "Prepare error: " + error->errorMessage();

                EXECUTOR_NAME_LOG(ERROR) << BLOCK_NUMBER(blockNumber) << errorMessage;
                callback(
                    BCOS_ERROR_WITH_PREV_PTR(ExecuteError::PREPARE_ERROR, errorMessage, *error));
                return;
            }

            EXECUTOR_NAME_LOG(INFO) << BLOCK_NUMBER(blockNumber) << "Prepare success";
            callback(nullptr);
        });
}

void TransactionExecutor::commit(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback)
{
    EXECUTOR_NAME_LOG(TRACE) << BLOCK_NUMBER(params.number) << "Commit request";

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(ERROR) << "TransactionExecutor is not running";
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"));
        return;
    }

    auto first = m_stateStorages.begin();
    if (first == m_stateStorages.end())
    {
        auto errorMessage = "Commit error: empty stateStorages";
        EXECUTOR_NAME_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(INVALID_BLOCKNUMBER, errorMessage));

        return;
    }

    if (first->number != params.number)
    {
        auto errorMessage =
            "Commit error: Request blockNumber: " +
            boost::lexical_cast<std::string>(params.number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(first->number);

        EXECUTOR_NAME_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(INVALID_BLOCKNUMBER, errorMessage));

        return;
    }

    bcos::protocol::TwoPCParams storageParams{
        params.number, params.primaryTableName, params.primaryTableKey, params.timestamp};
    m_backendStorage->asyncCommit(storageParams, [this, callback = std::move(callback),
                                                     blockNumber = params.number](
                                                     Error::Ptr&& error, uint64_t) {
        if (!m_isRunning)
        {
            callback(
                BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"));
            return;
        }

        if (error)
        {
            auto errorMessage = "Commit error: " + error->errorMessage();

            EXECUTOR_NAME_LOG(ERROR) << BLOCK_NUMBER(blockNumber) << errorMessage;
            callback(BCOS_ERROR_WITH_PREV_PTR(ExecuteError::COMMIT_ERROR, errorMessage, *error));
            return;
        }

        EXECUTOR_NAME_LOG(DEBUG) << BLOCK_NUMBER(blockNumber) << "Commit success";

        m_lastCommittedBlockNumber = blockNumber;

        removeCommittedState();

        callback(nullptr);
    });
}

void TransactionExecutor::rollback(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback)
{
    EXECUTOR_NAME_LOG(INFO) << BLOCK_NUMBER(params.number) << "Rollback request: ";

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(ERROR) << "TransactionExecutor is not running";
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"));
        return;
    }

    auto first = m_stateStorages.begin();
    if (first == m_stateStorages.end())
    {
        auto errorMessage = "Rollback error: empty stateStorages";
        EXECUTOR_NAME_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(-1, errorMessage));

        return;
    }

    if (first->number != params.number)
    {
        auto errorMessage =
            "Rollback error: Request blockNumber: " +
            boost::lexical_cast<std::string>(params.number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(first->number);

        EXECUTOR_NAME_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(ExecuteError::ROLLBACK_ERROR, errorMessage));

        return;
    }

    bcos::protocol::TwoPCParams storageParams{
        params.number, params.primaryTableName, params.primaryTableKey, params.timestamp};
    m_backendStorage->asyncRollback(storageParams,
        [this, callback = std::move(callback), blockNumber = params.number](auto&& error) {
            if (!m_isRunning)
            {
                callback(BCOS_ERROR_UNIQUE_PTR(
                    ExecuteError::STOPPED, "TransactionExecutor is not running"));
                return;
            }

            if (error)
            {
                auto errorMessage = "Rollback error: " + error->errorMessage();

                EXECUTOR_NAME_LOG(ERROR) << BLOCK_NUMBER(blockNumber) << errorMessage;
                callback(BCOS_ERROR_WITH_PREV_PTR(-1, errorMessage, *error));
                return;
            }

            EXECUTOR_NAME_LOG(INFO) << BLOCK_NUMBER(blockNumber) << "Rollback success";
            callback(nullptr);
        });
}

void TransactionExecutor::reset(std::function<void(bcos::Error::Ptr)> callback)
{
    m_stateStorages.clear();

    callback(nullptr);
}

void TransactionExecutor::getCode(
    std::string_view contract, std::function<void(bcos::Error::Ptr, bcos::bytes)> callback)
{
    EXECUTOR_NAME_LOG(INFO) << "Get code request" << LOG_KV("Contract", contract);

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(ERROR) << "TransactionExecutor is not running";
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"), {});
        return;
    }

    storage::StateStorageInterface::Ptr stateStorage;

    // create temp state storage
    if (m_cachedStorage)
    {
        stateStorage = createStateStorage(m_cachedStorage);
    }
    else
    {
        stateStorage = createStateStorage(m_backendStorage);
    }

    auto tableName = getContractTableName(contract, m_isWasm);
    stateStorage->asyncGetRow(tableName, "code",
        [this, callback = std::move(callback)](Error::UniquePtr error, std::optional<Entry> entry) {
            if (!m_isRunning)
            {
                callback(BCOS_ERROR_UNIQUE_PTR(
                             ExecuteError::STOPPED, "TransactionExecutor is not running"),
                    {});
                return;
            }

            if (error)
            {
                EXECUTOR_NAME_LOG(INFO) << "Get code error: " << error->errorMessage();

                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "Get code error", *error), {});
                return;
            }

            if (!entry)
            {
                EXECUTOR_NAME_LOG(DEBUG) << "Get code success, empty code";

                callback(nullptr, bcos::bytes());
                return;
            }

            auto code = entry->getField(0);
            EXECUTOR_NAME_LOG(INFO) << "Get code success" << LOG_KV("code size", code.size());

            auto codeBytes = bcos::bytes(code.begin(), code.end());
            callback(nullptr, std::move(codeBytes));
        });
}

void TransactionExecutor::getABI(
    std::string_view contract, std::function<void(bcos::Error::Ptr, std::string)> callback)
{
    EXECUTOR_NAME_LOG(INFO) << "Get ABI request" << LOG_KV("Contract", contract);

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(ERROR) << "TransactionExecutor is not running";
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"), {});
        return;
    }

    storage::StateStorageInterface::Ptr stateStorage;

    // create temp state storage
    if (m_cachedStorage)
    {
        stateStorage = createStateStorage(m_cachedStorage);
    }
    else
    {
        stateStorage = createStateStorage(m_backendStorage);
    }

    auto tableName = getContractTableName(contract, m_isWasm);
    stateStorage->asyncGetRow(tableName, ACCOUNT_ABI,
        [this, callback = std::move(callback)](Error::UniquePtr error, std::optional<Entry> entry) {
            if (!m_isRunning)
            {
                callback(BCOS_ERROR_UNIQUE_PTR(
                             ExecuteError::STOPPED, "TransactionExecutor is not running"),
                    {});
                return;
            }

            if (error)
            {
                EXECUTOR_NAME_LOG(INFO) << "Get ABI error: " << error->errorMessage();

                callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "Get ABI error", *error), {});
                return;
            }

            if (!entry)
            {
                EXECUTOR_NAME_LOG(DEBUG) << "Get ABI success, empty ABI";

                callback(nullptr, std::string());
                return;
            }

            auto abi = entry->getField(0);
            EXECUTOR_NAME_LOG(INFO) << "Get ABI success" << LOG_KV("ABI size", abi.size());
            callback(nullptr, std::string(abi));
        });
}

ExecutiveFlowInterface::Ptr TransactionExecutor::getExecutiveFlow(
    std::shared_ptr<BlockContext> blockContext, std::string codeAddress, bool useCoroutine)
{
    EXECUTOR_NAME_LOG(DEBUG) << "getExecutiveFlow" << LOG_KV("codeAddress", codeAddress);
    bcos::RecursiveGuard lock(x_executiveFlowLock);
    ExecutiveFlowInterface::Ptr executiveFlow = blockContext->getExecutiveFlow(codeAddress);
    if (executiveFlow == nullptr)
    {
        auto executiveFactory = std::make_shared<ExecutiveFactory>(blockContext,
            m_precompiledContract, m_constantPrecompiled, m_builtInPrecompiled, m_gasInjector);
        if (!useCoroutine)
        {
            executiveFlow = std::make_shared<ExecutiveSerialFlow>(executiveFactory);
            executiveFlow->setThreadPool(m_threadPool);
            blockContext->setExecutiveFlow(codeAddress, executiveFlow);
        }
        else
        {
            executiveFlow = std::make_shared<ExecutiveStackFlow>(executiveFactory);
            executiveFlow->setThreadPool(m_threadPool);
            blockContext->setExecutiveFlow(codeAddress, executiveFlow);
        }
    }
    return executiveFlow;
}


void TransactionExecutor::asyncExecuteExecutiveFlow(ExecutiveFlowInterface::Ptr executiveFlow,
    std::function<void(
        bcos::Error::UniquePtr&&, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>&&)>
        callback)
{
    ExecuteOutputs::Ptr allOutputs = std::make_shared<ExecuteOutputs>();
    EXECUTOR_NAME_LOG(DEBUG) << "asyncExecuteExecutiveFlow start";
    executiveFlow->asyncRun(
        // onTxReturn
        [this, allOutputs, callback](CallParameters::UniquePtr output) {
            if (!m_isRunning)
            {
                callback(BCOS_ERROR_UNIQUE_PTR(
                             ExecuteError::STOPPED, "TransactionExecutor is not running"),
                    {});
                return;
            }

            auto message = toExecutionResult(std::move(output));
            allOutputs->add(std::move(message));
        },
        // onFinished
        [this, allOutputs, callback](bcos::Error::UniquePtr error) {
            if (!m_isRunning)
            {
                callback(BCOS_ERROR_UNIQUE_PTR(
                             ExecuteError::STOPPED, "TransactionExecutor is not running"),
                    {});
                return;
            }

            // do nothing
            if (error != nullptr)
            {
                EXECUTOR_NAME_LOG(ERROR)
                    << "ExecutiveFlow asyncRun error: " << LOG_KV("errorCode", error->errorCode())
                    << LOG_KV("errorMessage", error->errorMessage());
                m_blockContext->clear();
                callback(std::move(error), std::vector<protocol::ExecutionMessage::UniquePtr>());
            }
            else
            {
                auto messages = allOutputs->dumpAndClear();
                callback(nullptr, std::move(messages));
            }
        });
}

void TransactionExecutor::asyncExecute(std::shared_ptr<BlockContext> blockContext,
    bcos::protocol::ExecutionMessage::UniquePtr input, bool useCoroutine,
    std::function<void(bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>
        callback)
{
    EXECUTOR_NAME_LOG(DEBUG) << BLOCK_NUMBER(blockContext->number()) << LOG_DESC("asyncExecute")
                             << LOG_KV("contextID", input->contextID())
                             << LOG_KV("seq", input->seq())
                             << LOG_KV("MessageType", std::to_string(input->type()))
                             << LOG_KV("To", input->to())
                             << LOG_KV("staticCall", input->staticCall())
                             << LOG_KV("Create", input->create());
    switch (input->type())
    {
    case bcos::protocol::ExecutionMessage::TXHASH:
    {
        // Get transaction first
        auto txHashes = std::make_shared<bcos::crypto::HashList>(1);
        (*txHashes)[0] = (input->transactionHash());

        m_txpool->asyncFillBlock(std::move(txHashes),
            [this, useCoroutine, inputPtr = input.release(), blockContext = std::move(blockContext),
                callback](Error::Ptr error, bcos::protocol::TransactionsPtr transactions) mutable {
                if (!m_isRunning)
                {
                    callback(BCOS_ERROR_UNIQUE_PTR(
                                 ExecuteError::STOPPED, "TransactionExecutor is not running"),
                        nullptr);
                    return;
                }

                auto input = std::unique_ptr<bcos::protocol::ExecutionMessage>(inputPtr);

                if (error)
                {
                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR,
                                 "Transaction does not exists: " + input->transactionHash().hex(),
                                 *error),
                        nullptr);
                    return;
                }

                if (!transactions || transactions->empty())
                {
                    callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR,
                                 "Transaction does not exists: " + input->transactionHash().hex()),
                        nullptr);
                    return;
                }

                auto tx = (*transactions)[0];
                if (!tx)
                {
                    callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR,
                                 "Transaction is null: " + input->transactionHash().hex()),
                        nullptr);
                    return;
                }

                auto callParameters = createCallParameters(*input, *tx);

                ExecutiveFlowInterface::Ptr executiveFlow =
                    getExecutiveFlow(blockContext, callParameters->codeAddress, useCoroutine);
                executiveFlow->submit(std::move(callParameters));

                asyncExecuteExecutiveFlow(executiveFlow,
                    [this, callback = std::move(callback)](bcos::Error::UniquePtr&& error,
                        std::vector<bcos::protocol::ExecutionMessage::UniquePtr>&& messages) {
                        if (!m_isRunning)
                        {
                            callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED,
                                         "TransactionExecutor is not running"),
                                nullptr);
                            return;
                        }

                        if (error)
                        {
                            EXECUTOR_LOG(ERROR) << "asyncExecuteExecutiveFlow error: "
                                                << LOG_KV("msg", error->errorMessage())
                                                << LOG_KV("code", error->errorCode());
                            callback(std::move(error), nullptr);
                        }
                        else
                        {
                            EXECUTOR_NAME_LOG(TRACE) << "asyncExecuteExecutiveFlow complete: "
                                                     << messages[0]->toString();
                            callback(std::move(error), std::move(messages[0]));
                        }
                    });
            });
        break;
    }
    case bcos::protocol::ExecutionMessage::MESSAGE:
    case bcos::protocol::ExecutionMessage::REVERT:
    case bcos::protocol::ExecutionMessage::FINISHED:
    case bcos::protocol::ExecutionMessage::KEY_LOCK:
    {
        auto callParameters = createCallParameters(*input, input->staticCall());
        ExecutiveFlowInterface::Ptr executiveFlow =
            getExecutiveFlow(blockContext, callParameters->codeAddress, useCoroutine);
        executiveFlow->submit(std::move(callParameters));
        asyncExecuteExecutiveFlow(executiveFlow,
            [this, callback = std::move(callback)](bcos::Error::UniquePtr&& error,
                std::vector<bcos::protocol::ExecutionMessage::UniquePtr>&& messages) {
                if (!m_isRunning)
                {
                    callback(BCOS_ERROR_UNIQUE_PTR(
                                 ExecuteError::STOPPED, "TransactionExecutor is not running"),
                        {});
                    return;
                }

                if (error)
                {
                    EXECUTOR_LOG(ERROR) << "asyncExecuteExecutiveFlow error: "
                                        << LOG_KV("msg", error->errorMessage())
                                        << LOG_KV("code", error->errorCode());
                    callback(std::move(error), nullptr);
                }
                else
                {
                    EXECUTOR_NAME_LOG(TRACE)
                        << "asyncExecuteExecutiveFlow complete: " << messages[0]->toString();
                    callback(std::move(error), std::move(messages[0]));
                }
            });

        break;
    }
    default:
    {
        EXECUTOR_NAME_LOG(ERROR) << "Unknown message type: " << input->type();
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::EXECUTE_ERROR,
                     "Unknown type" + boost::lexical_cast<std::string>(input->type())),
            nullptr);
        return;
    }
    }
}

std::function<void(const TransactionExecutive& executive, std::unique_ptr<CallParameters> input)>
TransactionExecutor::createExternalFunctionCall(
    std::function<void(bcos::Error::UniquePtr&&, bcos::protocol::ExecutionMessage::UniquePtr&&)>&
        callback)
{
    return [this, &callback](
               const TransactionExecutive& executive, CallParameters::UniquePtr input) {
        if (!m_isRunning)
        {
            callback(
                BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"),
                nullptr);
            return;
        }
        auto message = toExecutionResult(executive, std::move(input));
        callback(nullptr, std::move(message));
    };
}

std::unique_ptr<ExecutionMessage> TransactionExecutor::toExecutionResult(
    const TransactionExecutive& executive, std::unique_ptr<CallParameters> params)
{
    auto message = toExecutionResult(std::move(params));

    message->setContextID(executive.contextID());
    message->setSeq(executive.seq());

    return message;
}

std::unique_ptr<protocol::ExecutionMessage> TransactionExecutor::toExecutionResult(
    std::unique_ptr<CallParameters> params)
{
    auto message = m_executionMessageFactory->createExecutionMessage();
    switch (params->type)
    {
    case CallParameters::MESSAGE:
        message->setFrom(std::move(params->senderAddress));
        message->setTo(std::move(params->receiveAddress));
        message->setType(ExecutionMessage::MESSAGE);
        message->setKeyLocks(std::move(params->keyLocks));
        break;
    case CallParameters::KEY_LOCK:
        message->setFrom(params->senderAddress);
        message->setTo(std::move(params->senderAddress));
        message->setType(ExecutionMessage::KEY_LOCK);
        message->setKeyLockAcquired(std::move(params->acquireKeyLock));
        message->setKeyLocks(std::move(params->keyLocks));

        break;
    case CallParameters::FINISHED:
        // Response message, Swap the from and to
        message->setFrom(std::move(params->receiveAddress));
        message->setTo(std::move(params->senderAddress));
        message->setType(ExecutionMessage::FINISHED);
        break;
    case CallParameters::REVERT:
        // Response message, Swap the from and to
        message->setFrom(std::move(params->receiveAddress));
        message->setTo(std::move(params->senderAddress));
        message->setType(ExecutionMessage::REVERT);
        break;
    }

    message->setContextID(params->contextID);
    message->setSeq(params->seq);
    message->setOrigin(std::move(params->origin));
    message->setGasAvailable(params->gas);
    message->setData(std::move(params->data));
    message->setStaticCall(params->staticCall);
    message->setCreate(params->create);
    message->setInternalCreate(params->internalCreate);
    message->setInternalCall(params->internalCall);
    if (params->createSalt)
    {
        message->setCreateSalt(*params->createSalt);
    }

    message->setStatus(params->status);
    message->setMessage(std::move(params->message));
    message->setLogEntries(std::move(params->logEntries));
    message->setNewEVMContractAddress(std::move(params->newEVMContractAddress));

    return message;
}


TransactionExecutive::Ptr TransactionExecutor::createExecutive(
    const std::shared_ptr<BlockContext>& _blockContext, const std::string& _contractAddress,
    int64_t contextID, int64_t seq)
{
    auto executive = std::make_shared<TransactionExecutive>(
        _blockContext, _contractAddress, contextID, seq, m_gasInjector);
    executive->setConstantPrecompiled(m_constantPrecompiled);
    executive->setEVMPrecompiled(m_precompiledContract);
    executive->setBuiltInPrecompiled(m_builtInPrecompiled);

    // TODO: register User developed Precompiled contract
    // registerUserPrecompiled(context);
    return executive;
}

void TransactionExecutor::removeCommittedState()
{
    if (m_stateStorages.empty())
    {
        EXECUTOR_NAME_LOG(ERROR) << "Remove committed state failed, empty states";
        return;
    }

    bcos::protocol::BlockNumber number;
    bcos::storage::StateStorageInterface::Ptr storage;

    {
        std::unique_lock<std::shared_mutex> lock(m_stateStoragesMutex);
        auto it = m_stateStorages.begin();
        number = it->number;
        storage = it->storage;
    }

    if (m_cachedStorage)
    {
        auto keyPageStorage = std::dynamic_pointer_cast<bcos::storage::KeyPageStorage>(storage);
        if (keyPageStorage)
        {
            EXECUTOR_NAME_LOG(INFO)
                << LOG_DESC("merge keyPage to cachedStorage") << LOG_KV("number", number);
            keyPageStorage->setReadOnly(true);
        }
        else
        {
            EXECUTOR_NAME_LOG(INFO) << "Merge state number: " << number << " to cachedStorage";
        }

        m_cachedStorage->merge(true, *storage);

        std::unique_lock<std::shared_mutex> lock(m_stateStoragesMutex);
        auto it = m_stateStorages.begin();
        EXECUTOR_NAME_LOG(INFO) << "LatestStateStorage"
                                << LOG_KV("storageNumber", m_stateStorages.back().number)
                                << LOG_KV("commitNumber", number);
        it = m_stateStorages.erase(it);
        if (it != m_stateStorages.end())
        {
            EXECUTOR_NAME_LOG(INFO)
                << "Set state number: " << it->number << " prev to cachedStorage";
            it->storage->setPrev(m_cachedStorage);
        }
    }
    else if (m_backendStorage)
    {
        std::unique_lock<std::shared_mutex> lock(m_stateStoragesMutex);
        auto it = m_stateStorages.begin();
        EXECUTOR_NAME_LOG(DEBUG) << LOG_DESC("removeCommittedState")
                                 << LOG_KV("LatestStateStorage", m_stateStorages.back().number)
                                 << LOG_KV("commitNumber", number)
                                 << LOG_KV("erasedStorage", it->number)
                                 << LOG_KV("stateStorageSize", m_stateStorages.size());
        it = m_stateStorages.erase(it);
        if (it != m_stateStorages.end())
        {
            it->storage->setPrev(m_backendStorage);
        }
    }
}

std::unique_ptr<CallParameters> TransactionExecutor::createCallParameters(
    bcos::protocol::ExecutionMessage& input, bool staticCall)
{
    auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);

    switch (input.type())
    {
    case ExecutionMessage::MESSAGE:
    {
        break;
    }
    case ExecutionMessage::REVERT:
    {
        callParameters->type = CallParameters::REVERT;
        break;
    }
    case ExecutionMessage::FINISHED:
    {
        callParameters->type = CallParameters::FINISHED;
        break;
    }
    case ExecutionMessage::KEY_LOCK:
    {
        break;
    }
    case ExecutionMessage::SEND_BACK:
    case ExecutionMessage::TXHASH:
    {
        BOOST_THROW_EXCEPTION(BCOS_ERROR(
            ExecuteError::EXECUTE_ERROR, "Unexpected execution message type: " +
                                             boost::lexical_cast<std::string>(input.type())));
    }
    }

    callParameters->contextID = input.contextID();
    callParameters->seq = input.seq();
    callParameters->origin = input.origin();
    callParameters->senderAddress = input.from();
    callParameters->receiveAddress = input.to();
    callParameters->codeAddress = input.to();
    callParameters->create = input.create();
    callParameters->internalCreate = input.internalCreate();
    callParameters->internalCall = input.internalCall();
    callParameters->message = input.message();
    callParameters->data = input.takeData();
    callParameters->gas = input.gasAvailable();
    callParameters->staticCall = staticCall;
    callParameters->newEVMContractAddress = input.newEVMContractAddress();
    callParameters->status = input.status();
    callParameters->keyLocks = input.takeKeyLocks();
    if (input.create())
    {
        callParameters->abi = input.abi();
    }

    return callParameters;
}

std::unique_ptr<CallParameters> TransactionExecutor::createCallParameters(
    bcos::protocol::ExecutionMessage& input, const bcos::protocol::Transaction& tx)
{
    auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);

    callParameters->contextID = input.contextID();
    callParameters->seq = input.seq();
    callParameters->origin = toHex(tx.sender());
    callParameters->senderAddress = callParameters->origin;
    callParameters->receiveAddress = input.to();
    callParameters->codeAddress = input.to();
    callParameters->gas = input.gasAvailable();
    callParameters->staticCall = input.staticCall();
    callParameters->create = input.create();
    callParameters->internalCreate = input.internalCreate();
    callParameters->internalCall = input.internalCall();
    callParameters->message = input.message();
    callParameters->data = tx.input().toBytes();
    callParameters->keyLocks = input.takeKeyLocks();
    callParameters->abi = tx.abi();
    return callParameters;
}

void TransactionExecutor::executeTransactionsWithCriticals(
    critical::CriticalFieldsInterface::Ptr criticals,
    gsl::span<std::unique_ptr<CallParameters>> inputs,
    vector<protocol::ExecutionMessage::UniquePtr>& executionResults)
{
    // DAG run
    shared_ptr<TxDAGInterface> txDag = make_shared<TxDAG2>();
    txDag->init(criticals, [this, &inputs, &executionResults](ID id) {
        if (!m_isRunning)
        {
            return;
        }

        auto& input = inputs[id];
        auto executive =
            createExecutive(m_blockContext, input->codeAddress, input->contextID, input->seq);

        EXECUTOR_NAME_LOG(TRACE) << LOG_BADGE("executeTransactionsWithCriticals")
                                 << LOG_DESC("Start transaction")
                                 << LOG_KV("to", input->receiveAddress)
                                 << LOG_KV("data", toHexStringWithPrefix(input->data));
        try
        {
            auto output = executive->start(std::move(input));
            assert(output);
            if (output->type == CallParameters::MESSAGE)
            {
                EXECUTOR_NAME_LOG(DEBUG) << LOG_BADGE("call/deploy in dag")
                                         << LOG_KV("senderAddress", output->senderAddress)
                                         << LOG_KV("codeAddress", output->codeAddress);
            }
            executionResults[id] = toExecutionResult(*executive, std::move(output));
        }
        catch (std::exception& e)
        {
            EXECUTOR_NAME_LOG(ERROR)
                << "executeTransactionsWithCriticals error: " << boost::diagnostic_information(e);
        }
    });

    txDag->run(m_DAGThreadNum);
}

bcos::storage::StateStorageInterface::Ptr TransactionExecutor::createStateStorage(
    bcos::storage::StorageInterface::Ptr storage, bool ignoreNotExist)
{
    if (m_keyPageSize > 0)
    {
        return std::make_shared<bcos::storage::KeyPageStorage>(
            storage, m_keyPageSize, m_keyPageIgnoreTables, ignoreNotExist);
    }
    return std::make_shared<bcos::storage::StateStorage>(storage);
}

protocol::BlockNumber TransactionExecutor::getBlockNumberInStorage()
{
    std::promise<protocol::BlockNumber> blockNumberFuture;
    m_ledger->asyncGetBlockNumber(
        [this, &blockNumberFuture](Error::Ptr error, protocol::BlockNumber number) {
            if (error)
            {
                EXECUTOR_NAME_LOG(INFO) << "Get blockNumber from storage failed";
                blockNumberFuture.set_value(-1);
            }
            else
            {
                blockNumberFuture.set_value(number);
            }
        });

    return blockNumberFuture.get_future().get();
}

void TransactionExecutor::stop()
{
    m_isRunning = false;
    if (m_blockContext)
    {
        m_blockContext->stop();
    }
}
