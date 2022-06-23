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
 * @file FileSystemPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-06-10
 */

#include "FileSystemPrecompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <bcos-framework//executor/PrecompiledTypeDef.h>
#include <boost/algorithm/string/split.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;

constexpr const char* const FILE_SYSTEM_METHOD_LIST = "list(string)";
constexpr const char* const FILE_SYSTEM_METHOD_MKDIR = "mkdir(string)";
constexpr const char* const FILE_SYSTEM_METHOD_LINK = "link(string,string,string,string)";
constexpr const char* const FILE_SYSTEM_METHOD_RLINK = "readlink(string)";
constexpr const char* const FILE_SYSTEM_METHOD_TOUCH = "touch(string,string)";

FileSystemPrecompiled::FileSystemPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[FILE_SYSTEM_METHOD_LIST] = getFuncSelector(FILE_SYSTEM_METHOD_LIST, _hashImpl);
    name2Selector[FILE_SYSTEM_METHOD_MKDIR] = getFuncSelector(FILE_SYSTEM_METHOD_MKDIR, _hashImpl);
    name2Selector[FILE_SYSTEM_METHOD_LINK] = getFuncSelector(FILE_SYSTEM_METHOD_LINK, _hashImpl);
    name2Selector[FILE_SYSTEM_METHOD_TOUCH] = getFuncSelector(FILE_SYSTEM_METHOD_TOUCH, _hashImpl);
    name2Selector[FILE_SYSTEM_METHOD_RLINK] = getFuncSelector(FILE_SYSTEM_METHOD_RLINK, _hashImpl);
    BfsTypeSet = {FS_TYPE_DIR, FS_TYPE_CONTRACT, FS_TYPE_LINK};
}

std::shared_ptr<PrecompiledExecResult> FileSystemPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    uint32_t func = getParamFunc(_callParameters->input());
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("call")
                           << LOG_KV("func", func);

    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_callParameters->input().size());

    if (func == name2Selector[FILE_SYSTEM_METHOD_LIST])
    {
        // list(string) => (int32,fileList)
        listDir(_executive, gasPricer, _callParameters);
    }
    else if (func == name2Selector[FILE_SYSTEM_METHOD_MKDIR])
    {
        // mkdir(string) => int32
        makeDir(_executive, gasPricer, _callParameters);
    }
    else if (func == name2Selector[FILE_SYSTEM_METHOD_LINK])
    {
        // link(string name, string version, address, abi) => int32
        link(_executive, gasPricer, _callParameters);
    }
    else if (func == name2Selector[FILE_SYSTEM_METHOD_RLINK])
    {
        readLink(_executive, gasPricer, _callParameters);
    }
    else if (func == name2Selector[FILE_SYSTEM_METHOD_TOUCH])
    {
        // touch(string absolute,string type) => int32
        touch(_executive, gasPricer, _callParameters);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("call undefined function!");
        BOOST_THROW_EXCEPTION(PrecompiledError("FileSystemPrecompiled call undefined function!"));
    }

    gasPricer->updateMemUsed(_callParameters->m_execResult.size());
    _callParameters->setGas(_callParameters->m_gas - gasPricer->calTotalGas());
    return _callParameters;
}

int FileSystemPrecompiled::checkLinkParam(TransactionExecutive::Ptr _executive,
    std::string const& _contractAddress, std::string& _contractName, std::string& _contractVersion,
    std::string const& _contractAbi)
{
    boost::trim(_contractName);
    boost::trim(_contractVersion);
    // check the status of the contract(only print the error message to the log)
    std::string tableName =
        getContractTableName(_contractAddress, _executive->blockContext().lock()->isWasm());
    ContractStatus contractStatus = getContractStatus(_executive, tableName);

    if (contractStatus != ContractStatus::Available)
    {
        std::stringstream errorMessage;
        errorMessage << "Link operation failed for ";
        switch (contractStatus)
        {
        case ContractStatus::Frozen:
            errorMessage << "\"" << _contractName
                         << "\" has been frozen, contractAddress = " << _contractAddress;
            break;
        case ContractStatus::AddressNonExistent:
            errorMessage << "the contract \"" << _contractName << "\" with address "
                         << _contractAddress << " does not exist";
            break;
        case ContractStatus::NotContractAddress:
            errorMessage << "invalid address " << _contractAddress
                         << ", please make sure it's a contract address";
            break;
        default:
            errorMessage << "invalid contract \"" << _contractName << "\" with address "
                         << _contractAddress << ", error code:" << std::to_string(contractStatus);
            break;
        }
        PRECOMPILED_LOG(INFO) << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC(errorMessage.str())
                              << LOG_KV("contractAddress", _contractAddress)
                              << LOG_KV("contractName", _contractName);
        return CODE_ADDRESS_OR_VERSION_ERROR;
    }
    if (_contractVersion.find('/') != std::string::npos ||
        _contractName.find('/') != std::string::npos)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("version or name contains \"/\"")
                               << LOG_KV("contractName", _contractName)
                               << LOG_KV("version", _contractVersion);
        return CODE_ADDRESS_OR_VERSION_ERROR;
    }
    // check the length of the field value
    checkLengthValidate(
        _contractAbi, USER_TABLE_FIELD_VALUE_MAX_LENGTH, CODE_TABLE_FIELD_VALUE_LENGTH_OVERFLOW);
    return CODE_SUCCESS;
}

void FileSystemPrecompiled::makeDir(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)

{
    // mkdir(string)
    std::string absolutePath;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), absolutePath);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("FileSystemPrecompiled") << LOG_KV("mkdir", absolutePath);
    auto table = _executive->storage().openTable(absolutePath);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("file name exists, please check")
                               << LOG_KV("absolutePath", absolutePath);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_ALREADY_EXIST)));
        return;
    }

    auto bfsAddress = blockContext->isWasm() ? BFS_NAME : BFS_ADDRESS;

    auto response = externalTouchNewFile(_executive, _callParameters->m_origin, bfsAddress,
        absolutePath, FS_TYPE_DIR, _callParameters->m_gas - gasPricer->calTotalGas());
    _callParameters->setExecResult(codec.encode(response));
}

void FileSystemPrecompiled::listDir(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    // list(string)
    std::string absolutePath;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), absolutePath);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("FileSystemPrecompiled") << LOG_KV("ls", absolutePath);
    std::vector<BfsTuple> files = {};
    if (!checkPathValid(absolutePath))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("invalid path")
                               << LOG_KV("path", absolutePath);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_INVALID_PATH), files));
        return;
    }
    auto table = _executive->storage().openTable(absolutePath);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);

    if (table)
    {
        std::string baseName;
        std::tie(std::ignore, baseName) = getParentDirAndBaseName(absolutePath);
        // file exists, try to get type
        auto typeEntry = _executive->storage().getRow(absolutePath, FS_KEY_TYPE);
        if (typeEntry)
        {
            // get type success, this is dir or link
            // if dir
            if (typeEntry->getField(0) == FS_TYPE_DIR)
            {
                auto subEntry = _executive->storage().getRow(absolutePath, FS_KEY_SUB);
                std::map<std::string, std::string> bfsInfo;
                auto&& out = asBytes(std::string(subEntry->getField(0)));
                codec::scale::decode(bfsInfo, gsl::make_span(out));
                files.reserve(bfsInfo.size());
                for (const auto& bfs : bfsInfo)
                {
                    BfsTuple file =
                        std::make_tuple(bfs.first, bfs.second, std::vector<std::string>({}));
                    files.emplace_back(std::move(file));
                }
            }
            else if (typeEntry->getField(0) == FS_TYPE_LINK)
            {
                // if link
                auto addressEntry = _executive->storage().getRow(absolutePath, FS_LINK_ADDRESS);
                auto abiEntry = _executive->storage().getRow(absolutePath, FS_LINK_ABI);
                std::vector<std::string> ext = {std::string(addressEntry->getField(0)),
                    abiEntry.has_value() ? std::string(abiEntry->getField(0)) : ""};
                BfsTuple link = std::make_tuple(baseName, FS_TYPE_LINK, std::move(ext));
                files.emplace_back(std::move(link));
            }
        }
        else
        {
            // fail to get type, this is contract
            BfsTuple file =
                std::make_tuple(baseName, FS_TYPE_CONTRACT, std::vector<std::string>({}));
            files.emplace_back(std::move(file));
        }

        _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS), files));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("can't open table of file path")
                               << LOG_KV("path", absolutePath);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_NOT_EXIST), files));
    }
}

void FileSystemPrecompiled::link(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    std::string contractName, contractVersion, contractAddress, contractAbi;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(
        _callParameters->params(), contractName, contractVersion, contractAddress, contractAbi);
    if (!blockContext->isWasm())
    {
        contractAddress = boost::algorithm::to_lower_copy(trimHexPrefix(contractAddress));
    }

    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("link")
                           << LOG_KV("contractName", contractName)
                           << LOG_KV("contractVersion", contractVersion)
                           << LOG_KV("contractAddress", contractAddress);
    int validCode =
        checkLinkParam(_executive, contractAddress, contractName, contractVersion, contractAbi);
    auto linkTableName = USER_APPS_PREFIX + contractName + '/' + contractVersion;

    if (validCode < 0 || !checkPathValid(linkTableName))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("link params invalid")
                               << LOG_KV("contractName", contractName)
                               << LOG_KV("contractVersion", contractVersion)
                               << LOG_KV("contractAddress", contractAddress);
        _callParameters->setExecResult(
            codec.encode(int32_t(validCode < 0 ? validCode : CODE_FILE_INVALID_PATH)));
        return;
    }
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    auto linkTable = _executive->storage().openTable(linkTableName);
    if (linkTable)
    {
        // table exist, check this resource is a link
        auto typeEntry = _executive->storage().getRow(linkTableName, FS_KEY_TYPE);
        if (typeEntry && typeEntry->getField(0) == FS_TYPE_LINK)
        {
            // contract name and version exist, overwrite address and abi
            auto addressEntry = linkTable->newEntry();
            addressEntry.importFields({contractAddress});
            auto abiEntry = linkTable->newEntry();
            abiEntry.importFields({contractAbi});
            _executive->storage().setRow(linkTableName, FS_LINK_ADDRESS, std::move(addressEntry));
            _executive->storage().setRow(linkTableName, FS_LINK_ABI, std::move(abiEntry));
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("overwrite link successfully")
                << LOG_KV("contractName", contractName)
                << LOG_KV("contractVersion", contractVersion)
                << LOG_KV("contractAddress", contractAddress);
            _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
            return;
        }
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("File already exists.")
                               << LOG_KV("contractName", contractName)
                               << LOG_KV("version", contractVersion);
        _callParameters->setExecResult(codec.encode((int32_t)CODE_FILE_ALREADY_EXIST));
        return;
    }
    // table not exist, mkdir -p /apps/contractName first
    std::string bfsAddress = blockContext->isWasm() ? BFS_NAME : BFS_ADDRESS;

    auto response = externalTouchNewFile(_executive, _callParameters->m_origin, bfsAddress,
        linkTableName, FS_TYPE_LINK, _callParameters->m_gas - gasPricer->calTotalGas());
    if (response != 0)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("build link path error ")
                               << LOG_KV("contractName", contractName)
                               << LOG_KV("contractVersion", contractVersion);
        _callParameters->setExecResult(codec.encode((int32_t)CODE_FILE_BUILD_DIR_FAILED));
        return;
    }
    _executive->storage().createTable(linkTableName, STORAGE_VALUE);
    gasPricer->appendOperation(InterfaceOpcode::CreateTable);
    // set link info to link table
    Entry typeEntry, nameEntry, addressEntry, abiEntry;
    typeEntry.importFields({FS_TYPE_LINK});
    nameEntry.importFields({contractVersion});
    addressEntry.importFields({contractAddress});
    abiEntry.importFields({contractAbi});
    _executive->storage().setRow(linkTableName, FS_KEY_TYPE, std::move(typeEntry));
    _executive->storage().setRow(linkTableName, FS_KEY_NAME, std::move(nameEntry));
    _executive->storage().setRow(linkTableName, FS_LINK_ADDRESS, std::move(addressEntry));
    _executive->storage().setRow(linkTableName, FS_LINK_ABI, std::move(abiEntry));
    _callParameters->setExecResult(codec.encode((int32_t)CODE_SUCCESS));
}

void FileSystemPrecompiled::readLink(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    // readlink(string)
    std::string absolutePath;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), absolutePath);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("FileSystemPrecompiled")
                           << LOG_KV("readlink", absolutePath);
    bytes emptyResult =
        blockContext->isWasm() ? codec.encode(std::string("")) : codec.encode(Address());
    if (!checkPathValid(absolutePath))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("invalid link")
                               << LOG_KV("path", absolutePath);
        _callParameters->setExecResult(emptyResult);
        return;
    }
    auto table = _executive->storage().openTable(absolutePath);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);

    if (table)
    {
        // file exists, try to get type
        auto typeEntry = _executive->storage().getRow(absolutePath, FS_KEY_TYPE);
        if (typeEntry && typeEntry->getField(0) == FS_TYPE_LINK)
        {
            // if link
            auto addressEntry = _executive->storage().getRow(absolutePath, FS_LINK_ADDRESS);
            auto contractAddress = std::string(addressEntry->getField(0));
            auto codecAddress = blockContext->isWasm() ? codec.encode(contractAddress) :
                                                         codec.encode(Address(contractAddress));
            _callParameters->setExecResult(codecAddress);
            return;
        }

        _callParameters->setExecResult(emptyResult);
        return;
    }
    PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                           << LOG_DESC("can't open table of file path")
                           << LOG_KV("path", absolutePath);
    _callParameters->setExecResult(emptyResult);
}

void FileSystemPrecompiled::touch(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledGas::Ptr& gasPricer, const PrecompiledExecResult::Ptr& _callParameters)
{
    // touch(string absolute, string type)
    std::string absolutePath, type;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), absolutePath, type);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("touch new file")
                           << LOG_KV("absolutePath", absolutePath) << LOG_KV("type", type);
    if (!checkPathValid(absolutePath))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("file exists");

        _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_INVALID_PATH)));
        return;
    }
    if (BfsTypeSet.find(type) == BfsTypeSet.end())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("touch file in error type")
                               << LOG_KV("absolutePath", absolutePath) << LOG_KV("type", type);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_INVALID_TYPE)));
        return;
    }
    if (absolutePath.find(USER_APPS_PREFIX) != 0 && absolutePath.find(USER_TABLE_PREFIX) != 0)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("touch file in system dir")
                               << LOG_KV("absolutePath", absolutePath) << LOG_KV("type", type);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_INVALID_PATH)));
        return;
    }

    std::string parentDir, baseName;
    if (type == FS_TYPE_DIR)
    {
        parentDir = absolutePath;
    }
    else
    {
        std::tie(parentDir, baseName) = getParentDirAndBaseName(absolutePath);
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("FileSystemPrecompiled")
                           << LOG_DESC("directory not exists, recursive build dir")
                           << LOG_KV("parentDir", parentDir) << LOG_KV("baseName", baseName)
                           << LOG_KV("type", type);
    auto buildResult = recursiveBuildDir(_executive, parentDir);
    gasPricer->appendOperation(CreateTable);
    if (!buildResult)
    {
        BOOST_THROW_EXCEPTION(PrecompiledError("Recursive build bfs dir error."));
    }
    if (type == FS_TYPE_DIR)
    {
        _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
        return;
    }

    // set meta data in parent table
    std::map<std::string, std::string> bfsInfo;
    auto subEntry = _executive->storage().getRow(parentDir, FS_KEY_SUB);
    auto&& out = asBytes(std::string(subEntry->getField(0)));
    codec::scale::decode(bfsInfo, gsl::make_span(out));
    bfsInfo.insert(std::make_pair(baseName, type));
    subEntry->setField(0, asString(codec::scale::encode(bfsInfo)));
    _executive->storage().setRow(parentDir, FS_KEY_SUB, std::move(subEntry.value()));

    _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
}

bool FileSystemPrecompiled::recursiveBuildDir(
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
        auto newTableName = ((root == "/") ? root : (root + "/")) + dir;
        auto typeEntry = _executive->storage().getRow(root, FS_KEY_TYPE);
        if (typeEntry)
        {
            // can get type, then this type is directory
            // try open root + dir
            auto nextDirTable = _executive->storage().openTable(newTableName);
            if (nextDirTable.has_value())
            {
                // root + dir table exist, try to get type entry
                auto tryGetTypeEntry = _executive->storage().getRow(newTableName, FS_KEY_TYPE);
                if (tryGetTypeEntry.has_value() && tryGetTypeEntry->getField(0) == FS_TYPE_DIR)
                {
                    // if success and dir is directory, continue
                    root = newTableName;
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
            auto subEntry = _executive->storage().getRow(root, FS_KEY_SUB);
            auto&& out = asBytes(std::string(subEntry->getField(0)));
            // codec to map
            std::map<std::string, std::string> bfsInfo;
            codec::scale::decode(bfsInfo, gsl::make_span(out));

            /// create table and build bfs info
            bfsInfo.insert(std::make_pair(dir, FS_TYPE_DIR));
            _executive->storage().createTable(newTableName, SYS_VALUE_FIELDS);
            storage::Entry tEntry, newSubEntry, aclTypeEntry, aclWEntry, aclBEntry, extraEntry;
            std::map<std::string, std::string> newSubMap;
            tEntry.importFields({FS_TYPE_DIR});
            newSubEntry.importFields({asString(codec::scale::encode(newSubMap))});
            aclTypeEntry.importFields({"0"});
            aclWEntry.importFields({""});
            aclBEntry.importFields({""});
            extraEntry.importFields({""});
            _executive->storage().setRow(newTableName, FS_KEY_TYPE, std::move(tEntry));
            _executive->storage().setRow(newTableName, FS_KEY_SUB, std::move(newSubEntry));
            _executive->storage().setRow(newTableName, FS_ACL_TYPE, std::move(aclTypeEntry));
            _executive->storage().setRow(newTableName, FS_ACL_WHITE, std::move(aclWEntry));
            _executive->storage().setRow(newTableName, FS_ACL_BLACK, std::move(aclBEntry));
            _executive->storage().setRow(newTableName, FS_KEY_EXTRA, std::move(extraEntry));

            // set metadata in parent dir
            subEntry->setField(0, asString(codec::scale::encode(bfsInfo)));
            _executive->storage().setRow(root, FS_KEY_SUB, std::move(subEntry.value()));
            root = newTableName;
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