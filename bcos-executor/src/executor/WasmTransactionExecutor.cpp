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

BlockContext::Ptr WasmTransactionExecutor::createBlockContext(
    const protocol::BlockHeader::ConstPtr& currentHeader, storage::StateStorage::Ptr storage,
    storage::StorageInterface::Ptr lastStorage)
{
    BlockContext::Ptr context = make_shared<BlockContext>(
        storage, lastStorage, m_hashImpl, currentHeader, BCOSWASMSchedule, true, m_isAuthCheck);

    return context;
}

std::shared_ptr<BlockContext> WasmTransactionExecutor::createBlockContext(
    bcos::protocol::BlockNumber blockNumber, h256 blockHash, uint64_t timestamp,
    int32_t blockVersion, storage::StateStorage::Ptr storage)
{
    BlockContext::Ptr context = make_shared<BlockContext>(storage, m_hashImpl, blockNumber,
        blockHash, timestamp, blockVersion, BCOSWASMSchedule, true, m_isAuthCheck);

    return context;
}

void WasmTransactionExecutor::initPrecompiled()
{
    m_builtInPrecompiled = std::make_shared<std::set<std::string>>();
    m_constantPrecompiled =
        std::make_shared<std::map<std::string, std::shared_ptr<precompiled::Precompiled>>>();

    auto sysConfig = std::make_shared<precompiled::SystemConfigPrecompiled>(m_hashImpl);
    auto consensusPrecompiled = std::make_shared<precompiled::ConsensusPrecompiled>(m_hashImpl);
    // FIXME: not support crud now
    // auto tableFactoryPrecompiled =
    // std::make_shared<precompiled::TableFactoryPrecompiled>(m_hashImpl);
    auto kvTableFactoryPrecompiled =
        std::make_shared<precompiled::KVTableFactoryPrecompiled>(m_hashImpl);

    // in WASM
    m_constantPrecompiled->insert({SYS_CONFIG_NAME, sysConfig});
    m_constantPrecompiled->insert({CONSENSUS_NAME, consensusPrecompiled});
    // FIXME: not support crud now
    // m_constantPrecompiled.insert({TABLE_NAME, tableFactoryPrecompiled});
    m_constantPrecompiled->insert({KV_TABLE_NAME, kvTableFactoryPrecompiled});
    m_constantPrecompiled->insert(
        {DAG_TRANSFER_NAME, std::make_shared<precompiled::DagTransferPrecompiled>(m_hashImpl)});
    m_constantPrecompiled->insert({CRYPTO_NAME, std::make_shared<CryptoPrecompiled>(m_hashImpl)});
    m_constantPrecompiled->insert(
        {BFS_NAME, std::make_shared<precompiled::FileSystemPrecompiled>(m_hashImpl)});
    m_constantPrecompiled->insert(
        {CONTRACT_AUTH_NAME, std::make_shared<precompiled::ContractAuthPrecompiled>(m_hashImpl)});

    set<string> builtIn = {CRYPTO_NAME};
    m_builtInPrecompiled = make_shared<set<string>>(builtIn);
}
