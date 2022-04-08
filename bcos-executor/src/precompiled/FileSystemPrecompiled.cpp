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
#include "Common.h"
#include "PrecompiledResult.h"
#include "Utilities.h"
#include <bcos-framework/interfaces/executor/PrecompiledTypeDef.h>
#include <boost/algorithm/string/split.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;

const char* const FILE_SYSTEM_METHOD_LIST = "list(string)";
const char* const FILE_SYSTEM_METHOD_MKDIR = "mkdir(string)";
const char* const FILE_SYSTEM_METHOD_LINK = "link(string,string,string,string)";
const char* const FILE_SYSTEM_METHOD_RLINK = "readlink(string)";

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
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string& _origin, const std::string&, int64_t _gasLeft)
{
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("call")
                           << LOG_KV("func", func);

    auto callResult = std::make_shared<PrecompiledExecResult>();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();
    gasPricer->setMemUsed(_param.size());

    if (func == name2Selector[FILE_SYSTEM_METHOD_LIST])
    {
        // list(string)
        listDir(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[FILE_SYSTEM_METHOD_MKDIR])
    {
        // mkdir(string) => int256
        makeDir(_executive, data, callResult, _origin, gasPricer, _gasLeft);
    }
    else if (func == name2Selector[FILE_SYSTEM_METHOD_LINK])
    {
        // link(string name, string version, address, abi) => int256
        link(_executive, data, callResult, _origin, _gasLeft);
    }
    else if (func == name2Selector[FILE_SYSTEM_METHOD_RLINK])
    {
        readLink(_executive, data, callResult);
    }
    else if (func == name2Selector[FILE_SYSTEM_METHOD_TOUCH])
    {
        // touch(string absolute,string type)
        touch(_executive, data, callResult);
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("call undefined function!");
    }

    gasPricer->updateMemUsed(callResult->m_execResult.size());
    callResult->setGas(gasPricer->calTotalGas());
    return callResult;
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
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    std::shared_ptr<PrecompiledExecResult> callResult, const std::string& _origin,
    const PrecompiledGas::Ptr& gasPricer, int64_t gasLeft)
{
    // mkdir(string)
    std::string absolutePath;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, absolutePath);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("FileSystemPrecompiled") << LOG_KV("mkdir", absolutePath);
    gasPricer->appendOperation(InterfaceOpcode::CreateTable);
    auto bfsAddress = blockContext->isWasm() ? BFS_NAME : BFS_ADDRESS;

    auto response = externalTouchNewFile(_executive, _origin, bfsAddress, absolutePath, FS_TYPE_DIR,
        gasLeft - gasPricer->calTotalGas());
    callResult->setExecResult(codec->encode(response));
}

void FileSystemPrecompiled::listDir(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    std::shared_ptr<PrecompiledExecResult> callResult, const PrecompiledGas::Ptr& gasPricer)
{
    // list(string)
    std::string absolutePath;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, absolutePath);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("FileSystemPrecompiled") << LOG_KV("ls", absolutePath);
    std::vector<BfsTuple> files;
    if (!checkPathValid(absolutePath))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("invalid path")
                               << LOG_KV("path", absolutePath);
        callResult->setExecResult(codec->encode(s256((int)CODE_FILE_INVALID_PATH), files));
        return;
    }
    auto table = _executive->storage().openTable(absolutePath);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);

    if (table)
    {
        auto parentDirAndBaseName = getParentDirAndBaseName(absolutePath);
        auto baseName = parentDirAndBaseName.second;
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
                std::vector<std::string> ext;
                ext.emplace_back(addressEntry->getField(0));
                ext.emplace_back(abiEntry->getField(0));
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

        callResult->setExecResult(codec->encode(s256((int)CODE_SUCCESS), files));
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("can't open table of file path")
                               << LOG_KV("path", absolutePath);
        callResult->setExecResult(codec->encode(s256((int)CODE_FILE_NOT_EXIST), files));
    }
}

void FileSystemPrecompiled::link(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    bytesConstRef& data, std::shared_ptr<PrecompiledExecResult> callResult,
    const std::string& _origin, int64_t gasLeft)
{
    std::string contractName, contractVersion, contractAddress, contractAbi;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, contractName, contractVersion, contractAddress, contractAbi);
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
        getErrorCodeOut(callResult->mutableExecResult(),
            validCode < 0 ? validCode : CODE_FILE_INVALID_PATH, *codec);
        return;
    }
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
            callResult->setExecResult(codec->encode((s256)((int)CODE_SUCCESS)));
            return;
        }
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("File already exists.")
                               << LOG_KV("contractName", contractName)
                               << LOG_KV("version", contractVersion);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_FILE_ALREADY_EXIST, *codec);
        return;
    }
    // table not exist, mkdir -p /apps/contractName first
    std::string bfsAddress = blockContext->isWasm() ? BFS_NAME : BFS_ADDRESS;

    auto response =
        externalTouchNewFile(_executive, _origin, bfsAddress, linkTableName, FS_TYPE_LINK, gasLeft);
    if (response != 0)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("build link path error ")
                               << LOG_KV("contractName", contractName)
                               << LOG_KV("contractVersion", contractVersion);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_FILE_BUILD_DIR_FAILED, *codec);
        return;
    }
    // linkTable must exist
    linkTable = _executive->storage().openTable(linkTableName);
    // set link info to link table
    auto typeEntry = linkTable->newEntry();
    typeEntry.importFields({FS_TYPE_LINK});
    auto nameEntry = linkTable->newEntry();
    nameEntry.importFields({contractVersion});
    auto addressEntry = linkTable->newEntry();
    addressEntry.importFields({contractAddress});
    auto abiEntry = linkTable->newEntry();
    abiEntry.importFields({contractAbi});
    _executive->storage().setRow(linkTableName, FS_KEY_TYPE, std::move(typeEntry));
    _executive->storage().setRow(linkTableName, FS_KEY_NAME, std::move(nameEntry));
    _executive->storage().setRow(linkTableName, FS_LINK_ADDRESS, std::move(addressEntry));
    _executive->storage().setRow(linkTableName, FS_LINK_ABI, std::move(abiEntry));
    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, *codec);
}

void FileSystemPrecompiled::readLink(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    std::shared_ptr<PrecompiledExecResult> callResult)
{
    // readlink(string)
    std::string absolutePath;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, absolutePath);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("FileSystemPrecompiled")
                           << LOG_KV("readlink", absolutePath);
    bytes emptyResult =
        blockContext->isWasm() ? codec->encode(std::string("")) : codec->encode(Address());
    if (!checkPathValid(absolutePath))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("invalid link")
                               << LOG_KV("path", absolutePath);
        callResult->setExecResult(emptyResult);
        return;
    }
    std::string name, version;
    std::tie(name, version) = getLinkNameAndVersion(absolutePath);
    if (name.empty() || version.empty())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("invalid link")
                               << LOG_KV("path", absolutePath);
        callResult->setExecResult(emptyResult);
        return;
    }
    auto table = _executive->storage().openTable(absolutePath);

    if (table)
    {
        // file exists, try to get type
        auto typeEntry = _executive->storage().getRow(absolutePath, FS_KEY_TYPE);
        if (typeEntry && typeEntry->getField(0) == FS_TYPE_LINK)
        {
            // if link
            auto addressEntry = _executive->storage().getRow(absolutePath, FS_LINK_ADDRESS);
            auto contractAddress = std::string(addressEntry->getField(0));
            auto codecAddress = blockContext->isWasm() ? codec->encode(contractAddress) :
                                                         codec->encode(Address(contractAddress));
            callResult->setExecResult(codecAddress);
            return;
        }

        callResult->setExecResult(emptyResult);
        return;
    }
    PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                           << LOG_DESC("can't open table of file path")
                           << LOG_KV("path", absolutePath);
    callResult->setExecResult(emptyResult);
}

void FileSystemPrecompiled::touch(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    bytesConstRef& data, std::shared_ptr<PrecompiledExecResult> callResult)
{
    // touch(string absolute, string type)
    std::string absolutePath, type;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<CodecWrapper>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, absolutePath, type);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("touch new file")
                           << LOG_KV("absolutePath", absolutePath) << LOG_KV("type", type);
    if (!checkPathValid(absolutePath))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("file exists");
        callResult->setExecResult(codec->encode(s256((int)CODE_FILE_INVALID_PATH)));
        return;
    }
    if (BfsTypeSet.find(type) == BfsTypeSet.end())
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("touch file in error type")
                               << LOG_KV("absolutePath", absolutePath) << LOG_KV("type", type);
        callResult->setExecResult(codec->encode(s256((int)CODE_FILE_INVALID_TYPE)));
        return;
    }
    if (absolutePath.find(USER_APPS_PREFIX) != 0 && absolutePath.find(USER_TABLE_PREFIX) != 0)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("touch file in system dir")
                               << LOG_KV("absolutePath", absolutePath) << LOG_KV("type", type);
        callResult->setExecResult(codec->encode(s256((int)CODE_FILE_INVALID_PATH)));
        return;
    }
    auto table = _executive->storage().openTable(absolutePath);
    if (table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("file name exists, please check")
                               << LOG_KV("absolutePath", absolutePath);
        callResult->setExecResult(codec->encode(s256((int)CODE_FILE_ALREADY_EXIST)));
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
    if (!buildResult)
    {
        BOOST_THROW_EXCEPTION(PrecompiledError("Recursive build bfs dir error."));
    }
    if (type == FS_TYPE_DIR)
    {
        getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, *codec);
        return;
    }
    _executive->storage().createTable(absolutePath, SYS_VALUE_FIELDS);

    // set meta data in parent table
    std::map<std::string, std::string> bfsInfo;
    auto subEntry = _executive->storage().getRow(parentDir, FS_KEY_SUB);
    auto&& out = asBytes(std::string(subEntry->getField(0)));
    codec::scale::decode(bfsInfo, gsl::make_span(out));
    bfsInfo.insert(std::make_pair(baseName, type));
    subEntry->setField(0, asString(codec::scale::encode(bfsInfo)));
    _executive->storage().setRow(parentDir, FS_KEY_SUB, std::move(subEntry.value()));

    getErrorCodeOut(callResult->mutableExecResult(), CODE_SUCCESS, *codec);
}