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
#include "../precompiled/CastPrecompiled.h"
#include "../precompiled/ConsensusPrecompiled.h"
#include "../precompiled/CryptoPrecompiled.h"
#include "../precompiled/KVTablePrecompiled.h"
#include "../precompiled/ShardingPrecompiled.h"
#include "../precompiled/SystemConfigPrecompiled.h"
#include "../precompiled/TableManagerPrecompiled.h"
#include "../precompiled/TablePrecompiled.h"
#include "../precompiled/extension/AccountManagerPrecompiled.h"
#include "../precompiled/extension/AccountPrecompiled.h"
#include "../precompiled/extension/AuthManagerPrecompiled.h"
#include "../precompiled/extension/BalancePrecompiled.h"
#include "../precompiled/extension/ContractAuthMgrPrecompiled.h"
#include "../precompiled/extension/DagTransferPrecompiled.h"
#include "../precompiled/extension/GroupSigPrecompiled.h"
#include "../precompiled/extension/PaillierPrecompiled.h"
#include "../precompiled/extension/RingSigPrecompiled.h"
#include "../precompiled/extension/UserPrecompiled.h"
#include "../precompiled/extension/ZkpPrecompiled.h"
#include "../vm/Precompiled.h"

#include <array>
#include <cstring>

#ifdef WITH_WASM
#include "../vm/gas_meter/GasInjector.h"
#endif

#include "../Common.h"
#include "ExecuteOutputs.h"
#include "bcos-codec/abi/ContractABIType.h"
#include "bcos-executor/src/executive/BlockContext.h"
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
#include "bcos-table/src/StateStorageFactory.h"
#include "bcos-task/Wait.h"
#include "bcos-tool/BfsFileFactory.h"
#include "tbb/flow_graph.h"
#include <bcos-framework/executor/ExecuteError.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/ThreadPool.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
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
#include <utility>
#include <vector>

#ifdef USE_TCMALLOC
#include "gperftools/malloc_extension.h"
#endif

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
    storage::StateStorageFactory::Ptr stateStorageFactory, bcos::crypto::Hash::Ptr hashImpl,
    bool isWasm, bool isAuthCheck, std::shared_ptr<VMFactory> vmFactory,
    std::shared_ptr<std::set<std::string, std::less<>>> keyPageIgnoreTables = nullptr,
    std::string name = "default-executor-name")
  : m_name(std::move(name)),
    m_ledger(ledger),
    m_txpool(std::move(txpool)),
    m_cachedStorage(std::move(cachedStorage)),
    m_backendStorage(std::move(backendStorage)),
    m_executionMessageFactory(std::move(executionMessageFactory)),
    m_stateStorageFactory(std::move(stateStorageFactory)),
    m_hashImpl(std::move(hashImpl)),
    m_isAuthCheck(isAuthCheck),
    m_isWasm(isWasm),
    m_keyPageIgnoreTables(std::move(keyPageIgnoreTables)),
    m_ledgerCache(std::make_shared<LedgerCache>(ledger)),
    m_vmFactory(std::move(vmFactory))
{
    assert(m_backendStorage);
    m_ledgerCache->fetchCompatibilityVersion();
    m_ledgerCache->fetchBlockNumberAndHash();
    GlobalHashImpl::g_hashImpl = m_hashImpl;
    m_abiCache = make_shared<ClockCache<bcos::bytes, FunctionAbi>>(32);
#ifdef WITH_WASM
    m_gasInjector = std::make_shared<wasm::GasInjector>(wasm::GetInstructionTable());
#endif

    m_threadPool = std::make_shared<bcos::ThreadPool>(name, std::thread::hardware_concurrency());
    setBlockVersion(m_ledgerCache->ledgerConfig()->compatibilityVersion());
    if (m_ledgerCache->ledgerConfig()->compatibilityVersion() >= BlockVersion::V3_3_VERSION)
    {
        m_ledgerCache->fetchAuthCheckStatus();
        if (m_ledgerCache->ledgerConfig()->authCheckStatus() != UINT32_MAX)
        {
            // cannot get auth check status, use config value
            m_isAuthCheck = !m_isWasm && m_ledgerCache->ledgerConfig()->authCheckStatus() != 0;
        }
    }
    if (m_isWasm)
    {
        initWasmEnvironment();
    }
    else
    {
        initEvmEnvironment();
    }

    if (m_blockVersion == (uint32_t)protocol::BlockVersion::V3_0_VERSION)
    {
        // This 3.0.x's bug, but we still need to compatible with it
        initTestPrecompiledTable(m_backendStorage);
    }

    auto const header = getBlockHeaderInStorage(m_lastCommittedBlockNumber);
    m_lastCommittedBlockTimestamp = header != nullptr ? header->timestamp() : utcTime();

    assert(m_precompiled != nullptr && m_precompiled->size() > 0);
    start();
}

void TransactionExecutor::setBlockVersion(uint32_t blockVersion)
{
    if (m_blockVersion == blockVersion)
    {
        return;
    }

    RecursiveGuard l(x_resetEnvironmentLock);
    if (m_blockVersion != blockVersion)
    {
        m_blockVersion = blockVersion;

        resetEnvironment();  // should not be called concurrently, if called, there's a bug in
                             // caller
    }
}

void TransactionExecutor::resetEnvironment()
{
    RecursiveGuard l(x_resetEnvironmentLock);

    if (m_blockVersion >= (uint32_t)protocol::BlockVersion::V3_1_VERSION)
    {
        m_keyPageIgnoreTables = std::make_shared<std::set<std::string, std::less<>>>(
            storage::IGNORED_ARRAY_310.begin(), storage::IGNORED_ARRAY_310.end());
    }
    else
    {
        m_keyPageIgnoreTables = std::make_shared<std::set<std::string, std::less<>>>(
            storage::IGNORED_ARRAY.begin(), storage::IGNORED_ARRAY.end());
    }
}

void TransactionExecutor::initEvmEnvironment()
{
    auto fillZero = [](int _num) -> std::string {
        std::stringstream stream;
        stream << std::setfill('0') << std::setw(40) << std::hex << _num;
        return stream.str();
    };
    m_evmPrecompiled =
        std::make_shared<std::map<std::string, std::shared_ptr<PrecompiledContract>>>();
    m_staticPrecompiled = std::make_shared<std::set<std::string>>();
    m_precompiled = std::make_shared<PrecompiledMap>();

    m_evmPrecompiled->insert(std::make_pair(fillZero(1),
        make_shared<PrecompiledContract>(3000, 0, PrecompiledRegistrar::executor("ecrecover"))));
    m_evmPrecompiled->insert(std::make_pair(fillZero(2),
        make_shared<PrecompiledContract>(60, 12, PrecompiledRegistrar::executor("sha256"))));
    m_evmPrecompiled->insert(std::make_pair(fillZero(3),
        make_shared<PrecompiledContract>(600, 120, PrecompiledRegistrar::executor("ripemd160"))));
    m_evmPrecompiled->insert(std::make_pair(fillZero(4),
        make_shared<PrecompiledContract>(15, 3, PrecompiledRegistrar::executor("identity"))));
    m_evmPrecompiled->insert(
        {fillZero(5), make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("modexp"),
                          PrecompiledRegistrar::executor("modexp"))});
    m_evmPrecompiled->insert(
        {fillZero(6), make_shared<PrecompiledContract>(
                          150, 0, PrecompiledRegistrar::executor("alt_bn128_G1_add"))});
    m_evmPrecompiled->insert(
        {fillZero(7), make_shared<PrecompiledContract>(
                          6000, 0, PrecompiledRegistrar::executor("alt_bn128_G1_mul"))});
    m_evmPrecompiled->insert({fillZero(8),
        make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("alt_bn128_pairing_product"),
            PrecompiledRegistrar::executor("alt_bn128_pairing_product"))});
    m_evmPrecompiled->insert({fillZero(9),
        make_shared<PrecompiledContract>(PrecompiledRegistrar::pricer("blake2_compression"),
            PrecompiledRegistrar::executor("blake2_compression"))});

    auto sysConfig = std::make_shared<precompiled::SystemConfigPrecompiled>(m_hashImpl);
    auto consensusPrecompiled = std::make_shared<precompiled::ConsensusPrecompiled>(m_hashImpl);
    auto tableManagerPrecompiled =
        std::make_shared<precompiled::TableManagerPrecompiled>(m_hashImpl);
    auto kvTablePrecompiled = std::make_shared<precompiled::KVTablePrecompiled>(m_hashImpl);
    auto tablePrecompiled = std::make_shared<precompiled::TablePrecompiled>(m_hashImpl);

    // in EVM
    m_precompiled->insert(SYS_CONFIG_ADDRESS, std::move(sysConfig));
    m_precompiled->insert(CONSENSUS_ADDRESS, std::move(consensusPrecompiled));
    m_precompiled->insert(TABLE_MANAGER_ADDRESS, std::move(tableManagerPrecompiled));
    m_precompiled->insert(KV_TABLE_ADDRESS, std::move(kvTablePrecompiled));
    m_precompiled->insert(TABLE_ADDRESS, std::move(tablePrecompiled));
    m_precompiled->insert(
        DAG_TRANSFER_ADDRESS, std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl));
    m_precompiled->insert(CRYPTO_ADDRESS, std::make_shared<CryptoPrecompiled>(m_hashImpl));
    m_precompiled->insert(BFS_ADDRESS, std::make_shared<BFSPrecompiled>(m_hashImpl));
    m_precompiled->insert(PAILLIER_ADDRESS, std::make_shared<PaillierPrecompiled>(m_hashImpl),
        [](uint32_t, bool, ledger::Features const& features) {
            return features.get(ledger::Features::Flag::feature_paillier);
        });
    m_precompiled->insert(GROUP_SIG_ADDRESS, std::make_shared<GroupSigPrecompiled>(m_hashImpl));
    m_precompiled->insert(RING_SIG_ADDRESS, std::make_shared<RingSigPrecompiled>(m_hashImpl));
    m_precompiled->insert(DISCRETE_ZKP_ADDRESS, std::make_shared<ZkpPrecompiled>(m_hashImpl));

    m_precompiled->insert(AUTH_MANAGER_ADDRESS,
        std::make_shared<AuthManagerPrecompiled>(m_hashImpl, m_isWasm),
        [](uint32_t version, bool isAuthCheck, ledger::Features const& features) -> bool {
            return isAuthCheck || version >= BlockVersion::V3_3_VERSION;
        });
    m_precompiled->insert(AUTH_CONTRACT_MGR_ADDRESS,
        std::make_shared<ContractAuthMgrPrecompiled>(m_hashImpl, m_isWasm),
        [](uint32_t version, bool isAuthCheck, ledger::Features const& features) -> bool {
            return isAuthCheck || version >= BlockVersion::V3_3_VERSION;
        });

    m_precompiled->insert(SHARDING_PRECOMPILED_ADDRESS,
        std::make_shared<ShardingPrecompiled>(GlobalHashImpl::g_hashImpl),
        [](uint32_t version, bool isAuthCheck, ledger::Features const& features) {
            return features.get(ledger::Features::Flag::feature_sharding);
        });
    m_precompiled->insert(CAST_ADDRESS,
        std::make_shared<CastPrecompiled>(GlobalHashImpl::g_hashImpl), BlockVersion::V3_2_VERSION);
    m_precompiled->insert(ACCOUNT_MGR_ADDRESS,
        std::make_shared<AccountManagerPrecompiled>(m_hashImpl), BlockVersion::V3_1_VERSION);
    m_precompiled->insert(ACCOUNT_ADDRESS, std::make_shared<AccountPrecompiled>(m_hashImpl),
        BlockVersion::V3_1_VERSION);

    set<string> builtIn = {CRYPTO_ADDRESS, GROUP_SIG_ADDRESS, RING_SIG_ADDRESS, CAST_ADDRESS};
    m_staticPrecompiled = std::make_shared<set<string>>(builtIn);
    if (m_blockVersion <=> BlockVersion::V3_1_VERSION == 0 &&
        m_ledgerCache->ledgerConfig()->blockNumber() > 0)
    {
        // Only 3.1 goes here, here is a bug, ignore init test precompiled
    }
    else
    {
        CpuHeavyPrecompiled::registerPrecompiled(m_precompiled, m_hashImpl);
        SmallBankPrecompiled::registerPrecompiled(m_precompiled, m_hashImpl);
    }

    m_precompiled->insert(BALANCE_PRECOMPILED_ADDRESS,
        std::make_shared<BalancePrecompiled>(m_hashImpl),
        [](uint32_t version, bool isAuthCheck, ledger::Features const& features) {
            return features.get(ledger::Features::Flag::feature_balance_precompiled);
        });
}

void TransactionExecutor::initWasmEnvironment()
{
    m_precompiled = std::make_shared<PrecompiledMap>();

    auto sysConfig = std::make_shared<precompiled::SystemConfigPrecompiled>(m_hashImpl);
    auto consensusPrecompiled = std::make_shared<precompiled::ConsensusPrecompiled>(m_hashImpl);
    auto tableManagerPrecompiled =
        std::make_shared<precompiled::TableManagerPrecompiled>(m_hashImpl);
    auto kvTablePrecompiled = std::make_shared<precompiled::KVTablePrecompiled>(m_hashImpl);
    auto tablePrecompiled = std::make_shared<precompiled::TablePrecompiled>(m_hashImpl);

    // in WASM
    m_precompiled->insert(SYS_CONFIG_NAME, std::move(sysConfig));
    m_precompiled->insert(CONSENSUS_TABLE_NAME, std::move(consensusPrecompiled));
    m_precompiled->insert(TABLE_MANAGER_NAME, std::move(tableManagerPrecompiled));
    m_precompiled->insert(KV_TABLE_NAME, std::move(kvTablePrecompiled));
    m_precompiled->insert(TABLE_NAME, std::move(tablePrecompiled));
    m_precompiled->insert(
        DAG_TRANSFER_NAME, std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl));
    m_precompiled->insert(CRYPTO_NAME, std::make_shared<CryptoPrecompiled>(m_hashImpl));
    m_precompiled->insert(BFS_NAME, std::make_shared<BFSPrecompiled>(m_hashImpl));
    m_precompiled->insert(PAILLIER_SIG_NAME, std::make_shared<PaillierPrecompiled>(m_hashImpl),
        [](uint32_t, bool, ledger::Features const& features) {
            return features.get(ledger::Features::Flag::feature_paillier);
        });
    m_precompiled->insert(GROUP_SIG_NAME, std::make_shared<GroupSigPrecompiled>(m_hashImpl));
    m_precompiled->insert(RING_SIG_NAME, std::make_shared<RingSigPrecompiled>(m_hashImpl));
    m_precompiled->insert(DISCRETE_ZKP_NAME, std::make_shared<ZkpPrecompiled>(m_hashImpl));
    m_precompiled->insert(AUTH_MANAGER_NAME,
        std::make_shared<precompiled::AuthManagerPrecompiled>(m_hashImpl, m_isWasm),
        BlockVersion::V3_0_VERSION, true);
    m_precompiled->insert(AUTH_CONTRACT_MGR_ADDRESS,
        std::make_shared<precompiled::ContractAuthMgrPrecompiled>(m_hashImpl, m_isWasm),
        BlockVersion::V3_0_VERSION, true);

    m_precompiled->insert(CAST_NAME, std::make_shared<CastPrecompiled>(GlobalHashImpl::g_hashImpl),
        BlockVersion::V3_2_VERSION);
    m_precompiled->insert(ACCOUNT_MANAGER_NAME,
        std::make_shared<AccountManagerPrecompiled>(m_hashImpl), BlockVersion::V3_1_VERSION);
    m_precompiled->insert(ACCOUNT_ADDRESS, std::make_shared<AccountPrecompiled>(m_hashImpl),
        BlockVersion::V3_1_VERSION);

    set<string> builtIn = {CRYPTO_ADDRESS, GROUP_SIG_ADDRESS, RING_SIG_ADDRESS, CAST_ADDRESS};
    m_staticPrecompiled = std::make_shared<set<string>>(builtIn);

    if (m_blockVersion <=> BlockVersion::V3_1_VERSION == 0 &&
        m_ledgerCache->ledgerConfig()->blockNumber() > 0)
    {
        // Only 3.1 goes here, here is a bug, ignore init test precompiled
    }
    else
    {
        CpuHeavyPrecompiled::registerPrecompiled(m_precompiled, m_hashImpl);
        SmallBankPrecompiled::registerPrecompiled(m_precompiled, m_hashImpl);
    }
    // according to feature flag to register precompiled
    m_precompiled->insert(BALANCE_PRECOMPILED_NAME,
        std::make_shared<BalancePrecompiled>(m_hashImpl),
        [](uint32_t, bool, ledger::Features const& features) {
            return features.get(ledger::Features::Flag::feature_balance_precompiled);
        });
}

void TransactionExecutor::initTestPrecompiledTable(storage::StorageInterface::Ptr storage)
{
    SmallBankPrecompiled::createTable(storage);
    DagTransferPrecompiled::createDagTable(storage);
}

BlockContext::Ptr TransactionExecutor::createBlockContext(
    const protocol::BlockHeader::ConstPtr& currentHeader,
    storage::StateStorageInterface::Ptr storage)
{
    bcos::storage::StorageInterface::Ptr backend = m_backendStorage;
    if (m_cachedStorage != nullptr)
    {
        backend = m_cachedStorage;
    }
    BlockContext::Ptr context = make_shared<BlockContext>(storage, m_ledgerCache, m_hashImpl,
        *currentHeader, m_isWasm, m_isAuthCheck, std::move(backend), m_keyPageIgnoreTables);
    context->setVMFactory(m_vmFactory);
    if (f_onNeedSwitchEvent)
    {
        context->registerNeedSwitchEvent(f_onNeedSwitchEvent);
    }

    return context;
}

std::shared_ptr<BlockContext> TransactionExecutor::createBlockContextForCall(
    bcos::protocol::BlockNumber blockNumber, h256 blockHash, uint64_t timestamp,
    int32_t blockVersion, storage::StateStorageInterface::Ptr storage)
{
    BlockContext::Ptr context = make_shared<BlockContext>(storage, m_ledgerCache, m_hashImpl,
        blockNumber, blockHash, timestamp, blockVersion, m_isWasm, m_isAuthCheck);
    context->setVMFactory(m_vmFactory);
    return context;
}


void TransactionExecutor::nextBlockHeader(int64_t schedulerTermId,
    const bcos::protocol::BlockHeader::ConstPtr& blockHeader,
    std::function<void(bcos::Error::UniquePtr)> callback)
{
    m_schedulerTermId = schedulerTermId;

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(WARNING) << "TransactionExecutor is not running";
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"));
        return;
    }

    try
    {
        auto view = blockHeader->parentInfo();
        auto parentInfoIt = view.begin();
        EXECUTOR_NAME_LOG(DEBUG) << BLOCK_NUMBER(blockHeader->number())
                                 << "NextBlockHeader request: "
                                 << LOG_KV("blockVersion", blockHeader->version())
                                 << LOG_KV("schedulerTermId", schedulerTermId)
                                 << LOG_KV("parentHash", blockHeader->number() > 0 ?
                                                             (*parentInfoIt).blockHash.abridged() :
                                                             "null");
        setBlockVersion(blockHeader->version());
        {
            std::unique_lock<std::shared_mutex> lock(m_stateStoragesMutex);
            bcos::storage::StateStorageInterface::Ptr stateStorage;
            if (m_stateStorages.empty())
            {
                auto withDirtyFlag =
                    m_blockContext ? m_blockContext->features().get(
                                         ledger::Features::Flag::bugfix_set_row_with_dirty_flag) :
                                     false;
                if (m_cachedStorage)
                {
                    stateStorage = createStateStorage(m_cachedStorage, false, withDirtyFlag);
                }
                else
                {
                    stateStorage = createStateStorage(m_backendStorage, false, withDirtyFlag);
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
                    EXECUTOR_NAME_LOG(WARNING) << fmt;
                    m_stateStorages.clear();
                    callback(
                        BCOS_ERROR_UNIQUE_PTR(ExecuteError::SCHEDULER_TERM_ID_ERROR, fmt.str()));
                    return;
                }

                prev.storage->setReadOnly(true);
                stateStorage = createStateStorage(prev.storage, false,
                    m_blockContext->features().get(
                        ledger::Features::Flag::bugfix_set_row_with_dirty_flag));
            }

            if (m_blockContext)
            {
                m_blockContext->clear();
            }

            // set last commit state storage to blockContext, to auth read last block state
            m_blockContext = createBlockContext(blockHeader, stateStorage);
            m_stateStorages.emplace_back(blockHeader->number(), stateStorage);

            if (blockHeader->number() == 0)
            {
                if (m_blockVersion == (uint32_t)protocol::BlockVersion::V3_1_VERSION)
                {
                    // Only 3.1 goes here,for compat with the bug:
                    // 3.1 only init these two precompiled in genesis block
                    CpuHeavyPrecompiled::registerPrecompiled(m_precompiled, m_hashImpl);
                    SmallBankPrecompiled::registerPrecompiled(m_precompiled, m_hashImpl);
                }

                initTestPrecompiledTable(stateStorage);
            }
        }

        // cache parentHash
        if (blockHeader->number() > 0)
        {
            m_ledgerCache->setBlockNumber2Hash(
                blockHeader->number() - 1, (*parentInfoIt).blockHash);
        }
        m_lastCommittedBlockTimestamp = blockHeader->timestamp();

        EXECUTOR_NAME_LOG(DEBUG) << BLOCK_NUMBER(blockHeader->number()) << "NextBlockHeader success"
                                 << LOG_KV("number", blockHeader->number())
                                 << LOG_KV("parentHash", blockHeader->number() > 0 ?
                                                             (*parentInfoIt).blockHash.abridged() :
                                                             "null");
        callback(nullptr);
    }
    catch (std::exception& e)
    {
        EXECUTOR_NAME_LOG(WARNING)
            << "NextBlockHeader failed: " << boost::diagnostic_information(e);

        callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "nextBlockHeader unknown failed", e));
    }
}

void TransactionExecutor::dmcExecuteTransaction(bcos::protocol::ExecutionMessage::UniquePtr input,
    std::function<void(bcos::Error::UniquePtr, bcos::protocol::ExecutionMessage::UniquePtr)>
        callback)
{
    EXECUTOR_NAME_LOG(TRACE) << "ExecuteTransaction request"
                             << LOG_KV("ContextID", input->contextID())
                             << LOG_KV("seq", input->seq())
                             << LOG_KV("messageType", (int32_t)input->type())
                             << LOG_KV("to", input->to()) << LOG_KV("create", input->create());

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(WARNING) << "TransactionExecutor is not running";
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
                EXECUTOR_NAME_LOG(WARNING) << errorMessage;
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
        auto storage = createStateStorage(std::move(prev), true, false);

        // Create a temp block context
        blockContext = createBlockContextForCall(m_lastCommittedBlockNumber, h256(),
            m_lastCommittedBlockTimestamp, m_blockVersion, std::move(storage));

        auto inserted = m_calledContext->emplace(
            std::tuple{input->contextID(), input->seq()}, CallState{blockContext});

        if (!inserted)
        {
            auto message = "dmcCall failed, contextID: " +
                           boost::lexical_cast<std::string>(input->contextID()) +
                           " seq: " + boost::lexical_cast<std::string>(input->seq()) + " exists";
            EXECUTOR_NAME_LOG(WARNING) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
            return;
        }

        break;
    }
    case protocol::ExecutionMessage::FINISHED:
    case protocol::ExecutionMessage::REVERT:
    case protocol::ExecutionMessage::PRE_FINISH:
    {
        tbb::concurrent_hash_map<std::tuple<int64_t, int64_t>, CallState, HashCombine>::accessor it;
        m_calledContext->find(it, std::tuple{input->contextID(), input->seq()});

        if (it.empty())
        {
            auto message = "dmcCall failed, contextID: " +
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
            "dmcCall failed, Unknown call type: " + boost::lexical_cast<std::string>(input->type());
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
                    << LOG_DESC("Call failed") << LOG_KV("code", error->errorCode())
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
                    auto message = "dmcCall failed, erase contextID: " +
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
                             << LOG_KV("seq", input->seq())
                             << LOG_KV("messageType", (int32_t)input->type())
                             << LOG_KV("to", input->to()) << LOG_KV("create", input->create());

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(WARNING) << "TransactionExecutor is not running";
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
                EXECUTOR_NAME_LOG(WARNING) << errorMessage;
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
        EXECUTOR_NAME_LOG(WARNING) << "TransactionExecutor is not running";
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"),
            nullptr);
        return;
    }

    BlockContext::Ptr blockContext;
    switch (input->type())
    {
    case protocol::ExecutionMessage::MESSAGE:
    {
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
        auto storage =
            createStateStorage(std::move(prev), true, false /*call storage no need set flag*/);

        // Create a temp block context
        blockContext = createBlockContextForCall(m_lastCommittedBlockNumber, h256(),
            m_lastCommittedBlockTimestamp, m_blockVersion, std::move(storage));

        auto inserted = m_calledContext->emplace(
            std::tuple{input->contextID(), input->seq()}, CallState{blockContext});

        if (!inserted)
        {
            auto message =
                "Call failed, contextID: " + boost::lexical_cast<std::string>(input->contextID()) +
                " seq: " + boost::lexical_cast<std::string>(input->seq()) + " exists";
            EXECUTOR_NAME_LOG(WARNING) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
            return;
        }

        break;
    }
    case protocol::ExecutionMessage::FINISHED:
    case protocol::ExecutionMessage::REVERT:
    case protocol::ExecutionMessage::PRE_FINISH:
    {
        tbb::concurrent_hash_map<std::tuple<int64_t, int64_t>, CallState, HashCombine>::accessor it;
        m_calledContext->find(it, std::tuple{input->contextID(), input->seq()});

        if (it.empty())
        {
            auto message =
                "Call failed, contextID: " + boost::lexical_cast<std::string>(input->contextID()) +
                " seq: " + boost::lexical_cast<std::string>(input->seq()) + " does not exists";
            EXECUTOR_NAME_LOG(WARNING) << message;
            callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, message), nullptr);
            return;
        }

        blockContext = it->second.blockContext;

        break;
    }
    default:
    {
        auto message =
            "Call failed, Unknown call type: " + boost::lexical_cast<std::string>(input->type());
        EXECUTOR_NAME_LOG(WARNING) << message;
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
                    << LOG_DESC("Call failed") << LOG_KV("code", error->errorCode())
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
                    auto message = "Call failed, erase contextID: " +
                                   boost::lexical_cast<std::string>(result->contextID()) +
                                   " seq: " + boost::lexical_cast<std::string>(result->seq()) +
                                   " does not exists";
                    EXECUTOR_NAME_LOG(WARNING) << message;

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
    EXECUTOR_NAME_LOG(DEBUG) << BLOCK_NUMBER(blockNumber) << "executeTransactionsInternal request"
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
        EXECUTOR_NAME_LOG(WARNING) << "TransactionExecutor is not running";
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

    std::mutex writeMutex;
    tbb::parallel_for(tbb::blocked_range<size_t>(0U, inputs.size()), [&, this](auto const& range) {
        for (auto i = range.begin(); i < range.end(); ++i)
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
                std::unique_lock lock(writeMutex);
                txHashes->emplace_back(params->transactionHash());
                indexes.emplace_back(i);
                fillInputs->emplace_back(std::move(params));

                break;
            }
            case ExecutionMessage::MESSAGE:
            case bcos::protocol::ExecutionMessage::REVERT:
            case bcos::protocol::ExecutionMessage::FINISHED:
            case bcos::protocol::ExecutionMessage::KEY_LOCK:
            case bcos::protocol::ExecutionMessage::PRE_FINISH:
            {
                callParametersList->at(i) = createCallParameters(*params, params->staticCall());
                break;
            }
            default:
            {
                auto message =
                    (boost::format("Unsupported message type: %d") % params->type()).str();
                EXECUTOR_NAME_LOG(ERROR)
                    << BLOCK_NUMBER(blockNumber) << "Execute error, " << message;
                // callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::DAG_ERROR, message), {});
                break;
            }
            }
        }
    });

    auto prepareT = utcTime() - startT;
    startT = utcTime();

    if (!txHashes->empty())
    {
        m_txpool->asyncFillBlock(
            txHashes, [this, startT, useCoroutine, contractAddress, indexes = std::move(indexes),
                          fillInputs = std::move(fillInputs),
                          callParametersList = std::move(callParametersList),
                          callback = std::move(callback), txHashes, blockNumber](
                          Error::Ptr error, protocol::ConstTransactionsPtr transactions) mutable {
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
                    EXECUTOR_NAME_LOG(WARNING)
                        << BLOCK_NUMBER(blockNumber) << errorMessage << error->errorMessage();
                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                 ExecuteError::DAG_ERROR, errorMessage, *error),
                        {});
                    return;
                }
                auto recordT = utcTime();
                tbb::parallel_for(tbb::blocked_range<size_t>(0U, transactions->size()),
                    [this, &transactions, &callParametersList, &indexes, &fillInputs](
                        auto const& range) {
                        for (auto i = range.begin(); i < range.end(); ++i)
                        {
                            assert(transactions->at(i));
                            callParametersList->at(indexes[i]) =
                                createCallParameters(*fillInputs->at(i), *transactions->at(i));
                        }
                    });

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
    if (!m_isWasm)
    {
        // padding the address
        constexpr static auto addressSize = Address::SIZE * 2;
        if (contractAddress.size() < addressSize) [[unlikely]]
        {
            contractAddress.insert(0, addressSize - contractAddress.size(), '0');
        }
    }

    executeTransactionsInternal(
        std::move(contractAddress), std::move(inputs), true, std::move(_callback));
}

void TransactionExecutor::getHash(bcos::protocol::BlockNumber number,
    std::function<void(bcos::Error::UniquePtr, crypto::HashType)> callback)
{
    EXECUTOR_NAME_LOG(DEBUG) << BLOCK_NUMBER(number) << "GetTableHashes";

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(WARNING) << "TransactionExecutor is not running";
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"), {});
        return;
    }

    if (m_stateStorages.empty())
    {
        EXECUTOR_NAME_LOG(WARNING) << "GetTableHashes error: No uncommitted state";
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::GETHASH_ERROR, "No uncommitted state"),
            crypto::HashType());
        return;
    }

    auto& last = m_stateStorages.back();
    if (last.number != number)
    {
        auto errorMessage =
            "GetTableHashes failed: Request blockNumber: " +
            boost::lexical_cast<std::string>(number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(last.number);

        EXECUTOR_NAME_LOG(WARNING) << errorMessage;
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::GETHASH_ERROR, errorMessage), crypto::HashType());

        return;
    }

    // remove suicides beforehand
    m_blockContext->killSuicides();
    auto start = utcTime();
    auto hash = last.storage->hash(m_hashImpl, m_blockContext->features());
    auto end = utcTime();
    EXECUTOR_NAME_LOG(INFO) << BLOCK_NUMBER(number) << "GetTableHashes success"
                            << LOG_KV("hash", hash.hex()) << LOG_KV("time(ms)", (end - start));

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
                                 << LOG_KV("msg", error ? error->errorMessage() : "ok")
                                 << LOG_KV("timeCost", utcTime() - requestTimestamp);
        _callback(std::move(error), std::move(outputs));
    };


    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(WARNING) << "TransactionExecutor is not running";
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

    for (auto i = 0u; i < inputs.size(); ++i)
    {
        auto& params = inputs[i];
        switch (params->type())
        {
        case ExecutionMessage::TXHASH:
        {
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
                txHashes, blockNumber](
                Error::Ptr error, protocol::ConstTransactionsPtr transactions) mutable {
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
                    EXECUTOR_NAME_LOG(WARNING)
                        << BLOCK_NUMBER(blockNumber) << errorMessage << error->errorMessage();
                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                 ExecuteError::DAG_ERROR, errorMessage, *error),
                        {});
                    return;
                }
                auto recordT = utcTime();
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

    for (const auto& conflictField : functionAbi.conflictFields)
    {
        auto criticalKey = bytes();
        criticalKey.reserve(72);

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
            conflictFields->emplace_back(std::move(criticalKey));
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
            case EnvKind::BlkNumber:
            {
                auto blockNumber = _blockContext->number();
                auto bytes = static_cast<bcos::byte*>(static_cast<void*>(&blockNumber));
                criticalKey.insert(criticalKey.end(), bytes, bytes + sizeof(blockNumber));

                EXECUTOR_NAME_LOG(TRACE)
                    << LOG_BADGE("extractConflictFields") << LOG_DESC("use `BlockNumber`")
                    << LOG_KV("functionName", functionAbi.name)
                    << LOG_KV("blockNumber", blockNumber);
                break;
            }
            case EnvKind::Addr:
            {
                criticalKey.insert(criticalKey.end(), to.begin(), to.end());

                EXECUTOR_NAME_LOG(TRACE) << LOG_BADGE("extractConflictFields")
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
            conflictFields->emplace_back(std::move(criticalKey));
            break;
        }
        case Params:
        {
            assert(!conflictField.value.empty());
            const ParameterAbi* paramAbi = nullptr;
            const auto* components = &functionAbi.inputs;
            auto inputData = ref(params.data).getCroppedData(4).toBytes();
            if (_blockContext->isWasm())
            {
                auto startPos = 0u;
                for (const auto& segment : conflictField.value)
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
                const auto& typeName = functionAbi.flatInputs[index];
                if (typeName.empty())
                {
                    return nullptr;
                }
                auto out = getComponentBytes(index, typeName, ref(params.data).getCroppedData(4));
                criticalKey.insert(criticalKey.end(), out.begin(), out.end());
            }
            conflictFields->emplace_back(std::move(criticalKey));
            EXECUTOR_NAME_LOG(TRACE)
                << LOG_BADGE("extractConflictFields") << LOG_DESC("use `Params`")
                << LOG_KV("functionName", functionAbi.name)
                << LOG_KV("criticalKey", toHexStringWithPrefix(criticalKey));
            break;
        }
        case Const:
        {
            criticalKey.insert(
                criticalKey.end(), conflictField.value.begin(), conflictField.value.end());
            conflictFields->emplace_back(std::move(criticalKey));
            EXECUTOR_NAME_LOG(TRACE)
                << LOG_BADGE("extractConflictFields") << LOG_DESC("use `Const`")
                << LOG_KV("functionName", functionAbi.name)
                << LOG_KV("criticalKey", toHexStringWithPrefix(criticalKey));
            break;
        }
        case None:
        {
            EXECUTOR_NAME_LOG(TRACE) << LOG_BADGE("extractConflictFields") << LOG_DESC("use `None`")
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
                    auto executiveFactory = std::make_shared<ExecutiveFactory>(*m_blockContext,
                        m_evmPrecompiled, m_precompiled, m_staticPrecompiled, *m_gasInjector);
                    auto executive = executiveFactory->build(
                        params->codeAddress, params->contextID, params->seq, ExecutiveType::common);
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
                            // Note: must be sure that the log accessed data should be valid
                            // always
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
                                // new logic
                                std::string_view abiStr;
                                if (m_blockContext->blockVersion() >=
                                    uint32_t(bcos::protocol::BlockVersion::V3_1_VERSION))
                                {
                                    // get codehash
                                    auto entry = table->getRow(ACCOUNT_CODE_HASH);
                                    if (!entry || entry->get().empty())
                                    {
                                        executionResults[i] =
                                            toExecutionResult(std::move(inputs[i]));
                                        executionResults[i]->setType(ExecutionMessage::SEND_BACK);
                                        EXECUTOR_NAME_LOG(INFO)
                                            << "No codeHash found, please deploy first "
                                            << LOG_KV("tableName", tableName);
                                        continue;
                                    }

                                    auto codeHash = entry->getField(0);

                                    // get abi according to codeHash
                                    auto abiTable =
                                        storage->openTable(bcos::ledger::SYS_CONTRACT_ABI);
                                    auto abiEntry = abiTable->getRow(codeHash);
                                    if (!abiEntry || abiEntry->get().empty())
                                    {
                                        abiEntry = table->getRow(ACCOUNT_ABI);
                                        if (!abiEntry || abiEntry->get().empty())
                                        {
                                            executionResults[i] =
                                                toExecutionResult(std::move(inputs[i]));
                                            executionResults[i]->setType(
                                                ExecutionMessage::SEND_BACK);
                                            EXECUTOR_NAME_LOG(INFO)
                                                << "No ABI found, please deploy first "
                                                << LOG_KV("tableName", tableName);
                                            continue;
                                        }
                                    }
                                    abiStr = abiEntry->getField(0);
                                }
                                else
                                {
                                    // old logic
                                    auto entry = table->getRow(ACCOUNT_ABI);
                                    abiStr = entry->getField(0);
                                }
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
                EXECUTOR_NAME_LOG(WARNING)
                    << LOG_BADGE("dagExecuteTransactionsInternal")
                    << LOG_DESC("EXCEPTION during parallel extractConflictFields")
                    << LOG_KV("INFO", boost::diagnostic_information(e));
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
                                 << LOG_DESC("EXCEPTION during dag execution")
                                 << LOG_KV("INFO", boost::diagnostic_information(e));
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
    EXECUTOR_NAME_LOG(DEBUG) << BLOCK_NUMBER(params.number) << "Prepare request";

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(WARNING) << "TransactionExecutor is not running";
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"));
        return;
    }

    auto first = m_stateStorages.begin();
    if (first == m_stateStorages.end())
    {
        const auto* errorMessage = "Prepare failed: empty stateStorages";
        EXECUTOR_NAME_LOG(WARNING) << errorMessage;
        callback(BCOS_ERROR_PTR(-1, errorMessage));

        return;
    }

    if (first->number != params.number)
    {
        auto errorMessage =
            "Prepare failed: Request blockNumber: " +
            boost::lexical_cast<std::string>(params.number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(first->number) +
            " trigger switch";

        EXECUTOR_NAME_LOG(WARNING) << errorMessage;
        callback(BCOS_ERROR_PTR(ExecuteError::SCHEDULER_TERM_ID_ERROR, errorMessage));

        return;
    }

    bcos::protocol::TwoPCParams storageParams{params.number, params.primaryKey, params.timestamp};

    m_backendStorage->asyncPrepare(storageParams, *(first->storage),
        [this, callback = std::move(callback), blockNumber = params.number](
            auto&& error, uint64_t, const std::string&) {
            if (!m_isRunning)
            {
                callback(BCOS_ERROR_UNIQUE_PTR(
                    ExecuteError::STOPPED, "TransactionExecutor is not running"));
                return;
            }

            if (error)
            {
                auto errorMessage = "Prepare failed: " + error->errorMessage();

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
        EXECUTOR_NAME_LOG(WARNING) << "TransactionExecutor is not running";
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"));
        return;
    }

    auto first = m_stateStorages.begin();
    if (first == m_stateStorages.end())
    {
        auto errorMessage = "Commit failed: empty stateStorages";
        EXECUTOR_NAME_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(INVALID_BLOCKNUMBER, errorMessage));

        return;
    }

    if (first->number != params.number)
    {
        auto errorMessage =
            "Commit failed: Request blockNumber: " +
            boost::lexical_cast<std::string>(params.number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(first->number);

        EXECUTOR_NAME_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(INVALID_BLOCKNUMBER, errorMessage));

        return;
    }

    bcos::protocol::TwoPCParams storageParams{params.number, params.primaryKey, params.timestamp};
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
            auto errorMessage = "Commit failed: " + error->errorMessage();

            EXECUTOR_NAME_LOG(ERROR) << BLOCK_NUMBER(blockNumber) << errorMessage;
            callback(BCOS_ERROR_WITH_PREV_PTR(ExecuteError::COMMIT_ERROR, errorMessage, *error));
            return;
        }

        EXECUTOR_NAME_LOG(DEBUG) << BLOCK_NUMBER(blockNumber) << "Commit success";

        m_lastCommittedBlockNumber = blockNumber;
        m_ledgerCache->fetchCompatibilityVersion();
        auto version = m_ledgerCache->ledgerConfig()->compatibilityVersion();
        setBlockVersion(version);
        if (version >= BlockVersion::V3_3_VERSION)
        {
            m_ledgerCache->fetchAuthCheckStatus();
            if (m_ledgerCache->ledgerConfig()->authCheckStatus() != UINT32_MAX)
            {
                // cannot get auth check status, not update value
                m_isAuthCheck = !m_isWasm && m_ledgerCache->ledgerConfig()->authCheckStatus() != 0;
            }
        }
        removeCommittedState();

        callback(nullptr);
#ifdef USE_TCMALLOC
        // EXECUTOR_NAME_LOG(DEBUG) << BLOCK_NUMBER(blockNumber) << "TCMalloc release";
        // MallocExtension::instance()->ReleaseFreeMemory();
#endif
    });
}

void TransactionExecutor::rollback(
    const TwoPCParams& params, std::function<void(bcos::Error::Ptr)> callback)
{
    EXECUTOR_NAME_LOG(INFO) << BLOCK_NUMBER(params.number) << "Rollback request: ";

    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(WARNING) << "TransactionExecutor is not running";
        callback(
            BCOS_ERROR_UNIQUE_PTR(ExecuteError::STOPPED, "TransactionExecutor is not running"));
        return;
    }

    auto first = m_stateStorages.begin();
    if (first == m_stateStorages.end())
    {
        auto errorMessage = "Rollback failed: empty stateStorages";
        EXECUTOR_NAME_LOG(ERROR) << errorMessage;
        callback(BCOS_ERROR_PTR(-1, errorMessage));

        return;
    }

    if (first->number != params.number)
    {
        auto errorMessage =
            "Rollback failed: Request blockNumber: " +
            boost::lexical_cast<std::string>(params.number) +
            " not equal to last blockNumber: " + boost::lexical_cast<std::string>(first->number) +
            " trigger switch";

        EXECUTOR_NAME_LOG(WARNING) << errorMessage;
        callback(BCOS_ERROR_PTR(ExecuteError::SCHEDULER_TERM_ID_ERROR, errorMessage));

        return;
    }

    bcos::protocol::TwoPCParams storageParams{params.number, params.primaryKey, params.timestamp};
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
                auto errorMessage = "Rollback failed: " + error->errorMessage();

                EXECUTOR_NAME_LOG(WARNING) << BLOCK_NUMBER(blockNumber) << errorMessage;
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

    {
        std::unique_lock<std::shared_mutex> lock(m_stateStoragesMutex);
        if (!m_stateStorages.empty())
        {
            stateStorage = createStateStorage(m_stateStorages.back().storage, true, false);
        }
    }
    // create temp state storage
    if (!stateStorage)
    {
        if (m_cachedStorage)
        {
            stateStorage = createStateStorage(m_cachedStorage, true, false);
        }
        else
        {
            stateStorage = createStateStorage(m_backendStorage, true, false);
        }
    }

    std::string contractTableName = getContractTableName(executor::USER_APPS_PREFIX, contract);

    auto getCodeFromContractTable = [stateStorage, this](std::string_view contractTableName,
                                        decltype(callback) _callback) {
        stateStorage->asyncGetRow(contractTableName, ACCOUNT_CODE,
            [this, callback = std::move(_callback)](
                Error::UniquePtr error, std::optional<Entry> entry) {
                if (!m_isRunning)
                {
                    callback(BCOS_ERROR_UNIQUE_PTR(
                                 ExecuteError::STOPPED, "TransactionExecutor is not running"),
                        {});
                    return;
                }

                if (error)
                {
                    EXECUTOR_NAME_LOG(INFO) << "Get code failed: " << error->errorMessage();

                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "Get code failed", *error), {});
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
    };
    if (m_blockVersion >= uint32_t(bcos::protocol::BlockVersion::V3_1_VERSION))
    {
        auto codeHash = getCodeHash(contractTableName, stateStorage);
        // asyncGetRow key should not be empty
        auto codeKey = codeHash.empty() ? ACCOUNT_CODE : codeHash;
        // try to get abi from SYS_CODE_BINARY first
        ledger::Features features;
        task::syncWait(features.readFromStorage(*stateStorage, m_lastCommittedBlockNumber));
        stateStorage->asyncGetRow(bcos::ledger::SYS_CODE_BINARY, codeKey,
            [this, contractTableName, callback = std::move(callback),
                getCodeFromContractTable = std::move(getCodeFromContractTable),
                features = std::move(features)](
                Error::UniquePtr error, std::optional<Entry> entry) {
                if (!m_isRunning)
                {
                    callback(BCOS_ERROR_UNIQUE_PTR(
                                 ExecuteError::STOPPED, "TransactionExecutor is not running"),
                        {});
                    return;
                }

                if (error)
                {
                    EXECUTOR_NAME_LOG(INFO) << "Get code failed: " << error->errorMessage();

                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(-1, "Get code failed", *error), {});
                    return;
                }

                if (!entry)
                {
                    EXECUTOR_NAME_LOG(DEBUG)
                        << "Get code success, empty code, try to search in the contract table";
                    getCodeFromContractTable(contractTableName, std::move(callback));
                    return;
                }

                auto code = entry->getField(0);
                if ((features.get(ledger::Features::Flag::bugfix_eoa_as_contract) &&
                        bcos::precompiled::isDynamicPrecompiledAccountCode(code)) ||
                    (features.get(ledger::Features::Flag::bugfix_eoa_match_failed) &&
                        bcos::precompiled::matchDynamicAccountCode(code)))
                {
                    EXECUTOR_NAME_LOG(DEBUG) << "Get eoa code success, return empty code to evm";
                    callback(nullptr, bcos::bytes());
                }
                else
                {
                    EXECUTOR_NAME_LOG(DEBUG)
                        << "Get code success" << LOG_KV("code size", code.size());
                    auto codeBytes = bcos::bytes(code.begin(), code.end());
                    callback(nullptr, std::move(codeBytes));
                }
            });
        return;
    }
    getCodeFromContractTable(contractTableName, std::move(callback));
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
    {
        std::unique_lock<std::shared_mutex> lock(m_stateStoragesMutex);
        if (!m_stateStorages.empty())
        {
            stateStorage = createStateStorage(m_stateStorages.back().storage, true, false);
        }
    }
    // create temp state storage
    if (!stateStorage)
    {
        if (m_cachedStorage)
        {
            stateStorage = createStateStorage(m_cachedStorage, true, false);
        }
        else
        {
            stateStorage = createStateStorage(m_backendStorage, true, false);
        }
    }


    std::string contractTableName = getContractTableName(executor::USER_APPS_PREFIX, contract);
    auto getAbiFromContractTable = [stateStorage, this](std::string_view contractTableName,
                                       decltype(callback) _callback) {
        stateStorage->asyncGetRow(contractTableName, ACCOUNT_ABI,
            [this, callback = std::move(_callback)](
                Error::UniquePtr error, std::optional<Entry> entry) {
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
    };
    if (m_blockVersion >= uint32_t(bcos::protocol::BlockVersion::V3_1_VERSION))
    {
        auto codeHash = getCodeHash(contractTableName, stateStorage);
        // asyncGetRow key should not be empty
        std::string abiKey = codeHash.empty() ? std::string(ACCOUNT_ABI) : codeHash;
        // try to get abi from SYS_CONTRACT_ABI first
        EXECUTOR_LOG(TRACE) << LOG_DESC("get abi") << LOG_KV("abiKey", abiKey);

        stateStorage->asyncGetRow(bcos::ledger::SYS_CONTRACT_ABI, abiKey,
            [this, contractTableName, callback = std::move(callback),
                getAbiFromContractTable = std::move(getAbiFromContractTable)](
                Error::UniquePtr error, std::optional<Entry> entry) {
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

                // when get abi from SYS_CONTRACT_ABI failed, try to get abi from contract table
                if (!entry)
                {
                    EXECUTOR_NAME_LOG(DEBUG)
                        << "Get ABI failed, empty entry, try to search in the contract table";
                    getAbiFromContractTable(contractTableName, callback);
                    return;
                }

                auto abi = entry->getField(0);
                EXECUTOR_NAME_LOG(INFO) << "Get ABI success" << LOG_KV("ABI size", abi.size());
                callback(nullptr, std::string(abi));
            });
        return;
    }
    getAbiFromContractTable(contractTableName, std::move(callback));
}

ExecutiveFlowInterface::Ptr TransactionExecutor::getExecutiveFlow(
    std::shared_ptr<BlockContext> blockContext, std::string codeAddress, bool useCoroutine,
    bool isStaticCall)
{
    bcos::RecursiveGuard lock(x_executiveFlowLock);
    ExecutiveFlowInterface::Ptr executiveFlow = blockContext->getExecutiveFlow(codeAddress);
    if (executiveFlow == nullptr)
    {
        auto executiveFactory = std::make_shared<ExecutiveFactory>(
            *blockContext, m_evmPrecompiled, m_precompiled, m_staticPrecompiled, *m_gasInjector);
        if (!useCoroutine)
        {
            EXECUTOR_NAME_LOG(DEBUG) << "getExecutiveFlow" << LOG_KV("codeAddress", codeAddress)
                                     << LOG_KV("type", "ExecutiveSerialFlow");
            executiveFlow = std::make_shared<ExecutiveSerialFlow>(executiveFactory);
            executiveFlow->setThreadPool(m_threadPool);
            blockContext->setExecutiveFlow(codeAddress, executiveFlow);
        }
        else
        {
            EXECUTOR_NAME_LOG(DEBUG) << "getExecutiveFlow" << LOG_KV("codeAddress", codeAddress)
                                     << LOG_KV("type", "ExecutiveStackFlow");
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
                EXECUTOR_NAME_LOG(WARNING)
                    << "ExecutiveFlow asyncRun failed: " << LOG_KV("code", error->errorCode())
                    << LOG_KV("message", error->errorMessage());
                if (m_blockContext)
                {
                    m_blockContext->clear();
                }

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
                callback](
                Error::Ptr error, bcos::protocol::ConstTransactionsPtr transactions) mutable {
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

                ExecutiveFlowInterface::Ptr executiveFlow;

                executiveFlow = getExecutiveFlow(blockContext, callParameters->receiveAddress,
                    useCoroutine, input->staticCall());

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
                            EXECUTOR_LOG(WARNING) << "asyncExecuteExecutiveFlow failed: "
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
    case bcos::protocol::ExecutionMessage::PRE_FINISH:
    case bcos::protocol::ExecutionMessage::KEY_LOCK:
    {
        auto callParameters = createCallParameters(*input, input->staticCall());
        ExecutiveFlowInterface::Ptr executiveFlow = getExecutiveFlow(
            blockContext, callParameters->receiveAddress, useCoroutine, input->staticCall());
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
                    EXECUTOR_LOG(WARNING) << "asyncExecuteExecutiveFlow failed: "
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

inline std::string value2String(u256& value)
{
    if (value > 0)
    {
        return "0x" + value.str(256, std::ios_base::hex);
    }
    else
    {
        return {};
    }
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
    case CallParameters::PRE_FINISH:
        // Response message, Swap the from and to
        message->setFrom(std::move(params->receiveAddress));
        message->setTo(std::move(params->senderAddress));
        message->setType(ExecutionMessage::PRE_FINISH);
        break;
    }

    message->setContextID(params->contextID);
    message->setSeq(params->seq);
    message->setOrigin(std::move(params->origin));
    message->setGasAvailable(params->gas);

    message->setValue(value2String(params->value));
    // message->setGasLimit(params->gasLimit); // Notice: gasLimit will get from storage when
    // execute

    // Notice: gasPrice should pass here,
    // could not fetch from storage during execution for future user will set in a tx
    message->setGasPrice(value2String(params->gasPrice));
    message->setMaxFeePerGas(value2String(params->maxFeePerGas));
    message->setMaxPriorityFeePerGas(value2String(params->maxPriorityFeePerGas));
    message->setEffectiveGasPrice(value2String(params->effectiveGasPrice));

    message->setData(std::move(params->data));
    message->setStaticCall(params->staticCall);
    message->setCreate(params->create);
    message->setInternalCreate(params->internalCreate);
    message->setInternalCall(params->internalCall);
    if (params->createSalt)
    {
        message->setCreateSalt(*params->createSalt);
    }

    message->setEvmStatus(params->evmStatus);
    message->setStatus(params->status);
    message->setMessage(std::move(params->message));
    message->setLogEntries(std::move(params->logEntries));
    message->setNewEVMContractAddress(std::move(params->newEVMContractAddress));
    message->setDelegateCall(params->delegateCall);
    message->setDelegateCallAddress(std::move(params->codeAddress));
    message->setDelegateCallCode(std::move(params->delegateCallCode));
    message->setDelegateCallSender(std::move(params->delegateCallSender));
    message->setHasContractTableChanged(params->hasContractTableChanged);

    return message;
}

void TransactionExecutor::removeCommittedState()
{
    if (m_stateStorages.empty())
    {
        EXECUTOR_NAME_LOG(ERROR) << "Remove committed state failed, empty states";
        return;
    }

    bcos::protocol::BlockNumber number = 0;
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
        it = m_stateStorages.erase(it);
        if (it != m_stateStorages.end())
        {
            EXECUTOR_NAME_LOG(INFO)
                << "Set state number, " << it->number << " prev to cachedStorage"
                << LOG_KV("stateStorageSize", m_stateStorages.size());
            it->storage->setPrev(m_cachedStorage);
        }
    }
    else if (m_backendStorage)
    {
        std::unique_lock<std::shared_mutex> lock(m_stateStoragesMutex);
        auto it = m_stateStorages.begin();
        EXECUTOR_NAME_LOG(DEBUG) << LOG_DESC("removeCommittedState")
                                 << LOG_KV("commitNumber", number)
                                 << LOG_KV("erasedStorage", it->number)
                                 << LOG_KV("stateStorageSize", m_stateStorages.size());

        it = m_stateStorages.erase(it);
        if (it != m_stateStorages.end())
        {
            it->storage->setPrev(m_backendStorage);
        }
    }

    m_ledgerCache->clearCacheByNumber(number);
}

std::unique_ptr<CallParameters> TransactionExecutor::createCallParameters(
    bcos::protocol::ExecutionMessage& input, bool staticCall)
{
    auto callParameters = std::make_unique<CallParameters>(CallParameters::MESSAGE);
    callParameters->status = input.status();

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
    case ExecutionMessage::PRE_FINISH:
    {
        // just set to finish type
        callParameters->type = CallParameters::FINISHED;
    }
    }

    callParameters->contextID = input.contextID();
    callParameters->seq = input.seq();
    constexpr static auto addressSize = Address::SIZE * 2;
    if (staticCall)
    {
        // padding zero
        callParameters->origin = std::string(addressSize - input.origin().size(), '0');
        // NOTE: if wasm and use dmc static call external call, should not padding zero, because it
        // is contract address
        if (!(m_isWasm && input.origin() != input.from())) [[unlikely]]
        {
            callParameters->senderAddress = std::string(addressSize - input.from().size(), '0');
        }
    }
    callParameters->origin += input.origin();
    callParameters->senderAddress += input.from();
    callParameters->receiveAddress = input.to();
    callParameters->codeAddress = input.to();
    callParameters->create = input.create();
    callParameters->internalCreate = input.internalCreate();
    callParameters->internalCall = input.internalCall();
    callParameters->evmStatus = input.evmStatus();
    callParameters->message = input.message();
    callParameters->data = input.takeData();
    callParameters->gas = input.gasAvailable();
    callParameters->staticCall = staticCall;
    callParameters->newEVMContractAddress = input.newEVMContractAddress();
    callParameters->keyLocks = input.takeKeyLocks();
    callParameters->logEntries = input.takeLogEntries();
    if (input.create())
    {
        callParameters->abi = input.abi();
    }
    callParameters->delegateCall = input.delegateCall();
    callParameters->delegateCallCode = input.takeDelegateCallCode();
    callParameters->delegateCallSender = input.delegateCallSender();
    if (input.delegateCall())
    {
        callParameters->codeAddress = input.delegateCallAddress();
    }

    if (!m_isWasm && !callParameters->create)
    {
        if (callParameters->codeAddress.size() < addressSize) [[unlikely]]
        {
            callParameters->codeAddress.insert(
                0, addressSize - callParameters->codeAddress.size(), '0');
        }
        if (callParameters->receiveAddress.size() < addressSize) [[unlikely]]
        {
            callParameters->receiveAddress.insert(
                0, addressSize - callParameters->receiveAddress.size(), '0');
        }
    }
    // FIXME)): if input is hex without prefix, will throw exception and abort coredump
    callParameters->value = u256(input.value());
    callParameters->gasPrice = u256(input.gasPrice());
    callParameters->gasLimit = input.gasLimit();
    callParameters->effectiveGasPrice = u256(input.effectiveGasPrice());
    callParameters->maxFeePerGas = u256(input.maxFeePerGas());
    callParameters->maxPriorityFeePerGas = u256(input.maxPriorityFeePerGas());

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
    callParameters->evmStatus = input.evmStatus();
    callParameters->create = input.create();
    callParameters->internalCreate = input.internalCreate();
    callParameters->internalCall = input.internalCall();
    callParameters->message = input.message();
    callParameters->data = tx.input().toBytes();
    callParameters->keyLocks = input.takeKeyLocks();
    callParameters->logEntries = input.takeLogEntries();
    callParameters->abi = tx.abi();
    callParameters->delegateCall = false;
    callParameters->delegateCallCode = bytes();
    callParameters->delegateCallSender = "";
    callParameters->value = u256(input.value());
    callParameters->gasPrice = u256(input.gasPrice());
    callParameters->gasLimit = input.gasLimit();
    callParameters->maxFeePerGas = u256(input.maxFeePerGas());
    callParameters->maxPriorityFeePerGas = u256(input.maxPriorityFeePerGas());


    if (!m_isWasm && !callParameters->create)
    {
        constexpr static auto addressSize = Address::SIZE * 2;
        if (callParameters->codeAddress.size() < addressSize) [[unlikely]]
        {
            callParameters->codeAddress.insert(
                0, addressSize - callParameters->codeAddress.size(), '0');
        }
        if (callParameters->receiveAddress.size() < addressSize) [[unlikely]]
        {
            callParameters->receiveAddress.insert(
                0, addressSize - callParameters->receiveAddress.size(), '0');
        }
    }
    return callParameters;
}


void TransactionExecutor::executeTransactionsWithCriticals(
    critical::CriticalFieldsInterface::Ptr criticals,
    gsl::span<std::unique_ptr<CallParameters>> inputs,
    vector<protocol::ExecutionMessage::UniquePtr>& executionResults)
{
    // DAG run
    shared_ptr<TxDAGInterface> txDag = make_shared<TxDAG2>();
    txDag->setExecuteTxFunc([this, &inputs, &executionResults](ID id) {
        if (!m_isRunning)
        {
            return;
        }

        auto& input = inputs[id];
        auto executiveFactory = std::make_shared<ExecutiveFactory>(
            *m_blockContext, m_evmPrecompiled, m_precompiled, m_staticPrecompiled, *m_gasInjector);
        auto executive = executiveFactory->build(
            input->codeAddress, input->contextID, input->seq, ExecutiveType::common);

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
            EXECUTOR_NAME_LOG(WARNING)
                << "executeTransactionsWithCriticals error: " << boost::diagnostic_information(e);
        }
    });

    txDag->init(criticals);

    txDag->run(m_DAGThreadNum);
}

bcos::storage::StateStorageInterface::Ptr TransactionExecutor::createStateStorage(
    bcos::storage::StorageInterface::Ptr storage, bool ignoreNotExist, bool setRowWithDirtyFlag)
{
    auto stateStorage = m_stateStorageFactory->createStateStorage(std::move(storage),
        m_blockVersion, setRowWithDirtyFlag, ignoreNotExist, m_keyPageIgnoreTables);
    return stateStorage;
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

protocol::BlockHeader::Ptr TransactionExecutor::getBlockHeaderInStorage(
    protocol::BlockNumber number)
{
    std::promise<protocol::BlockHeader::Ptr> blockHeaderFuture;

    m_ledger->asyncGetBlockDataByNumber(number, bcos::ledger::HEADER,
        [this, &blockHeaderFuture](Error::Ptr error, Block::Ptr block) {
            if (error)
            {
                EXECUTOR_NAME_LOG(INFO) << "Get getBlockHeader from storage failed";
                blockHeaderFuture.set_value(nullptr);
            }
            else
            {
                blockHeaderFuture.set_value(block->blockHeader());
            }
        });


    return blockHeaderFuture.get_future().get();
}

std::string TransactionExecutor::getCodeHash(
    std::string_view tableName, storage::StateStorageInterface::Ptr const& stateStorage)
{
    std::promise<std::string> codeHashPromise;
    stateStorage->asyncGetRow(
        tableName, ACCOUNT_CODE_HASH, [&codeHashPromise, this](auto&& error, auto&& entry) mutable {
            if (error || !entry)
            {
                EXECUTOR_NAME_LOG(DEBUG) << "Get codeHashes success, empty codeHash";
                codeHashPromise.set_value(std::string());
                return;
            }
            auto codeHash = std::string(entry->getField(0));
            codeHashPromise.set_value(std::move(codeHash));
        });
    return codeHashPromise.get_future().get();
}


void TransactionExecutor::stop()
{
    EXECUTOR_NAME_LOG(INFO) << "Try to stop executor";
    if (!m_isRunning)
    {
        EXECUTOR_NAME_LOG(INFO) << "Executor has just tried to stop";
        return;
    }
    m_isRunning = false;
    if (m_blockContext)
    {
        m_blockContext->stop();
    }
}
