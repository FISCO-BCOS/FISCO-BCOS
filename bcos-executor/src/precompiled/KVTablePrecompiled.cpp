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
 * @file KVTablePrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-27
 */

#include "KVTablePrecompiled.h"
#include "Common.h"
#include "PrecompiledResult.h"
#include "Utilities.h"
#include <bcos-framework/interfaces/executor/PrecompiledTypeDef.h>
#include <bcos-framework/interfaces/protocol/Exceptions.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;

const char* const KV_TABLE_METHOD_SET = "set(string,string)";
const char* const KV_TABLE_METHOD_GET = "get(string)";
const char* const KV_TABLE_METHOD_DESC = "desc()";

KVTablePrecompiled::KVTablePrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[KV_TABLE_METHOD_SET] = getFuncSelector(KV_TABLE_METHOD_SET, _hashImpl);
    name2Selector[KV_TABLE_METHOD_GET] = getFuncSelector(KV_TABLE_METHOD_GET, _hashImpl);
    name2Selector[KV_TABLE_METHOD_DESC] = getFuncSelector(KV_TABLE_METHOD_DESC, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> KVTablePrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string&, const std::string&, int64_t)
{
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    std::string tableName;
    bytes param;
    codec->decode(_param, tableName, param);
    tableName = getActualTableName(tableName);
    auto originParam = ref(param);
    uint32_t func = getParamFunc(originParam);
    bytesConstRef data = getParamData(originParam);

    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    auto table = _executive->storage().openTable(tableName);
    if (!table.has_value())
    {
        BOOST_THROW_EXCEPTION(PrecompiledError(tableName + " does not exist"));
    }

    if (func == name2Selector[KV_TABLE_METHOD_SET])
    {
        /// set(string,string)
        set(tableName, _executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[KV_TABLE_METHOD_GET])
    {
        /// get(string)
        get(tableName, _executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[KV_TABLE_METHOD_DESC])
    {
        /// desc()
        desc(tableName, _executive, callResult, gasPricer);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("KVTablePrecompiled")
                               << LOG_DESC("call undefined function!");
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

void KVTablePrecompiled::get(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    /// get(string) => (bool, Entry)
    std::string key;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, key);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVTable") << LOG_KV("tableName", tableName)
                           << LOG_KV("get", key);
    auto table = _executive->storage().openTable(tableName);

    auto entry = table->getRow(key);
    gasPricer->appendOperation(InterfaceOpcode::Select);
    EntryTuple entryTuple({});
    if (!entry)
    {
        callResult->setExecResult(codec->encode(false, std::move(entryTuple)));
        return;
    }

    gasPricer->updateMemUsed(entry->size());
    entryTuple = {key, {std::string(entry->get())}};

    callResult->setExecResult(codec->encode(true, std::move(entryTuple)));
}

void KVTablePrecompiled::set(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    /// set(string,string)
    std::string key, value;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, key, value);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVTable") << LOG_KV("tableName", tableName)
                           << LOG_KV("key", key) << LOG_KV("value", value);

    checkLengthValidate(key, USER_TABLE_KEY_VALUE_MAX_LENGTH, CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);

    checkLengthValidate(
        value, USER_TABLE_FIELD_VALUE_MAX_LENGTH, CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);

    Entry entry;
    entry.importFields({value});
    _executive->storage().setRow(tableName, key, std::move(entry));
    callResult->setExecResult(codec->encode(int32_t(1)));
    gasPricer->setMemUsed(1);
    gasPricer->appendOperation(InterfaceOpcode::Insert);
}

void KVTablePrecompiled::desc(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    /// desc()
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("KV Table desc") << LOG_KV("tableName", tableName);

    auto sysEntry = _executive->storage().getRow(storage::StorageInterface::SYS_TABLES, tableName);
    auto keyAndValue = sysEntry->get();
    auto keyField = std::string(keyAndValue.substr(0, keyAndValue.find_first_of(',')));
    auto valueFields = std::string(keyAndValue.substr(keyAndValue.find_first_of(',') + 1));
    std::vector<std::string> values;
    boost::split(values, std::move(valueFields), boost::is_any_of(","));

    TableInfoTuple tableInfo = {std::move(keyField), std::move(values)};

    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    callResult->setExecResult(codec->encode(std::move(tableInfo)));
}