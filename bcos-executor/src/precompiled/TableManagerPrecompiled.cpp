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
 * @file TableManagerPrecompiled.cpp
 * @author: kyonGuo
 * @date 2022/4/8
 */

#include "TableManagerPrecompiled.h"

#include "Common.h"
#include "PrecompiledResult.h"
#include "Utilities.h"
#include <bcos-framework/interfaces/protocol/Exceptions.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;

const char* const TABLE_METHOD_CREATE = "createTable(string,(string,string[]))";
const char* const TABLE_METHOD_CREATE_KV = "createKVTable(string,string,string)";
const char* const TABLE_METHOD_APPEND = "appendColumns(string,string[])";

TableManagerPrecompiled::TableManagerPrecompiled(crypto::Hash::Ptr _hashImpl)
  : Precompiled(_hashImpl)
{
    name2Selector[TABLE_METHOD_CREATE] = getFuncSelector(TABLE_METHOD_CREATE, _hashImpl);
    name2Selector[TABLE_METHOD_APPEND] = getFuncSelector(TABLE_METHOD_APPEND, _hashImpl);
    name2Selector[TABLE_METHOD_CREATE_KV] = getFuncSelector(TABLE_METHOD_CREATE_KV, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> TableManagerPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string& _origin, const std::string&, int64_t gasLeft)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[TABLE_METHOD_CREATE])
    {
        /// createTable(string,string,string)
        createTable(_executive, data, callResult, gasPricer, _origin, gasLeft);
    }
    else if (func == name2Selector[TABLE_METHOD_CREATE_KV])
    {
        /// createKVTable(string,string,string)
        createKVTable(_executive, data, callResult, gasPricer, _origin, gasLeft);
    }
    else if (func == name2Selector[TABLE_METHOD_APPEND])
    {
        /// appendColumns(string,string[])
        appendColumns(_executive, data, callResult, gasPricer);
    }
    else
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_DESC("call undefined function!");
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

void TableManagerPrecompiled::createTable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer,
    const std::string& _origin, int64_t gasLeft)
{
    // createTable(string,(string,string[]))
    std::string tableName;
    TableInfoTuple tableInfoTuple;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, tableName, tableInfoTuple);
    // TODO: check trim
    auto& [keyField, valueFields] = tableInfoTuple;
    precompiled::checkCreateTableParam(tableName, keyField, valueFields);
    auto valueField = boost::join(valueFields, ",");
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TableManagerPrecompiled")
                           << LOG_KV("createTable", tableName) << LOG_KV("keyField", keyField)
                           << LOG_KV("valueField", valueField);
    gasPricer->appendOperation(InterfaceOpcode::CreateTable, 1);
    // /tables + tableName
    auto newTableName = getTableName(tableName);
    auto table = _executive->storage().openTable(newTableName);
    if (table)
    {
        // table already exist
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_NAME_ALREADY_EXIST, *codec);
        return;
    }
    std::string tableManagerAddress =
        blockContext->isWasm() ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS;
    std::string tableAddress = blockContext->isWasm() ? TABLE_NAME : TABLE_ADDRESS;
    /// TODO: add codeString
    std::string codeString = getDynamicPrecompiledCodeString(tableAddress, newTableName);
    auto input = codec->encode(newTableName, codeString);
    auto response = externalRequest(_executive, ref(input), _origin, tableManagerAddress,
        blockContext->isWasm() ? newTableName : "", false, true,
        gasLeft - gasPricer->calTotalGas());

    if (response->status != (int32_t)TransactionStatus::None)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TableManagerPrecompiled")
                               << LOG_DESC("create table error")
                               << LOG_KV("tableName", newTableName)
                               << LOG_KV("valueField", valueField);
        BOOST_THROW_EXCEPTION(PrecompiledError("Create table error."));
    }

    // here is a trick to set table key field info
    valueField = keyField + "," + valueField;
    _executive->storage().createTable(getActualTableName(newTableName), valueField);
    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, *codec);
}

void TableManagerPrecompiled::createKVTable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer,
    const std::string& _origin, int64_t gasLeft)
{
    /// createKVTable(string,string,string)
    std::string tableName, key, value;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, tableName, key, value);
    precompiled::checkCreateTableParam(tableName, key, value);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TableManagerPrecompiled")
                           << LOG_KV("createKVTable", tableName) << LOG_KV("keyField", key)
                           << LOG_KV("valueField", value);
    gasPricer->appendOperation(InterfaceOpcode::CreateTable);
    // /tables + tableName
    auto newTableName = getTableName(tableName);
    auto table = _executive->storage().openTable(newTableName);
    if (table)
    {
        // table already exist
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_NAME_ALREADY_EXIST, *codec);
        return;
    }
    std::string tableManagerAddress =
        blockContext->isWasm() ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS;
    std::string kvTableAddress = blockContext->isWasm() ? KV_TABLE_NAME : KV_TABLE_ADDRESS;
    std::string codeString = getDynamicPrecompiledCodeString(kvTableAddress, newTableName);

    auto input = codec->encode(newTableName, codeString);
    auto response = externalRequest(_executive, ref(input), _origin, tableManagerAddress,
        blockContext->isWasm() ? newTableName : "", false, true,
        gasLeft - gasPricer->calTotalGas());

    if (response->status != (int32_t)TransactionStatus::None)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TableManagerPrecompiled")
                               << LOG_DESC("create kv table error")
                               << LOG_KV("tableName", newTableName) << LOG_KV("valueField", value);
        BOOST_THROW_EXCEPTION(PrecompiledError("Create table error."));
    }

    // here is a trick to set table key field info
    value = key + "," + value;
    _executive->storage().createTable(getActualTableName(newTableName), value);
    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, *codec);
}

void TableManagerPrecompiled::appendColumns(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    // appendColumns(string,string[])
    std::string tableName;
    std::vector<std::string> newColumns;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, tableName, newColumns);
    tableName = getActualTableName(getTableName(tableName));

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TableManagerPrecompiled") << LOG_DESC("appendColumns")
                           << LOG_KV("tableName", tableName)
                           << LOG_KV("newColumns", boost::join(newColumns, ","));
    // 1. get origin table info
    auto table = _executive->storage().openTable(tableName);
    if (!table)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TableManagerPrecompiled")
                               << LOG_DESC("table not exists") << LOG_KV("tableName", tableName);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_NOT_EXIST, *codec);
        return;
    }
    // here is a trick to avoid key field dup, s_table save user table (key,fields)
    auto originFields = table->tableInfo()->fields();
    std::set<std::string> checkDupFields(originFields.begin() + 1, originFields.end());
    // 2. check columns not duplicate
    bool insertSuccess = true;
    for (const auto& col : newColumns)
    {
        checkLengthValidate(
            col, USER_TABLE_FIELD_NAME_MAX_LENGTH, CODE_TABLE_FIELD_LENGTH_OVERFLOW);
        std::tie(std::ignore, insertSuccess) = checkDupFields.insert(col);
        originFields.emplace_back(col);
        if (!insertSuccess)
            break;
    }
    if (!insertSuccess)
    {
        // dup
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TableManagerPrecompiled")
                               << LOG_DESC("columns duplicate") << LOG_KV("tableName", tableName);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_TABLE_DUPLICATE_FIELD, *codec);
        return;
    }
    // 3. append new columns
    // manually change sys_table
    // here is a trick to update table value fields
    Entry newEntry;
    auto newField = boost::join(originFields, ",");

    newEntry.importFields({std::move(newField)});
    _executive->storage().setRow(StorageInterface::SYS_TABLES, tableName, std::move(newEntry));
    gasPricer->appendOperation(InterfaceOpcode::Set, 1);
    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, *codec);
}
