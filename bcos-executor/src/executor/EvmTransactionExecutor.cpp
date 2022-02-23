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
 * @brief EvmTransactionExecutor
 * @file EvmTransactionExecutor.cpp
 * @author: jimmyshi
 * @date: 2022-01-18
 */

#include "EvmTransactionExecutor.h"
#include "../Common.h"
#include "../dag/Abi.h"
#include "../dag/ClockCache.h"
#include "../dag/CriticalFields.h"
#include "../dag/ScaleUtils.h"
#include "../dag/TxDAG.h"
#include "../dag/TxDAG2.h"
#include "../executive/BlockContext.h"
#include "../executive/TransactionExecutive.h"
#include "../precompiled/Common.h"
#include "../precompiled/ConsensusPrecompiled.h"
#include "../precompiled/CryptoPrecompiled.h"
#include "../precompiled/FileSystemPrecompiled.h"
#include "../precompiled/KVTableFactoryPrecompiled.h"
#include "../precompiled/ParallelConfigPrecompiled.h"
#include "../precompiled/PrecompiledResult.h"
#include "../precompiled/SystemConfigPrecompiled.h"
#include "../precompiled/TableFactoryPrecompiled.h"
#include "../precompiled/Utilities.h"
#include "../precompiled/extension/ContractAuthPrecompiled.h"
#include "../precompiled/extension/DagTransferPrecompiled.h"
#include "../precompiled/extension/UserPrecompiled.h"
#include "../vm/Precompiled.h"
#include "../vm/gas_meter/GasInjector.h"
#include "bcos-codec/abi/ContractABIType.h"
#include "bcos-framework/interfaces/dispatcher/SchedulerInterface.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/executor/PrecompiledTypeDef.h"
#include "bcos-framework/interfaces/ledger/LedgerTypeDef.h"
#include "bcos-framework/interfaces/protocol/ProtocolTypeDef.h"
#include "bcos-framework/interfaces/protocol/TransactionReceipt.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-framework/interfaces/storage/Table.h"
#include "bcos-protocol/LogEntry.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-utilities/Error.h"
#include "bcos-utilities/ThreadPool.h"
#include "tbb/flow_graph.h"
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
#include <cassert>
#include <exception>
#include <functional>
#include <gsl/gsl_util>
#include <iterator>
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

CriticalFields<string>::CriticalFieldPtr getTxCriticals(const CallParameters& _params,
    // temp code params
    crypto::Hash::Ptr _hashImpl, TransactionExecutive::Ptr _executive, bool _isWasm)
{
    if (_params.create)
    {
        // Not to parallel contract creation transaction
        return nullptr;
    }

    // temp executive
    // auto executive = createExecutive(m_blockContext, std::string(_params.receiveAddress), 0, 0);
    auto p = _executive->getPrecompiled(_params.receiveAddress);
    if (p)
    {
        // Precompile transaction
        if (p->isParallelPrecompiled())
        {
            auto criticals = vector<string>(p->getParallelTag(ref(_params.data), _isWasm));
            vector<vector<string>> ret;
            for (string& critical : criticals)
            {
                critical += _params.receiveAddress;
                ret.push_back({critical});
            }
            return make_shared<vector<vector<string>>>(std::move(ret));
        }
        return nullptr;
    }
    uint32_t selector = precompiled::getParamFunc(ref(_params.data));

    auto receiveAddress = _params.receiveAddress;
    std::shared_ptr<precompiled::ParallelConfig> config = nullptr;
    // hit the cache, fetch ParallelConfig from the cache directly
    // Note: Only when initializing DAG, get ParallelConfig, will not get
    // during transaction execution
    // TODO: add parallel config cache
    // auto parallelKey = std::make_pair(string(receiveAddress), selector);
    auto parallelConfigPrecompiled =
        std::make_shared<precompiled::ParallelConfigPrecompiled>(_hashImpl);

    EXECUTOR_LOG(TRACE) << LOG_DESC("[getTxCriticals] get parallel config")
                        << LOG_KV("receiveAddress", receiveAddress) << LOG_KV("selector", selector)
                        << LOG_KV("sender", _params.origin);

    config = parallelConfigPrecompiled->getParallelConfig(
        _executive, receiveAddress, selector, _params.origin);

    if (config == nullptr)
    {
        return nullptr;
    }
    // Testing code
    auto criticals = vector<string>();

    codec::abi::ABIFunc af;
    bool isOk = af.parser(config->functionName);
    if (!isOk)
    {
        EXECUTOR_LOG(DEBUG) << LOG_DESC("[getTxCriticals] parser function signature failed, ")
                            << LOG_KV("func signature", config->functionName);

        return nullptr;
    }

    auto paramTypes = af.getParamsType();
    if (paramTypes.size() < (size_t)config->criticalSize)
    {
        EXECUTOR_LOG(DEBUG) << LOG_DESC("[getTxCriticals] params type less than  criticalSize")
                            << LOG_KV("func signature", config->functionName)
                            << LOG_KV("func criticalSize", config->criticalSize);

        return nullptr;
    }

    paramTypes.resize((size_t)config->criticalSize);

    codec::abi::ContractABICodec abi(_hashImpl);
    isOk = abi.abiOutByFuncSelector(ref(_params.data).getCroppedData(4), paramTypes, criticals);
    if (!isOk)
    {
        EXECUTOR_LOG(DEBUG) << LOG_DESC("[getTxCriticals] abiout failed, ")
                            << LOG_KV("func signature", config->functionName);

        return nullptr;
    }

    vector<vector<string>> ret;
    for (string& critical : criticals)
    {
        critical += _params.receiveAddress;
        ret.push_back({critical});
    }
    return make_shared<vector<vector<string>>>(std::move(ret));
}

void EvmTransactionExecutor::dagExecuteTransactions(
    gsl::span<bcos::protocol::ExecutionMessage::UniquePtr> inputs,
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        callback)
{
    // for fill block
    auto txHashes = make_shared<HashList>();
    std::vector<decltype(inputs)::index_type> indexes;
    auto fillInputs = std::make_shared<std::vector<bcos::protocol::ExecutionMessage::UniquePtr>>();

    // final result
    auto callParametersList =
        std::make_shared<std::vector<CallParameters::UniquePtr>>(inputs.size());

#pragma omp parallel for
    for (decltype(inputs)::index_type i = 0; i < inputs.size(); ++i)
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
            EXECUTOR_LOG(ERROR) << "DAG Execute error, " << message;
            // callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::DAG_ERROR, message), {});
            break;
        }
        }
    }
    if (!txHashes->empty())
    {
        m_txpool->asyncFillBlock(txHashes,
            [this, indexes = std::move(indexes), fillInputs = std::move(fillInputs),
                callParametersList = std::move(callParametersList), callback = std::move(callback),
                txHashes](Error::Ptr error, protocol::TransactionsPtr transactions) mutable {
                if (error)
                {
                    auto errorMessage = "asyncFillBlock failed";
                    EXECUTOR_LOG(ERROR) << errorMessage << boost::diagnostic_information(*error);
                    callback(BCOS_ERROR_WITH_PREV_UNIQUE_PTR(
                                 ExecuteError::DAG_ERROR, errorMessage, *error),
                        {});
                    return;
                }

#pragma omp parallel for
                for (size_t i = 0; i < transactions->size(); ++i)
                {
                    assert(transactions->at(i));
                    callParametersList->at(indexes[i]) =
                        createCallParameters(*fillInputs->at(i), *transactions->at(i));
                }

                dagExecuteTransactionsInternal(*callParametersList, std::move(callback));
            });
    }
    else
    {
        dagExecuteTransactionsInternal(*callParametersList, std::move(callback));
    }
}

void EvmTransactionExecutor::dagExecuteTransactionsInternal(
    gsl::span<std::unique_ptr<CallParameters>> inputs,
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        callback)
{
    auto transactionsNum = inputs.size();
    vector<ExecutionMessage::UniquePtr> executionResults(transactionsNum);

    // get criticals
    CriticalFields<string>::Ptr txsCriticals = make_shared<CriticalFields<string>>(transactionsNum);
#pragma omp parallel for
    for (decltype(transactionsNum) i = 0; i < transactionsNum; i++)
    {
        auto& input = inputs[i];
        auto contextID = input->contextID;
        auto seq = input->seq;

        auto executive = createExecutive(m_blockContext, input->codeAddress, contextID, seq);
        CriticalFields<string>::CriticalFieldPtr criticals =
            getTxCriticals(*inputs[i], m_hashImpl, executive, false);
        txsCriticals->put(i, criticals);
        if (criticals == nullptr)
        {
            executionResults[i] = toExecutionResult(std::move(inputs[i]));
            executionResults[i]->setType(ExecutionMessage::SEND_BACK);
        }
    }

    // DAG run
    try
    {
        executeTransactionsWithCriticals(txsCriticals, inputs, executionResults);
    }
    catch (exception& e)
    {
        EXECUTOR_LOG(ERROR) << LOG_BADGE("executeBlock")
                            << LOG_DESC("Error during parallel block execution")
                            << LOG_KV("EINFO", boost::diagnostic_information(e));
        callback(BCOS_ERROR_UNIQUE_PTR(ExecuteError::CALL_ERROR, boost::diagnostic_information(e)),
            vector<ExecutionMessage::UniquePtr>{});
        return;
    }

    callback(nullptr, std::move(executionResults));
}

BlockContext::Ptr EvmTransactionExecutor::createBlockContext(
    const protocol::BlockHeader::ConstPtr& currentHeader, storage::StateStorage::Ptr storage,
    storage::StorageInterface::Ptr lastStorage)
{
    BlockContext::Ptr context = make_shared<BlockContext>(
        storage, lastStorage, m_hashImpl, currentHeader, FiscoBcosScheduleV4, false, m_isAuthCheck);

    return context;
}

std::shared_ptr<BlockContext> EvmTransactionExecutor::createBlockContext(
    bcos::protocol::BlockNumber blockNumber, h256 blockHash, uint64_t timestamp,
    int32_t blockVersion, storage::StateStorage::Ptr storage)
{
    BlockContext::Ptr context = make_shared<BlockContext>(storage, m_hashImpl, blockNumber,
        blockHash, timestamp, blockVersion, FiscoBcosScheduleV4, false, m_isAuthCheck);

    return context;
}

void EvmTransactionExecutor::initPrecompiled()
{
    auto fillZero = [](int _num) -> std::string {
        std::stringstream stream;
        stream << std::setfill('0') << std::setw(40) << std::hex << _num;
        return stream.str();
    };
    m_precompiledContract =
        std::make_shared<std::map<std::string, std::shared_ptr<PrecompiledContract>>>();
    m_builtInPrecompiled = std::make_shared<std::set<std::string>>();

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

    auto sysConfig = std::make_shared<precompiled::SystemConfigPrecompiled>(m_hashImpl);
    auto parallelConfigPrecompiled =
        std::make_shared<precompiled::ParallelConfigPrecompiled>(m_hashImpl);
    auto consensusPrecompiled = std::make_shared<precompiled::ConsensusPrecompiled>(m_hashImpl);
    // FIXME: not support crud now
    // auto tableFactoryPrecompiled =
    // std::make_shared<precompiled::TableFactoryPrecompiled>(m_hashImpl);
    auto kvTableFactoryPrecompiled =
        std::make_shared<precompiled::KVTableFactoryPrecompiled>(m_hashImpl);

    // in EVM
    m_constantPrecompiled.insert({SYS_CONFIG_ADDRESS, sysConfig});
    m_constantPrecompiled.insert({CONSENSUS_ADDRESS, consensusPrecompiled});
    m_constantPrecompiled.insert({PARALLEL_CONFIG_ADDRESS, parallelConfigPrecompiled});
    // FIXME: not support crud now
    // m_constantPrecompiled.insert({TABLE_ADDRESS, tableFactoryPrecompiled});
    m_constantPrecompiled.insert({KV_TABLE_ADDRESS, kvTableFactoryPrecompiled});
    m_constantPrecompiled.insert(
        {DAG_TRANSFER_ADDRESS, std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl)});
    m_constantPrecompiled.insert({CRYPTO_ADDRESS, std::make_shared<CryptoPrecompiled>(m_hashImpl)});
    m_constantPrecompiled.insert(
        {BFS_ADDRESS, std::make_shared<precompiled::FileSystemPrecompiled>(m_hashImpl)});
    m_constantPrecompiled.insert({CONTRACT_AUTH_ADDRESS,
        std::make_shared<precompiled::ContractAuthPrecompiled>(m_hashImpl)});
    CpuHeavyPrecompiled::registerPrecompiled(m_constantPrecompiled, m_hashImpl);
    set<string> builtIn = {CRYPTO_ADDRESS};
    m_builtInPrecompiled = make_shared<set<string>>(builtIn);
}