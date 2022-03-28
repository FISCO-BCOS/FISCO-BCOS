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
 * @file KVTableFactoryPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-27
 */

#include "KVTableFactoryPrecompiled.h"
#include "Common.h"
#include "PrecompiledResult.h"
#include "Utilities.h"
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

const char* const KV_TABLE_METHOD_CREATE = "createTable(string,string,string)";
const char* const KV_TABLE_METHOD_SET = "set(string,string,((string,string)[]))";
const char* const KV_TABLE_METHOD_GET = "get(string,string)";
const char* const KV_TABLE_METHOD_DESC = "desc(string)";

KVTableFactoryPrecompiled::KVTableFactoryPrecompiled(crypto::Hash::Ptr _hashImpl)
  : Precompiled(_hashImpl)
{
    name2Selector[KV_TABLE_METHOD_CREATE] = getFuncSelector(KV_TABLE_METHOD_CREATE, _hashImpl);
    name2Selector[KV_TABLE_METHOD_SET] = getFuncSelector(KV_TABLE_METHOD_SET, _hashImpl);
    name2Selector[KV_TABLE_METHOD_GET] = getFuncSelector(KV_TABLE_METHOD_GET, _hashImpl);
    name2Selector[KV_TABLE_METHOD_DESC] = getFuncSelector(KV_TABLE_METHOD_DESC, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> KVTableFactoryPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string&, const std::string&, int64_t)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVTableFactory") << LOG_DESC("call")
                           << LOG_KV("func", func);

    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[KV_TABLE_METHOD_CREATE])
    {
        // createTable(string,string,string)
        createTable(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[KV_TABLE_METHOD_SET])
    {
        // set(string,string,((string,string)[]))
        set(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[KV_TABLE_METHOD_GET])
    {
        // get(string,string)
        get(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[KV_TABLE_METHOD_DESC])
    {
        // desc(string)
        desc(_executive, data, callResult, gasPricer);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("KVTableFactoryPrecompiled")
                               << LOG_DESC("call undefined function!");
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

void KVTableFactoryPrecompiled::createTable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    // createTable(string,string,string)
    std::string tableName;
    std::string keyField;
    std::string valueField;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, tableName, keyField, valueField);

    if (tableName.empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("KVTableFactoryPrecompiled")
                               << LOG_DESC("error tableName") << LOG_KV("tableName", tableName);
        BOOST_THROW_EXCEPTION(PrecompiledError("Table name error."));
    }

    precompiled::checkCreateTableParam(tableName, keyField, valueField);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVCreateTable") << LOG_KV("tableName", tableName)
                           << LOG_KV("keyField", keyField) << LOG_KV("valueField", valueField);

    // /tables/ + tableName
    auto newTableName = getTableName(tableName);
    int result = CODE_SUCCESS;
    auto table = _executive->storage().openTable(newTableName);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (table)
    {
        // table already exist
        result = CODE_TABLE_NAME_ALREADY_EXIST;
        getErrorCodeOut(callResult->mutableExecResult(), result, *codec);
        return;
    }
    auto parentDirAndBaseName = getParentDirAndBaseName(newTableName);
    auto parentDir = parentDirAndBaseName.first;
    auto tableBaseName = parentDirAndBaseName.second;
    if (!recursiveBuildDir(_executive, parentDir))
    {
        result = CODE_FILE_BUILD_DIR_FAILED;
    }
    else
    {
        auto ret = _executive->storage().createTable(newTableName, valueField);
        auto sysTable = _executive->storage().openTable(StorageInterface::SYS_TABLES);
        auto sysEntry = sysTable->getRow(newTableName);
        if (!ret || !sysEntry)
        {
            result = CODE_TABLE_CREATE_ERROR;
            getErrorCodeOut(callResult->mutableExecResult(), result, *codec);
            return;
        }
        sysEntry->setField(0, valueField + "," + keyField);
        sysTable->setRow(newTableName, sysEntry.value());
        gasPricer->appendOperation(InterfaceOpcode::CreateTable);

        // parentPath table must exist
        // update parentDir
        auto parentTable = _executive->storage().openTable(parentDir);
        // decode sub
        std::map<std::string, std::string> bfsInfo;
        auto stubEntry = parentTable->getRow(FS_KEY_SUB);
        auto&& out = asBytes(std::string(stubEntry->getField(0)));
        codec::scale::decode(bfsInfo, gsl::make_span(out));
        bfsInfo.insert(std::make_pair(tableBaseName, FS_TYPE_CONTRACT));
        stubEntry->setField(0, asString(codec::scale::encode(bfsInfo)));
        parentTable->setRow(FS_KEY_SUB, std::move(stubEntry.value()));
    }
    getErrorCodeOut(callResult->mutableExecResult(), result, *codec);
}

void KVTableFactoryPrecompiled::get(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    std::string tableName, key;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, tableName, key);
    tableName = getTableName(tableName);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVTable") << LOG_KV("get", key)
                           << LOG_KV("tableName", tableName);
    auto table = _executive->storage().openTable(tableName);
    if (table == std::nullopt)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("KVTable") << LOG_DESC("Open table failed")
                               << LOG_KV("tableName", tableName);
        BOOST_THROW_EXCEPTION(PrecompiledError(tableName + " does not exist"));
    }
    auto entry = table->getRow(key);
    gasPricer->appendOperation(InterfaceOpcode::Select, 1);
    EntryTuple entryTuple({});
    if (!entry)
    {
        callResult->setExecResult(codec->encode(false, entryTuple));
        return;
    }

    gasPricer->updateMemUsed(entry->size());
    entryTuple = entry->getObject<EntryTuple>();

    // for (const auto& fieldName : table->tableInfo()->fields())
    // {
    //     std::get<0>(entryTuple)
    //         .emplace_back(std::make_tuple(fieldName, entry->getField(fieldName)));
    // }
    callResult->setExecResult(codec->encode(true, entryTuple));
}

void KVTableFactoryPrecompiled::set(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    // set(string,string,((string,string)[]))
    std::string tableName, key;
    EntryTuple entryTuple;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, tableName, key, entryTuple);
    tableName = getTableName(tableName);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("KVTable") << LOG_KV("set", key)
                           << LOG_KV("tableName", tableName);
    auto table = _executive->storage().openTable(tableName);
    if (table == std::nullopt)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("KVTable") << LOG_DESC("Open table failed")
                               << LOG_KV("tableName", tableName);
        BOOST_THROW_EXCEPTION(PrecompiledError(tableName + " does not exist"));
    }
    auto entry = table->newEntry();
    checkLengthValidate(key, USER_TABLE_KEY_VALUE_MAX_LENGTH, CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
    for (auto const& kv : std::get<0>(entryTuple))
    {
        checkLengthValidate(std::get<1>(kv), USER_TABLE_FIELD_VALUE_MAX_LENGTH,
            CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
    }
    // transferEntry(entryTuple, entry);
    entry.setObject(entryTuple);
    table->setRow(key, entry);
    callResult->setExecResult(codec->encode(s256(1)));
    gasPricer->setMemUsed(entry.size());
    gasPricer->appendOperation(InterfaceOpcode::Insert, 1);
}

void KVTableFactoryPrecompiled::desc(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    std::string tableName;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, tableName);
    tableName = getTableName(tableName);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table desc") << LOG_KV("tableName", tableName);

    std::string keyField;
    std::string valueFields;

    auto table = _executive->storage().openTable(tableName);
    auto sysTable = _executive->storage().openTable(storage::StorageInterface::SYS_TABLES);
    auto sysEntry = sysTable->getRow(tableName);
    if (!table || !sysEntry)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("KVTableFactoryPrecompiled")
                                 << LOG_DESC("Open table failed") << LOG_KV("tableName", tableName);
        gasPricer->appendOperation(InterfaceOpcode::OpenTable);
        callResult->setExecResult(codec->encode(keyField, valueFields));
        return;
    }
    auto valueKey = sysEntry->getField(0);
    keyField = std::string(valueKey.substr(valueKey.find_last_of(',') + 1));
    valueFields = std::string(valueKey.substr(0, valueKey.find_last_of(',')));

    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    callResult->setExecResult(codec->encode(keyField, valueFields));
}