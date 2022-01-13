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

FileSystemPrecompiled::FileSystemPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[FILE_SYSTEM_METHOD_LIST] = getFuncSelector(FILE_SYSTEM_METHOD_LIST, _hashImpl);
    name2Selector[FILE_SYSTEM_METHOD_MKDIR] = getFuncSelector(FILE_SYSTEM_METHOD_MKDIR, _hashImpl);
    name2Selector[FILE_SYSTEM_METHOD_LINK] = getFuncSelector(FILE_SYSTEM_METHOD_LINK, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> FileSystemPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive, bytesConstRef _param,
    const std::string&, const std::string&)
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
        // mkdir(string)
        makeDir(_executive, data, callResult, gasPricer);
    }
    else if (func == name2Selector[FILE_SYSTEM_METHOD_LINK])
    {
        // link(string name, string version, address, abi)
        link(_executive, data, callResult);
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
        USER_APPS_PREFIX +
        (_contractAddress[0] == '/' ? _contractAddress.substr(1) : _contractAddress);
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
    }
    if (_contractVersion.size() > LINK_VERSION_MAX_LENGTH)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("version length overflow 128")
                               << LOG_KV("contractName", _contractName)
                               << LOG_KV("address", _contractAddress)
                               << LOG_KV("version", _contractVersion);
        return CODE_VERSION_LENGTH_OVERFLOW;
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
    // check the length of the key
    checkLengthValidate(
        _contractName, LINK_CONTRACT_NAME_MAX_LENGTH, CODE_TABLE_KEY_VALUE_LENGTH_OVERFLOW);
    // check the length of the field value
    checkLengthValidate(
        _contractAbi, USER_TABLE_FIELD_VALUE_MAX_LENGTH, CODE_TABLE_FIELD_VALUE_LENGTH_OVERFLOW);
    return CODE_SUCCESS;
}

void FileSystemPrecompiled::makeDir(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    std::shared_ptr<PrecompiledExecResult> callResult, const PrecompiledGas::Ptr& gasPricer)
{
    // mkdir(string)
    std::string absolutePath;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, absolutePath);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("FileSystemPrecompiled") << LOG_KV("mkdir", absolutePath);
    if (!checkPathValid(absolutePath))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("directory exists");
        callResult->setExecResult(codec->encode(s256((int)CODE_FILE_INVALID_PATH)));
        return;
    }
    if (absolutePath.find(USER_APPS_PREFIX) != 0 && absolutePath.find(USER_TABLE_PREFIX) != 0)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("mkdir in system dir")
                               << LOG_KV("absolutePath", absolutePath);
        callResult->setExecResult(codec->encode(s256((int)CODE_FILE_INVALID_PATH)));
        return;
    }
    auto table = _executive->storage().openTable(absolutePath);
    gasPricer->appendOperation(InterfaceOpcode::OpenTable);
    if (table)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("file name exists, please check");
        callResult->setExecResult(codec->encode(s256((int)CODE_FILE_ALREADY_EXIST)));
    }
    else
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("directory not exists, recursive build dir")
                               << LOG_KV("path", absolutePath);
        auto buildResult = recursiveBuildDir(_executive, absolutePath);
        auto result = buildResult ? CODE_SUCCESS : CODE_FILE_BUILD_DIR_FAILED;
        getErrorCodeOut(callResult->mutableExecResult(), result, *codec);
    }
}

void FileSystemPrecompiled::listDir(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, bytesConstRef& data,
    std::shared_ptr<PrecompiledExecResult> callResult, const PrecompiledGas::Ptr& gasPricer)
{
    // list(string)
    std::string absolutePath;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
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
        auto typeEntry = table->getRow(FS_KEY_TYPE);
        if (typeEntry)
        {
            // get type success, this is dir or link
            // if dir
            if (typeEntry->getField(0) == FS_TYPE_DIR)
            {
                auto subEntry = table->getRow(FS_KEY_SUB);
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
                auto addressEntry = table->getRow(FS_LINK_ADDRESS);
                auto abiEntry = table->getRow(FS_LINK_ABI);
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
    bytesConstRef& data, std::shared_ptr<PrecompiledExecResult> callResult)
{
    std::string contractName, contractVersion, contractAddress, contractAbi;
    auto blockContext = _executive->blockContext().lock();
    auto codec =
        std::make_shared<PrecompiledCodec>(blockContext->hashHandler(), blockContext->isWasm());
    codec->decode(data, contractName, contractVersion, contractAddress, contractAbi);
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("FileSystemPrecompiled") << LOG_DESC("link")
                           << LOG_KV("contractName", contractName)
                           << LOG_KV("contractVersion", contractVersion)
                           << LOG_KV("contractAddress", contractAddress);
    int validCode =
        checkLinkParam(_executive, contractAddress, contractName, contractVersion, contractAbi);
    if (validCode < 0)
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("link params invalid")
                               << LOG_KV("contractName", contractName)
                               << LOG_KV("contractVersion", contractVersion)
                               << LOG_KV("contractAddress", contractAddress);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_ADDRESS_OR_VERSION_ERROR, *codec);
        return;
    }
    auto linkTableName = USER_APPS_PREFIX + contractName + '/' + contractVersion;
    auto table = _executive->storage().openTable(linkTableName);
    if (table)
    {
        // table exist, check this resource is a link
        auto typeEntry = table->getRow(FS_KEY_TYPE);
        if (typeEntry && typeEntry->getField(0) == FS_TYPE_LINK)
        {
            // contract name and version exist, overwrite address and abi
            auto addressEntry = table->newEntry();
            addressEntry.importFields({contractAddress});
            table->setRow(FS_LINK_ADDRESS, std::move(addressEntry));
            auto abiEntry = table->newEntry();
            abiEntry.importFields({contractAbi});
            table->setRow(FS_LINK_ABI, std::move(abiEntry));
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
    auto parentTableName = USER_APPS_PREFIX + contractName;
    if (!recursiveBuildDir(_executive, parentTableName))
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("FileSystemPrecompiled")
                               << LOG_DESC("build link path error ")
                               << LOG_KV("contractName", contractName)
                               << LOG_KV("contractVersion", contractVersion);
        getErrorCodeOut(callResult->mutableExecResult(), CODE_FILE_BUILD_DIR_FAILED, *codec);
        return;
    }
    auto linkTable = _executive->storage().createTable(linkTableName, SYS_VALUE_FIELDS);

    // set meta data in parent table
    auto parentTable = _executive->storage().openTable(parentTableName);
    std::map<std::string, std::string> bfsInfo;
    auto stubEntry = parentTable->getRow(FS_KEY_SUB);
    auto&& out = asBytes(std::string(stubEntry->getField(0)));
    codec::scale::decode(bfsInfo, gsl::make_span(out));
    bfsInfo.insert(std::make_pair(contractVersion, FS_TYPE_LINK));
    stubEntry->setField(0, asString(codec::scale::encode(bfsInfo)));
    parentTable->setRow(FS_KEY_SUB, std::move(stubEntry.value()));

    // set link info to link table
    auto typeEntry = linkTable->newEntry();
    typeEntry.importFields({FS_TYPE_LINK});
    linkTable->setRow(FS_KEY_TYPE, std::move(typeEntry));
    auto nameEntry = linkTable->newEntry();
    nameEntry.importFields({contractVersion});
    linkTable->setRow(FS_KEY_NAME, std::move(nameEntry));
    auto addressEntry = linkTable->newEntry();
    addressEntry.importFields({contractAddress});
    linkTable->setRow(FS_LINK_ADDRESS, std::move(addressEntry));
    auto abiEntry = linkTable->newEntry();
    abiEntry.importFields({contractAbi});
    linkTable->setRow(FS_LINK_ABI, std::move(abiEntry));
}
