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

#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <bcos-framework/protocol/Exceptions.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;

constexpr const char* const TABLE_METHOD_CREATE = "createTable(string,(string,string[]))";
constexpr const char* const TABLE_METHOD_CREATE_KV = "createKVTable(string,string,string)";
constexpr const char* const TABLE_METHOD_APPEND = "appendColumns(string,string[])";
constexpr const char* const TABLE_METHOD_OPEN = "openTable(string)";
constexpr const char* const TABLE_METHOD_DESC = "desc(string)";


TableManagerPrecompiled::TableManagerPrecompiled(crypto::Hash::Ptr _hashImpl)
  : Precompiled(_hashImpl)
{
    name2Selector[TABLE_METHOD_CREATE] = getFuncSelector(TABLE_METHOD_CREATE, _hashImpl);
    name2Selector[TABLE_METHOD_APPEND] = getFuncSelector(TABLE_METHOD_APPEND, _hashImpl);
    name2Selector[TABLE_METHOD_CREATE_KV] = getFuncSelector(TABLE_METHOD_CREATE_KV, _hashImpl);
    name2Selector[TABLE_METHOD_OPEN] = getFuncSelector(TABLE_METHOD_OPEN, _hashImpl);
    name2Selector[TABLE_METHOD_DESC] = getFuncSelector(TABLE_METHOD_DESC, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> TableManagerPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    uint32_t func = getParamFunc(_callParameters->input());
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_callParameters->input().size());

    if (func == name2Selector[TABLE_METHOD_CREATE])
    {
        /// createTable(string,string,string)
        createTable(_executive, gasPricer, _callParameters);
    }
    else if (func == name2Selector[TABLE_METHOD_CREATE_KV])
    {
        /// createKVTable(string,string,string)
        createKVTable(_executive, gasPricer, _callParameters);
    }
    else if (func == name2Selector[TABLE_METHOD_APPEND])
    {
        /// appendColumns(string,string[])
        appendColumns(_executive, gasPricer, _callParameters);
    }
    else if (func == name2Selector[TABLE_METHOD_DESC])
    {
        /// desc(string)
        desc(_executive, gasPricer, _callParameters);
    }
    else if (!_executive->blockContext().lock()->isWasm() &&
             func == name2Selector[TABLE_METHOD_OPEN])
    {
        /// only solidity: openTable(string) => address
        openTable(_executive, gasPricer, _callParameters);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TableManager") << LOG_DESC("call undefined function!");
        BOOST_THROW_EXCEPTION(PrecompiledError("TableManager call undefined function!"));
    }
    gasPricer->updateMemUsed(_callParameters->m_execResult.size());
    _callParameters->setGas(_callParameters->m_gas - gasPricer->calTotalGas());
    return _callParameters;
}

void TableManagerPrecompiled::createTable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    // createTable(string,(string,string[]))
    std::string tableName;
    TableInfoTuple tableInfoTuple;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), tableName, tableInfoTuple);
    auto& [keyField, valueFields] = tableInfoTuple;
    auto valueField = precompiled::checkCreateTableParam(tableName, keyField, valueFields);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TableManagerPrecompiled")
                           << LOG_KV("createTable", tableName) << LOG_KV("keyField", keyField)
                           << LOG_KV("valueField", valueField);
    gasPricer->appendOperation(InterfaceOpcode::CreateTable);
    // /tables + tableName
    auto newTableName = getTableName(tableName);
    auto table = _executive->storage().openTable(newTableName);
    if (table)
    {
        // table already exist
        _callParameters->setExecResult(codec.encode(int32_t(CODE_TABLE_NAME_ALREADY_EXIST)));
        return;
    }
    std::string tableManagerAddress =
        blockContext->isWasm() ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS;
    std::string tableAddress = blockContext->isWasm() ? TABLE_NAME : TABLE_ADDRESS;

    // here is a trick to set table key field info
    valueField = keyField + "," + valueField;
    std::string codeString = getDynamicPrecompiledCodeString(tableAddress, newTableName);
    auto input = codec.encode(newTableName, codeString);
    auto response = externalRequest(_executive, ref(input), _callParameters->m_origin,
        tableManagerAddress, blockContext->isWasm() ? newTableName : "", false, true,
        _callParameters->m_gas - gasPricer->calTotalGas());

    if (response->status != (int32_t)TransactionStatus::None)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TableManagerPrecompiled")
                               << LOG_DESC("create table error")
                               << LOG_KV("tableName", newTableName)
                               << LOG_KV("valueField", valueField);
        BOOST_THROW_EXCEPTION(PrecompiledError("Create table error."));
    }

    _executive->storage().createTable(getActualTableName(newTableName), valueField);
    _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
}

void TableManagerPrecompiled::createKVTable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// createKVTable(string,string,string)
    std::string tableName, key, value;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), tableName, key, value);
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
        _callParameters->setExecResult(codec.encode(int32_t(CODE_TABLE_NAME_ALREADY_EXIST)));
        return;
    }
    std::string tableManagerAddress =
        blockContext->isWasm() ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS;
    std::string kvTableAddress = blockContext->isWasm() ? KV_TABLE_NAME : KV_TABLE_ADDRESS;
    std::string codeString = getDynamicPrecompiledCodeString(kvTableAddress, newTableName);

    auto input = codec.encode(newTableName, codeString);
    auto response = externalRequest(_executive, ref(input), _callParameters->m_origin,
        tableManagerAddress, blockContext->isWasm() ? newTableName : "", false, true,
        _callParameters->m_gas - gasPricer->calTotalGas());

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
    _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
}

void TableManagerPrecompiled::appendColumns(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    // appendColumns(string,string[])
    std::string tableName;
    std::vector<std::string> newColumns;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), tableName, newColumns);
    tableName = getActualTableName(getTableName(tableName));

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TableManagerPrecompiled") << LOG_DESC("appendColumns")
                           << LOG_KV("tableName", tableName)
                           << LOG_KV("newColumns", boost::join(newColumns, ","));
    // 1. get origin table info
    auto table = _executive->storage().openTable(StorageInterface::SYS_TABLES);
    auto existEntry = table->getRow(tableName);
    if (!existEntry)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TableManagerPrecompiled")
                               << LOG_DESC("table not exists") << LOG_KV("tableName", tableName);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_TABLE_NOT_EXIST)));
        return;
    }
    // here is a trick to avoid key field dup, s_table save user table (key,fields)
    std::vector<std::string> originFields;
    boost::split(originFields, std::string(existEntry->get()), boost::is_any_of(","));
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
        _callParameters->setExecResult(codec.encode(int32_t(CODE_TABLE_DUPLICATE_FIELD)));
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
    _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
}

void TableManagerPrecompiled::openTable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// only solidity: openTable(string) => address
    std::string tableName;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), tableName);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TableManagerPrecompiled")
                           << LOG_KV("openTable", tableName);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    // /tables + tableName
    auto absolutePath = getTableName(tableName);
    auto table = _executive->storage().openTable(absolutePath);

    if (table)
    {
        // file exists, try to get type
        auto typeEntry = _executive->storage().getRow(absolutePath, FS_KEY_TYPE);
        if (typeEntry && typeEntry->getField(0) == FS_TYPE_LINK)
        {
            // if link
            auto addressEntry = _executive->storage().getRow(absolutePath, FS_LINK_ADDRESS);
            auto contractAddress = std::string(addressEntry->get());
            auto codecAddress = codec.encode(Address(std::move(contractAddress)));
            _callParameters->setExecResult(std::move(codecAddress));
            return;
        }

        _callParameters->setExecResult(codec.encode(Address()));
        return;
    }
    PRECOMPILED_LOG(ERROR) << LOG_BADGE("TableManagerPrecompiled")
                           << LOG_DESC("can't open table of file path")
                           << LOG_KV("path", absolutePath);
    _callParameters->setExecResult(codec.encode(Address()));
}

void TableManagerPrecompiled::desc(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// desc(string)
    std::string tableName;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), tableName);

    tableName = getActualTableName(getTableName(tableName));
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table desc") << LOG_KV("tableName", tableName);

    auto sysEntry = _executive->storage().getRow(storage::StorageInterface::SYS_TABLES, tableName);
    if (!sysEntry)
    {
        TableInfoTuple tableInfo = {};
        _callParameters->setExecResult(codec.encode(std::move(tableInfo)));
        return;
    }
    auto keyAndValue = sysEntry->get();
    auto keyField = std::string(keyAndValue.substr(0, keyAndValue.find_first_of(',')));
    auto valueFields = std::string(keyAndValue.substr(keyAndValue.find_first_of(',') + 1));
    std::vector<std::string> values;
    boost::split(values, std::move(valueFields), boost::is_any_of(","));

    TableInfoTuple tableInfo = {std::move(keyField), std::move(values)};

    gasPricer->appendOperation(InterfaceOpcode::Select);
    _callParameters->setExecResult(codec.encode(std::move(tableInfo)));
}