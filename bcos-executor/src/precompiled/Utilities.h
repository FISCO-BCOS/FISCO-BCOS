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
 * @file Utilities.h
 * @author: kyonRay
 * @date 2021-05-25
 */

#pragma once

#include "../executive/TransactionExecutive.h"
#include "Common.h"
#include "PrecompiledCodec.h"
#include <bcos-codec/abi/ContractABICodec.h>
#include <bcos-framework/interfaces/storage/Table.h>
#include <bcos-utilities/Common.h>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>

namespace bcos
{
namespace precompiled
{
using ConditionTuple = std::tuple<std::vector<std::tuple<std::string, std::string, u256>>>;
using EntryTuple = std::tuple<std::vector<std::tuple<std::string, std::string>>>;
using BfsTuple = std::tuple<std::string, std::string, std::vector<std::string>>;

static const std::string USER_TABLE_PREFIX = "/tables/";
static const std::string USER_APPS_PREFIX = "/apps/";
static const std::string USER_SYS_PREFIX = "/sys/";


inline void getErrorCodeOut(bytes& out, int const& result, const PrecompiledCodec& _codec)
{
    if (result >= 0 && result < 128)
    {
        out = _codec.encode(u256(result));
        return;
    }
    out = _codec.encode(s256(result));
}
inline std::string getTableName(const std::string& _tableName)
{
    auto tableName = (_tableName[0] == '/') ? _tableName.substr(1) : _tableName;
    return USER_TABLE_PREFIX + tableName;
}

inline std::string trimHexPrefix(const std::string& _hex)
{
    if (_hex.size() >= 2 && _hex[1] == 'x' && _hex[0] == '0')
    {
        return _hex.substr(2);
    }
    return _hex;
}

void checkNameValidate(std::string_view tableName, std::vector<std::string>& keyFieldList,
    std::vector<std::string>& valueFieldList);
int checkLengthValidate(std::string_view field_value, int32_t max_length, int32_t errorCode);

void checkCreateTableParam(
    const std::string& _tableName, std::string& _keyField, std::string& _valueField);

uint32_t getFuncSelector(std::string const& _functionName, const crypto::Hash::Ptr& _hashImpl);
uint32_t getParamFunc(bytesConstRef _param);
uint32_t getFuncSelectorByFunctionName(
    std::string const& _functionName, const crypto::Hash::Ptr& _hashImpl);

bcos::precompiled::ContractStatus getContractStatus(
    std::shared_ptr<bcos::executor::TransactionExecutive> _executive,
    std::string const& _tableName);

bytesConstRef getParamData(bytesConstRef _param);

uint64_t getEntriesCapacity(precompiled::EntriesPtr _entries);

void sortKeyValue(std::vector<std::string>& _v);

bool checkPathValid(std::string const& _absolutePath);

std::pair<std::string, std::string> getParentDirAndBaseName(const std::string& _absolutePath);

std::pair<std::string, std::string> getLinkNameAndVersion(const std::string& _absolutePath);

bool recursiveBuildDir(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const std::string& _absoluteDir);
}  // namespace precompiled
}  // namespace bcos