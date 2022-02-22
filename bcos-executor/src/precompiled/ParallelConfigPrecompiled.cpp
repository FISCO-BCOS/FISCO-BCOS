/**
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
 * @file ParallelConfigPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-28
 */

#include "ParallelConfigPrecompiled.h"
#include "Common.h"
#include "PrecompiledResult.h"
#include "Utilities.h"
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-framework/interfaces/protocol/Exceptions.h>
#include <bcos-framework/interfaces/storage/Table.h>
#include <boost/algorithm/string.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>

using namespace std;
using namespace bcos;
using namespace bcos::storage;
using namespace bcos::executor;
using namespace bcos::precompiled;

ParallelConfigPrecompiled::ParallelConfigPrecompiled(crypto::Hash::Ptr _hashImpl)
  : Precompiled(_hashImpl)
{
    name2Selector[PARA_CONFIG_REGISTER_METHOD_ADDR_STR_UINT] =
        getFuncSelector(PARA_CONFIG_REGISTER_METHOD_ADDR_STR_UINT, _hashImpl);
    name2Selector[PARA_CONFIG_REGISTER_METHOD_STR_STR_UINT] =
        getFuncSelector(PARA_CONFIG_REGISTER_METHOD_STR_STR_UINT, _hashImpl);
    name2Selector[PARA_CONFIG_UNREGISTER_METHOD_ADDR_STR] =
        getFuncSelector(PARA_CONFIG_UNREGISTER_METHOD_ADDR_STR, _hashImpl);
    name2Selector[PARA_CONFIG_UNREGISTER_METHOD_STR_STR] =
        getFuncSelector(PARA_CONFIG_UNREGISTER_METHOD_STR_STR, _hashImpl);
}

std::string ParallelConfigPrecompiled::toString()
{
    return "ParallelConfig";
}

std::shared_ptr<PrecompiledExecResult> ParallelConfigPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string& _origin, const std::string&)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    if (func == name2Selector[PARA_CONFIG_REGISTER_METHOD_ADDR_STR_UINT] ||
        func == name2Selector[PARA_CONFIG_REGISTER_METHOD_STR_STR_UINT])
    {
        registerParallelFunction(codec, _executive, data, _origin, callResult->mutableExecResult());
    }
    else if (func == name2Selector[PARA_CONFIG_UNREGISTER_METHOD_ADDR_STR] ||
             func == name2Selector[PARA_CONFIG_UNREGISTER_METHOD_STR_STR])
    {
        unregisterParallelFunction(
            codec, _executive, data, _origin, callResult->mutableExecResult());
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ParallelConfigPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

std::string ParallelConfigPrecompiled::getTableName(const std::string_view& _contractName)
{
    std::string tableName = std::string(PARA_CONFIG_TABLE_PREFIX_SHORT).append(_contractName);
    return tableName;
}

// TODO: use origin to check authority
std::shared_ptr<Table> ParallelConfigPrecompiled::openTable(
    std::shared_ptr<executor::TransactionExecutive> _executive, std::string const& _contractName,
    std::string const&, bool _needCreate)
{
    auto blockContext = _executive->blockContext().lock();
    std::string tableName = getTableName(_contractName);
    auto table = blockContext->storage()->openTable(tableName);

    if (!table && _needCreate)
    {  //__dat_transfer__ is not exist, then create it first.
        auto ret = _executive->storage().createTable(tableName, PARA_VALUE_NAMES);
        if (ret)
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("ParallelConfigPrecompiled") << LOG_DESC("open table")
                << LOG_DESC(" create parallel config table. ") << LOG_KV("tableName", tableName);
            table = _executive->storage().openTable(tableName);
        }
        else
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("ParallelConfigPrecompiled")
                << LOG_DESC("create parallel config table error") << LOG_KV("tableName", tableName);
            return nullptr;
        }
    }
    return std::make_shared<Table>(table.value());
}

void ParallelConfigPrecompiled::registerParallelFunction(PrecompiledCodec::Ptr _codec,
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _data,
    std::string const& _origin, bytes& _out)
{
    std::shared_ptr<Table> table = nullptr;
    std::string functionName;
    u256 criticalSize;
    auto blockContext = _executive->blockContext().lock();

    if (blockContext->isWasm())
    {
        std::string contractName;
        _codec->decode(_data, contractName, functionName, criticalSize);
        table = openTable(_executive, contractName, _origin);
    }
    else
    {
        Address contractName;
        _codec->decode(_data, contractName, functionName, criticalSize);
        table = openTable(_executive, contractName.hex(), _origin);
    }
    uint32_t selector = getFuncSelector(functionName, m_hashImpl);
    if (table)
    {
        Entry entry = table->newEntry();
        ParallelConfig config{functionName, criticalSize};
        entry.setObject(config);

        table->setRow(std::to_string(selector), entry);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ParallelConfigPrecompiled")
                               << LOG_DESC("registerParallelFunction success")
                               << LOG_KV(PARA_SELECTOR, std::to_string(selector))
                               << LOG_KV(PARA_FUNC_NAME, functionName)
                               << LOG_KV(PARA_CRITICAL_SIZE, criticalSize);
        _out = _codec->encode(u256(0));
    }
}

void ParallelConfigPrecompiled::unregisterParallelFunction(PrecompiledCodec::Ptr _codec,
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _data,
    std::string const&, bytes& _out)
{
    std::string functionName;
    std::optional<Table> table = nullopt;
    auto blockContext = _executive->blockContext().lock();
    if (blockContext->isWasm())
    {
        std::string contractAddress;
        _codec->decode(_data, contractAddress, functionName);
        table = _executive->storage().openTable(getTableName(contractAddress));
    }
    else
    {
        Address contractAddress;
        _codec->decode(_data, contractAddress, functionName);
        table = _executive->storage().openTable(getTableName(contractAddress.hex()));
    }

    uint32_t selector = getFuncSelector(functionName, m_hashImpl);
    if (table)
    {
        table->setRow(std::to_string(selector), table->newDeletedEntry());
        _out = _codec->encode(u256(0));
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ParallelConfigPrecompiled")
                               << LOG_DESC("unregisterParallelFunction success")
                               << LOG_KV(PARA_SELECTOR, std::to_string(selector));
    }
}

ParallelConfig::Ptr ParallelConfigPrecompiled::getParallelConfig(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    const std::string_view& _contractAddress, uint32_t _selector, const std::string_view&)
{
    auto blockContext = _executive->blockContext().lock();
    auto table = blockContext->storage()->openTable(getTableName(_contractAddress));
    if (!table)
    {
        return nullptr;
    }
    auto entry = table->getRow(std::to_string(_selector));
    if (!entry)
    {
        return nullptr;
    }
    else
    {
        auto config = entry->getObject<ParallelConfig>();
        return std::make_shared<ParallelConfig>(std::move(config));
    }
}
