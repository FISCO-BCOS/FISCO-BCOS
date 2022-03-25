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
 * @file TableFactoryPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-30
 */

#if 0
#include "TableFactoryPrecompiled.h"
#include "Common.h"
#include "PrecompiledResult.h"
#include "Utilities.h"
#include <bcos-framework/interfaces/protocol/Exceptions.h>
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

const char* const TABLE_METHOD_CREATE = "createTable(string,string,string)";
const char* const TABLE_METHOD_SELECT = "select(string,((string,string,uint8)[]))";
const char* const TABLE_METHOD_INSERT = "insert(string,((string,string)[]))";
const char* const TABLE_METHOD_UPDATE =
    "update(string,((string,string)[]),((string,string,uint8)[]))";
const char* const TABLE_METHOD_REMOVE = "remove(string,((string,string,uint8)[]))";
const char* const TABLE_METHOD_DESC = "desc(string)";

TableFactoryPrecompiled::TableFactoryPrecompiled(crypto::Hash::Ptr _hashImpl)
  : Precompiled(_hashImpl)
{
    name2Selector[TABLE_METHOD_CREATE] = getFuncSelector(TABLE_METHOD_CREATE, _hashImpl);
    name2Selector[TABLE_METHOD_SELECT] = getFuncSelector(TABLE_METHOD_SELECT, _hashImpl);
    name2Selector[TABLE_METHOD_INSERT] = getFuncSelector(TABLE_METHOD_INSERT, _hashImpl);
    name2Selector[TABLE_METHOD_UPDATE] = getFuncSelector(TABLE_METHOD_UPDATE, _hashImpl);
    name2Selector[TABLE_METHOD_REMOVE] = getFuncSelector(TABLE_METHOD_REMOVE, _hashImpl);
    name2Selector[TABLE_METHOD_DESC] = getFuncSelector(TABLE_METHOD_DESC, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> TableFactoryPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string&, const std::string&)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[TABLE_METHOD_CREATE])
    {
        // createTable(string,string,string)
        createTable(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[TABLE_METHOD_SELECT])
    {
        // select(string,((string,string,uint8)[]))
        select(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[TABLE_METHOD_INSERT])
    {
        // insert(string,((string,string)[]))
        insert(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[TABLE_METHOD_UPDATE])
    {
        // update(string,((string,string)[]),((string,string,uint8)[]))
        update(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[TABLE_METHOD_REMOVE])
    {
        // remove(string,((string,string,uint8)[]))
        remove(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[TABLE_METHOD_DESC])
    {
        // desc(string)
        desc(_executive, data, callResult, gasPricer);
    }
    else
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_DESC("call undefined function!");
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

std::tuple<std::string, std::string> TableFactoryPrecompiled::getTableField(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const std::string& _tableName)
{
    auto table = _executive->storage().openTable(_tableName);
    auto sysTable = _executive->storage().openTable(storage::StorageInterface::SYS_TABLES);
    auto sysEntry = sysTable->getRow(_tableName);
    if (!table || !sysEntry)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("TableFactoryPrecompiled")
                                 << LOG_DESC("Open table failed")
                                 << LOG_KV("tableName", _tableName);
        BOOST_THROW_EXCEPTION(PrecompiledError(_tableName + " does not exist"));
    }
    auto valueKey = sysEntry->getField(0);
    auto keyField = std::string(valueKey.substr(valueKey.find_last_of(',') + 1));
    auto valueFields = std::string(valueKey.substr(0, valueKey.find_last_of(',')));
    return {std::move(keyField), std::move(valueFields)};
}

bool TableFactoryPrecompiled::buildConditionCtx(const precompiled::Condition::Ptr& _condition,
    const precompiled::ConditionTuple& _tuple, std::shared_ptr<storage::Condition>& _keyCondition,
    std::vector<std::string>& _eqKeyList, const std::string& _keyField)
{
    bool flag = false;
    for (const auto& compareTuple : std::get<0>(_tuple))
    {
        auto k = std::get<0>(compareTuple);
        auto v = std::get<1>(compareTuple);
        auto cmp = (Comparator)(static_cast<int>(std::get<2>(compareTuple)));

        if (k == _keyField)
        {
            flag = true;
            if (cmp == precompiled::Comparator::EQ)
            {
                _eqKeyList.emplace_back(v);
            }
            else
            {
                transferKeyCond(cmp, v, _keyCondition);
            }
        }
        CompareTriple cmpTriple(std::move(k), std::move(v), cmp);
        _condition->m_conditions.emplace_back(std::move(cmpTriple));
    }
    return flag;
}

void TableFactoryPrecompiled::createTable(
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

    precompiled::checkCreateTableParam(tableName, keyField, valueField);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("StateStorage") << LOG_KV("createTable", tableName)
                           << LOG_KV("keyField", keyField) << LOG_KV("valueField", valueField);
    // /tables + tableName
    auto newTableName = getTableName(tableName);
    int result = CODE_SUCCESS;
    auto table = _executive->storage().openTable(newTableName);
    if (table)
    {
        // table already exist
        result = CODE_TABLE_NAME_ALREADY_EXIST;
        getErrorCodeOut(callResult->mutableExecResult(), result, *codec);
        return;
    }
    else
    {
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
            parentTable->setRow(tableBaseName, std::move(stubEntry.value()));
        }
    }
    getErrorCodeOut(callResult->mutableExecResult(), result, *codec);
}

void TableFactoryPrecompiled::select(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    // select(string,((string,string,uint8)[]))
    std::string tableName;
    precompiled::ConditionTuple conditions;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, tableName, conditions);
    tableName = getTableName(tableName);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table select") << LOG_KV("tableName", tableName);

    std::string keyField, valueFields;
    std::tie(keyField, valueFields) = getTableField(_executive, tableName);
    std::vector<std::string> valueFieldList;
    boost::split(valueFieldList, valueFields, boost::is_any_of(","));

    auto entryCondition = std::make_shared<precompiled::Condition>();
    std::shared_ptr<storage::Condition> keyCondition = std::make_shared<storage::Condition>();
    std::vector<std::string> eqKeyList;
    bool findKeyFlag =
        buildConditionCtx(entryCondition, conditions, keyCondition, eqKeyList, keyField);
    if (!findKeyFlag)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("SELECT")
                               << LOG_DESC("can't get any primary key in condition");
        callResult->setExecResult(codec->encode(EntryTuple()));
        return;
    }

    // merge keys from storage and eqKeys
    auto tableKeyList = _executive->storage().getPrimaryKeys(tableName, *keyCondition);
    std::set<std::string> tableKeySet{tableKeyList.begin(), tableKeyList.end()};
    tableKeySet.insert(eqKeyList.begin(), eqKeyList.end());
    std::vector<EntryTuple> entries({});
    auto table = _executive->storage().openTable(tableName);
    for (auto& key : tableKeySet)
    {
        auto entry = table->getRow(key);
        if (entryCondition->filter(entry))
        {
            entries.emplace_back(entry->getObject<EntryTuple>());
        }
    }
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table select") << LOG_KV("entries.size", entries.size());
    // update the memory gas and the computation gas
    gasPricer->updateMemUsed(entries.size());
    gasPricer->appendOperation(InterfaceOpcode::Select, entries.size());
    callResult->setExecResult(codec->encode(entries));
}

void TableFactoryPrecompiled::insert(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    // insert(string,((string,string)[]))
    std::string tableName;
    precompiled::EntryTuple insertEntry;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, tableName, insertEntry);
    tableName = getTableName(tableName);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table insert") << LOG_KV("tableName", tableName);

    std::string keyField, valueFields;
    std::tie(keyField, valueFields) = getTableField(_executive, tableName);
    auto table = _executive->storage().openTable(tableName);
    auto tableInfo = table->tableInfo();

    std::string keyValue;
    for (auto const& entryValue : std::get<0>(insertEntry))
    {
        checkLengthValidate(std::get<1>(entryValue), USER_TABLE_FIELD_VALUE_MAX_LENGTH,
            CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
        if (std::get<0>(entryValue) == keyField)
        {
            keyValue = std::get<1>(entryValue);
        }
    }
    if (keyValue.empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("INSERT")
                               << LOG_DESC("can't get any primary key in entry string")
                               << LOG_KV("primaryKeyField", keyField);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_KEY_NOT_EXIST_IN_ENTRY, *codec);
    }
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table insert") << LOG_KV("key", keyValue);
    // auto checkExistEntry = table->getRow(keyValue);
    auto checkExistEntry = _executive->storage().getRow(tableName, keyValue);
    if (checkExistEntry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("INSERT")
                               << LOG_DESC("key already exist in table, please use UPDATE method")
                               << LOG_KV("primaryKey", keyField) << LOG_KV("existKey", keyValue);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_INSERT_KEY_EXIST, *codec);
    }
    else
    {
        // auto entry = table->newEntry();
        Entry entry;
        entry.setObject(insertEntry);

        gasPricer->appendOperation(InterfaceOpcode::Insert, 1);
        gasPricer->updateMemUsed(entry.size());
        _executive->storage().setRow(tableName, keyValue, std::move(entry));
        callResult->setExecResult(codec->encode(u256(1)));
    }
}

void TableFactoryPrecompiled::update(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    // update(string,((string,string)[]),((string,string,uint8)[]))
    std::string tableName;
    precompiled::EntryTuple entry;
    precompiled::ConditionTuple conditions;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, tableName, entry, conditions);
    tableName = getTableName(tableName);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table update") << LOG_KV("tableName", tableName);

    std::string keyField;
    std::tie(keyField, std::ignore) = getTableField(_executive, tableName);

    auto entryCondition = std::make_shared<precompiled::Condition>();
    std::shared_ptr<storage::Condition> keyCondition = std::make_shared<storage::Condition>();
    std::vector<std::string> eqKeyList;
    bool findKeyFlag =
        buildConditionCtx(entryCondition, conditions, keyCondition, eqKeyList, keyField);
    if (!findKeyFlag)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                               << LOG_DESC("can't get any primary key in condition");
        callResult->setExecResult(codec->encode(EntryTuple()));
        return;
    }
    // check eq key exist in table
    for (auto& key : eqKeyList)
    {
        auto checkExistEntry = _executive->storage().getRow(tableName, key);
        if (checkExistEntry == std::nullopt)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                                   << LOG_DESC("key not exist in table, please use INSERT method")
                                   << LOG_KV("primaryKey", keyField) << LOG_KV("notExistKey", key);
            getErrorCodeOut(callResult->mutableExecResult(), CODE_UPDATE_KEY_NOT_EXIST, *codec);
            return;
        }
    }
    auto table = _executive->storage().openTable(tableName);
    auto tableKeyList = table->getPrimaryKeys(*keyCondition);

    std::set<std::string> tableKeySet{tableKeyList.begin(), tableKeyList.end()};
    tableKeySet.insert(eqKeyList.begin(), eqKeyList.end());
    u256 updateCount = 0;
    auto updateEntry = table->newEntry();
    // updateEntry->getObject<>();
    updateEntry.setObject(entry);
    for (auto& tableKey : tableKeySet)
    {
        auto tableEntry = table->getRow(tableKey);
        if (entryCondition->filter(tableEntry))
        {
            tableEntry = updateEntry;

            table->setRow(tableKey, std::move(*tableEntry));
            PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table update") << LOG_KV("key", tableKey);
            updateCount++;
        }
    }
    gasPricer->setMemUsed(updateEntry.size());
    gasPricer->appendOperation(InterfaceOpcode::Update, (unsigned int)updateCount);
    callResult->setExecResult(codec->encode(updateCount));
}

void TableFactoryPrecompiled::remove(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    std::string tableName;
    precompiled::ConditionTuple conditions;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, tableName, conditions);
    tableName = getTableName(tableName);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table remove") << LOG_KV("tableName", tableName);

    std::string keyField;
    std::tie(keyField, std::ignore) = getTableField(_executive, tableName);

    auto entryCondition = std::make_shared<precompiled::Condition>();
    std::shared_ptr<storage::Condition> keyCondition = std::make_shared<storage::Condition>();
    std::vector<std::string> eqKeyList;
    bool findKeyFlag =
        buildConditionCtx(entryCondition, conditions, keyCondition, eqKeyList, keyField);
    if (!findKeyFlag)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("REMOVE")
                               << LOG_DESC("can't get any primary key in condition");
        callResult->setExecResult(codec->encode(EntryTuple()));
        return;
    }
    auto tableKeyList = _executive->storage().getPrimaryKeys(tableName, *keyCondition);
    std::set<std::string> tableKeySet{tableKeyList.begin(), tableKeyList.end()};
    tableKeySet.insert(eqKeyList.begin(), eqKeyList.end());

    auto table = _executive->storage().openTable(tableName);
    for (auto& tableKey : tableKeySet)
    {
        auto entry = table->getRow(tableKey);
        // Note: entry maybe nullptr
        if (entryCondition->filter(entry))
        {
            table->setRow(tableKey, table->newDeletedEntry());
            PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table remove") << LOG_KV("removeKey", tableKey);
        }
    }
    gasPricer->appendOperation(InterfaceOpcode::Remove, 1);
    callResult->setExecResult(codec->encode(u256(1)));
}

void TableFactoryPrecompiled::desc(
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
    std::tie(keyField, valueFields) = getTableField(_executive, tableName);

    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    callResult->setExecResult(codec->encode(keyField, valueFields));
}
#endif