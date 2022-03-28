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

void bcos::precompiled::checkNameValidate(std::string_view tableName,
    std::vector<std::string>& keyFieldList, std::vector<std::string>& valueFieldList)
{
    std::set<std::string> valueFieldSet;
    std::set<std::string> keyFieldSet;
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
                STORAGE_LOG(ERROR) << LOG_DESC(errorMsg.str());
                // Note: the StorageException and PrecompiledException content can't
                // be modified at will for the information will be write to the
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
            STORAGE_LOG(ERROR) << LOG_DESC(errorMessage.str()) << LOG_KV("field name", fieldName)
                               << LOG_KV("table name", tableName);
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

                STORAGE_LOG(ERROR)
                    << LOG_DESC(errorMessage.str()) << LOG_KV("field name", fieldName)
                    << LOG_KV("table name", tableName);
                BOOST_THROW_EXCEPTION(PrecompiledError("invalid field: " + std::string(fieldName)));
            }
        }
    };

    checkTableNameValidate(tableName);

    for (auto& keyField : keyFieldList)
    {
        auto ret = keyFieldSet.insert(keyField);
        if (!ret.second)
        {
            PRECOMPILED_LOG(ERROR) << LOG_DESC("duplicated key") << LOG_KV("key name", keyField)
                                   << LOG_KV("table name", tableName);
            BOOST_THROW_EXCEPTION(PrecompiledError("duplicated key: " + keyField));
        }
        checkFieldNameValidate(tableName, keyField);
    }

    for (auto& valueField : valueFieldList)
    {
        auto ret = valueFieldSet.insert(valueField);
        if (!ret.second)
        {
            PRECOMPILED_LOG(ERROR)
                << LOG_DESC("duplicated field") << LOG_KV("field name", valueField)
                << LOG_KV("table name", tableName);
            BOOST_THROW_EXCEPTION(PrecompiledError("duplicated field: " + valueField));
        }
        checkFieldNameValidate(tableName, valueField);
    }
}

int bcos::precompiled::checkLengthValidate(
    std::string_view fieldValue, int32_t maxLength, int32_t errorCode)
{
    if (fieldValue.size() > (size_t)maxLength)
    {
        PRECOMPILED_LOG(ERROR) << "key:" << fieldValue << " value size:" << fieldValue.size()
                               << " greater than " << maxLength;
        BOOST_THROW_EXCEPTION(
            PrecompiledError("size of value/key greater than" + std::to_string(maxLength) +
                             " error code: " + std::to_string(errorCode)));
    }
    return 0;
}

void bcos::precompiled::checkCreateTableParam(
    const std::string& _tableName, std::string& _keyField, std::string& _valueField)
{
    std::vector<std::string> keyNameList;
    boost::split(keyNameList, _keyField, boost::is_any_of(","));
    std::vector<std::string> fieldNameList;
    boost::split(fieldNameList, _valueField, boost::is_any_of(","));

    if (_keyField.size() > (size_t)SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)
    {  // mysql TableName and fieldName length limit is 64
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError("table field name length overflow " +
                                       std::to_string(SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)));
    }
    for (auto& str : keyNameList)
    {
        boost::trim(str);
        if (str.size() > (size_t)SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)
        {  // mysql TableName and fieldName length limit is 64
            BOOST_THROW_EXCEPTION(protocol::PrecompiledError(
                "errorCode: " + std::to_string(CODE_TABLE_FIELD_LENGTH_OVERFLOW) +
                std::string("table key name length overflow ") +
                std::to_string(SYS_TABLE_KEY_FIELD_NAME_MAX_LENGTH)));
        }
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

    checkNameValidate(_tableName, keyNameList, fieldNameList);

    _keyField = boost::join(keyNameList, ",");
    _valueField = boost::join(fieldNameList, ",");
    if (_keyField.size() > (size_t)SYS_TABLE_KEY_FIELD_MAX_LENGTH)
    {
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError(std::string("total table key name length overflow ") +
                                       std::to_string(SYS_TABLE_KEY_FIELD_MAX_LENGTH)));
    }
    if (_valueField.size() > (size_t)SYS_TABLE_VALUE_FIELD_MAX_LENGTH)
    {
        BOOST_THROW_EXCEPTION(
            protocol::PrecompiledError(std::string("total table field name length overflow ") +
                                       std::to_string(SYS_TABLE_VALUE_FIELD_MAX_LENGTH)));
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
}

uint32_t bcos::precompiled::getFuncSelector(
    std::string const& _functionName, const crypto::Hash::Ptr& _hashImpl)
{
    // global function selector cache
    if (s_name2SelectCache.count(_functionName))
    {
        return s_name2SelectCache[_functionName];
    }
    auto selector = getFuncSelectorByFunctionName(_functionName, _hashImpl);
    s_name2SelectCache.insert(std::make_pair(_functionName, selector));
    return selector;
}

uint32_t bcos::precompiled::getParamFunc(bytesConstRef _param)
{
    auto funcBytes = _param.getCroppedData(0, 4);
    uint32_t func = *((uint32_t*)(funcBytes.data()));

    return ((func & 0x000000FF) << 24) | ((func & 0x0000FF00) << 8) | ((func & 0x00FF0000) >> 8) |
           ((func & 0xFF000000) >> 24);
}

bytesConstRef bcos::precompiled::getParamData(bytesConstRef _param)
{
    return _param.getCroppedData(4);
}

uint32_t bcos::precompiled::getFuncSelectorByFunctionName(
    std::string const& _functionName, const crypto::Hash::Ptr& _hashImpl)
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
    auto codeHashStr = std::string(codeHashEntry->getField(0));
    auto codeHash = HashType(codeHashStr, FixedBytes<32>::FromBinary);

    if (codeHash == HashType())
    {
        return ContractStatus::NotContractAddress;
    }

    // FIXME: frozen in BFS
    auto frozenEntry = table->getRow(executor::ACCOUNT_FROZEN);
    if (frozenEntry != std::nullopt && "true" == frozenEntry->getField(0))
    {
        return ContractStatus::Frozen;
    }
    else
    {
        return ContractStatus::Available;
    }
}

uint64_t precompiled::getEntriesCapacity(precompiled::EntriesPtr _entries)
{
    int64_t totalCapacity = 0;
    for (auto& entry : *_entries)
    {
        totalCapacity += entry.size();
    }
    return totalCapacity;
}

bool precompiled::checkPathValid(std::string const& _path)
{
    if (_path.empty())
        return false;
    if (_path.length() > FS_PATH_MAX_LENGTH)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("checkPathValid") << LOG_DESC("path too long")
                               << LOG_KV("path", _path);
        return false;
    }
    if (_path == "/")
        return true;
    std::string absoluteDir = _path;
    if (absoluteDir[0] == '/')
    {
        absoluteDir = absoluteDir.substr(1);
    }
    if (absoluteDir.at(absoluteDir.size() - 1) == '/')
    {
        absoluteDir = absoluteDir.substr(0, absoluteDir.size() - 1);
    }
    std::vector<std::string> pathList;
    boost::split(pathList, absoluteDir, boost::is_any_of("/"), boost::token_compress_on);
    if (pathList.size() > FS_PATH_MAX_LEVEL || pathList.empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("checkPathValid")
                               << LOG_DESC("resource path's level is too deep")
                               << LOG_KV("path", _path);
        return false;
    }
    // TODO: adapt Chinese
    std::regex reg(R"(^[0-9a-zA-Z][^\>\<\*\?\/\=\+\(\)\$\"\']*$)");
    auto checkFieldNameValidate = [&reg](const std::string& fieldName) -> bool {
        if (fieldName.empty())
        {
            std::stringstream errorMessage;
            errorMessage << "Invalid field \"" + fieldName
                         << "\", the size of the field must be larger than 0";
            PRECOMPILED_LOG(ERROR)
                << LOG_DESC(errorMessage.str()) << LOG_KV("field name", fieldName);
            return false;
        }
        if (!std::regex_match(fieldName, reg))
        {
            std::stringstream errorMessage;
            errorMessage << "Invalid field \"" << fieldName << "\", the field name must be in reg: "
                         << R"(^[0-9a-zA-Z\u4e00-\u9fa5][^\>\<\*\?\/\=\+\(\)\$\"\']*$)";
            PRECOMPILED_LOG(ERROR)
                << LOG_DESC(errorMessage.str()) << LOG_KV("field name", fieldName);
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
    std::vector<std::string> dirList;
    std::string absoluteDir = _absolutePath;
    if (absoluteDir[0] == '/')
    {
        absoluteDir = absoluteDir.substr(1);
    }
    if (absoluteDir.at(absoluteDir.size() - 1) == '/')
    {
        absoluteDir = absoluteDir.substr(0, absoluteDir.size() - 1);
    }
    boost::split(dirList, absoluteDir, boost::is_any_of("/"), boost::token_compress_on);
    std::string baseName = dirList.at(dirList.size() - 1);
    dirList.pop_back();
    std::string parentDir = "/" + boost::join(dirList, "/");
    return {parentDir, baseName};
}

std::pair<std::string, std::string> precompiled::getLinkNameAndVersion(
    const std::string& _absolutePath)
{
    // transfer /usr/local/bin => ["usr", "local", "bin"]
    if (_absolutePath == "/")
        return {"", ""};
    std::vector<std::string> dirList;
    std::string absoluteDir = _absolutePath;
    if (absoluteDir[0] == '/')
    {
        absoluteDir = absoluteDir.substr(1);
    }
    if (absoluteDir.at(absoluteDir.size() - 1) == '/')
    {
        absoluteDir = absoluteDir.substr(0, absoluteDir.size() - 1);
    }
    boost::split(dirList, absoluteDir, boost::is_any_of("/"), boost::token_compress_on);
    if (dirList.size() != 3)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("getLinkNameAndVersion")
                               << LOG_DESC("link path level not equal 3")
                               << LOG_KV("absolutePath", _absolutePath);
        return {};
    }
    std::string name = dirList.at(1);
    std::string version = dirList.at(2);
    return {name, version};
}

bool precompiled::recursiveBuildDir(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const std::string& _absoluteDir)
{
    if (_absoluteDir.empty())
    {
        return false;
    }
    // transfer /usr/local/bin => ["usr", "local", "bin"]
    std::vector<std::string> dirList;
    std::string absoluteDir = _absoluteDir;
    if (absoluteDir[0] == '/')
    {
        absoluteDir = absoluteDir.substr(1);
    }
    if (absoluteDir.at(absoluteDir.size() - 1) == '/')
    {
        absoluteDir = absoluteDir.substr(0, absoluteDir.size() - 1);
    }
    boost::split(dirList, absoluteDir, boost::is_any_of("/"), boost::token_compress_on);
    std::string root = "/";

    for (auto dir : dirList)
    {
        auto table = _executive->storage().openTable(root);
        if (!table)
        {
            EXECUTIVE_LOG(ERROR) << LOG_BADGE("recursiveBuildDir")
                                 << LOG_DESC("can not open path table")
                                 << LOG_KV("tableName", root);
            return false;
        }
        if (root != "/")
        {
            root += "/";
        }
        auto typeEntry = table->getRow(FS_KEY_TYPE);
        if (typeEntry)
        {
            // can get type, then this type is directory
            // try open root + dir
            auto nextDirTable = _executive->storage().openTable(root + dir);
            if (nextDirTable.has_value())
            {
                // root + dir table exist, try to get type entry
                auto tryGetTypeEntry = nextDirTable->getRow(FS_KEY_TYPE);
                if (tryGetTypeEntry.has_value() && tryGetTypeEntry->getField(0) == FS_TYPE_DIR)
                {
                    // if success and dir is directory, continue
                    root += dir;
                    continue;
                }
                else
                {
                    // can not get type, it means this dir is not a directory
                    EXECUTIVE_LOG(ERROR)
                        << LOG_BADGE("recursiveBuildDir")
                        << LOG_DESC("file had already existed, and not directory type")
                        << LOG_KV("parentDir", root) << LOG_KV("dir", dir);
                    return false;
                }
            }

            // root + dir not exist, create root + dir and build bfs info in root table
            auto subEntry = table->getRow(FS_KEY_SUB);
            auto&& out = asBytes(std::string(subEntry->getField(0)));
            // codec to map
            std::map<std::string, std::string> bfsInfo;
            codec::scale::decode(bfsInfo, gsl::make_span(out));

            /// create table and build bfs info
            bfsInfo.insert(std::make_pair(dir, FS_TYPE_DIR));
            auto newTable = _executive->storage().createTable(root + dir, SYS_VALUE_FIELDS);
            storage::Entry tEntry, newSubEntry, aclTypeEntry, aclWEntry, aclBEntry, extraEntry;
            std::map<std::string, std::string> newSubMap;
            tEntry.importFields({FS_TYPE_DIR});
            newSubEntry.importFields({asString(codec::scale::encode(newSubMap))});
            aclTypeEntry.importFields({"0"});
            aclWEntry.importFields({""});
            aclBEntry.importFields({""});
            extraEntry.importFields({""});
            newTable->setRow(FS_KEY_TYPE, std::move(tEntry));
            newTable->setRow(FS_KEY_SUB, std::move(newSubEntry));
            newTable->setRow(FS_ACL_TYPE, std::move(aclTypeEntry));
            newTable->setRow(FS_ACL_WHITE, std::move(aclWEntry));
            newTable->setRow(FS_ACL_BLACK, std::move(aclBEntry));
            newTable->setRow(FS_KEY_EXTRA, std::move(extraEntry));
            subEntry->setField(0, asString(codec::scale::encode(bfsInfo)));
            table->setRow(FS_KEY_SUB, std::move(subEntry.value()));
            root += dir;
        }
        else
        {
            EXECUTIVE_LOG(ERROR) << LOG_BADGE("recursiveBuildDir")
                                 << LOG_DESC("file had already existed, and not directory type")
                                 << LOG_KV("parentDir", root) << LOG_KV("dir", dir);
            return false;
        }
    }
    return true;
}