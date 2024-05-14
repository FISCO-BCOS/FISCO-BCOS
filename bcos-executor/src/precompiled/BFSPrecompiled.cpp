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
 * @file BFSPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-06-10
 */

#include "BFSPrecompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-framework/storage/StorageInterface.h>
#include <bcos-tool/BfsFileFactory.h>
#include <bcos-utilities/Ranges.h>
#include <boost/algorithm/string/split.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/throw_exception.hpp>
#include <queue>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::protocol;

static constexpr std::string_view FILE_SYSTEM_METHOD_LIST = "list(string)";
static constexpr std::string_view FILE_SYSTEM_METHOD_LIST_PAGE = "list(string,uint256,uint256)";
static constexpr std::string_view FILE_SYSTEM_METHOD_MKDIR = "mkdir(string)";
static constexpr std::string_view FILE_SYSTEM_METHOD_LINK_CNS = "link(string,string,string,string)";
static constexpr std::string_view FILE_SYSTEM_METHOD_LINK = "link(string,string,string)";
static constexpr std::string_view FILE_SYSTEM_METHOD_RLINK = "readlink(string)";
static constexpr std::string_view FILE_SYSTEM_METHOD_TOUCH = "touch(string,string)";
static constexpr std::string_view FILE_SYSTEM_METHOD_INIT = "initBfs()";
static constexpr std::string_view FILE_SYSTEM_METHOD_REBUILD = "rebuildBfs(uint256,uint256)";
static constexpr std::string_view FILE_SYSTEM_METHOD_FIX = "fixBfs(uint256)";

static void buildSysSubs(
    const std::shared_ptr<executor::TransactionExecutive>& _executive, auto toVersion)
{
    for (const auto& sysSub : BFS_SYS_SUBS)
    {
        if (sysSub == SHARDING_PRECOMPILED_NAME && toVersion <=> BlockVersion::V3_3_VERSION < 0)
            [[unlikely]]
        {
            continue;
        }
        if (sysSub == CAST_NAME && versionCompareTo(toVersion, BlockVersion::V3_2_VERSION) < 0)
        {
            continue;
        }
        Entry entry;
        // type, status, acl_type, acl_white, acl_black, extra
        tool::BfsFileFactory::buildDirEntry(entry, tool::LINK);
        _executive->storage().setRow(
            tool::FS_SYS_BIN, sysSub.substr(tool::FS_SYS_BIN.length() + 1), std::move(entry));
    }
    // build sys contract
    for (const auto& [name, address] : SYS_NAME_ADDRESS_MAP)
    {
        if (name == SHARDING_PRECOMPILED_NAME && toVersion < BlockVersion::V3_3_VERSION)
            [[unlikely]]
        {
            continue;
        }

        if (name == CAST_NAME && toVersion < BlockVersion::V3_2_VERSION)

        {
            continue;
        }
        auto linkTable = _executive->storage().createTable(std::string(name), SYS_VALUE_FIELDS);
        tool::BfsFileFactory::buildLink(
            linkTable.value(), std::string(address), "", static_cast<uint32_t>(toVersion));
    }
}

BFSPrecompiled::BFSPrecompiled(crypto::Hash::Ptr _hashImpl) : Precompiled(_hashImpl)
{
    name2Selector[std::string(FILE_SYSTEM_METHOD_LIST)] =
        getFuncSelector(FILE_SYSTEM_METHOD_LIST, _hashImpl);
    name2Selector[std::string(FILE_SYSTEM_METHOD_LIST_PAGE)] =
        getFuncSelector(FILE_SYSTEM_METHOD_LIST_PAGE, _hashImpl);
    name2Selector[std::string(FILE_SYSTEM_METHOD_MKDIR)] =
        getFuncSelector(FILE_SYSTEM_METHOD_MKDIR, _hashImpl);
    name2Selector[std::string(FILE_SYSTEM_METHOD_LINK)] =
        getFuncSelector(FILE_SYSTEM_METHOD_LINK, _hashImpl);
    name2Selector[std::string(FILE_SYSTEM_METHOD_LINK_CNS)] =
        getFuncSelector(FILE_SYSTEM_METHOD_LINK_CNS, _hashImpl);
    name2Selector[std::string(FILE_SYSTEM_METHOD_TOUCH)] =
        getFuncSelector(FILE_SYSTEM_METHOD_TOUCH, _hashImpl);
    name2Selector[std::string(FILE_SYSTEM_METHOD_RLINK)] =
        getFuncSelector(FILE_SYSTEM_METHOD_RLINK, _hashImpl);
    name2Selector[std::string(FILE_SYSTEM_METHOD_INIT)] =
        getFuncSelector(FILE_SYSTEM_METHOD_INIT, _hashImpl);
    name2Selector[std::string(FILE_SYSTEM_METHOD_REBUILD)] =
        getFuncSelector(FILE_SYSTEM_METHOD_REBUILD, _hashImpl);
    name2Selector[std::string(FILE_SYSTEM_METHOD_FIX)] =
        getFuncSelector(FILE_SYSTEM_METHOD_FIX, _hashImpl);
    BfsTypeSet = {
        std::string(FS_TYPE_DIR), std::string(FS_TYPE_CONTRACT), std::string(FS_TYPE_LINK)};
}

std::shared_ptr<PrecompiledExecResult> BFSPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    uint32_t func = getParamFunc(_callParameters->input());
    uint32_t version = _executive->blockContext().blockVersion();

    if (func == name2Selector[std::string(FILE_SYSTEM_METHOD_LIST)])
    {
        // list(string) => (int32,fileList)
        listDir(_executive, _callParameters);
    }
    else if (version >= static_cast<uint32_t>(BlockVersion::V3_1_VERSION) &&
             func == name2Selector[std::string(FILE_SYSTEM_METHOD_LIST_PAGE)])
    {
        // list(string,uint,uint) => (int32,fileList)
        listDirPage(_executive, _callParameters);
    }
    else if (func == name2Selector[std::string(FILE_SYSTEM_METHOD_MKDIR)])
    {
        // mkdir(string) => int32
        makeDir(_executive, _callParameters);
    }
    else if (func == name2Selector[std::string(FILE_SYSTEM_METHOD_LINK_CNS)])
    {
        // link(string name, string version, address, abi) => int32
        linkAdaptCNS(_executive, _callParameters);
    }
    else if (version >= static_cast<uint32_t>(BlockVersion::V3_1_VERSION) &&
             func == name2Selector[std::string(FILE_SYSTEM_METHOD_LINK)])
    {
        // link(absolutePath, address, abi) => int32
        link(_executive, _callParameters);
    }
    else if (func == name2Selector[std::string(FILE_SYSTEM_METHOD_RLINK)])
    {
        readLink(_executive, _callParameters);
    }
    else if (func == name2Selector[std::string(FILE_SYSTEM_METHOD_TOUCH)])
    {
        // touch(string absolute,string type) => int32
        touch(_executive, _callParameters);
    }
    else if (version >= static_cast<uint32_t>(BlockVersion::V3_1_VERSION) &&
             func == name2Selector[std::string(FILE_SYSTEM_METHOD_INIT)])
    {
        // initBfs for the first time
        initBfs(_executive, _callParameters);
    }
    else if (func == name2Selector[std::string(FILE_SYSTEM_METHOD_REBUILD)])
    {
        // initBfs for the first time
        rebuildBfs(_executive, _callParameters);
    }
    else if (version >= BlockVersion::V3_3_VERSION &&
             func == name2Selector[std::string(FILE_SYSTEM_METHOD_FIX)])
    {
        fixBfs(_executive, _callParameters);
    }
    else [[unlikely]]
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("BFSPrecompiled")
                              << LOG_DESC("call undefined function!");
        BOOST_THROW_EXCEPTION(PrecompiledError("BFSPrecompiled call undefined function!"));
    }

    return _callParameters;
}

int BFSPrecompiled::checkLinkParam(TransactionExecutive::Ptr _executive,
    std::string const& _contractAddress, std::string& _contractName, std::string& _contractVersion,
    std::string const& _contractAbi)
{
    boost::trim(_contractName);
    boost::trim(_contractVersion);
    // check the status of the contract(only print the error message to the log)
    std::string tableName = getContractTableName(getLinkRootDir(), _contractAddress);
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
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled") << LOG_DESC(errorMessage.str())
                               << LOG_KV("contractAddress", _contractAddress)
                               << LOG_KV("contractName", _contractName);
        return CODE_ADDRESS_OR_VERSION_ERROR;
    }
    if (_contractVersion.find('/') != std::string::npos ||
        _contractName.find('/') != std::string::npos)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled")
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


void BFSPrecompiled::makeDir(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    // mkdir(string)
    std::string absolutePath;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), absolutePath);


    if (isShardPath(absolutePath) &&
        blockContext.blockVersion() >= (uint32_t)(bcos::protocol::BlockVersion::V3_3_VERSION))
    {
        PRECOMPILED_LOG(DEBUG)
            << LOG_BADGE("BFSPrecompiled")
            << LOG_DESC(
                   "check mkDir params failed, normal BFS operation could not mkdir a shard dir")
            << LOG_KV("absolutePath", absolutePath);
        _callParameters->setExecResult(codec.encode(s256((int)CODE_FILE_BUILD_DIR_FAILED)));
        return;
    }

    makeDirImpl(absolutePath, _executive, _callParameters);
}

void BFSPrecompiled::makeDirImpl(const std::string& _absolutePath,
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());

    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BFSPrecompiled")
                          << LOG_KV("mkdir", _absolutePath);
    auto table = _executive->storage().openTable(_absolutePath);
    if (table)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled")
                               << LOG_DESC("BFS file name already exists, please check")
                               << LOG_KV("absolutePath", _absolutePath);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_ALREADY_EXIST)));
        return;
    }

    const auto* bfsAddress = getThisAddress(blockContext.isWasm());

    auto response = externalTouchNewFile(_executive, _callParameters->m_origin, bfsAddress,
        getThisAddress(blockContext.isWasm()), _absolutePath, FS_TYPE_DIR,
        _callParameters->m_gasLeft);
    auto result = codec.encode(response);
    if (blockContext.isWasm() &&
        protocol::versionCompareTo(blockContext.blockVersion(), BlockVersion::V3_2_VERSION) >= 0)
    {
        result = codec.encode((int32_t)response);
    }
    _callParameters->setExecResult(std::move(result));
}

void BFSPrecompiled::listDir(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    // list(string)
    std::string absolutePath;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), absolutePath);
    std::vector<BfsTuple> files = {};
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("ls path")
                           << LOG_KV("path", absolutePath);
    if (!checkPathValid(absolutePath, blockContext.blockVersion()))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("invalid path name")
                               << LOG_KV("path", absolutePath);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_INVALID_PATH), files));
        return;
    }
    auto table = _executive->storage().openTable(absolutePath);

    if (table)
    {
        // exist
        auto [parentDir, baseName] = getParentDirAndBaseName(absolutePath);
        if (blockContext.blockVersion() >= (uint32_t)BlockVersion::V3_1_VERSION)
        {
            // check parent dir to get type
            auto baseNameEntry = _executive->storage().getRow(parentDir, baseName);
            if (baseName == tool::FS_ROOT)
            {
                // root special logic
                Entry entry;
                tool::BfsFileFactory::buildDirEntry(entry, FS_TYPE_DIR);
                baseNameEntry = std::make_optional<Entry>(entry);
            }
            if (!baseNameEntry) [[unlikely]]
            {
                // maybe hidden table
                PRECOMPILED_LOG(DEBUG)
                    << LOG_BADGE("BFSPrecompiled") << LOG_DESC("list not exist file")
                    << LOG_KV("absolutePath", absolutePath);
                _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_NOT_EXIST), files));
                return;
            }
            auto baseFields = baseNameEntry->getObject<std::vector<std::string>>();
            if (baseFields[0] == tool::FS_TYPE_DIR)
            {
                // if type is dir, then return sub resource
                auto keyCondition = std::make_optional<storage::Condition>();
                // max return is 500
                keyCondition->limit(0, USER_TABLE_MAX_LIMIT_COUNT);
                auto keys = _executive->storage().getPrimaryKeys(absolutePath, keyCondition);
                for (const auto& key : keys | RANGES::views::all)
                {
                    auto entry = _executive->storage().getRow(absolutePath, key);
                    auto fields = entry->getObject<std::vector<std::string>>();
                    files.emplace_back(
                        key, fields[0], std::vector<std::string>{fields.begin() + 1, fields.end()});
                }
            }
            else if (baseFields[0] == tool::FS_TYPE_LINK)
            {
                // if type is link, then return link address
                auto addressEntry = _executive->storage().getRow(absolutePath, FS_LINK_ADDRESS);
                auto abiEntry = _executive->storage().getRow(absolutePath, FS_LINK_ABI);
                std::vector<std::string> ext = {std::string(addressEntry->getField(0)),
                    abiEntry.has_value() ? std::string(abiEntry->getField(0)) : ""};
                BfsTuple link =
                    std::make_tuple(baseName, std::string(FS_TYPE_LINK), std::move(ext));
                files.emplace_back(std::move(link));
            }
            else if (baseFields[0] == tool::FS_TYPE_CONTRACT)
            {
                // if type is contract, then return contract name
                files.emplace_back(baseName, tool::FS_TYPE_CONTRACT,
                    std::vector<std::string>{baseFields.begin() + 1, baseFields.end()});
            }
            _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS), files));
            return;
        }

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
                BfsTuple link =
                    std::make_tuple(baseName, std::string(FS_TYPE_LINK), std::move(ext));
                files.emplace_back(std::move(link));
            }
        }
        else
        {
            // fail to get type, this is contract
            BfsTuple file = std::make_tuple(
                baseName, std::string(FS_TYPE_CONTRACT), std::vector<std::string>({}));
            files.emplace_back(std::move(file));
        }

        _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS), files));
    }
    else
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("list not exist file")
                               << LOG_KV("absolutePath", absolutePath);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_NOT_EXIST), files));
    }
}

void BFSPrecompiled::listDirPage(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    // list(string,uint,uint)
    std::string absolutePath;
    u256 offset = 0;
    u256 count = 0;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), absolutePath, offset, count);
    std::vector<BfsTuple> files = {};
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("ls path")
                           << LOG_KV("path", absolutePath) << LOG_KV("offset", offset)
                           << LOG_KV("count", count);
    if (!checkPathValid(absolutePath, blockContext.blockVersion()))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("invalid path name")
                               << LOG_KV("path", absolutePath);
        _callParameters->setExecResult(codec.encode(s256((int)CODE_FILE_INVALID_PATH), files));
        return;
    }
    auto table = _executive->storage().openTable(absolutePath);

    if (!table)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("list not exist file")
                               << LOG_KV("absolutePath", absolutePath);
        _callParameters->setExecResult(codec.encode(s256((int)CODE_FILE_NOT_EXIST), files));
        return;
    }

    // exist
    auto [parentDir, baseName] = getParentDirAndBaseName(absolutePath);

    // check parent dir to get type
    auto baseNameEntry = _executive->storage().getRow(parentDir, baseName);
    if (baseName == tool::FS_ROOT)
    {
        baseNameEntry = std::make_optional<Entry>();
        tool::BfsFileFactory::buildDirEntry(baseNameEntry.value(), tool::FileType::DIRECTOR);
    }
    if (!baseNameEntry) [[unlikely]]
    {
        // maybe hidden table
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("list not exist file")
                               << LOG_KV("parentDir", parentDir) << LOG_KV("baseName", baseName);
        _callParameters->setExecResult(codec.encode(s256((int)CODE_FILE_NOT_EXIST), files));
        return;
    }
    auto baseFields = baseNameEntry->getObject<std::vector<std::string>>();
    if (baseFields[0] == tool::FS_TYPE_DIR)
    {
        // if type is dir, then return sub resource
        auto keyCondition = std::make_optional<storage::Condition>();
        keyCondition->limit((size_t)offset, (size_t)count);
        auto keys = _executive->storage().getPrimaryKeys(absolutePath, keyCondition);

        for (const auto& key : keys | RANGES::views::all)
        {
            auto entry = _executive->storage().getRow(absolutePath, key);
            auto fields = entry->getObject<std::vector<std::string>>();
            files.emplace_back(
                key, fields[0], std::vector<std::string>{fields.begin() + 1, fields.end()});
        }
        if (count == keys.size())
        {
            // count is full, maybe still left elements
            auto [total, error] = _executive->storage().count(absolutePath);
            if (error)
            {
                PRECOMPILED_LOG(DEBUG)
                    << LOG_BADGE("BFSPrecompiled") << LOG_DESC("list not exist file")
                    << LOG_KV("absolutePath", absolutePath);
                _callParameters->setExecResult(
                    codec.encode(s256((int)CODE_FILE_COUNT_ERROR), std::vector<BfsTuple>{}));
                return;
            }
            auto result = codec.encode(s256(total - offset - count), files);
            if (c_fileLogLevel == LogLevel::TRACE)
            {
                PRECOMPILED_LOG(TRACE)
                    << LOG_BADGE("BFSPrecompiled") << LOG_DESC("list trace")
                    << LOG_KV("filesSize", files.size()) << LOG_KV("resultDataSize", result.size());
            }
            _callParameters->setExecResult(std::move(result));
            return;
        }
    }
    else if (baseFields[0] == tool::FS_TYPE_LINK)
    {
        // if type is link, then return link address
        auto addressEntry = _executive->storage().getRow(absolutePath, FS_LINK_ADDRESS);
        auto abiEntry = _executive->storage().getRow(absolutePath, FS_LINK_ABI);
        std::vector<std::string> ext = {std::string(addressEntry->getField(0)),
            abiEntry.has_value() ? std::string(abiEntry->getField(0)) : ""};
        BfsTuple link = std::make_tuple(baseName, std::string(FS_TYPE_LINK), std::move(ext));
        files.emplace_back(std::move(link));
    }
    else if (baseFields[0] == tool::FS_TYPE_CONTRACT)
    {
        // if type is contract, then return contract name
        files.emplace_back(baseName, tool::FS_TYPE_CONTRACT,
            std::vector<std::string>{baseFields.begin() + 1, baseFields.end()});
    }
    auto result = codec.encode(s256((int)CODE_SUCCESS), files);
    if (c_fileLogLevel <= LogLevel::TRACE)
    {
        PRECOMPILED_LOG(TRACE) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("list trace")
                               << LOG_KV("filesSize", files.size())
                               << LOG_KV("resultDataSize", result.size());
    }
    _callParameters->setExecResult(std::move(result));
}

void BFSPrecompiled::link(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    std::string absolutePath, contractAddress, contractAbi;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), absolutePath, contractAddress, contractAbi);

    if (isShardPath(absolutePath) &&
        blockContext.blockVersion() >= (uint32_t)(bcos::protocol::BlockVersion::V3_3_VERSION))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled")
                               << LOG_DESC(
                                      "check link params failed, normal BFS operation could not "
                                      "link a contract to shard")
                               << LOG_KV("absolutePath", absolutePath);
        _callParameters->setExecResult(codec.encode(s256((int)CODE_FILE_INVALID_PATH)));
        return;
    }

    linkImpl(absolutePath, contractAddress, contractAbi, _executive, _callParameters);
}

void BFSPrecompiled::linkImpl(const std::string& _absolutePath, const std::string& _contractAddress,
    const std::string& _contractAbi,
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());

    std::string contractAddress = _contractAddress;
    if (!blockContext.isWasm())
    {
        contractAddress = trimHexPrefix(contractAddress);
    }

    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BFSPrecompiled")
                           << LOG_DESC("link") << LOG_KV("absolutePath", _absolutePath)
                           << LOG_KV("contractAddress", contractAddress)
                           << LOG_KV("contractAbiSize", _contractAbi.size());
    auto linkTableName = getContractTableName(getLinkRootDir(), _absolutePath);

    if (!checkPathValid(linkTableName, blockContext.blockVersion()))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled")
                               << LOG_DESC("check link params failed, invalid path name")
                               << LOG_KV("absolutePath", _absolutePath)
                               << LOG_KV("contractAddress", contractAddress);
        _callParameters->setExecResult(codec.encode(s256((int)CODE_FILE_INVALID_PATH)));
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
            tool::BfsFileFactory::buildLink(
                linkTable.value(), contractAddress, _contractAbi, blockContext.blockVersion());
            _callParameters->setExecResult(codec.encode(s256((int)CODE_SUCCESS)));
            return;
        }
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("File already exists.")
                               << LOG_KV("absolutePath", _absolutePath);
        _callParameters->setExecResult(codec.encode(s256((int)CODE_FILE_ALREADY_EXIST)));
        return;
    }
    // table not exist, mkdir -p /apps/contractName first
    std::string bfsAddress = getThisAddress(blockContext.isWasm());

    // Will goes to BFSPrecompiled or ShardingPrecompiled according with getThisAddress()
    auto response = externalTouchNewFile(_executive, _callParameters->m_origin, bfsAddress,
        getThisAddress(blockContext.isWasm()), linkTableName, FS_TYPE_LINK,
        _callParameters->m_gasLeft);
    if (response != 0)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled")
                               << LOG_DESC("external build link file metadata failed")
                               << LOG_KV("absolutePath", _absolutePath);
        _callParameters->setExecResult(codec.encode(s256((int)CODE_FILE_BUILD_DIR_FAILED)));
        return;
    }
    auto newLinkTable =
        _executive->storage().createTable(linkTableName, std::string(STORAGE_VALUE));
    // set link info to link table
    tool::BfsFileFactory::buildLink(
        newLinkTable.value(), contractAddress, _contractAbi, blockContext.blockVersion());
    _callParameters->setExecResult(codec.encode(s256((int)CODE_SUCCESS)));
}

void BFSPrecompiled::linkAdaptCNS(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    std::string contractName, contractVersion, contractAddress, contractAbi;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(
        _callParameters->params(), contractName, contractVersion, contractAddress, contractAbi);
    if (!blockContext.isWasm())
    {
        contractAddress = trimHexPrefix(contractAddress);
    }

    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BFSPrecompiled")
                           << LOG_DESC("link") << LOG_KV("contractName", contractName)
                           << LOG_KV("contractVersion", contractVersion)
                           << LOG_KV("contractAddress", contractAddress)
                           << LOG_KV("contractAbiSize", contractAbi.size());
    int validCode =
        checkLinkParam(_executive, contractAddress, contractName, contractVersion, contractAbi);
    auto linkTableName = std::string(USER_APPS_PREFIX) + contractName + '/' + contractVersion;

    if (validCode < 0 || !checkPathValid(linkTableName, blockContext.blockVersion()))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled")
                               << LOG_DESC("check link params failed, invalid path name")
                               << LOG_KV("contractName", contractName)
                               << LOG_KV("contractVersion", contractVersion)
                               << LOG_KV("contractAddress", contractAddress);
        _callParameters->setExecResult(
            codec.encode(int32_t(validCode < 0 ? validCode : CODE_FILE_INVALID_PATH)));
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
            _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
            return;
        }
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("File already exists.")
                               << LOG_KV("contractName", contractName)
                               << LOG_KV("version", contractVersion);
        _callParameters->setExecResult(codec.encode((int32_t)CODE_FILE_ALREADY_EXIST));
        return;
    }
    // table not exist, mkdir -p /apps/contractName first
    std::string bfsAddress = getThisAddress(blockContext.isWasm());

    auto response = externalTouchNewFile(_executive, _callParameters->m_origin, bfsAddress,
        getThisAddress(blockContext.isWasm()), linkTableName, FS_TYPE_LINK,
        _callParameters->m_gasLeft);
    if (response != 0)
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled")
                               << LOG_DESC("external build link file metadata failed")
                               << LOG_KV("contractName", contractName)
                               << LOG_KV("contractVersion", contractVersion);
        _callParameters->setExecResult(codec.encode((int32_t)CODE_FILE_BUILD_DIR_FAILED));
        return;
    }
    auto newLinkTable =
        _executive->storage().createTable(linkTableName, std::string(STORAGE_VALUE));
    // set link info to link table
    tool::BfsFileFactory::buildLink(newLinkTable.value(), contractAddress, contractAbi,
        blockContext.blockVersion(), contractVersion);
    _callParameters->setExecResult(codec.encode((int32_t)CODE_SUCCESS));
}

void BFSPrecompiled::readLink(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    // readlink(string)
    std::string absolutePath;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), absolutePath);
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("readLink path")
                           << LOG_KV("path", absolutePath);
    bytes emptyResult =
        blockContext.isWasm() ? codec.encode(std::string("")) : codec.encode(Address());
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
            auto codecAddress = blockContext.isWasm() ? codec.encode(contractAddress) :
                                                        codec.encode(Address(contractAddress));
            _callParameters->setExecResult(codecAddress);
            return;
        }

        _callParameters->setExecResult(emptyResult);
        return;
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled")
                           << LOG_DESC("link file not exist, return empty address")
                           << LOG_KV("path", absolutePath);
    _callParameters->setExecResult(emptyResult);
}

bool BFSPrecompiled::checkPathPrefixValid(
    const std::string_view& path, uint32_t blockVersion, const std::string_view& type)
{
    if (!path.starts_with(USER_APPS_PREFIX) && !path.starts_with(USER_TABLE_PREFIX))
    {
        if (blockVersion >= (uint32_t)(bcos::protocol::BlockVersion::V3_1_VERSION) &&
            path.starts_with(USER_USR_PREFIX))
        {
            PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("touch /usr/ file")
                                   << LOG_KV("absolutePath", path) << LOG_KV("type", type);
        }
        else
        {
            PRECOMPILED_LOG(DEBUG)
                << LOG_BADGE("BFSPrecompiled")
                << LOG_DESC(
                       "only support touch file under the system dir /apps/, /tables/, /shards/")
                << LOG_KV("absolutePath", path) << LOG_KV("type", type);
            return false;
        }
    }
    return true;
}

void BFSPrecompiled::touch(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    // touch(string absolute, string type)
    std::string absolutePath;
    std::string type;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), absolutePath, type);
    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("BFSPrecompiled")
                           << LOG_DESC("touch new file") << LOG_KV("absolutePath", absolutePath)
                           << LOG_KV("type", type);
    if (!checkPathValid(absolutePath, blockContext.blockVersion()))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("file name is invalid");

        _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_INVALID_PATH)));
        return;
    }
    if (BfsTypeSet.find(type) == BfsTypeSet.end())
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled")
                               << LOG_DESC("touch file in error type")
                               << LOG_KV("absolutePath", absolutePath) << LOG_KV("type", type);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_INVALID_TYPE)));
        return;
    }

    if (_callParameters->m_origin != ACCOUNT_ADDRESS)
    {
        // if comming from accountPrecompiled, check path prefix
        if (!checkPathPrefixValid(absolutePath, blockContext.blockVersion(), type))
        {
            _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_INVALID_PATH)));
            return;
        }
    }

    std::string parentDir;
    std::string baseName;
    if (type == FS_TYPE_DIR)
    {
        parentDir = absolutePath;
    }
    else
    {
        std::tie(parentDir, baseName) = getParentDirAndBaseName(absolutePath);
    }
    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled")
                           << LOG_DESC("directory not exists, build dir first")
                           << LOG_KV("parentDir", parentDir) << LOG_KV("baseName", baseName)
                           << LOG_KV("type", type);
    auto buildResult = recursiveBuildDir(_executive, parentDir);
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
    if (blockContext.blockVersion() >= (uint32_t)BlockVersion::V3_1_VERSION)
    {
        Entry subEntry;
        tool::BfsFileFactory::buildDirEntry(subEntry, type);
        _executive->storage().setRow(parentDir, baseName, std::move(subEntry));

        _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
    }
    else
    {
        std::map<std::string, std::string> bfsInfo;
        auto subEntry = _executive->storage().getRow(parentDir, FS_KEY_SUB);
        auto&& out = asBytes(std::string(subEntry->getField(0)));
        codec::scale::decode(bfsInfo, gsl::make_span(out));
        bfsInfo.insert(std::make_pair(baseName, type));
        subEntry->setField(0, asString(codec::scale::encode(bfsInfo)));
        _executive->storage().setRow(parentDir, FS_KEY_SUB, std::move(subEntry.value()));

        _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
    }
}

void BFSPrecompiled::initBfs(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    PRECOMPILED_LOG(INFO) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("initBfs");
    const auto& blockContext = _executive->blockContext();
    auto table = _executive->storage().openTable(tool::FS_ROOT);
    if (table.has_value())
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("BFSPrecompiled")
                              << LOG_DESC("initBfs, root table already exist, return by default");
        return;
    }
    // create / dir
    _executive->storage().createTable(std::string(tool::FS_ROOT), std::string(tool::FS_DIR_FIELDS));
    // build root subs metadata
    for (const auto& subName : tool::FS_ROOT_SUBS | RANGES::views::drop(1))
    {
        Entry entry;
        // type, status, acl_type, acl_white, acl_black, extra
        tool::BfsFileFactory::buildDirEntry(entry, tool::FileType::DIRECTOR);
        _executive->storage().setRow(tool::FS_ROOT,
            subName == tool::FS_ROOT ? subName : subName.substr(1), std::move(entry));
    }
    // build apps, usr, tables metadata
    _executive->storage().createTable(std::string(tool::FS_USER), std::string(tool::FS_DIR_FIELDS));
    _executive->storage().createTable(std::string(tool::FS_APPS), std::string(tool::FS_DIR_FIELDS));
    _executive->storage().createTable(
        std::string(tool::FS_USER_TABLE), std::string(tool::FS_DIR_FIELDS));
    _executive->storage().createTable(
        std::string(tool::FS_SYS_BIN), std::string(tool::FS_DIR_FIELDS));

    // build /sys/
    buildSysSubs(_executive, blockContext.blockVersion());
}

void BFSPrecompiled::rebuildBfs(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    if (_callParameters->m_sender != precompiled::SYS_CONFIG_ADDRESS &&
        _callParameters->m_sender != precompiled::SYS_CONFIG_NAME)
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("BFSPrecompiled")
                              << LOG_DESC("rebuildBfs not called by sys config")
                              << LOG_KV("sender", _callParameters->m_sender);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_NO_AUTHORIZED)));
        return;
    }
    uint32_t fromVersion = 0;
    uint32_t toVersion = 0;
    codec.decode(_callParameters->params(), fromVersion, toVersion);
    PRECOMPILED_LOG(INFO) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("rebuildBfs")
                          << LOG_KV("fromVersion", fromVersion) << LOG_KV("toVersion", toVersion);
    if (fromVersion <= static_cast<uint32_t>(BlockVersion::V3_0_VERSION) &&
        toVersion >= static_cast<uint32_t>(BlockVersion::V3_1_VERSION))
    {
        rebuildBfs310(_executive);
    }
    _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
}

void BFSPrecompiled::fixBfs(const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    u256 fixVersion = 0;
    codec.decode(_callParameters->params(), fixVersion);
    PRECOMPILED_LOG(INFO) << LOG_BADGE("BFSPrecompiled") << LOG_DESC("fixBfs")
                          << LOG_KV("fixVersion", fixVersion);
    if (fixVersion == static_cast<uint32_t>(BlockVersion::V3_3_VERSION)) [[likely]]
    {
        fixBfs330(_executive);
    }
    else [[unlikely]]
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("BFSPrecompiled")
                              << LOG_DESC("fixBfs version not supported")
                              << LOG_KV("fixVersion", fixVersion)
                              << LOG_KV("blockVersion", blockContext.blockVersion());
        BOOST_THROW_EXCEPTION(PrecompiledError("BFSPrecompiled call undefined function!"));
    }
    _callParameters->setExecResult(codec.encode(int32_t(CODE_SUCCESS)));
}

void BFSPrecompiled::fixBfs330(const std::shared_ptr<executor::TransactionExecutive>& _executive)
{
    const auto& blockContext = _executive->blockContext();
    auto keyPageIgnoreTables = blockContext.keyPageIgnoreTables();
    auto backendStorage = blockContext.backendStorage();
    if (backendStorage == nullptr) [[unlikely]]
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("BFSPrecompiled")
                               << LOG_DESC("fixBfs320 backendStorage is null");
        BOOST_THROW_EXCEPTION(PrecompiledError("BFSPrecompiled fixBfs320 backendStorage is null."));
    }
    auto existEntries = _executive->storage().getRows(tool::FS_ROOT, tool::FS_ROOT_SUBS_NAME);
    if (std::all_of(existEntries.begin(), existEntries.end(),
            [](const auto& entry) { return entry.has_value(); }))
    {
        PRECOMPILED_LOG(INFO) << LOG_BADGE("BFSPrecompiled")
                              << LOG_DESC("BFS logical looks good, skip fix.");
        return;
    }

    for (const auto& dir : tool::FS_ROOT_SUBS)
    {
        std::string sysDir = std::string(dir);
        std::vector<std::string> keys;
        if (dir == tool::FS_ROOT)
        {
            keys.assign(tool::FS_ROOT_SUBS_NAME.begin(), tool::FS_ROOT_SUBS_NAME.end());
        }
        else
        {
            storage::Condition condition{};
            condition.startsWith(sysDir + "/");
            std::promise<std::vector<std::string>> promise;
            blockContext.backendStorage()->asyncGetPrimaryKeys(
                "s_tables", condition, [&promise](auto&& error, auto&& keys) {
                    if (error)
                    {
                        PRECOMPILED_LOG(ERROR) << LOG_BADGE("BFSPrecompiled")
                                               << LOG_DESC("fixBfs320 asyncGetPrimaryKeys error")
                                               << LOG_KV("code", error->errorCode())
                                               << LOG_KV("message", error->errorMessage());
                        BOOST_THROW_EXCEPTION(PrecompiledError(
                            "BFSPrecompiled fixBfs320 asyncGetPrimaryKeys failed."));
                    }
                    promise.set_value(std::forward<decltype(keys)>(keys));
                });
            keys = promise.get_future().get();
        }
        std::unordered_set<std::string> tablePathFilter;
        // read without keyPage

        for (const auto& key : keys)
        {
            auto index = key.find_first_of('/', dir.size() + 1);
            auto subPath =
                (dir == tool::FS_ROOT) ?
                    key :
                    key.substr(dir.size() + 1,
                        index == std::string::npos ? std::string::npos : index - dir.size() - 1);
            if (subPath.empty() || tablePathFilter.contains(subPath))
            {
                continue;
            }
            std::promise<std::optional<storage::Entry>> getRowPromise;
            backendStorage->asyncGetRow(dir, subPath, [&getRowPromise](auto&& error, auto&& entry) {
                if (error)
                {
                    PRECOMPILED_LOG(ERROR)
                        << LOG_BADGE("BFSPrecompiled") << LOG_DESC("fixBfs320 asyncGetRow error")
                        << LOG_KV("code", error->errorCode())
                        << LOG_KV("message", error->errorMessage());
                    BOOST_THROW_EXCEPTION(
                        PrecompiledError("BFSPrecompiled fixBfs320 asyncGetRow failed."));
                }
                getRowPromise.set_value(std::forward<decltype(entry)>(entry));
            });
            auto entry = getRowPromise.get_future().get();
            if (!entry.has_value()) [[unlikely]]
            {
                continue;
            }
            keyPageIgnoreTables->insert(sysDir);
            Entry deletedEntry;
            deletedEntry.setStatus(Entry::Status::DELETED);
            _executive->storage().setRow(dir, subPath, std::move(deletedEntry));
            keyPageIgnoreTables->erase(sysDir);
            // write in keyPage
            _executive->storage().setRow(dir, subPath, entry.value());
            tablePathFilter.insert(subPath);
        }
    }
}

void BFSPrecompiled::rebuildBfs310(
    const std::shared_ptr<executor::TransactionExecutive>& _executive)
{
    const auto& blockContext = _executive->blockContext();
    auto keyPageIgnoreTables = blockContext.keyPageIgnoreTables();
    // child, parent, all absolute path
    std::queue<std::pair<std::string, std::string>> rebuildQ;
    rebuildQ.push({std::string(tool::FS_ROOT), ""});
    bool rebuildSys = true;
    while (!rebuildQ.empty())
    {
        keyPageIgnoreTables->insert(tool::FS_ROOT_SUBS.begin(), tool::FS_ROOT_SUBS.end());
        auto [rebuildPath, parentPath] = rebuildQ.front();
        rebuildQ.pop();
        auto subEntry = _executive->storage().getRow(rebuildPath, tool::FS_KEY_SUB);
        auto table = _executive->storage().openTable(rebuildPath);
        if (!subEntry.has_value() || table->tableInfo()->fields().size() > 1)
        {
            // not old data structure
            // root is new data structure
            rebuildSys = (rebuildPath != tool::FS_ROOT);
            continue;
        }

        // rewrite type, acl_type, acl_white, acl_black, extra to parent
        auto typeEntry = _executive->storage().getRow(rebuildPath, tool::FS_KEY_TYPE);
        auto aclTypeEntry = _executive->storage().getRow(rebuildPath, tool::FS_ACL_TYPE);
        auto aclWhiteEntry = _executive->storage().getRow(rebuildPath, tool::FS_ACL_WHITE);
        auto aclBlackEntry = _executive->storage().getRow(rebuildPath, tool::FS_ACL_BLACK);
        auto extraEntry = _executive->storage().getRow(rebuildPath, tool::FS_KEY_EXTRA);
        // root has no parent
        Entry newFormEntry;
        newFormEntry.setObject(std::vector<std::string>{
            std::string(typeEntry->get()),
            std::string("0"),
            std::string(aclTypeEntry->get()),
            std::string(aclWhiteEntry->get()),
            std::string(aclBlackEntry->get()),
            std::string(extraEntry->get()),
        });
        typeEntry->setStatus(Entry::Status::DELETED);
        aclTypeEntry->setStatus(Entry::Status::DELETED);
        aclWhiteEntry->setStatus(Entry::Status::DELETED);
        aclBlackEntry->setStatus(Entry::Status::DELETED);
        extraEntry->setStatus(Entry::Status::DELETED);
        _executive->storage().setRow(rebuildPath, tool::FS_KEY_TYPE, std::move(typeEntry.value()));
        _executive->storage().setRow(
            rebuildPath, tool::FS_ACL_TYPE, std::move(aclTypeEntry.value()));
        _executive->storage().setRow(
            rebuildPath, tool::FS_ACL_WHITE, std::move(aclWhiteEntry.value()));
        _executive->storage().setRow(
            rebuildPath, tool::FS_ACL_BLACK, std::move(aclBlackEntry.value()));
        _executive->storage().setRow(
            rebuildPath, tool::FS_KEY_EXTRA, std::move(extraEntry.value()));

        std::map<std::string, std::string> bfsInfo;
        auto&& out = asBytes(std::string(subEntry->get()));
        codec::scale::decode(bfsInfo, gsl::make_span(out));

        // delete sub
        subEntry->setStatus(Entry::Status::DELETED);
        _executive->storage().setRow(rebuildPath, tool::FS_KEY_SUB, std::move(subEntry.value()));

        // use keyPage to rewrite info
        for (const auto& _sub : tool::FS_ROOT_SUBS)
        {
            std::string sub(_sub);
            keyPageIgnoreTables->erase(sub);
        }
        if (!parentPath.empty())
        {
            _executive->storage().setRow(
                parentPath, getPathBaseName(rebuildPath), std::move(newFormEntry));
        }

        // rewrite sub info
        for (const auto& [name, type] : bfsInfo)
        {
            Entry entry;
            tool::BfsFileFactory::buildDirEntry(entry, type);
            _executive->storage().setRow(rebuildPath, name, std::move(entry));
            if (type == tool::FS_TYPE_DIR)
            {
                rebuildQ.push(
                    {(rebuildPath == tool::FS_ROOT ? rebuildPath : rebuildPath + "/") + name,
                        rebuildPath});
            }
        }
    }
    if (rebuildSys)
    {
        // build /sys/
        buildSysSubs(_executive, BlockVersion::V3_1_VERSION);
    }
}

bool BFSPrecompiled::recursiveBuildDir(
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
    if (absoluteDir.starts_with('/'))
    {
        absoluteDir = absoluteDir.substr(1);
    }
    if (absoluteDir.ends_with('/'))
    {
        absoluteDir.pop_back();
    }
    boost::split(dirList, absoluteDir, boost::is_any_of("/"), boost::token_compress_on);
    std::string root = "/";

    auto version = _executive->blockContext().blockVersion();
    for (const auto& dir : dirList)
    {
        auto table = _executive->storage().openTable(root);
        if (!table)
        {
            EXECUTIVE_LOG(TRACE) << LOG_BADGE("recursiveBuildDir")
                                 << LOG_DESC("can not open path table")
                                 << LOG_KV("tableName", root);
            return false;
        }
        auto newTableName = ((root == "/") ? root : (root + "/")).append(dir);

        if (version >= (uint32_t)BlockVersion::V3_1_VERSION)
        {
            auto dirEntry = _executive->storage().getRow(root, dir);
            if (!dirEntry)
            {
                // not exist, then set row to root, create dir
                Entry newEntry;
                // type, status, acl_type, acl_white, acl_black, extra
                tool::BfsFileFactory::buildDirEntry(newEntry, tool::FileType::DIRECTOR);
                _executive->storage().setRow(root, dir, std::move(newEntry));

                _executive->storage().createTable(newTableName, std::string(tool::FS_DIR_FIELDS));
                root = newTableName;
                continue;
            }
            else
            {
                auto dirFields = dirEntry->getObject<std::vector<std::string>>();
                if (dirFields[0] == tool::FS_TYPE_DIR)
                {
                    // if dir is directory, continue
                    root = newTableName;
                    continue;
                }
                // exist in root, it means this dir is not a directory
                EXECUTIVE_LOG(DEBUG) << LOG_BADGE("recursiveBuildDir")
                                     << LOG_DESC("file had already existed, and not directory type")
                                     << LOG_KV("parentDir", root) << LOG_KV("dir", dir)
                                     << LOG_KV("type", dirFields[0]);
                return false;
            }
        }

        // if version < 3.0.0
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

                // can not get type, it means this dir is not a directory
                EXECUTIVE_LOG(DEBUG) << LOG_BADGE("recursiveBuildDir")
                                     << LOG_DESC("file had already existed, and not directory type")
                                     << LOG_KV("parentDir", root) << LOG_KV("dir", dir);
                return false;
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
            EXECUTIVE_LOG(TRACE) << LOG_BADGE("recursiveBuildDir")
                                 << LOG_DESC("parent type not found") << LOG_KV("parentDir", root)
                                 << LOG_KV("dir", dir);
            return false;
        }
    }
    return true;
}