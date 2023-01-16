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
#include <bcos-framework/protocol/Exceptions.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/throw_exception.hpp>
#include <limits>

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

/// v3.2.0 new interfaces
constexpr const char* const TABLE_METHOD_SELECT_CON_V320 =
    "select((uint8,string,string)[],(uint32,uint32))";
constexpr const char* const TABLE_METHOD_COUNT_V320 = "count((uint8,string,string)[])";
constexpr const char* const TABLE_METHOD_UPDATE_CON_V320 =
    "update((uint8,string,string)[],(uint32,uint32),(string,string)[])";
constexpr const char* const TABLE_METHOD_REMOVE_CON_V320 =
    "remove((uint8,string,string)[],(uint32,uint32))";

static std::string toNumericalOrder(std::string_view lexicographicKey)
{
    try
    {
        auto number = boost::lexical_cast<int64_t>(lexicographicKey);
        // strict restrictions on lexicographicKey (for example, "000001234")
        if (std::to_string(number) != lexicographicKey) [[unlikely]]
        {
            PRECOMPILED_LOG(DEBUG) << "The key cannot be converted to a number(int64)";
            BOOST_THROW_EXCEPTION(
                PrecompiledError("The key cannot be converted to a number(int64)"));
        }
        int64_t offset = std::numeric_limits<int64_t>::max();
        // map int64 to uint64 ([INT64_MIN, INT64_MAX] --> [UINT64_MIN, UINT64_MAX])
        uint64_t _number =
            number < 0 ? (uint64_t)((number + offset) + 1) : ((uint64_t)(number) + offset) + 1;
        std::stringstream stream;
        // convert int64 to a string with length of 32
        stream << std::setfill('0') << std::setw(32) << std::right << _number;
        return stream.str();
    }
    catch (boost::bad_lexical_cast& e)
    {
        PRECOMPILED_LOG(DEBUG) << "The key cannot be converted to a number(int64)";
        BOOST_THROW_EXCEPTION(PrecompiledError("The key cannot be converted to a number(int64)"));
    }
}

static std::string toLexicographicOrder(std::string_view numericalKey)
{
    auto number = boost::lexical_cast<uint64_t>(numericalKey);
    int64_t offset = std::numeric_limits<int64_t>::max();
    // map uint64 to int64 ([UINT64_MIN, UINT64_MAX] --> [INT64_MIN, INT64_MAX])
    int64_t _number = number > (uint64_t)offset ? (int64_t)((number - offset) - 1) :
                                                  ((int64_t)number - offset) - 1;
    return std::to_string(_number);
}

bool TablePrecompiled::isNumericalOrder(const TableInfoTupleV320& tableInfo)
{
    uint8_t keyOrder = std::get<0>(tableInfo);
    if (keyOrder != 0 && keyOrder != 1) [[unlikely]]
    {
        PRECOMPILED_LOG(DEBUG) << std::to_string((int)keyOrder) + " KeyOrder not exist!";
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError(std::to_string((int)keyOrder) + " KeyOrder not exist!"));
    }
    return keyOrder == 1;
}

bool TablePrecompiled::isNumericalOrder(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters, const std::string& _tableName)
{
    precompiled::TableInfo tableInfo;
    // external call table manager desc
    desc(tableInfo, _tableName, _executive, _callParameters, true);
    return isNumericalOrder(tableInfo.info_v320);
}

static size_t selectByValueCond(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const std::string& tableName, const std::vector<std::string>& tableKeyList,
    std::vector<EntryTuple>& entries, std::optional<precompiled::Condition> valueCondition,
    bool toLexicographic = false)
{
    auto [offset, total] = valueCondition->getLimit();
    if (total == 0)
        return 0;

    size_t validCount = 0;
    for (const auto& key : tableKeyList)
    {
        auto tableEntry = _executive->storage().getRow(tableName, key);
        EntryTuple entryTuple;
        // Convert key back to lexicographical order, when the table uses numerical order
        if (toLexicographic)
        {
            entryTuple = {
                toLexicographicOrder(key), tableEntry->getObject<std::vector<std::string>>()};
        }
        else
        {
            entryTuple = {key, tableEntry->getObject<std::vector<std::string>>()};
        }

        if (valueCondition->isValid(std::get<1>(entryTuple)))
        {
            if (validCount >= offset && validCount < offset + total)
            {
                entries.emplace_back(std::move(entryTuple));
            }
            ++validCount;
            if (validCount >= offset + total)
            {
                break;
            }
        }
    }
    return validCount;
}

template <typename Functor>
static void processEntryByValueCond(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const std::string& tableName,
    std::optional<storage::Condition> keyCondition,
    std::optional<precompiled::Condition> valueCondition, Functor&& processEntry)
{
    size_t totalCount = 0;
    size_t singleCountByKeyMax = keyCondition->getLimit().second;
    size_t singleCountByKey = singleCountByKeyMax;
    auto [valueLimitOffset, valueLimitCount] = valueCondition->getLimit();
    while (singleCountByKey >= singleCountByKeyMax && totalCount < valueLimitCount)
    {
        auto tableKeyList = _executive->storage().getPrimaryKeys(tableName, keyCondition);
        auto [validCount, entrySize] = processEntry(tableKeyList, valueCondition);
        singleCountByKey = tableKeyList.size();
        totalCount += entrySize;
        valueLimitOffset = std::max(int(valueLimitOffset - validCount), 0);
        auto [keyLimitOffset, keyLimitCount] = keyCondition->getLimit();
        keyCondition->limit(keyLimitOffset + singleCountByKey, keyLimitCount);
        valueCondition->limit(valueLimitOffset, valueLimitCount - totalCount);
    }
}

TablePrecompiled::TablePrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    registerFunc(getFuncSelector(TABLE_METHOD_SELECT_KEY),
        [this](auto&& table, auto&& executive, auto&& data, auto&& pricer, auto&& params) {
            selectByKey(table, executive, data, pricer, params);
        });
    registerFunc(getFuncSelector(TABLE_METHOD_SELECT_CON),
        [this](auto&& table, auto&& executive, auto&& data, auto&& pricer, auto&& params) {
            selectByCondition(table, executive, data, pricer, params);
        });
    registerFunc(getFuncSelector(TABLE_METHOD_COUNT),
        [this](auto&& table, auto&& executive, auto&& data, auto&& pricer, auto&& params) {
            count(table, executive, data, pricer, params);
        });
    registerFunc(getFuncSelector(TABLE_METHOD_INSERT),
        [this](auto&& table, auto&& executive, auto&& data, auto&& pricer, auto&& params) {
            insert(table, executive, data, pricer, params);
        });
    registerFunc(getFuncSelector(TABLE_METHOD_UPDATE_KEY),
        [this](auto&& table, auto&& executive, auto&& data, auto&& pricer, auto&& params) {
            updateByKey(table, executive, data, pricer, params);
        });
    registerFunc(getFuncSelector(TABLE_METHOD_UPDATE_CON),
        [this](auto&& table, auto&& executive, auto&& data, auto&& pricer, auto&& params) {
            updateByCondition(table, executive, data, pricer, params);
        });
    registerFunc(getFuncSelector(TABLE_METHOD_REMOVE_KEY),
        [this](auto&& table, auto&& executive, auto&& data, auto&& pricer, auto&& params) {
            removeByKey(table, executive, data, pricer, params);
        });
    registerFunc(getFuncSelector(TABLE_METHOD_REMOVE_CON),
        [this](auto&& table, auto&& executive, auto&& data, auto&& pricer, auto&& params) {
            removeByCondition(table, executive, data, pricer, params);
        });
    registerFunc(
        getFuncSelector(TABLE_METHOD_SELECT_CON_V320),
        [this](auto&& table, auto&& executive, auto&& data, auto&& pricer, auto&& params) {
            selectByConditionV32(table, executive, data, pricer, params);
        },
        BlockVersion::V3_2_VERSION);
    registerFunc(
        getFuncSelector(TABLE_METHOD_COUNT_V320),
        [this](auto&& table, auto&& executive, auto&& data, auto&& pricer, auto&& params) {
            countV32(table, executive, data, pricer, params);
        },
        BlockVersion::V3_2_VERSION);
    registerFunc(
        getFuncSelector(TABLE_METHOD_UPDATE_CON_V320),
        [this](auto&& table, auto&& executive, auto&& data, auto&& pricer, auto&& params) {
            updateByConditionV32(table, executive, data, pricer, params);
        },
        BlockVersion::V3_2_VERSION);
    registerFunc(
        getFuncSelector(TABLE_METHOD_REMOVE_CON_V320),
        [this](auto&& table, auto&& executive, auto&& data, auto&& pricer, auto&& params) {
            removeByConditionV32(table, executive, data, pricer, params);
        },
        BlockVersion::V3_2_VERSION);
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

    auto selector = selector2Func.find(func);
    if (selector != selector2Func.end()) [[likely]]
    {
        auto& [minVersion, execFunc] = selector->second;
        if (versionCompareTo(blockContext->blockVersion(), minVersion) >= 0)
        {
            if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
            {
                PRECOMPILED_LOG(TRACE) << LOG_BADGE("TablePrecompiled") << LOG_DESC("call function")
                                       << LOG_KV("func", func) << LOG_KV("minVersion", minVersion);
            }
            execFunc(tableName, _executive, data, gasPricer, _callParameters);
            gasPricer->updateMemUsed(_callParameters->m_execResult.size());
            _callParameters->setGasLeft(_callParameters->m_gasLeft - gasPricer->calTotalGas());
            return _callParameters;
        }
    }

    PRECOMPILED_LOG(INFO) << LOG_BADGE("TablePrecompiled") << LOG_DESC("call undefined function!");
    BOOST_THROW_EXCEPTION(PrecompiledError("TablePrecompiled call undefined function!"));
}

void TablePrecompiled::desc(precompiled::TableInfo& _tableInfo, const std::string& _tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters, bool withKeyOrder) const
{
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    auto tableName = _tableName.starts_with("u_") ? _tableName.substr(2) : _tableName;
    PRECOMPILED_LOG(DEBUG) << LOG_DESC("TablePrecompiled desc") << LOG_KV("tableName", tableName);

    auto input = withKeyOrder ? codec.encodeWithSig("descWithKeyOrder(string)", tableName) :
                                codec.encodeWithSig("desc(string)", tableName);
    std::string tableManagerAddress =
        blockContext->isWasm() ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS;

    // external call to get desc
    auto response = externalRequest(_executive, ref(input), _callParameters->m_origin,
        _callParameters->m_codeAddress, tableManagerAddress, _callParameters->m_staticCall,
        _callParameters->m_create, _callParameters->m_gasLeft);

    if (blockContext->blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_2_VERSION &&
        withKeyOrder)
    {
        codec.decode(ref(response->data), _tableInfo.info_v320);
    }
    else
    {
        codec.decode(ref(response->data), _tableInfo.info);
    }
}

void TablePrecompiled::buildKeyCondition(std::shared_ptr<storage::Condition>& keyCondition,
    const std::vector<precompiled::ConditionTuple>& conditions, const LimitTuple& limit) const
{
    const auto& offset = std::get<0>(limit);
    const auto& count = std::get<1>(limit);
    if (count > USER_TABLE_MAX_LIMIT_COUNT || offset > offset + count)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("build key condition limit overflow")
                               << LOG_KV("offset", offset) << LOG_KV("count", count);
        BOOST_THROW_EXCEPTION(PrecompiledError("Limit overflow."));
    }
    if (conditions.empty())
    {
        BOOST_THROW_EXCEPTION(PrecompiledError("Condition is empty"));
    }
    for (const auto& condition : conditions)
    {
        const auto& cmp = std::get<0>(condition);
        const auto& value = std::get<1>(condition);
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

bool TablePrecompiled::buildConditions(std::optional<precompiled::Condition>& valueCondition,
    const std::vector<precompiled::ConditionTupleV320>& conditions, const LimitTuple& limit,
    precompiled::TableInfoTupleV320& tableInfo) const
{
    const auto& offset = std::get<0>(limit);
    const auto& count = std::get<1>(limit);
    if (count > USER_TABLE_MAX_LIMIT_COUNT || offset > offset + count)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_DESC("build key condition limit overflow")
                               << LOG_KV("offset", offset) << LOG_KV("count", count);
        BOOST_THROW_EXCEPTION(PrecompiledError("Limit overflow."));
    }
    if (conditions.empty())
    {
        BOOST_THROW_EXCEPTION(PrecompiledError("Condition is empty"));
    }

    auto& keyField = std::get<1>(tableInfo);
    auto& columns = std::get<2>(tableInfo);

    bool isRangeSelect = true;
    for (const auto& [cmp, field, field_value] : conditions)
    {
        uint32_t field_idx = 0;
        auto const it = std::find(columns.begin(), columns.end(), field);
        if (it == columns.end() && field != keyField)
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("TablePrecompiled") << LOG_DESC("Table condition field not found")
                << LOG_KV("field", field);
            BOOST_THROW_EXCEPTION(PrecompiledError("Table condition fields not found"));
        }
        if (field != keyField)
        {
            field_idx = std::distance(columns.begin(), it) + 1;
        }

        auto value = field_value;

        if (field_idx == 0)
        {
            if (isNumericalOrder(tableInfo) &&
                (cmp < (uint8_t)storage::Condition::Comparator::STARTS_WITH ||
                    cmp > (uint8_t)storage::Condition::Comparator::CONTAINS))
            {
                value = toNumericalOrder(value);
            }
            isRangeSelect = isRangeSelect && !(cmp < (uint8_t)storage::Condition::Comparator::GT ||
                                                 cmp > (uint8_t)storage::Condition::Comparator::LE);
        }
        valueCondition->addOp(cmp, field_idx, value);
    }
    bool onlyUseKey = valueCondition->contains(0) && valueCondition->size() == 1;
    if (onlyUseKey)
    {
        valueCondition->limitKey(offset, count);
    }
    else
    {
        valueCondition->limit(offset, count);
        if (isRangeSelect && valueCondition->contains(0))
        {
            valueCondition->limitKey(0, std::max(count, (uint32_t)USER_TABLE_MIN_LIMIT_COUNT));
        }
        else
        {
            valueCondition->limitKey(0, USER_TABLE_MAX_LIMIT_COUNT);
        }
    }
    return !onlyUseKey;
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
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("SELECT")
                           << LOG_KV("tableName", tableName);

    std::string originKey = key;
    if (blockContext->blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_2_VERSION &&
        isNumericalOrder(_executive, _callParameters, tableName))
    {
        key = toNumericalOrder(key);
    }

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
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("SELECT")
                           << LOG_KV("key", key) << LOG_KV("valueSize", values.size());

    // Return the original key instead of the key converted to numerical order
    EntryTuple entryTuple = {originKey, std::move(values)};
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

    auto keyCondition = std::make_shared<storage::Condition>();
    // will throw exception when wrong condition cmp or limit count overflow
    buildKeyCondition(keyCondition, conditions, limit);

    std::vector<EntryTuple> entries({});

    // merge keys from storage and eqKeys
    auto tableKeyList = _executive->storage().getPrimaryKeys(tableName, *keyCondition);
    entries.reserve(tableKeyList.size());
    for (auto& key : tableKeyList)
    {
        auto tableEntry = _executive->storage().getRow(tableName, key);
        EntryTuple entryTuple = {key, tableEntry->getObject<std::vector<std::string>>()};
        entries.emplace_back(std::move(entryTuple));
    }

    PRECOMPILED_LOG(TRACE) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("SELECT")
                           << LOG_KV("entries.size", entries.size());
    // update the memory gas and the computation gas
    gasPricer->updateMemUsed(entries.size());
    gasPricer->appendOperation(InterfaceOpcode::Select, entries.size());
    _callParameters->setExecResult(codec.encode(entries));
}

void TablePrecompiled::selectByConditionV32(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// select((uint8,string,string)[],(uint32,uint32))
    std::vector<precompiled::ConditionTupleV320> conditions;
    precompiled::LimitTuple limit;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    bool _isNumericalOrder = false;

    codec.decode(data, conditions, limit);
    precompiled::TableInfo tableInfo;
    // external call table manager desc
    desc(tableInfo, tableName, _executive, _callParameters, true);
    _isNumericalOrder = isNumericalOrder(tableInfo.info_v320);

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("SELECT")
                           << LOG_KV("tableName", tableName)
                           << LOG_KV("ConditionSize", conditions.size())
                           << LOG_KV("limitOffset", std::get<0>(limit))
                           << LOG_KV("limitCount", std::get<1>(limit));

    auto valueCondition = std::make_optional<precompiled::Condition>();
    auto useValueCond =
        buildConditions(valueCondition, std::move(conditions), limit, tableInfo.info_v320);

    std::vector<EntryTuple> entries({});
    // when limitcount==0, skip select operation directly
    if (std::get<1>(limit) > 0)
    {
        if (useValueCond)
        {
            auto func = [_executive, &tableName, &entries, _isNumericalOrder](
                            const std::vector<std::string>& tableKeyList,
                            std::optional<precompiled::Condition> _valueCondition) {
                size_t entrySize = entries.size();
                size_t validCount = selectByValueCond(_executive, tableName, tableKeyList, entries,
                    std::move(_valueCondition), _isNumericalOrder);
                return std::pair<size_t, size_t>{validCount, entries.size() - entrySize};
            };
            processEntryByValueCond(
                _executive, tableName, *valueCondition->at(0), valueCondition, std::move(func));
        }
        else
        {
            // merge keys from storage and eqKeys
            // keyCondition must exist
            auto tableKeyList =
                _executive->storage().getPrimaryKeys(tableName, *valueCondition->at(0));
            entries.reserve(tableKeyList.size());
            for (auto& key : tableKeyList)
            {
                auto tableEntry = _executive->storage().getRow(tableName, key);
                EntryTuple entryTuple;
                if (_isNumericalOrder)
                {
                    entryTuple = {toLexicographicOrder(key),
                        tableEntry->getObject<std::vector<std::string>>()};
                }
                else
                {
                    entryTuple = {key, tableEntry->getObject<std::vector<std::string>>()};
                }
                entries.emplace_back(std::move(entryTuple));
            }
        }
    }
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("SELECT")
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

    uint32_t totalCount = 0;
    uint32_t singleCount = 0;
    do
    {
        auto keyCondition = std::make_shared<storage::Condition>();
        if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_1_VERSION) >= 0)
        {
            // will throw exception when wrong condition cmp or limit count overflow
            buildKeyCondition(
                keyCondition, conditions, {0 + totalCount, USER_TABLE_MAX_LIMIT_COUNT});
        }
        else if (versionCompareTo(blockContext->blockVersion(), BlockVersion::V3_0_VERSION) <= 0)
        {
            /// NOTE: if version <= 3.0, here will use empty limit, which means count always return
            /// 0
            buildKeyCondition(keyCondition, conditions, {});
        }

        singleCount = _executive->storage().getPrimaryKeys(tableName, *keyCondition).size();
        if (totalCount > totalCount + singleCount)
        {
            // overflow
            totalCount = UINT32_MAX;
            break;
        }
        totalCount += singleCount;
    } while (singleCount >= USER_TABLE_MAX_LIMIT_COUNT);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("COUNT")
                           << LOG_KV("totalCount", totalCount);
    // update the memory gas and the computation gas
    gasPricer->appendOperation(InterfaceOpcode::Select);
    _callParameters->setExecResult(codec.encode(uint32_t(totalCount)));
}

void TablePrecompiled::countV32(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// count((uint8,string,string)[])
    std::vector<precompiled::ConditionTupleV320> conditions;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());

    codec.decode(data, conditions);
    precompiled::TableInfo tableInfo;
    // external call table manager desc
    desc(tableInfo, tableName, _executive, _callParameters, true);

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("COUNT")
                           << LOG_KV("tableName", tableName)
                           << LOG_KV("ConditionSize", conditions.size());

    auto valueCondition = std::make_optional<precompiled::Condition>();
    bool useValueCond = buildConditions(
        valueCondition, conditions, {0, USER_TABLE_MAX_LIMIT_COUNT}, tableInfo.info_v320);
    auto keyCondition = valueCondition->at(0);
    uint32_t totalCount = 0;
    uint32_t singleCount = 0;
    uint32_t singleCountByKey = 0;
    uint32_t singleCountByKeyMax = valueCondition->getKeyLimit().second;
    do
    {
        auto tableKeyList = _executive->storage().getPrimaryKeys(tableName, *keyCondition);
        singleCountByKey = tableKeyList.size();
        singleCount = singleCountByKey;
        auto [keyLimitOffset, keyLimitCount] = keyCondition->m_limit;
        keyCondition->limit(keyLimitOffset + singleCountByKey, keyLimitCount);
        if (useValueCond)
        {
            std::vector<EntryTuple> entries({});
            entries.reserve(tableKeyList.size());
            selectByValueCond(_executive, tableName, tableKeyList, entries, valueCondition);
            singleCount = entries.size();
        }
        if (totalCount > totalCount + singleCount)
        {
            // overflow
            totalCount = UINT32_MAX;
            break;
        }
        totalCount += singleCount;
    } while (singleCountByKey >= singleCountByKeyMax);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("COUNT")
                           << LOG_KV("totalCount", totalCount);
    // update the memory gas and the computation gas
    gasPricer->appendOperation(InterfaceOpcode::Select);
    _callParameters->setExecResult(codec.encode(uint32_t(totalCount)));
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

    precompiled::TableInfo tableInfo;
    // external call table manager desc
    std::vector<std::string> columns;
    if (blockContext->blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_2_VERSION)
    {
        desc(tableInfo, tableName, _executive, _callParameters, true);
        if (isNumericalOrder(tableInfo.info_v320))
        {
            key = toNumericalOrder(key);
        }
        columns = std::get<2>(tableInfo.info_v320);
    }
    else
    {
        desc(tableInfo, tableName, _executive, _callParameters, false);
        columns = std::get<1>(tableInfo.info);
    }

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("INSERT")
                           << LOG_KV("tableName", tableName) << LOG_KV("key", key)
                           << LOG_KV("valueSize", values.size());

    if (values.size() != columns.size())
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("INSERT")
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
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("INSERT")
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

    precompiled::TableInfo tableInfo;
    // external call table manager desc
    std::string keyField;
    std::vector<std::string> columns;
    if (blockContext->blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_2_VERSION)
    {
        desc(tableInfo, tableName, _executive, _callParameters, true);
        if (isNumericalOrder(tableInfo.info_v320))
        {
            key = toNumericalOrder(key);
        }
        keyField = std::get<1>(tableInfo.info_v320);
        columns = std::get<2>(tableInfo.info_v320);
    }
    else
    {
        desc(tableInfo, tableName, _executive, _callParameters, false);
        keyField = std::get<0>(tableInfo.info);
        columns = std::get<1>(tableInfo.info);
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                           << LOG_KV("tableName", tableName) << LOG_KV("updateKey", key)
                           << LOG_KV("updateFieldsSize", updateFields.size());
    auto existEntry = _executive->storage().getRow(tableName, key);
    if (!existEntry)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                               << LOG_DESC("key not exist in table, please use INSERT method")
                               << LOG_KV("notExistKey", key);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_UPDATE_KEY_NOT_EXIST)));
        return;
    }

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
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                << LOG_DESC("Table cannot update keyField") << LOG_KV("keyField", keyField);
            BOOST_THROW_EXCEPTION(
                PrecompiledError("Table update fields cannot contains key field"));
        }
        if (it == columns.end())
        {
            PRECOMPILED_LOG(DEBUG)
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
    auto keyCondition = std::make_shared<storage::Condition>();

    // will throw exception when wrong condition cmp or limit count overflow
    buildKeyCondition(keyCondition, std::move(conditions), std::move(limitTuple));

    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                               << LOG_DESC("keyCond trace ") << keyCondition->toString();
    }

    auto tableKeyList = _executive->storage().getPrimaryKeys(tableName, *keyCondition);

    precompiled::TableInfo tableInfo;
    // external call table manager desc
    desc(tableInfo, tableName, _executive, _callParameters, false);
    auto keyField = std::get<0>(tableInfo.info);
    auto columns = std::get<1>(tableInfo.info);

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
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                << LOG_DESC("Table cannot update keyField") << LOG_KV("keyField", keyField);
            BOOST_THROW_EXCEPTION(
                PrecompiledError("Table update fields cannot contains key field"));
        }
        if (it == columns.end())
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                << LOG_DESC("Table update field not found") << LOG_KV("field", field);
            BOOST_THROW_EXCEPTION(PrecompiledError("Table update fields not found"));
        }
        std::pair<uint32_t, std::string> p = {std::distance(columns.begin(), it), std::move(value)};
        updateValue.emplace_back(std::move(p));
    }

    auto entries = _executive->storage().getRows(tableName, tableKeyList);
    for (size_t i = 0; i < entries.size(); ++i)
    {
        auto&& entry = entries[i];
        auto values = entry->getObject<std::vector<std::string>>();
        for (auto& kv : updateValue)
        {
            values[kv.first] = kv.second;
        }
        entry->setObject(std::move(values));
        _executive->storage().setRow(tableName, tableKeyList[i], std::move(entry.value()));
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                           << LOG_KV("selectKeySize", tableKeyList.size())
                           << LOG_KV("affectedRows", entries.size());
    gasPricer->setMemUsed(tableKeyList.size() * columns.size());
    gasPricer->appendOperation(InterfaceOpcode::Update, tableKeyList.size());
    _callParameters->setExecResult(codec.encode((int32_t)tableKeyList.size()));
}

void TablePrecompiled::updateByConditionV32(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// update((uint8,string,string)[],(uint32,uint32),(uint,string)[])
    std::vector<precompiled::ConditionTupleV320> conditions;
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

    precompiled::TableInfo tableInfo;
    // external call table manager desc
    desc(tableInfo, tableName, _executive, _callParameters, true);
    std::string keyField = std::get<1>(tableInfo.info_v320);
    std::vector<std::string> columns = std::get<2>(tableInfo.info_v320);

    auto valueCondition = std::make_optional<precompiled::Condition>();
    // will throw exception when wrong condition cmp or limit count overflow
    bool useValueCond =
        buildConditions(valueCondition, std::move(conditions), limitTuple, tableInfo.info_v320);
    auto keyCondition = valueCondition->at(0);

    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                               << LOG_DESC("keyCond trace ") << keyCondition->toString();
    }

    std::vector<std::pair<uint32_t, std::string>> updateValue;
    updateValue.reserve(updateFields.size());
    for (const auto& kv : updateFields)
    {
        auto& field = std::get<0>(kv);
        auto& value = std::get<1>(kv);
        checkLengthValidate(
            value, USER_TABLE_FIELD_VALUE_MAX_LENGTH, CODE_TABLE_FIELD_VALUE_LENGTH_OVERFLOW);
        if (field == keyField)
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                << LOG_DESC("Table cannot update keyField") << LOG_KV("keyField", keyField);
            BOOST_THROW_EXCEPTION(
                PrecompiledError("Table update fields cannot contains key field"));
        }
        auto const it = std::find(columns.begin(), columns.end(), field);
        if (it == columns.end())
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                << LOG_DESC("Table update field not found") << LOG_KV("field", field);
            BOOST_THROW_EXCEPTION(PrecompiledError("Table update fields not found"));
        }
        std::pair<uint32_t, std::string> p = {std::distance(columns.begin(), it), std::move(value)};
        updateValue.emplace_back(std::move(p));
    }

    uint32_t affectedRows = 0;
    // when limitcount==0, skip update operation directly
    if (std::get<1>(limitTuple) > 0)
    {
        if (useValueCond)
        {
            auto func = [_executive, &tableName, &updateValue, &affectedRows](
                            const std::vector<std::string>& tableKeyList,
                            std::optional<precompiled::Condition> _valueCondition) {
                std::vector<EntryTuple> entries({});
                size_t validCount = selectByValueCond(
                    _executive, tableName, tableKeyList, entries, _valueCondition);
                for (auto& entryTuple : entries)
                {
                    auto& values = std::get<1>(entryTuple);
                    for (auto& kv : updateValue)
                    {
                        values[kv.first] = kv.second;
                    }
                    storage::Entry entry;
                    entry.setObject(values);
                    _executive->storage().setRow(
                        tableName, std::get<0>(entryTuple), std::move(entry));
                }
                affectedRows += entries.size();
                return std::pair<size_t, size_t>{validCount, entries.size()};
            };
            processEntryByValueCond(
                _executive, tableName, *keyCondition, valueCondition, std::move(func));
        }
        else
        {
            auto tableKeyList = _executive->storage().getPrimaryKeys(tableName, *keyCondition);
            auto entries = _executive->storage().getRows(tableName, tableKeyList);
            for (size_t i = 0; i < entries.size(); ++i)
            {
                auto&& entry = entries[i];
                auto values = entry->getObject<std::vector<std::string>>();
                for (auto& kv : updateValue)
                {
                    values[kv.first] = kv.second;
                }
                entry->setObject(std::move(values));
                _executive->storage().setRow(tableName, tableKeyList[i], std::move(entry.value()));
            }
            affectedRows = tableKeyList.size();
        }
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("UPDATE")
                           << LOG_KV("selectSize", affectedRows)
                           << LOG_KV("affectedRows", affectedRows);
    gasPricer->setMemUsed(affectedRows * columns.size());
    gasPricer->appendOperation(InterfaceOpcode::Update, affectedRows);
    _callParameters->setExecResult(codec.encode((int32_t)affectedRows));
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

    if (blockContext->blockVersion() >= (uint32_t)bcos::protocol::BlockVersion::V3_2_VERSION &&
        isNumericalOrder(_executive, _callParameters, tableName))
    {
        key = toNumericalOrder(key);
    }

    auto existEntry = _executive->storage().getRow(tableName, key);
    if (!existEntry)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("REMOVE")
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

    auto keyCondition = std::make_shared<storage::Condition>();

    // will throw exception when wrong condition cmp or limit count overflow
    buildKeyCondition(keyCondition, std::move(conditions), std::move(limitTuple));

    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("REMOVE")
                               << LOG_DESC("keyCond trace ") << keyCondition->toString();
    }

    auto tableKeyList = _executive->storage().getPrimaryKeys(tableName, *keyCondition);

    for (auto& tableKey : tableKeyList)
    {
        Entry deletedEntry;
        deletedEntry.setStatus(Entry::DELETED);
        _executive->storage().setRow(tableName, tableKey, std::move(deletedEntry));
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("REMOVE")
                           << LOG_KV("affectedRows", tableKeyList.size());
    gasPricer->setMemUsed(tableKeyList.size());
    gasPricer->appendOperation(InterfaceOpcode::Remove, tableKeyList.size());
    _callParameters->setExecResult(codec.encode((int32_t)tableKeyList.size()));
}

void TablePrecompiled::removeByConditionV32(const std::string& tableName,
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    /// remove((uint8,string,string)[],(uint32,uint32))
    std::vector<precompiled::ConditionTupleV320> conditions;
    precompiled::LimitTuple limitTuple;
    auto blockContext = _executive->blockContext().lock();

    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(data, conditions, limitTuple);
    precompiled::TableInfo tableInfo;
    // external call table manager desc
    desc(tableInfo, tableName, _executive, _callParameters, true);

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("REMOVE")
                           << LOG_KV("tableName", tableName)
                           << LOG_KV("ConditionSize", conditions.size())
                           << LOG_KV("limitOffset", std::get<0>(limitTuple))
                           << LOG_KV("limitCount", std::get<1>(limitTuple));

    auto valueCondition = std::make_optional<precompiled::Condition>();
    // will throw exception when wrong condition cmp or limit count overflow
    bool useValueCond =
        buildConditions(valueCondition, std::move(conditions), limitTuple, tableInfo.info_v320);
    auto keyCondition = valueCondition->at(0);

    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("REMOVE")
                               << LOG_DESC("keyCond trace ") << keyCondition->toString();
    }

    uint32_t removedRows = 0;
    // when limitcount==0, skip remove operation directly
    if (std::get<1>(limitTuple) > 0)
    {
        if (useValueCond)
        {
            auto func = [_executive, &tableName, &removedRows](
                            const std::vector<std::string>& tableKeyList,
                            std::optional<precompiled::Condition> _valueCondition) {
                std::vector<EntryTuple> entries({});
                size_t validCount = selectByValueCond(
                    _executive, tableName, tableKeyList, entries, _valueCondition);
                for (auto& entry : entries)
                {
                    storage::Entry deletedEntry;
                    deletedEntry.setStatus(Entry::DELETED);
                    _executive->storage().setRow(
                        tableName, std::get<0>(entry), std::move(deletedEntry));
                }
                removedRows += entries.size();
                return std::pair<size_t, size_t>{validCount, entries.size()};
            };
            processEntryByValueCond(
                _executive, tableName, *keyCondition, valueCondition, std::move(func));
        }
        else
        {
            auto tableKeyList = _executive->storage().getPrimaryKeys(tableName, *keyCondition);
            for (auto& tableKey : tableKeyList)
            {
                storage::Entry deletedEntry;
                deletedEntry.setStatus(Entry::DELETED);
                _executive->storage().setRow(tableName, tableKey, std::move(deletedEntry));
            }
            removedRows = tableKeyList.size();
        }
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("TablePrecompiled") << LOG_BADGE("REMOVE")
                           << LOG_KV("removedRows", removedRows);
    gasPricer->setMemUsed(removedRows);
    gasPricer->appendOperation(InterfaceOpcode::Remove, removedRows);
    _callParameters->setExecResult(codec.encode((int32_t)removedRows));
}