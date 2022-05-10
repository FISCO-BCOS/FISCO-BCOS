/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file TablePrecompiled.cpp
 * @author: kyonGuo
 * @date 2022/4/20
 */

#include "TablePrecompiled.h"
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

const char* const TABLE_METHOD_SELECT_KEY = "select(string)";
const char* const TABLE_METHOD_SELECT_CON = "select((uint8,string)[],(uint32,uint32))";
const char* const TABLE_METHOD_INSERT = "insert((string,string[]))";
const char* const TABLE_METHOD_UPDATE_KEY = "update(string,(uint32,string)[])";
const char* const TABLE_METHOD_UPDATE_CON =
    "update((uint8,string)[],(uint32,uint32),(uint32,string)[])";
const char* const TABLE_METHOD_REMOVE_KEY = "remove(string)";
const char* const TABLE_METHOD_REMOVE_CON = "remove((uint8,string)[],(uint32,uint32))";
const char* const TABLE_METHOD_DESC = "desc()";

TablePrecompiled::TablePrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[TABLE_METHOD_SELECT_KEY] = getFuncSelector(TABLE_METHOD_SELECT_KEY, _hashImpl);
    name2Selector[TABLE_METHOD_SELECT_CON] = getFuncSelector(TABLE_METHOD_SELECT_CON, _hashImpl);
    name2Selector[TABLE_METHOD_INSERT] = getFuncSelector(TABLE_METHOD_INSERT, _hashImpl);
    name2Selector[TABLE_METHOD_UPDATE_KEY] = getFuncSelector(TABLE_METHOD_UPDATE_KEY, _hashImpl);
    name2Selector[TABLE_METHOD_UPDATE_CON] = getFuncSelector(TABLE_METHOD_UPDATE_CON, _hashImpl);
    name2Selector[TABLE_METHOD_REMOVE_KEY] = getFuncSelector(TABLE_METHOD_REMOVE_KEY, _hashImpl);
    name2Selector[TABLE_METHOD_REMOVE_CON] = getFuncSelector(TABLE_METHOD_REMOVE_CON, _hashImpl);
    name2Selector[TABLE_METHOD_DESC] = getFuncSelector(TABLE_METHOD_DESC, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> TablePrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string&, const std::string&, int64_t)
{
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    std::vector<std::string> dynamicParams;
    bytes param;
    codec->decode(_param, dynamicParams, param);
    auto tableName = dynamicParams.at(0);
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

    if (func == name2Selector[TABLE_METHOD_SELECT_KEY])
    {
        /// select(string)
        selectByKey(tableName, _executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[TABLE_METHOD_SELECT_CON])
    {
        /// select((uint8,string)[],(uint32,uint32))
        selectByCondition(tableName, _executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[TABLE_METHOD_INSERT])
    {
        /// insert((string,string[]))
        insert(tableName, _executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[TABLE_METHOD_UPDATE_KEY])
    {
        /// update(string,(uint,string)[])
        updateByKey(tableName, _executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[TABLE_METHOD_UPDATE_CON])
    {
        /// update((uint8,string)[],(uint32,uint32),(uint,string)[])
        updateByCondition(tableName, _executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[TABLE_METHOD_REMOVE_KEY])
    {
        /// remove(string)
        removeByKey(tableName, _executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[TABLE_METHOD_REMOVE_CON])
    {
        /// remove((uint8,string)[],(uint32,uint32))
        removeByCondition(tableName, _executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[TABLE_METHOD_DESC])
    {
        /// desc()
        desc(tableName, _executive, callResult, gasPricer);
    }
    else
    {
        STORAGE_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_DESC("call undefined function!");
    }
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
}

void TablePrecompiled::buildKeyCondition(std::optional<storage::Condition>& keyCondition,
    const std::vector<precompiled::ConditionTuple>& conditions, const LimitTuple& limit) const
{
    auto& offset = std::get<0>(limit);
    auto& count = std::get<1>(limit);
    if (count > USER_TABLE_MAX_LIMIT_COUNT || offset > offset + count)
    {
        PRECOMPILED_LOG(ERROR) << LOG_DESC("") << LOG_KV("offset", offset)
                               << LOG_KV("count", count);
        BOOST_THROW_EXCEPTION(PrecompiledError("Limit overflow."));
    }
    for (const auto& condition : conditions)
    {
        auto& cmp = std::get<0>(condition);
        auto& value = std::get<1>(condition);
        switch (cmp)
        {
        case 0:
            keyCondition->GT(value);
            break;
        case 1:
            keyCondition->GE(value);
            break;
        case 2:
            keyCondition->LT(value);
            break;
        case 3:
            keyCondition->LE(value);
            break;
        default:
            BOOST_THROW_EXCEPTION(
                PrecompiledError(std::to_string(cmp) + " ConditionOP not exist!"));
        }
    }

    keyCondition->limit(offset, offset + count);
}

/// FIXME: sys table not support small contract structure
void TablePrecompiled::desc(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    /// desc()
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table desc") << LOG_KV("tableName", tableName);

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

void TablePrecompiled::selectByKey(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    /// select(string)
    std::string key;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, key);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table select") << LOG_KV("tableName", tableName);

    auto entry = _executive->storage().getRow(tableName, key);
    if (!entry.has_value())
    {
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table select not exist") << LOG_KV("key", key);
        EntryTuple emptyEntry = {};
        callResult->setExecResult(codec->encode(std::move(emptyEntry)));
        return;
    }
    auto values = entry->getObject<std::vector<std::string>>();

    // update the memory gas and the computation gas
    gasPricer->updateMemUsed(values.size());
    gasPricer->appendOperation(InterfaceOpcode::Select);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table select") << LOG_KV("key", key)
                           << LOG_KV("valueSize", values.size());

    EntryTuple entryTuple = {key, std::move(values)};
    callResult->setExecResult(codec->encode(std::move(entryTuple)));
}

void TablePrecompiled::selectByCondition(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    /// select((uint8,string)[],(uint32,uint32))
    std::vector<precompiled::ConditionTuple> conditions;
    precompiled::LimitTuple limit;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, conditions, limit);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table select") << LOG_KV("tableName", tableName)
                           << LOG_KV("ConditionSize", conditions.size())
                           << LOG_KV("limitOffset", std::get<0>(limit))
                           << LOG_KV("limitCount", std::get<1>(limit));

    auto keyCondition = std::make_optional<storage::Condition>();
    // will throw exception when wrong condition cmp or limit count overflow
    buildKeyCondition(keyCondition, std::move(conditions), std::move(limit));

    // merge keys from storage and eqKeys
    auto tableKeyList = _executive->storage().getPrimaryKeys(tableName, keyCondition);
    std::vector<EntryTuple> entries({});
    for (auto& key : tableKeyList)
    {
        auto tableEntry = _executive->storage().getRow(tableName, key);
        EntryTuple entryTuple = {key, tableEntry->getObject<std::vector<std::string>>()};
        entries.emplace_back(std::move(entryTuple));
    }
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table select") << LOG_KV("entries.size", entries.size());
    // update the memory gas and the computation gas
    gasPricer->updateMemUsed(entries.size());
    gasPricer->appendOperation(InterfaceOpcode::Select, entries.size());
    callResult->setExecResult(codec->encode(entries));
}

void TablePrecompiled::insert(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    /// insert((string,string[]))
    precompiled::EntryTuple insertEntry;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, insertEntry);

    auto table = _executive->storage().openTable(tableName);
    auto& key = std::get<0>(insertEntry);
    auto& values = std::get<1>(insertEntry);

    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table insert") << LOG_KV("tableName", tableName)
                           << LOG_KV("key", key) << LOG_KV("valueSize", values.size());

    // here is a trick, s_table save table info as (key,values)
    if (values.size() != table->tableInfo()->fields().size() - 1)
    {
        PRECOMPILED_LOG(ERROR) << LOG_DESC("Table insert entry fields number mismatch")
                               << LOG_KV("valueSize", values.size())
                               << LOG_KV("filedSize", table->tableInfo()->fields().size() - 1);
        BOOST_THROW_EXCEPTION(PrecompiledError("Table insert entry fields number mismatch"));
    }
    checkLengthValidate(key, USER_TABLE_KEY_VALUE_MAX_LENGTH, CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
    std::for_each(values.begin(), values.end(), [](std::string_view _v) {
        checkLengthValidate(
            _v, USER_TABLE_FIELD_VALUE_MAX_LENGTH, CODE_TABLE_FIELD_VALUE_LENGTH_OVERFLOW);
    });

    if (_executive->storage().getRow(tableName, key))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("INSERT")
                               << LOG_DESC("key already exist in table, please use UPDATE method")
                               << LOG_KV("key", key);
        callResult->setExecResult(codec->encode(int32_t(CODE_INSERT_KEY_EXIST)));
        return;
    }

    Entry entry;
    entry.setObject(std::move(values));

    gasPricer->appendOperation(InterfaceOpcode::Insert);
    gasPricer->updateMemUsed(entry.size());
    _executive->storage().setRow(tableName, key, std::move(entry));
    callResult->setExecResult(codec->encode(int32_t(1)));
}

void TablePrecompiled::updateByKey(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    /// update(string,(uint,string)[])
    std::string key;
    std::vector<precompiled::UpdateFieldTuple> updateFields;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, key, updateFields);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table update") << LOG_KV("tableName", tableName)
                           << LOG_KV("updateKey", key)
                           << LOG_KV("updateFieldsSize", updateFields.size());
    auto existEntry = _executive->storage().getRow(tableName, key);
    if (!existEntry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                               << LOG_DESC("key not exist in table, please use INSERT method")
                               << LOG_KV("notExistKey", key);
        callResult->setExecResult(codec->encode(int32_t(CODE_UPDATE_KEY_NOT_EXIST)));
        return;
    }

    auto table = _executive->storage().openTable(tableName);

    // here is a trick, s_table save table info as (key,values)
    auto fieldsSize = table->tableInfo()->fields().size() - 1;
    auto values = existEntry->getObject<std::vector<std::string>>();
    for (const auto& field : updateFields)
    {
        auto& index = std::get<0>(field);
        auto& value = std::get<1>(field);
        checkLengthValidate(
            value, USER_TABLE_FIELD_VALUE_MAX_LENGTH, CODE_TABLE_FIELD_VALUE_LENGTH_OVERFLOW);
        if (index >= fieldsSize)
        {
            PRECOMPILED_LOG(ERROR) << LOG_DESC("Table update fields index overflow size")
                                   << LOG_KV("index", index) << LOG_KV("filedSize", fieldsSize);
            BOOST_THROW_EXCEPTION(PrecompiledError("Table update fields index overflow size"));
        }
        values[index] = value;
    }
    Entry updateEntry;
    updateEntry.setObject(std::move(values));
    _executive->storage().setRow(tableName, key, std::move(updateEntry));

    gasPricer->setMemUsed(fieldsSize);
    gasPricer->appendOperation(InterfaceOpcode::Update);
    callResult->setExecResult(codec->encode(int32_t(1)));
}

void TablePrecompiled::updateByCondition(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    /// update((uint8,string)[],(uint32,uint32),(uint,string)[])
    std::vector<precompiled::ConditionTuple> conditions;
    precompiled::LimitTuple limitTuple;
    std::vector<precompiled::UpdateFieldTuple> updateFields;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, conditions, limitTuple, updateFields);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table update") << LOG_KV("tableName", tableName)
                           << LOG_KV("ConditionSize", conditions.size())
                           << LOG_KV("limitOffset", std::get<0>(limitTuple))
                           << LOG_KV("limitCount", std::get<1>(limitTuple))
                           << LOG_KV("updateFieldsSize", updateFields.size());
    auto keyCondition = std::make_optional<storage::Condition>();

    // will throw exception when wrong condition cmp or limit count overflow
    buildKeyCondition(keyCondition, std::move(conditions), std::move(limitTuple));

    auto table = _executive->storage().openTable(tableName);
    auto tableKeyList = table->getPrimaryKeys(keyCondition);

    // here is a trick, s_table save table info as (key,values)
    auto fieldsSize = table->tableInfo()->fields().size() - 1;

    for (auto& key : tableKeyList)
    {
        auto tableEntry = _executive->storage().getRow(tableName, key);
        auto values = tableEntry->getObject<std::vector<std::string>>();
        for (const auto& field : updateFields)
        {
            auto& index = std::get<0>(field);
            auto& value = std::get<1>(field);
            checkLengthValidate(
                value, USER_TABLE_FIELD_VALUE_MAX_LENGTH, CODE_TABLE_FIELD_VALUE_LENGTH_OVERFLOW);
            if (index >= fieldsSize)
            {
                PRECOMPILED_LOG(ERROR) << LOG_DESC("Table update fields index overflow size")
                                       << LOG_KV("index", index) << LOG_KV("filedSize", fieldsSize);
                BOOST_THROW_EXCEPTION(PrecompiledError("Table update fields index overflow size"));
            }
            values[index] = value;
        }
        Entry updateEntry;
        updateEntry.setObject(std::move(values));
        _executive->storage().setRow(tableName, key, std::move(updateEntry));
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table update") << LOG_KV("key", key);
    }
    gasPricer->setMemUsed(tableKeyList.size() * fieldsSize);
    gasPricer->appendOperation(InterfaceOpcode::Update, tableKeyList.size());
    callResult->setExecResult(codec->encode((int32_t)tableKeyList.size()));
}

void TablePrecompiled::removeByKey(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    /// remove(string)
    std::string key;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, key);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table remove") << LOG_KV("tableName", tableName)
                           << LOG_KV("removeKey", key);

    auto existEntry = _executive->storage().getRow(tableName, key);
    if (!existEntry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("REMOVE")
                               << LOG_DESC("key not exist in table") << LOG_KV("notExistKey", key);
        callResult->setExecResult(codec->encode(int32_t(CODE_REMOVE_KEY_NOT_EXIST)));
        return;
    }
    Entry deletedEntry;
    deletedEntry.setStatus(Entry::DELETED);
    _executive->storage().setRow(tableName, key, std::move(deletedEntry));

    gasPricer->appendOperation(InterfaceOpcode::Remove);
    callResult->setExecResult(codec->encode(int32_t(1)));
}

void TablePrecompiled::removeByCondition(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const std::shared_ptr<PrecompiledExecResult>& callResult, const PrecompiledGas::Ptr& gasPricer)
{
    /// remove((uint8,string)[],(uint32,uint32))
    std::vector<precompiled::ConditionTuple> conditions;
    precompiled::LimitTuple limitTuple;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, conditions, limitTuple);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table remove") << LOG_KV("tableName", tableName)
                           << LOG_KV("ConditionSize", conditions.size())
                           << LOG_KV("limitOffset", std::get<0>(limitTuple))
                           << LOG_KV("limitCount", std::get<1>(limitTuple));

    auto keyCondition = std::make_optional<storage::Condition>();

    // will throw exception when wrong condition cmp or limit count overflow
    buildKeyCondition(keyCondition, std::move(conditions), std::move(limitTuple));

    auto tableKeyList = _executive->storage().getPrimaryKeys(tableName, keyCondition);

    for (auto& tableKey : tableKeyList)
    {
        Entry deletedEntry;
        deletedEntry.setStatus(Entry::DELETED);
        _executive->storage().setRow(tableName, tableKey, std::move(deletedEntry));
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table remove") << LOG_KV("removeKey", tableKey);
    }
    gasPricer->setMemUsed(tableKeyList.size());
    gasPricer->appendOperation(InterfaceOpcode::Remove, tableKeyList.size());
    callResult->setExecResult(codec->encode((int32_t)tableKeyList.size()));
}