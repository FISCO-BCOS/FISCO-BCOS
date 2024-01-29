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
 * @file Utilities.cpp
 * @author: kyonRay
 * @date 2021-05-25
 */

#include "Utilities.h"
#include "Common.h"
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <tbb/concurrent_unordered_map.h>
#include <boost/core/ignore_unused.hpp>
#include <regex>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::precompiled;
using namespace bcos::protocol;
using namespace bcos::crypto;

static tbb::concurrent_unordered_map<std::string, uint32_t> s_name2SelectCache;

void bcos::precompiled::checkNameValidate(std::string_view tableName, std::string_view _keyField,
    std::vector<std::string>& valueFieldList)
{
    std::set<std::string> valueFieldSet;
    std::vector<char> allowChar = {'$', '_', '@'};
    std::vector<char> tableAllowChar = {'_', '/'};
    std::string allowCharString = "{$, _, @}";
    std::string tableAllowCharString = "{_, /}";
    auto checkTableNameValidate = [&tableAllowChar, &tableAllowCharString](
                                      std::string_view tableName) {
        size_t iSize = tableName.size();
        for (size_t i = 0; i < iSize; i++)
        {
            if (!isalnum(tableName[i]) &&
                (tableAllowChar.end() ==
                    find(tableAllowChar.begin(), tableAllowChar.end(), tableName[i])))
            {
                std::stringstream errorMsg;
                errorMsg << "Invalid table name \"" << tableName
                         << "\", the table name must be letters or numbers, and "
                            "only supports \""
                         << tableAllowCharString << "\" as special character set";
                PRECOMPILED_LOG(DEBUG)
                    << LOG_BADGE("checkNameValidate") << LOG_DESC(errorMsg.str());
                // Note: the StorageException and PrecompiledException content can't
                // be modified at will for the information will be written to the
                // blockchain
                BOOST_THROW_EXCEPTION(PrecompiledError(errorMsg.str()));
            }
        }
    };

    auto checkFieldNameValidate = [&allowChar, &allowCharString](
                                      std::string_view tableName, std::string_view fieldName) {
        if (fieldName.empty() || fieldName[0] == '_')
        {
            std::stringstream errorMessage;
            errorMessage << "Invalid field \"" << fieldName
                         << "\", the size of the field must be larger than 0 and "
                            "the field can't start with \"_\"";
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("checkNameValidate") << LOG_DESC(errorMessage.str())
                << LOG_KV("field name", fieldName) << LOG_KV("table name", tableName);
            BOOST_THROW_EXCEPTION(PrecompiledError("invalid field: " + std::string(fieldName)));
        }
        size_t iSize = fieldName.size();
        for (size_t i = 0; i < iSize; i++)
        {
            if (!isalnum(fieldName[i]) &&
                (allowChar.end() == find(allowChar.begin(), allowChar.end(), fieldName[i])))
            {
                std::stringstream errorMessage;
                errorMessage
                    << "Invalid field \"" << fieldName
                    << "\", the field name must be letters or numbers, and only supports \""
                    << allowCharString << "\" as special character set";

                PRECOMPILED_LOG(DEBUG)
                    << LOG_BADGE("checkNameValidate") << LOG_DESC(errorMessage.str())
                    << LOG_KV("field name", fieldName) << LOG_KV("table name", tableName);
                BOOST_THROW_EXCEPTION(PrecompiledError("invalid field: " + std::string(fieldName)));
            }
        }
    };

    checkTableNameValidate(tableName);

    checkFieldNameValidate(tableName, _keyField);

    for (auto& valueField : valueFieldList)
    {
        auto ret = valueFieldSet.insert(valueField);
        if (!ret.second) [[unlikely]]
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("checkNameValidate") << LOG_DESC("duplicated field")
                << LOG_KV("field name", valueField) << LOG_KV("table name", tableName);
            BOOST_THROW_EXCEPTION(PrecompiledError("duplicated field: " + valueField));
        }
        checkFieldNameValidate(tableName, valueField);
    }
}

void bcos::precompiled::checkLengthValidate(
    std::string_view fieldValue, int32_t maxLength, int32_t errorCode)
{
    if (fieldValue.size() > (size_t)maxLength)
    {
        PRECOMPILED_LOG(DEBUG) << "key:" << fieldValue << " value size:" << fieldValue.size()
                               << " greater than " << maxLength;
        BOOST_THROW_EXCEPTION(
            PrecompiledError("size of value/key greater than" + std::to_string(maxLength) +
                             " error code: " + std::to_string(errorCode)));
    }
}

std::string bcos::precompiled::checkCreateTableParam(const std::string_view& _tableName,
    std::string& _keyField, const std::variant<std::string, std::vector<std::string>>& _valueField,
    std::optional<uint8_t> keyOrder)
{
    if (keyOrder && (*keyOrder != 0 && *keyOrder != 1))
    {
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError(std::to_string(int(*keyOrder)) + " KeyOrder not exist!"));
    }
    std::vector<std::string> fieldNameList;
    if (_valueField.index() == 1)
    {
        fieldNameList.assign(std::get<1>(_valueField).begin(), std::get<1>(_valueField).end());
    }
    else
    {
        boost::split(fieldNameList, std::get<0>(_valueField), boost::is_any_of(","));
    }

    if (_keyField.size() > (size_t)SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)
    {  // mysql TableName and fieldName length limit is 64
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError("table field name length overflow " +
                                       std::to_string(SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)));
    }
    boost::trim(_keyField);
    if (_keyField.size() > (size_t)SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)
    {  // mysql TableName and fieldName length limit is 64
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError(
            "errorCode: " + std::to_string(CODE_TABLE_FIELD_LENGTH_OVERFLOW) +
            std::string("table key name length overflow ") +
            std::to_string(SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)));
    }

    for (auto& str : fieldNameList)
    {
        boost::trim(str);
        if (str.size() > (size_t)SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)
        {  // mysql TableName and fieldName length limit is 64
            BOOST_THROW_EXCEPTION(protocol::PrecompiledError(
                "errorCode: " + std::to_string(CODE_TABLE_FIELD_LENGTH_OVERFLOW) +
                std::string("table field name length overflow ") +
                std::to_string(SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)));
        }
    }

    checkNameValidate(_tableName, _keyField, fieldNameList);

    auto valueField = boost::join(fieldNameList, ",");
    if (valueField.size() > (size_t)SYS_TABLE_VALUE_FIELD_NAME_MAX_LENGTH)
    {
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError(std::string("total table field name length overflow ") +
                                       std::to_string(SYS_TABLE_VALUE_FIELD_NAME_MAX_LENGTH)));
    }

    auto tableName = precompiled::getTableName(_tableName);
    if (tableName.size() > (size_t)USER_TABLE_NAME_MAX_LENGTH_S)
    {
        // mysql TableName and fieldName length limit is 64
        BOOST_THROW_EXCEPTION(protocol::PrecompiledError(
            "errorCode: " + std::to_string(CODE_TABLE_NAME_LENGTH_OVERFLOW) +
            std::string(" tableName length overflow ") +
            std::to_string(USER_TABLE_NAME_MAX_LENGTH_S)));
    }
    return valueField;
}

uint32_t bcos::precompiled::getFuncSelector(
    std::string_view functionName, const crypto::Hash::Ptr& _hashImpl)
{
    // global function selector cache
    auto it = s_name2SelectCache.find(std::string(functionName));
    if (it != s_name2SelectCache.end())
    {
        return it->second;
    }
    auto selector = getFuncSelectorByFunctionName(functionName, _hashImpl);
    s_name2SelectCache.insert(std::make_pair(functionName, selector));
    return selector;
}

// for ut
void bcos::precompiled::clearName2SelectCache()
{
    s_name2SelectCache.clear();
}

uint32_t bcos::precompiled::getParamFunc(bytesConstRef _param)
{
    if (_param.size() < 4) [[unlikely]]
    {
        PRECOMPILED_LOG(INFO) << LOG_DESC(
                                     "getParamFunc param too short, not enough to call precompiled")
                              << LOG_KV("param", toHexStringWithPrefix(_param.toBytes()));
        BOOST_THROW_EXCEPTION(PrecompiledError("Empty param data in precompiled call"));
    }
    auto funcBytes = _param.getCroppedData(0, 4);
    uint32_t func = *((uint32_t*)(funcBytes.data()));

    return ((func & 0x000000FF) << 24) | ((func & 0x0000FF00) << 8) | ((func & 0x00FF0000) >> 8) |
           ((func & 0xFF000000) >> 24);
}

uint32_t bcos::precompiled::getFuncSelectorByFunctionName(
    std::string_view _functionName, const crypto::Hash::Ptr& _hashImpl)
{
    uint32_t func = *(uint32_t*)(_hashImpl->hash(_functionName).ref().getCroppedData(0, 4).data());
    uint32_t selector = ((func & 0x000000FF) << 24) | ((func & 0x0000FF00) << 8) |
                        ((func & 0x00FF0000) >> 8) | ((func & 0xFF000000) >> 24);
    return selector;
}

bcos::precompiled::ContractStatus bcos::precompiled::getContractStatus(
    std::shared_ptr<bcos::executor::TransactionExecutive> _executive, const std::string& _tableName)
{
    auto table = _executive->storage().openTable(_tableName);
    if (!table)
    {
        return ContractStatus::AddressNonExistent;
    }

    auto codeHashEntry = table->getRow(executor::ACCOUNT_CODE_HASH);
    if (!codeHashEntry)
    {
        // this may happen when register link in contract constructor
        return ContractStatus::Available;
    }
    auto codeHashStr = codeHashEntry->getField(0);
    auto codeHash = HashType(codeHashStr, FixedBytes<32>::FromBinary);

    if (codeHash == HashType())
    {
        return ContractStatus::NotContractAddress;
    }
    return ContractStatus::Available;
}

bool precompiled::checkPathValid(
    std::string_view _path, std::variant<uint32_t, protocol::BlockVersion> version)
{
    if (_path.empty()) [[unlikely]]
        return false;

    if (versionCompareTo(version, BlockVersion::V3_3_VERSION) >= 0) [[unlikely]]
    {
        if (_path.length() > FS_PATH_MAX_LENGTH_330)
        {
            PRECOMPILED_LOG(DEBUG) << LOG_BADGE("checkPathValid")
                                   << LOG_DESC("path too long, over flow FS_PATH_MAX_LENGTH_330")
                                   << LOG_KV("limit", FS_PATH_MAX_LENGTH_330)
                                   << LOG_KV("len", _path.length()) << LOG_KV("path", _path);
            return false;
        }
    }
    else if (_path.length() > FS_PATH_MAX_LENGTH) [[unlikely]]
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("checkPathValid")
                               << LOG_DESC("path too long, over flow FS_PATH_MAX_LENGTH")
                               << LOG_KV("limit", FS_PATH_MAX_LENGTH)
                               << LOG_KV("len", _path.length()) << LOG_KV("path", _path);
        return false;
    }
    if (_path == "/")
        return true;
    std::string_view absoluteDir = _path;
    if (absoluteDir.starts_with('/'))
    {
        absoluteDir.remove_prefix(1);
    }
    if (absoluteDir.ends_with('/'))
    {
        absoluteDir.remove_suffix(1);
    }
    std::vector<std::string> pathList;
    //    constexpr std::string_view delim{"/"};
    //    auto spliter = RANGES::split_view(absoluteDir, delim);
    boost::split(pathList, absoluteDir, boost::is_any_of("/"), boost::token_compress_on);
    if (pathList.size() > FS_PATH_MAX_LEVEL || pathList.empty())
    {
        PRECOMPILED_LOG(DEBUG)
            << LOG_BADGE("checkPathValid")
            << LOG_DESC("resource path's level is too deep, level over FS_PATH_MAX_LEVEL")
            << LOG_KV("path", _path);
        return false;
    }
    std::regex reg(R"(^[0-9a-zA-Z][^\>\<\*\?\/\=\+\(\)\$\"\']*$)");
    if (versionCompareTo(version, BlockVersion::V3_2_VERSION) >= 0)
    {
        reg = (R"(^[\w]+[\w\-#@.]*$)");
    }
    auto checkFieldNameValidate = [&reg](std::string fieldName) -> bool {
        if (fieldName.empty()) [[unlikely]]
        {
            std::stringstream errorMessage;
            errorMessage << "Invalid field \"" + fieldName
                         << "\", the size of the field must be larger than 0";
            PRECOMPILED_LOG(DEBUG)
                << LOG_DESC(errorMessage.str()) << LOG_KV("field name", fieldName);
            return false;
        }
        if (!std::regex_match(fieldName, reg))
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_DESC("Invalid path field " + fieldName) << LOG_KV("field name", fieldName);
            return false;
        }
        return true;
    };
    return std::all_of(pathList.begin(), pathList.end(),
        [checkFieldNameValidate](const std::string& s) { return checkFieldNameValidate(s); });
}

std::pair<std::string, std::string> precompiled::getParentDirAndBaseName(
    const std::string& _absolutePath)
{
    // transfer /usr/local/bin => ["usr", "local", "bin"]
    if (_absolutePath == "/")
        return {"/", "/"};

    std::string absoluteDir = _absolutePath;

    if (absoluteDir.ends_with('/'))
    {
        absoluteDir.pop_back();
    }
    size_t index = absoluteDir.find_last_of('/');
    auto parent = absoluteDir.substr(0, index);
    return {parent.empty() ? "/" : parent, absoluteDir.substr(index + 1)};
}

executor::CallParameters::UniquePtr precompiled::externalRequest(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, const bytesConstRef& _param,
    std::string_view _origin, std::string_view _sender, std::string_view _to, bool _isStatic,
    bool _isCreate, int64_t gasLeft, bool _isInternal, std::string _abi)
{
    auto request = std::make_unique<executor::CallParameters>(executor::CallParameters::MESSAGE);

    request->senderAddress = _sender;
    request->receiveAddress = _to;
    request->origin = _origin;
    request->codeAddress = request->receiveAddress;
    request->status = 0;
    request->data = _param.toBytes();
    request->create = _isCreate;
    request->staticCall = !_isCreate && _isStatic;
    request->internalCreate = _isCreate && _isInternal;
    request->internalCall = !_isCreate && _isInternal;
    request->gas = gasLeft;
    if (_isCreate && !_abi.empty())
    {
        request->abi = _abi;
    }
    return _executive->externalCall(std::move(request));
}

s256 precompiled::externalTouchNewFile(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, std::string_view _origin,
    std::string_view _sender, std::string_view _to, std::string_view _filePath,
    std::string_view _fileType, int64_t gasLeft)
{
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    auto codecResult =
        codec.encodeWithSig("touch(string,string)", std::string(_filePath), std::string(_fileType));
    auto response =
        externalRequest(_executive, ref(codecResult), _origin, _sender, _to, false, false, gasLeft);
    int32_t result = 0;
    if (response->status == (int32_t)TransactionStatus::None)
    {
        codec.decode(ref(response->data), result);
    }
    else
    {
        result = (int)CODE_FILE_BUILD_DIR_FAILED;
    }
    return result;
}


std::vector<Address> precompiled::getGovernorList(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters, const CodecWrapper& codec)
{
    const auto& blockContext = _executive->blockContext();
    const auto& sender = _executive->contractAddress();
    auto getCommittee = codec.encodeWithSig("_committee()");
    auto getCommitteeResponse = externalRequest(_executive, ref(getCommittee),
        _callParameters->m_origin, sender, AUTH_COMMITTEE_ADDRESS, _callParameters->m_staticCall,
        false, _callParameters->m_gasLeft);
    if (getCommitteeResponse->status != (int32_t)TransactionStatus::None) [[unlikely]]
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("Precompiled") << LOG_DESC("get committee failed")
                               << LOG_KV("status", getCommitteeResponse->status);
        BOOST_THROW_EXCEPTION(PrecompiledError("Get committee failed."));
    }

    Address committee;
    codec.decode(ref(getCommitteeResponse->data), committee);

    auto getInfo = codec.encodeWithSig("getCommitteeInfo()");
    auto getInfoResponse = externalRequest(_executive, ref(getInfo), _callParameters->m_origin,
        sender, committee.hex(), _callParameters->m_staticCall, false, _callParameters->m_gasLeft);

    if (getInfoResponse->status != (int32_t)TransactionStatus::None) [[unlikely]]
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("Precompiled") << LOG_DESC("get committee info failed")
                               << LOG_KV("committee", committee.hex());
        BOOST_THROW_EXCEPTION(PrecompiledError("Get committee info failed."));
    }
    uint8_t participatesRate = 0;
    uint8_t winRate = 0;
    std::vector<Address> governors;
    std::vector<uint32_t> weights;
    codec.decode(ref(getInfoResponse->data), participatesRate, winRate, governors, weights);
    return governors;
}