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
#include "bcos-executor/src/precompiled/common/PrecompiledAbi.h"
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
constexpr const char* const TABLE_METHOD_DESC_V32 = "descWithKeyOrder(string)";
constexpr const char* const TABLE_METHOD_CREATE_V320 =
    "createTable(string,(uint8,string,string[]))";


TableManagerPrecompiled::TableManagerPrecompiled(crypto::Hash::Ptr _hashImpl)
  : Precompiled(_hashImpl)
{
    registerFunc(getFuncSelector(TABLE_METHOD_CREATE, _hashImpl),
        [this](auto&& executive, auto&& pricer, auto&& params) {
            createTable(executive, pricer, params);
        });
    registerFunc(getFuncSelector(TABLE_METHOD_APPEND, _hashImpl),
        [this](auto&& executive, auto&& pricer, auto&& params) {
            appendColumns(executive, pricer, params);
        });
    registerFunc(getFuncSelector(TABLE_METHOD_CREATE_KV, _hashImpl),
        [this](auto&& executive, auto&& pricer, auto&& params) {
            createKVTable(executive, pricer, params);
        });
    registerFunc(getFuncSelector(TABLE_METHOD_OPEN, _hashImpl),
        [this](auto&& executive, auto&& pricer, auto&& params) {
            openTable(executive, pricer, params);
        });
    registerFunc(getFuncSelector(TABLE_METHOD_DESC, _hashImpl),
        [this](
            auto&& executive, auto&& pricer, auto&& params) { desc(executive, pricer, params); });
    registerFunc(
        getFuncSelector(TABLE_METHOD_DESC_V32, _hashImpl),
        [this](auto&& executive, auto&& pricer, auto&& params) {
            descWithKeyOrder(executive, pricer, params);
        },
        protocol::BlockVersion::V3_2_VERSION);
    registerFunc(
        getFuncSelector(TABLE_METHOD_CREATE_V320, _hashImpl),
        [this](auto&& executive, auto&& pricer, auto&& params) {
            createTableV32(executive, pricer, params);
        },
        protocol::BlockVersion::V3_2_VERSION);
}

std::shared_ptr<PrecompiledExecResult> TableManagerPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    const auto& blockContext = _executive->blockContext();
    uint32_t func = getParamFunc(_callParameters->input());
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_callParameters->input().size());

    auto selector = selector2Func.find(func);
    if (selector != selector2Func.end())
    {
        if (blockContext.isWasm() && func == name2Selector[TABLE_METHOD_OPEN])
        {
        }
        auto& [minVersion, execFunc] = selector->second;
        if (versionCompareTo(blockContext.blockVersion(), minVersion) >= 0)
        {
            execFunc(_executive, gasPricer, _callParameters);

            if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
            {
                PRECOMPILED_LOG(TRACE)
                    << LOG_BADGE("TableManagerPrecompiled") << LOG_DESC("call function")
                    << LOG_KV("func", func) << LOG_KV("minVersion", minVersion);
            }
            gasPricer->updateMemUsed(_callParameters->m_execResult.size());
            _callParameters->setGasLeft(_callParameters->m_gasLeft - gasPricer->calTotalGas());
            return _callParameters;
        }
    }
    PRECOMPILED_LOG(INFO) << LOG_BADGE("TableManager") << LOG_DESC("call undefined function!");
    BOOST_THROW_EXCEPTION(PrecompiledError("TableManager call undefined function!"));
}

void TableManagerPrecompiled::createTable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    // createTable(string,(string,string[]))
    std::string tableName;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());

    TableInfoTuple tableInfo;
    codec.decode(_callParameters->params(), tableName, tableInfo);
    std::string keyField = std::get<0>(tableInfo);
    auto& valueFields = std::get<1>(tableInfo);
    std::string valueField = precompiled::checkCreateTableParam(tableName, keyField, valueFields);
    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number())
                           << LOG_BADGE("TableManagerPrecompiled")
                           << LOG_KV("createTable", tableName) << LOG_KV("keyField", keyField)
                           << LOG_KV("valueField", valueField);
    // here is a trick to set table key field info
    valueField = keyField + "," + valueField;

    externalCreateTable(_executive, gasPricer, _callParameters, tableName, codec, valueField);
}

void TableManagerPrecompiled::createTableV32(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    // createTable(string,(string,string[])) or createTable(string,(uint8,string,string[]))
    std::string tableName;
    std::string keyField;
    std::string valueField;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());

    TableInfoTupleV320 tableInfo;
    codec.decode(_callParameters->params(), tableName, tableInfo);
    auto keyOrder = std::make_optional<uint8_t>(std::get<0>(tableInfo));
    keyField = std::get<1>(tableInfo);
    auto& valueFields = std::get<2>(tableInfo);
    valueField = precompiled::checkCreateTableParam(tableName, keyField, valueFields, keyOrder);
    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number())
                           << LOG_BADGE("TableManagerPrecompiled")
                           << LOG_KV("createTable", tableName) << LOG_KV("keyOrder", int(*keyOrder))
                           << LOG_KV("keyField", keyField) << LOG_KV("valueField", valueField);
    // Compatible with older versions (< v3.2) of table and KvTable
    valueField =
        V320_TABLE_INFO_PREFIX + std::to_string(*keyOrder) + "," + keyField + "," + valueField;

    externalCreateTable(_executive, gasPricer, _callParameters, tableName, codec, valueField);
}

void TableManagerPrecompiled::createKVTable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// createKVTable(string,string,string)
    std::string tableName, key, value;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), tableName, key, value);
    precompiled::checkCreateTableParam(tableName, key, value);
    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number())
                           << LOG_BADGE("TableManagerPrecompiled")
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
        blockContext.isWasm() ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS;
    std::string kvTableAddress = blockContext.isWasm() ? KV_TABLE_NAME : KV_TABLE_ADDRESS;
    std::string codeString = getDynamicPrecompiledCodeString(kvTableAddress, newTableName);

    auto input = codec.encode(newTableName, codeString);
    std::string abi =
        blockContext.blockVersion() >= static_cast<uint32_t>(BlockVersion::V3_1_VERSION) ?
            std::string(KV_TABLE_ABI) :
            "";
    auto response = externalRequest(_executive, ref(input), _callParameters->m_origin,
        tableManagerAddress, blockContext.isWasm() ? newTableName : "", false, true,
        _callParameters->m_gasLeft - gasPricer->calTotalGas(), true, std::move(abi));

    if (response->status != (int32_t)TransactionStatus::None)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TableManagerPrecompiled")
                              << LOG_DESC("create kv table failed")
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
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), tableName, newColumns);
    tableName = getActualTableName(getTableName(tableName));

    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number())
                           << LOG_BADGE("TableManagerPrecompiled") << LOG_DESC("appendColumns")
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
    const auto& blockContext = _executive->blockContext();
    if (blockContext.isWasm())
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TableManager") << LOG_DESC("call undefined function!");
        BOOST_THROW_EXCEPTION(PrecompiledError("TableManager call undefined function!"));
    }
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
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
    _callParameters->setExecResult(codec.encode(Address()));
}

void TableManagerPrecompiled::desc(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// desc(string)
    std::string tableName;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
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
    // for compatibility
    if (keyAndValue.starts_with(V320_TABLE_INFO_PREFIX))
    {
        keyAndValue = keyAndValue.substr(keyAndValue.find_first_of(',') + 1);
    }
    auto keyField = std::string(keyAndValue.substr(0, keyAndValue.find_first_of(',')));
    auto valueFields = std::string(keyAndValue.substr(keyAndValue.find_first_of(',') + 1));
    std::vector<std::string> values;
    boost::split(values, std::move(valueFields), boost::is_any_of(","));

    TableInfoTuple tableInfo = {std::move(keyField), std::move(values)};

    gasPricer->appendOperation(InterfaceOpcode::Select);
    _callParameters->setExecResult(codec.encode(std::move(tableInfo)));
}

void TableManagerPrecompiled::descWithKeyOrder(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// descWithKeyOrder(string)
    std::string tableName;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), tableName);

    tableName = getActualTableName(getTableName(tableName));
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table desc") << LOG_KV("tableName", tableName);

    auto sysEntry = _executive->storage().getRow(storage::StorageInterface::SYS_TABLES, tableName);
    if (!sysEntry)
    {
        TableInfoTupleV320 tableInfo = {};
        _callParameters->setExecResult(codec.encode(std::move(tableInfo)));
        return;
    }
    auto keyAndValue = sysEntry->get();

    uint8_t keyOrder = 0;
    if (keyAndValue.starts_with(V320_TABLE_INFO_PREFIX))
    {
        keyAndValue = keyAndValue.substr(V320_TABLE_INFO_PREFIX.length());
        keyOrder = (uint8_t)boost::lexical_cast<int>(
            keyAndValue.substr(0, keyAndValue.find_first_of(',')));
        keyAndValue = keyAndValue.substr(keyAndValue.find_first_of(',') + 1);
    }
    auto keyField = std::string(keyAndValue.substr(0, keyAndValue.find_first_of(',')));
    auto valueFields = std::string(keyAndValue.substr(keyAndValue.find_first_of(',') + 1));
    std::vector<std::string> values;
    boost::split(values, std::move(valueFields), boost::is_any_of(","));

    TableInfoTupleV320 tableInfo = {keyOrder, std::move(keyField), std::move(values)};

    gasPricer->appendOperation(InterfaceOpcode::Select);
    _callParameters->setExecResult(codec.encode(std::move(tableInfo)));
}

void TableManagerPrecompiled::externalCreateTable(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters,
    const std::string& tableName, const CodecWrapper& codec, const std::string& valueField) const
{
    const auto& blockContext = _executive->blockContext();
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
        blockContext.isWasm() ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS;
    std::string tableAddress = blockContext.isWasm() ? TABLE_NAME : TABLE_ADDRESS;

    std::string codeString = getDynamicPrecompiledCodeString(tableAddress, newTableName);
    auto input = codec.encode(newTableName, codeString);
    std::string abi =
        blockContext.blockVersion() >= static_cast<uint32_t>(BlockVersion::V3_1_VERSION) ?
            std::string(TABLE_ABI) :
            "";
    auto response = externalRequest(_executive, ref(input), _callParameters->m_origin,
        tableManagerAddress, blockContext.isWasm() ? newTableName : "", false, true,
        _callParameters->m_gasLeft - gasPricer->calTotalGas(), true, std::move(abi));

    if (response->status != (int32_t)TransactionStatus::None)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("TableManagerPrecompiled")
                              << LOG_DESC("create table failed")
                              << LOG_KV("tableName", newTableName)
                              << LOG_KV("valueField", valueField);
        BOOST_THROW_EXCEPTION(PrecompiledError("Create table error."));
    }

    _executive->storage().createTable(getActualTableName(newTableName), valueField);
    _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
}
