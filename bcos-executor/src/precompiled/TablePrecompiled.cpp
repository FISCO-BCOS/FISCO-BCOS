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
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
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

constexpr const char* const TABLE_METHOD_SELECT_KEY = "select(string)";
constexpr const char* const TABLE_METHOD_SELECT_CON = "select((uint8,string)[],(uint32,uint32))";
constexpr const char* const TABLE_METHOD_COUNT = "count((uint8,string)[])";
constexpr const char* const TABLE_METHOD_INSERT = "insert((string,string[]))";
constexpr const char* const TABLE_METHOD_UPDATE_KEY = "update(string,(string,string)[])";
constexpr const char* const TABLE_METHOD_UPDATE_CON =
    "update((uint8,string)[],(uint32,uint32),(string,string)[])";
constexpr const char* const TABLE_METHOD_REMOVE_KEY = "remove(string)";
constexpr const char* const TABLE_METHOD_REMOVE_CON = "remove((uint8,string)[],(uint32,uint32))";

TablePrecompiled::TablePrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[TABLE_METHOD_SELECT_KEY] = getFuncSelector(TABLE_METHOD_SELECT_KEY, _hashImpl);
    name2Selector[TABLE_METHOD_SELECT_CON] = getFuncSelector(TABLE_METHOD_SELECT_CON, _hashImpl);
    name2Selector[TABLE_METHOD_COUNT] = getFuncSelector(TABLE_METHOD_COUNT, _hashImpl);
    name2Selector[TABLE_METHOD_INSERT] = getFuncSelector(TABLE_METHOD_INSERT, _hashImpl);
    name2Selector[TABLE_METHOD_UPDATE_KEY] = getFuncSelector(TABLE_METHOD_UPDATE_KEY, _hashImpl);
    name2Selector[TABLE_METHOD_UPDATE_CON] = getFuncSelector(TABLE_METHOD_UPDATE_CON, _hashImpl);
    name2Selector[TABLE_METHOD_REMOVE_KEY] = getFuncSelector(TABLE_METHOD_REMOVE_KEY, _hashImpl);
    name2Selector[TABLE_METHOD_REMOVE_CON] = getFuncSelector(TABLE_METHOD_REMOVE_CON, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> TablePrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    // [tableName,keyField,valueFields][actualParams]
    std::vector<std::string> dynamicParams;
    bytes param;
    codec.decode(_callParameters->input(), dynamicParams, param);
    auto tableName = dynamicParams.at(0);
    tableName = getActualTableName(tableName);
    // get user call actual params
    auto originParam = ref(param);
    uint32_t func = getParamFunc(originParam);
    bytesConstRef data = getParamData(originParam);
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(param.size());

    auto table = _executive->storage().openTable(tableName);
    if (!table.has_value())
    {
        BOOST_THROW_EXCEPTION(PrecompiledError(tableName + " does not exist"));
    }

    if (func == name2Selector[TABLE_METHOD_SELECT_KEY])
    {
        /// select(string)
        selectByKey(tableName, _executive, data, gasPricer, _callParameters);
    }
    else if (func == name2Selector[TABLE_METHOD_SELECT_CON])
    {
        /// select((uint8,string)[],(uint32,uint32))
        selectByCondition(tableName, _executive, data, gasPricer, _callParameters);
    }
    else if (func == name2Selector[TABLE_METHOD_INSERT])
    {
        /// insert((string,string[]))
        insert(tableName, _executive, data, gasPricer, _callParameters);
    }
    else if (func == name2Selector[TABLE_METHOD_UPDATE_KEY])
    {
        /// update(string,(uint,string)[])
        updateByKey(tableName, _executive, data, gasPricer, _callParameters);
    }
    else if (func == name2Selector[TABLE_METHOD_UPDATE_CON])
    {
        /// update((uint8,string)[],(uint32,uint32),(uint,string)[])
        updateByCondition(tableName, _executive, data, gasPricer, _callParameters);
    }
    else if (func == name2Selector[TABLE_METHOD_REMOVE_KEY])
    {
        /// remove(string)
        removeByKey(tableName, _executive, data, gasPricer, _callParameters);
    }
    else if (func == name2Selector[TABLE_METHOD_REMOVE_CON])
    {
        /// remove((uint8,string)[],(uint32,uint32))
        removeByCondition(tableName, _executive, data, gasPricer, _callParameters);
    }
    else if (func == name2Selector[TABLE_METHOD_COUNT])
    {
        /// count((uint8,string)[])
        count(tableName, _executive, data, gasPricer, _callParameters);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled")
                               << LOG_DESC("call undefined function!");
        BOOST_THROW_EXCEPTION(PrecompiledError("TablePrecompiled call undefined function!"));
    }
    gasPricer->updateMemUsed(_callParameters->m_execResult.size());
    _callParameters->setGas(_callParameters->m_gas - gasPricer->calTotalGas());
    return _callParameters;
}

void TablePrecompiled::desc(TableInfoTuple& _tableInfo, const std::string& _tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters) const
{
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    auto tableName = _tableName.substr(0, 2) == "u_" ? _tableName.substr(2) : _tableName;
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("TablePrecompiled desc") << LOG_KV("tableName", tableName);

    auto input = codec.encodeWithSig("desc(string)", tableName);
    std::string tableManagerAddress =
        blockContext->isWasm() ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS;

    // external call to get desc
    auto response = externalRequest(_executive, ref(input), _callParameters->m_origin,
        _callParameters->m_codeAddress, tableManagerAddress, _callParameters->m_staticCall,
        _callParameters->m_create, _callParameters->m_gas);

    codec.decode(ref(response->data), _tableInfo);
}

void TablePrecompiled::buildKeyCondition(std::optional<storage::Condition>& keyCondition,
    const std::vector<precompiled::ConditionTuple>& conditions, const LimitTuple& limit) const
{
    auto& offset = std::get<0>(limit);
    auto& count = std::get<1>(limit);
    if (count > USER_TABLE_MAX_LIMIT_COUNT || offset > offset + count)
    {
        PRECOMPILED_LOG(ERROR) << LOG_DESC("build key condition limit overflow")
                               << LOG_KV("offset", offset) << LOG_KV("count", count);
        BOOST_THROW_EXCEPTION(PrecompiledError("Limit overflow."));
    }
    if (conditions.empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_DESC("Condition is empty");
        BOOST_THROW_EXCEPTION(PrecompiledError("Condition is empty"));
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

    keyCondition->limit(offset, count);
}

void TablePrecompiled::selectByKey(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// select(string)
    std::string key;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(data, key);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("SELECT")
                           << LOG_KV("tableName", tableName);

    auto entry = _executive->storage().getRow(tableName, key);
    if (!entry.has_value())
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("SELECT")
                               << LOG_DESC("Table select not exist") << LOG_KV("key", key);
        EntryTuple emptyEntry = {};
        _callParameters->setExecResult(codec.encode(std::move(emptyEntry)));
        return;
    }
    auto values = entry->getObject<std::vector<std::string>>();

    // update the memory gas and the computation gas
    gasPricer->updateMemUsed(values.size());
    gasPricer->appendOperation(InterfaceOpcode::Select);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("SELECT")
                           << LOG_KV("key", key) << LOG_KV("valueSize", values.size());

    EntryTuple entryTuple = {key, std::move(values)};
    _callParameters->setExecResult(codec.encode(std::move(entryTuple)));
}

void TablePrecompiled::selectByCondition(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// select((uint8,string)[],(uint32,uint32))
    std::vector<precompiled::ConditionTuple> conditions;
    precompiled::LimitTuple limit;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(data, conditions, limit);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("SELECT")
                           << LOG_KV("tableName", tableName)
                           << LOG_KV("ConditionSize", conditions.size())
                           << LOG_KV("limitOffset", std::get<0>(limit))
                           << LOG_KV("limitCount", std::get<1>(limit));

    auto keyCondition = std::make_optional<storage::Condition>();
    // will throw exception when wrong condition cmp or limit count overflow
    buildKeyCondition(keyCondition, std::move(conditions), std::move(limit));

    if (c_fileLogLevel >= LogLevel::TRACE)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("SELECT")
                               << LOG_DESC("keyCond trace ") << keyCondition->toString();
    }

    // merge keys from storage and eqKeys
    auto tableKeyList = _executive->storage().getPrimaryKeys(tableName, keyCondition);
    std::vector<EntryTuple> entries({});
    entries.reserve(tableKeyList.size());
    for (auto& key : tableKeyList)
    {
        auto tableEntry = _executive->storage().getRow(tableName, key);
        EntryTuple entryTuple = {key, tableEntry->getObject<std::vector<std::string>>()};
        entries.emplace_back(std::move(entryTuple));
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("SELECT")
                           << LOG_KV("entries.size", entries.size());
    // update the memory gas and the computation gas
    gasPricer->updateMemUsed(entries.size());
    gasPricer->appendOperation(InterfaceOpcode::Select, entries.size());
    _callParameters->setExecResult(codec.encode(entries));
}

void TablePrecompiled::count(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)

{
    /// count((uint8,string)[])
    std::vector<precompiled::ConditionTuple> conditions;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(data, conditions);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("COUNT")
                           << LOG_KV("tableName", tableName)
                           << LOG_KV("ConditionSize", conditions.size());

    auto keyCondition = std::make_optional<storage::Condition>();
    // will throw exception when wrong condition cmp or limit count overflow
    buildKeyCondition(keyCondition, std::move(conditions), {});

    if (c_fileLogLevel >= LogLevel::TRACE)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("COUNT")
                               << LOG_DESC("keyCond trace ") << keyCondition->toString();
    }

    auto tableKeyList = _executive->storage().getPrimaryKeys(tableName, keyCondition);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("COUNT")
                           << LOG_KV("entries.size", tableKeyList.size());
    // update the memory gas and the computation gas
    gasPricer->appendOperation(InterfaceOpcode::Select);
    _callParameters->setExecResult(codec.encode(uint32_t(tableKeyList.size())));
}

void TablePrecompiled::insert(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// insert((string,string[]))
    precompiled::EntryTuple insertEntry;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(data, insertEntry);

    auto& key = std::get<0>(insertEntry);
    auto& values = std::get<1>(insertEntry);

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("INSERT")
                           << LOG_KV("tableName", tableName) << LOG_KV("key", key)
                           << LOG_KV("valueSize", values.size());

    TableInfoTuple tableInfo;
    // external call table manager desc
    desc(tableInfo, tableName, _executive, _callParameters);
    auto columns = std::get<1>(tableInfo);

    if (values.size() != columns.size())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("INSERT")
                               << LOG_DESC("Table insert entry fields number mismatch")
                               << LOG_KV("valueSize", values.size())
                               << LOG_KV("fieldSize", columns.size());
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
        _callParameters->setExecResult(codec.encode(int32_t(CODE_INSERT_KEY_EXIST)));
        return;
    }

    Entry entry;
    entry.setObject(std::move(values));

    gasPricer->appendOperation(InterfaceOpcode::Insert);
    gasPricer->updateMemUsed(entry.size());
    _executive->storage().setRow(tableName, key, std::move(entry));
    _callParameters->setExecResult(codec.encode(int32_t(1)));
}

void TablePrecompiled::updateByKey(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// update(string,(string,string)[])
    std::string key;
    std::vector<precompiled::UpdateFieldTuple> updateFields;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(data, key, updateFields);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                           << LOG_KV("tableName", tableName) << LOG_KV("updateKey", key)
                           << LOG_KV("updateFieldsSize", updateFields.size());
    auto existEntry = _executive->storage().getRow(tableName, key);
    if (!existEntry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                               << LOG_DESC("key not exist in table, please use INSERT method")
                               << LOG_KV("notExistKey", key);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_UPDATE_KEY_NOT_EXIST)));
        return;
    }

    TableInfoTuple tableInfo;
    // external call table manager desc
    desc(tableInfo, tableName, _executive, _callParameters);
    auto keyField = std::get<0>(tableInfo);
    auto columns = std::get<1>(tableInfo);
    auto values = existEntry->getObject<std::vector<std::string>>();
    for (const auto& kv : updateFields)
    {
        auto& field = std::get<0>(kv);
        auto& value = std::get<1>(kv);
        checkLengthValidate(
            value, USER_TABLE_FIELD_VALUE_MAX_LENGTH, CODE_TABLE_FIELD_VALUE_LENGTH_OVERFLOW);
        auto const it = std::find(columns.begin(), columns.end(), field);
        if (field == keyField)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                << LOG_DESC("Table cannot update keyField") << LOG_KV("keyField", keyField);
            BOOST_THROW_EXCEPTION(
                PrecompiledError("Table update fields cannot contains key field"));
        }
        if (it == columns.end())
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                << LOG_DESC("Table update field not found") << LOG_KV("field", field);
            BOOST_THROW_EXCEPTION(PrecompiledError("Table update fields not found"));
        }
        auto index = std::distance(columns.begin(), it);
        values[index] = value;
    }
    Entry updateEntry;
    updateEntry.setObject(std::move(values));
    _executive->storage().setRow(tableName, key, std::move(updateEntry));

    gasPricer->appendOperation(InterfaceOpcode::Update);
    _callParameters->setExecResult(codec.encode(int32_t(1)));
}

void TablePrecompiled::updateByCondition(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// update((uint8,string)[],(uint32,uint32),(uint,string)[])
    std::vector<precompiled::ConditionTuple> conditions;
    precompiled::LimitTuple limitTuple;
    std::vector<precompiled::UpdateFieldTuple> updateFields;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(data, conditions, limitTuple, updateFields);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                           << LOG_KV("tableName", tableName)
                           << LOG_KV("ConditionSize", conditions.size())
                           << LOG_KV("limitOffset", std::get<0>(limitTuple))
                           << LOG_KV("limitCount", std::get<1>(limitTuple))
                           << LOG_KV("updateFieldsSize", updateFields.size());
    auto keyCondition = std::make_optional<storage::Condition>();

    // will throw exception when wrong condition cmp or limit count overflow
    buildKeyCondition(keyCondition, std::move(conditions), std::move(limitTuple));

    if (c_fileLogLevel >= LogLevel::TRACE)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                               << LOG_DESC("keyCond trace ") << keyCondition->toString();
    }

    auto tableKeyList = _executive->storage().getPrimaryKeys(tableName, keyCondition);

    TableInfoTuple tableInfo;
    // external call table manager desc
    desc(tableInfo, tableName, _executive, _callParameters);
    auto keyField = std::get<0>(tableInfo);
    auto columns = std::get<1>(tableInfo);

    std::vector<std::pair<uint32_t, std::string>> updateValue;
    updateValue.reserve(updateFields.size());
    for (const auto& kv : updateFields)
    {
        auto& field = std::get<0>(kv);
        auto& value = std::get<1>(kv);
        checkLengthValidate(
            value, USER_TABLE_FIELD_VALUE_MAX_LENGTH, CODE_TABLE_FIELD_VALUE_LENGTH_OVERFLOW);
        auto const it = std::find(columns.begin(), columns.end(), field);
        if (field == keyField)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                << LOG_DESC("Table cannot update keyField") << LOG_KV("keyField", keyField);
            BOOST_THROW_EXCEPTION(
                PrecompiledError("Table update fields cannot contains key field"));
        }
        if (it == columns.end())
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                << LOG_DESC("Table update field not found") << LOG_KV("field", field);
            BOOST_THROW_EXCEPTION(PrecompiledError("Table update fields not found"));
        }
        std::pair<uint32_t, std::string> p = {std::distance(columns.begin(), it), std::move(value)};
        updateValue.emplace_back(std::move(p));
    }

    for (auto& key : tableKeyList)
    {
        auto tableEntry = _executive->storage().getRow(tableName, key);
        auto values = tableEntry->getObject<std::vector<std::string>>();
        for (auto& kv : updateValue)
        {
            values[kv.first] = kv.second;
        }
        Entry updateEntry;
        updateEntry.setObject(std::move(values));
        _executive->storage().setRow(tableName, key, std::move(updateEntry));
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                               << LOG_KV("key", key);
    }
    gasPricer->setMemUsed(tableKeyList.size() * columns.size());
    gasPricer->appendOperation(InterfaceOpcode::Update, tableKeyList.size());
    _callParameters->setExecResult(codec.encode((int32_t)tableKeyList.size()));
}

void TablePrecompiled::removeByKey(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// remove(string)
    std::string key;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(data, key);
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("Table remove") << LOG_KV("tableName", tableName)
                           << LOG_KV("removeKey", key);

    auto existEntry = _executive->storage().getRow(tableName, key);
    if (!existEntry)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("REMOVE")
                               << LOG_DESC("key not exist in table") << LOG_KV("notExistKey", key);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_REMOVE_KEY_NOT_EXIST)));
        return;
    }
    Entry deletedEntry;
    deletedEntry.setStatus(Entry::DELETED);
    _executive->storage().setRow(tableName, key, std::move(deletedEntry));

    gasPricer->appendOperation(InterfaceOpcode::Remove);
    _callParameters->setExecResult(codec.encode(int32_t(1)));
}

void TablePrecompiled::removeByCondition(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// remove((uint8,string)[],(uint32,uint32))
    std::vector<precompiled::ConditionTuple> conditions;
    precompiled::LimitTuple limitTuple;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(data, conditions, limitTuple);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("REMOVE")
                           << LOG_KV("tableName", tableName)
                           << LOG_KV("ConditionSize", conditions.size())
                           << LOG_KV("limitOffset", std::get<0>(limitTuple))
                           << LOG_KV("limitCount", std::get<1>(limitTuple));

    auto keyCondition = std::make_optional<storage::Condition>();

    // will throw exception when wrong condition cmp or limit count overflow
    buildKeyCondition(keyCondition, std::move(conditions), std::move(limitTuple));

    if (c_fileLogLevel >= LogLevel::TRACE)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("REMOVE")
                               << LOG_DESC("keyCond trace ") << keyCondition->toString();
    }

    auto tableKeyList = _executive->storage().getPrimaryKeys(tableName, keyCondition);

    for (auto& tableKey : tableKeyList)
    {
        Entry deletedEntry;
        deletedEntry.setStatus(Entry::DELETED);
        _executive->storage().setRow(tableName, tableKey, std::move(deletedEntry));
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("REMOVE")
                               << LOG_KV("removeKey", tableKey);
    }
    gasPricer->setMemUsed(tableKeyList.size());
    gasPricer->appendOperation(InterfaceOpcode::Remove, tableKeyList.size());
    _callParameters->setExecResult(codec.encode((int32_t)tableKeyList.size()));
}