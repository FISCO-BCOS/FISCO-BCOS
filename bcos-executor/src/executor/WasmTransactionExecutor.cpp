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
 * @brief WasmTransactionExecutor
 * @file WasmTransactionExecutor.cpp
 * @author: jimmyshi
 * @date: 2022-01-18
 */

#include "WasmTransactionExecutor.h"
#include "../Common.h"
#include "../dag/Abi.h"
#include "../dag/ClockCache.h"
#include "../dag/CriticalFields.h"
#include "../dag/ScaleUtils.h"
#include "../dag/TxDAG.h"
#include "../dag/TxDAG2.h"
#include "../precompiled/ConsensusPrecompiled.h"
#include "../precompiled/CryptoPrecompiled.h"
#include "../precompiled/FileSystemPrecompiled.h"
#include "../precompiled/KVTableFactoryPrecompiled.h"
#include "../precompiled/ParallelConfigPrecompiled.h"
#include "../precompiled/SystemConfigPrecompiled.h"
#include "../precompiled/extension/ContractAuthPrecompiled.h"
#include "../precompiled/extension/DagTransferPrecompiled.h"
#include "../vm/gas_meter/GasInjector.h"
#include "bcos-codec/abi/ContractABIType.h"
#include "bcos-framework/interfaces/executor/ExecutionMessage.h"
#include "bcos-framework/interfaces/executor/PrecompiledTypeDef.h"
#include "bcos-framework/interfaces/protocol/ProtocolTypeDef.h"
#include "bcos-framework/interfaces/protocol/TransactionReceipt.h"
#include "bcos-framework/interfaces/storage/StorageInterface.h"
#include "bcos-table/src/StateStorage.h"
#include "bcos-utilities/Error.h"
#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/format/format_fwd.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <cassert>
#include <exception>
#include <functional>
#include <iterator>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

using namespace bcos;
using namespace std;
using namespace bcos::executor;
using namespace bcos::executor::critical;
using namespace bcos::wasm;
using namespace bcos::protocol;
using namespace bcos::storage;
using namespace bcos::precompiled;

CriticalFields<bytes>::CriticalFieldPtr decodeConflictFields(const FunctionAbi& functionAbi,
    const CallParameters& params, std::shared_ptr<BlockContext> _blockContext)
{
    if (functionAbi.conflictFields.empty())
    {
        return nullptr;
    }

    const auto& to = params.receiveAddress;
    auto hasher = boost::hash<string_view>();
    auto toHash = hasher(to);

    auto conflictFields = vector<vector<bytes>>();

    for (auto& conflictField : functionAbi.conflictFields)
    {
        auto slotBytes = bytes();
        auto valueBytes = bytes();

        size_t slot = toHash + conflictField.slot;
        auto slotBegin = (uint8_t*)static_cast<void*>(&slot);
        slotBytes.insert(slotBytes.end(), slotBegin, slotBegin + sizeof(slot));

        EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_KV("to", to)
                            << LOG_KV("functionName", functionAbi.name)
                            << LOG_KV("slot", (int)conflictField.slot);

        switch (conflictField.kind)
        {
        case All:
        {
            EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `All`");
            break;
        }
        case Len:
        {
            EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `Len`");
            break;
        }
        case Env:
        {
            assert(conflictField.accessPath.size() == 1);

            auto envKind = conflictField.accessPath[0];
            switch (envKind)
            {
            case EnvKind::Caller:
            {
                const auto& sender = params.senderAddress;
                valueBytes.insert(valueBytes.end(), sender.begin(), sender.end());

                EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `Caller`")
                                    << LOG_KV("caller", sender);
                break;
            }
            case EnvKind::Origin:
            {
                const auto& sender = params.origin;
                valueBytes.insert(valueBytes.end(), sender.begin(), sender.end());

                EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `Origin`")
                                    << LOG_KV("origin", sender);
                break;
            }
            case EnvKind::Now:
            {
                auto now = _blockContext->timestamp();
                auto bytes = static_cast<bcos::byte*>(static_cast<void*>(&now));
                valueBytes.insert(valueBytes.end(), bytes, bytes + sizeof(now));

                EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `Now`")
                                    << LOG_KV("now", now);
                break;
            }
            case EnvKind::BlockNumber:
            {
                auto blockNumber = _blockContext->number();
                auto bytes = static_cast<bcos::byte*>(static_cast<void*>(&blockNumber));
                valueBytes.insert(valueBytes.end(), bytes, bytes + sizeof(blockNumber));

                EXECUTOR_LOG(DEBUG)
                    << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `BlockNumber`")
                    << LOG_KV("functionName", functionAbi.name)
                    << LOG_KV("blockNumber", blockNumber);
                break;
            }
            case EnvKind::Addr:
            {
                valueBytes.insert(valueBytes.end(), to.begin(), to.end());

                EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `Addr`")
                                    << LOG_KV("addr", to);
                break;
            }
            default:
            {
                EXECUTOR_LOG(ERROR) << LOG_BADGE("unknown env kind in conflict field")
                                    << LOG_KV("envKind", envKind);
                return nullptr;
            }
            }
            break;
        }
        case Var:
        {
            assert(!conflictField.accessPath.empty());
            const ParameterAbi* paramAbi = nullptr;
            auto components = &functionAbi.inputs;
            auto inputData = ref(params.data).getCroppedData(4).toBytes();

            auto startPos = 0u;
            for (auto segment : conflictField.accessPath)
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
            bytes var(inputData.begin() + startPos, inputData.begin() + startPos + length.value());
            valueBytes.insert(valueBytes.end(), var.begin(), var.end());


            EXECUTOR_LOG(DEBUG) << LOG_BADGE("decodeConflictFields") << LOG_DESC("use `Var`")
                                << LOG_KV("functionName", functionAbi.name)
                                << LOG_KV("var", toHexStringWithPrefix(var));
            break;
        }
        default:
        {
            EXECUTOR_LOG(ERROR) << LOG_BADGE("unknown conflict field kind")
                                << LOG_KV("conflictFieldKind", conflictField.kind);
            return nullptr;
        }
        }

        vector<bytes> field = {slotBytes};
        if (!valueBytes.empty())
        {
            field.emplace_back(std::move(valueBytes));
        }

        conflictFields.emplace_back(std::move(field));
    }
    return make_shared<CriticalFields<bytes>::CriticalField>(std::move(conflictFields));
}

void WasmTransactionExecutor::dagExecuteTransactions(
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


void WasmTransactionExecutor::dagExecuteTransactionsInternal(
    gsl::span<std::unique_ptr<CallParameters>> inputs,
    std::function<void(
        bcos::Error::UniquePtr, std::vector<bcos::protocol::ExecutionMessage::UniquePtr>)>
        callback)
{
    auto transactionsNum = inputs.size();
    auto executionResults = vector<ExecutionMessage::UniquePtr>(transactionsNum);

    CriticalFields<bytes>::Ptr txsCriticals = make_shared<CriticalFields<bytes>>(transactionsNum);

    mutex tableMutex;
    tbb::parallel_for(tbb::blocked_range<uint64_t>(0, transactionsNum),
        [&](const tbb::blocked_range<uint64_t>& range) {
            // for (auto i = 0; i < transactionsNum; i++ )
            for (auto i = range.begin(); i != range.end(); ++i)
            {
                auto defaultExecutionResult = m_executionMessageFactory->createExecutionMessage();
                executionResults[i].swap(defaultExecutionResult);

                const auto& params = inputs[i];

                const auto& to = params->receiveAddress;
                const auto& input = params->data;

                if (params->create)
                {
                    executionResults[i] = toExecutionResult(std::move(inputs[i]));
                    executionResults[i]->setType(ExecutionMessage::SEND_BACK);
                    continue;
                }

                auto selector = ref(input).getCroppedData(0, 4);
                auto abiKey = bytes(to.cbegin(), to.cend());
                abiKey.insert(abiKey.end(), selector.begin(), selector.end());

                auto cacheHandle = m_abiCache->lookup(abiKey);
                CriticalFields<bytes>::CriticalFieldPtr conflictFields = nullptr;
                if (!cacheHandle.isValid())
                {
                    EXECUTOR_LOG(DEBUG) << LOG_BADGE("dagExecuteTransactionsForWasm")
                                        << LOG_DESC("No ABI found in cache, try to load")
                                        << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));

                    std::lock_guard guard(tableMutex);

                    cacheHandle = m_abiCache->lookup(abiKey);
                    if (cacheHandle.isValid())
                    {
                        EXECUTOR_LOG(DEBUG) << LOG_BADGE("dagExecuteTransactionsForWasm")
                                            << LOG_DESC("ABI had beed loaded by other workers")
                                            << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));
                        auto& functionAbi = cacheHandle.value();
                        conflictFields = decodeConflictFields(functionAbi, *params, m_blockContext);
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
                            EXECUTOR_LOG(WARNING) << LOG_BADGE("dagExecuteTransactionsForWasm")
                                                  << LOG_DESC("No ABI found, please deploy first")
                                                  << LOG_KV("tableName", tableName);
                            continue;
                        }

                        auto entry = table->getRow(ACCOUNT_ABI);
                        auto abiStr = entry->getField(0);

                        EXECUTOR_LOG(DEBUG) << LOG_BADGE("dagExecuteTransactionsForWasm")
                                            << LOG_DESC("ABI loaded") << LOG_KV("ABI", abiStr);

                        auto functionAbi =
                            FunctionAbi::deserialize(abiStr, selector.toBytes(), m_hashImpl);
                        if (!functionAbi)
                        {
                            executionResults[i] = toExecutionResult(std::move(inputs[i]));
                            executionResults[i]->setType(ExecutionMessage::SEND_BACK);
                            // If abi is not valid, we don't impact the cache. In such a
                            // situation, if the caller invokes this method over and over
                            // again, executor will read the contract table repeatedly,
                            // which may cause performance loss. But we think occurrence
                            // of invalid abi is impossible in actual situations.
                            continue;
                        }

                        auto abiPtr = functionAbi.get();
                        if (m_abiCache->insert(abiKey, abiPtr, &cacheHandle))
                        {
                            // If abi object had been inserted into the cache successfully,
                            // the cache will take charge of life time management of the
                            // object. After this object being eliminated, the cache will
                            // delete its memory storage.
                            std::ignore = functionAbi.release();
                        }
                        conflictFields = decodeConflictFields(*abiPtr, *params, m_blockContext);
                    }
                }
                else
                {
                    EXECUTOR_LOG(DEBUG) << LOG_BADGE("dagExecuteTransactionsForWasm")
                                        << LOG_DESC("Found ABI in cache")
                                        << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));
                    auto& functionAbi = cacheHandle.value();
                    conflictFields = decodeConflictFields(functionAbi, *params, m_blockContext);
                }

                if (conflictFields == nullptr)
                {
                    EXECUTOR_LOG(DEBUG)
                        << LOG_BADGE("dagExecuteTransactionsForWasm")
                        << LOG_DESC("The transaction can't be executed concurrently")
                        << LOG_KV("abiKey", toHexStringWithPrefix(abiKey));
                    executionResults[i]->setType(ExecutionMessage::SEND_BACK);
                    continue;
                }
                txsCriticals->put(i, conflictFields);
            }
        });


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

BlockContext::Ptr WasmTransactionExecutor::createBlockContext(
    const protocol::BlockHeader::ConstPtr& currentHeader, storage::StateStorage::Ptr storage,
    storage::StorageInterface::Ptr lastStorage)
{
    BlockContext::Ptr context = make_shared<BlockContext>(
        storage, lastStorage, m_hashImpl, currentHeader, FiscoBcosScheduleV4, true, m_isAuthCheck);

    return context;
}

std::shared_ptr<BlockContext> WasmTransactionExecutor::createBlockContext(
    bcos::protocol::BlockNumber blockNumber, h256 blockHash, uint64_t timestamp,
    int32_t blockVersion, storage::StateStorage::Ptr storage)
{
    BlockContext::Ptr context = make_shared<BlockContext>(storage, m_hashImpl, blockNumber,
        blockHash, timestamp, blockVersion, FiscoBcosScheduleV4, true, m_isAuthCheck);

    return context;
}

void WasmTransactionExecutor::initPrecompiled()
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

    // in WASM
    m_constantPrecompiled.insert({SYS_CONFIG_NAME, sysConfig});
    m_constantPrecompiled.insert({CONSENSUS_NAME, consensusPrecompiled});
    m_constantPrecompiled.insert({PARALLEL_CONFIG_NAME, parallelConfigPrecompiled});
    // FIXME: not support crud now
    // m_constantPrecompiled.insert({TABLE_NAME, tableFactoryPrecompiled});
    m_constantPrecompiled.insert({KV_TABLE_NAME, kvTableFactoryPrecompiled});
    m_constantPrecompiled.insert(
        {DAG_TRANSFER_NAME, std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl)});
    m_constantPrecompiled.insert({CRYPTO_NAME, std::make_shared<CryptoPrecompiled>(m_hashImpl)});
    m_constantPrecompiled.insert(
        {BFS_NAME, std::make_shared<precompiled::FileSystemPrecompiled>(m_hashImpl)});
    m_constantPrecompiled.insert(
        {CONTRACT_AUTH_NAME, std::make_shared<precompiled::ContractAuthPrecompiled>(m_hashImpl)});

    set<string> builtIn = {CRYPTO_NAME};
    m_builtInPrecompiled = make_shared<set<string>>(builtIn);
}
