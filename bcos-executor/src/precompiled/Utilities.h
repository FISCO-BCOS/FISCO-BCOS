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
#include "bcos-codec/wrapper/CodecWrapper.h"
#include <bcos-codec/abi/ContractABICodec.h>
#include <bcos-framework/interfaces/executor/PrecompiledTypeDef.h>
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

inline void getErrorCodeOut(bytes& out, int const& result, const CodecWrapper& _codec)
{
    if (result >= 0 && result < 128)
    {
        out = _codec.encode(u256(result));
        return;
    }
    out = _codec.encode(s256(result));
}

inline std::string getTableName(const std::string_view& _tableName)
{
    auto tableName = (_tableName[0] == '/') ? _tableName.substr(1) : _tableName;
    return executor::USER_TABLE_PREFIX + std::string(tableName);
}

inline std::string getActualTableName(const std::string& _tableName)
{
    return "u_" + _tableName;
}

inline std::string getDynamicPrecompiledCodeString(
    const std::string& _address, const std::string& _params)
{
    /// Prefix , address , params
    return boost::join(std::vector<std::string>({PRECOMPILED_CODE_FIELD, _address, _params}), ",");
}

inline std::string trimHexPrefix(const std::string& _hex)
{
    if (_hex.size() >= 2 && _hex[1] == 'x' && _hex[0] == '0')
    {
        return _hex.substr(2);
    }
    return _hex;
}

void checkNameValidate(std::string_view tableName, std::string_view _keyField,
    std::vector<std::string>& valueFieldList);
void checkLengthValidate(std::string_view field_value, int32_t max_length, int32_t errorCode);

void checkCreateTableParam(const std::string_view& _tableName, std::string& _keyField,
    const std::variant<std::string, std::vector<std::string>>& _valueField);

uint32_t getFuncSelector(std::string const& _functionName, const crypto::Hash::Ptr& _hashImpl);
uint32_t getParamFunc(bytesConstRef _param);
uint32_t getFuncSelectorByFunctionName(
    std::string const& _functionName, const crypto::Hash::Ptr& _hashImpl);

bcos::precompiled::ContractStatus getContractStatus(
    std::shared_ptr<bcos::executor::TransactionExecutive> _executive,
    std::string const& _tableName);

inline bytesConstRef getParamData(bytesConstRef _param)
{
    return _param.getCroppedData(4);
}


bool checkPathValid(std::string const& _absolutePath);

std::pair<std::string, std::string> getParentDirAndBaseName(const std::string& _absolutePath);

inline bool checkSenderFromAuth(std::string_view _sender)
{
    return _sender == precompiled::AUTH_COMMITTEE_ADDRESS;
}

executor::CallParameters::UniquePtr externalRequest(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const bytesConstRef& _param,
    std::string_view _origin, std::string_view _sender, std::string_view _to, bool _isStatic,
    bool _isCreate, int64_t gasLeft, bool _isInternalCall = false);

s256 externalTouchNewFile(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    std::string_view _origin, std::string_view _sender, std::string_view _filePath,
    std::string_view _fileType, int64_t gasLeft);
}  // namespace precompiled
}  // namespace bcos