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
 * @file ShardPrecompiled.cpp
 * @author: JimmyShi22
 * @date 2022-12-27
 */

#include "ShardingPrecompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include "bcos-executor/src/precompiled/common/PrecompiledResult.h"
#include "bcos-executor/src/precompiled/common/Utilities.h"
#include "bcos-table/src/ContractShardUtils.h"
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/protocol/Protocol.h>
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

constexpr const char* const FILE_SYSTEM_METHOD_MK_SHARD = "makeShard(string)";
constexpr const char* const FILE_SYSTEM_METHOD_LINK_SHARD = "linkShard(string,string)";
constexpr const char* const FILE_SYSTEM_SET_CONTRACT_SHARD_INTERNAL =
    "setShardInternal(string,string)";
constexpr const char* const FILE_SYSTEM_GET_CONTRACT_SHARD = "getContractShard(string)";
constexpr const char* const FILE_SYSTEM_GET_CONTRACT_SHARD_INTERNAL = "getShardInternal(string)";

ShardingPrecompiled::ShardingPrecompiled(crypto::Hash::Ptr _hashImpl) : BFSPrecompiled(_hashImpl)
{
    name2Selector[FILE_SYSTEM_METHOD_MK_SHARD] =
        getFuncSelector(FILE_SYSTEM_METHOD_MK_SHARD, _hashImpl);
    name2Selector[FILE_SYSTEM_METHOD_LINK_SHARD] =
        getFuncSelector(FILE_SYSTEM_METHOD_LINK_SHARD, _hashImpl);
    name2Selector[FILE_SYSTEM_SET_CONTRACT_SHARD_INTERNAL] =
        getFuncSelector(FILE_SYSTEM_SET_CONTRACT_SHARD_INTERNAL, _hashImpl);
    name2Selector[FILE_SYSTEM_GET_CONTRACT_SHARD] =
        getFuncSelector(FILE_SYSTEM_GET_CONTRACT_SHARD, _hashImpl);
    name2Selector[FILE_SYSTEM_GET_CONTRACT_SHARD_INTERNAL] =
        getFuncSelector(FILE_SYSTEM_GET_CONTRACT_SHARD_INTERNAL, _hashImpl);
}

inline bool isFromThisOrSDK(
    PrecompiledExecResult::Ptr _callParameters, const std::string& thisAddress)
{
    return _callParameters->m_origin == _callParameters->m_sender ||
           _callParameters->m_sender == thisAddress || _callParameters->m_staticCall;
}

inline bool isFromThis(PrecompiledExecResult::Ptr _callParameters, const std::string& thisAddress)
{
    return _callParameters->m_sender == thisAddress || _callParameters->m_staticCall;
}

inline bool isSuccess(PrecompiledExecResult::Ptr _callParameters, CodecWrapper& codec)
{
    s256 m;
    codec.decode(ref(_callParameters->execResult()), m);

    return m == s256((int)CODE_SUCCESS);
}

inline bool isFromThisOrGovernors(std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters, const std::string& thisAddress)
{
    if (isFromThis(_callParameters, thisAddress))
    {
        return true;
    }

    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());

    auto governors = getGovernorList(_executive, _callParameters, codec);
    return (RANGES::find_if(governors, [&_callParameters](const Address& address) {
        return address.hex() == _callParameters->m_sender;
    }) != governors.end());
}

std::shared_ptr<PrecompiledExecResult> ShardingPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    const auto& blockContext = _executive->blockContext();
    if (!isFromThisOrSDK(_callParameters, getThisAddress(blockContext.isWasm())))
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("ShardPrecompiled")
                                 << LOG_DESC("call: request should only call from SDK")
                                 << LOG_KV("origin", _callParameters->m_origin)
                                 << LOG_KV("sender", _callParameters->m_sender);

        BOOST_THROW_EXCEPTION(
            PrecompiledError("ShardPrecompiled call: request should only call from SDK"));
    }

    if (blockContext.isAuthCheck() &&
        !isFromThisOrGovernors(_executive, _callParameters, getThisAddress(blockContext.isWasm())))
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("ShardPrecompiled")
                                 << LOG_DESC("call: Permission denied.")
                                 << LOG_KV("origin", _callParameters->m_origin)
                                 << LOG_KV("sender", _callParameters->m_sender);

        BOOST_THROW_EXCEPTION(PrecompiledError("ShardPrecompiled call: Permission denied."));
    }

    uint32_t func = getParamFunc(_callParameters->input());

    if (func == name2Selector[FILE_SYSTEM_METHOD_MK_SHARD])
    {
        makeShard(_executive, _callParameters);
    }
    else if (func == name2Selector[FILE_SYSTEM_METHOD_LINK_SHARD])
    {
        linkShard(_executive, _callParameters);
    }
    else if (func == name2Selector[FILE_SYSTEM_SET_CONTRACT_SHARD_INTERNAL])
    {
        handleSetContractShard(_executive, _callParameters);
    }
    else if (func == name2Selector[FILE_SYSTEM_GET_CONTRACT_SHARD])
    {
        getContractShard(_executive, _callParameters);
    }
    else if (func == name2Selector[FILE_SYSTEM_GET_CONTRACT_SHARD_INTERNAL])
    {
        handleGetContractShard(_executive, _callParameters);
    }
    else
    {
        if (isFromThis(_callParameters, getThisAddress(blockContext.isWasm())))
        {
            return BFSPrecompiled::call(_executive, _callParameters);
        }
        else
        {
            PRECOMPILED_LOG(WARNING)
                << LOG_BADGE("ShardPrecompiled")
                << LOG_DESC("call: BFS request should only call from ShardPrecompiled")
                << LOG_KV("origin", _callParameters->m_origin)
                << LOG_KV("sender", _callParameters->m_sender);

            BOOST_THROW_EXCEPTION(PrecompiledError(
                "ShardPrecompiled call: BFS request should only call from ShardPrecompiled"));
        }
    }

    return _callParameters;
}


void ShardingPrecompiled::makeShard(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    // makeShard(string)
    std::string shardName;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), shardName);

    if (shardName.find("/") != std::string::npos)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("ShardPrecompiled")
                                 << LOG_DESC(
                                        "makeShard: Shard name should not be a path, please check")
                                 << LOG_KV("shardName", shardName);
        BOOST_THROW_EXCEPTION(
            PrecompiledError("makeShard: Shard name should not be a path, please check"));
        return;
    }

    std::string absolutePath = std::string(USER_SHARD_PREFIX) + shardName;

    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("ShardPrecompiled")
                          << LOG_KV("mkShard", absolutePath);

    BFSPrecompiled::makeDirImpl(absolutePath, _executive, _callParameters);

    if (!isSuccess(_callParameters, codec))
    {
        std::string message;
        if (codec.encode(int32_t(CODE_FILE_ALREADY_EXIST)) == _callParameters->execResult())
        {
            message = "shard '" + shardName + "' has already exists";
        }
        else
        {
            int32_t code;
            codec.decode(ref(_callParameters->execResult()), code);
            message = "errorCode: " + std::to_string(code);
        }

        PRECOMPILED_LOG(WARNING) << LOG_BADGE("ShardPrecompiled") << LOG_DESC("BFS makeDir error: ")
                                 << message;
        BOOST_THROW_EXCEPTION(PrecompiledError("ShardPrecompiled BFS makeDir error: " + message));
    }
}

void ShardingPrecompiled::linkShard(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    // linkShard(string,string,string)
    std::string shardName, contractAddress;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), shardName, contractAddress);

    if (!blockContext.isWasm())
    {
        contractAddress = trimHexPrefix(contractAddress);
    }

    if (shardName.find("/") != std::string::npos)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("ShardPrecompiled")
                                 << LOG_DESC(
                                        "linkShard: Shard name should not be a path, please check")
                                 << LOG_KV("shardName", shardName);
        BOOST_THROW_EXCEPTION(
            PrecompiledError("linkShard: Shard name should not be a path, please check"));
        return;
    }

    if (!checkContractAddressValid(blockContext.isWasm(), contractAddress))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ShardPrecompiled")
                               << LOG_DESC("linkShard: invalid contract address")
                               << LOG_KV("contractAddress", contractAddress);

        BOOST_THROW_EXCEPTION(
            PrecompiledError("linkShard: invalid contract address: " + contractAddress));
        return;
    }

    auto tableName = getContractTableName(BFSPrecompiled::getLinkRootDir(), contractAddress);

    auto historyShard = ContractShardUtils::getContractShard(_executive->storage(), tableName);
    if (!historyShard.empty())
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ShardPrecompiled")
                               << LOG_DESC("linkShard: contract has already belongs to a shard")
                               << LOG_KV("contractAddress", contractAddress)
                               << LOG_KV("historyShard", historyShard);

        BOOST_THROW_EXCEPTION(PrecompiledError(
            "linkShard: contract has already belongs to a shard: " + historyShard));
        return;
    }

    std::string contractAbi = "";

    std::string absolutePath = shardName + "/" + contractAddress;

    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("ShardPrecompiled")
                          << LOG_DESC("linkShard") << LOG_KV("absolutePath", absolutePath)
                          << LOG_KV("contractAddress", contractAddress)
                          << LOG_KV("contractAbiSize", contractAbi.size());

    setContractShard(
        _executive, contractAddress, shardName, _callParameters);  // will throw exception if failed

    BFSPrecompiled::linkImpl(
        absolutePath, contractAddress, contractAbi, _executive, _callParameters);

    if (!isSuccess(_callParameters, codec))
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("ShardPrecompiled") << LOG_DESC("BFS link error");
        BOOST_THROW_EXCEPTION(PrecompiledError("ShardPrecompiled BFS link error"));
    }
}

void ShardingPrecompiled::getContractShard(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    // (errorCode, shardName) = getContractShard(string)
    std::string contractAddress;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), contractAddress);

    if (!blockContext.isWasm())
    {
        contractAddress = trimHexPrefix(contractAddress);
    }

    if (!checkContractAddressValid(blockContext.isWasm(), contractAddress))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ShardPrecompiled")
                               << LOG_DESC("getContractShard: invalid contract address")
                               << LOG_KV("contractAddress", contractAddress);

        BOOST_THROW_EXCEPTION(
            PrecompiledError("getContractShard: invalid contract address: " + contractAddress));
        return;
    }

    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("ShardPrecompiled")
                           << "getContractShard internalCall request"
                           << LOG_KV("contractAddress", contractAddress);
    // externalRequest
    bytes params = codec.encodeWithSig(FILE_SYSTEM_GET_CONTRACT_SHARD_INTERNAL, contractAddress);
    auto thisAddress = std::string(getThisAddress(blockContext.isWasm()));
    auto internalCallParams = codec.encode(thisAddress, params);

    auto response = externalRequest(_executive, ref(internalCallParams), _callParameters->m_origin,
        thisAddress, contractAddress, _callParameters->m_staticCall, false,
        _callParameters->m_gasLeft, true);

    _callParameters->setExternalResult(std::move(response));
}


void ShardingPrecompiled::handleGetContractShard(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    // getContractShard(string)
    std::string contractAddress;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), contractAddress);

    if (!blockContext.isWasm())
    {
        contractAddress = trimHexPrefix(contractAddress);
    }

    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("ShardPrecompiled")
                           << "handleGetContractShard"
                           << LOG_KV("contractAddress", contractAddress);

    auto tableName = getContractTableName(BFSPrecompiled::getLinkRootDir(), contractAddress);

    auto shardName = ContractShardUtils::getContractShard(_executive->storage(), tableName);
    _callParameters->setExecResult(codec.encode(s256(CODE_SUCCESS), shardName));
}

// only for internal call
void ShardingPrecompiled::setContractShard(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const std::string_view& contractAddress, const std::string_view& shardName,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    const auto& blockContext = _executive->blockContext();
    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext.number()) << LOG_BADGE("ShardPrecompiled")
                          << "setContractShard internalCall request"
                          << LOG_KV("contractAddress", contractAddress)
                          << LOG_KV("shardName", shardName);
    // externalRequest
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());

    bytes params = codec.encodeWithSig(FILE_SYSTEM_SET_CONTRACT_SHARD_INTERNAL,
        std::string(contractAddress), std::string(shardName));
    auto thisAddress = std::string(getThisAddress(blockContext.isWasm()));
    auto internalCallParams = codec.encode(thisAddress, params);

    auto response = externalRequest(_executive, ref(internalCallParams), _callParameters->m_origin,
        thisAddress, contractAddress, _callParameters->m_staticCall, false,
        _callParameters->m_gasLeft, true);

    _callParameters->setExternalResult(std::move(response));
}
void ShardingPrecompiled::handleSetContractShard(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    // setContractShard(string,string)
    std::string contractAddress, shardName;
    const auto& blockContext = _executive->blockContext();
    auto codec = CodecWrapper(blockContext.hashHandler(), blockContext.isWasm());
    codec.decode(_callParameters->params(), contractAddress, shardName);

    if (!blockContext.isWasm())
    {
        contractAddress = trimHexPrefix(contractAddress);
    }

    auto tableName = getContractTableName(BFSPrecompiled::getLinkRootDir(), contractAddress);
    ContractShardUtils::setContractShard(_executive->storage(), tableName, shardName);
}

bool ShardingPrecompiled::checkPathPrefixValid(
    const std::string_view& path, uint32_t blockVersion, const std::string_view& type)
{
    if (blockVersion >= (uint32_t)(bcos::protocol::BlockVersion::V3_3_VERSION) &&
        path.starts_with(USER_SHARD_PREFIX))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("BFSPrecompiled")
                               << LOG_DESC("touch " + std::string(USER_SHARD_PREFIX) + " file")
                               << LOG_KV("absolutePath", path) << LOG_KV("type", type);
        return true;
    }

    if (!BFSPrecompiled::checkPathPrefixValid(path, blockVersion, type))
    {
        PRECOMPILED_LOG(DEBUG)
            << LOG_BADGE("ShardingPrecompiled")
            << LOG_DESC("only support touch file under the system dir /apps/, /tables/, /shards/")
            << LOG_KV("absolutePath", path) << LOG_KV("type", type);
        return false;
    }
    return true;
}


bool ShardingPrecompiled::checkContractAddressValid(
    bool isWasm, const std::string& address, uint32_t blockVersion)
{
    if (!isWasm)
    {
        try
        {
            toAddress(address);
        }
        catch (...)
        {
            return false;
        }
    }

    return checkPathValid(address, blockVersion);
}