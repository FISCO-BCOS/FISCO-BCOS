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
#include "common/ContractShardUtils.h"
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
constexpr const char* const FILE_SYSTEM_METHOD_LINK = "linkShard(string,string,string)";
constexpr const char* const FILE_SYSTEM_SET_CONTRACT_SHARD_INTERNAL =
    "setShardInternal(string,string)";
constexpr const char* const FILE_SYSTEM_GET_CONTRACT_SHARD = "getContractShard(string)";
constexpr const char* const FILE_SYSTEM_GET_CONTRACT_SHARD_INTERNAL = "getShardInternal(string)";

ShardingPrecompiled::ShardingPrecompiled(crypto::Hash::Ptr _hashImpl) : BFSPrecompiled(_hashImpl)
{
    name2Selector[FILE_SYSTEM_METHOD_MK_SHARD] =
        getFuncSelector(FILE_SYSTEM_METHOD_MK_SHARD, _hashImpl);
    name2Selector[FILE_SYSTEM_METHOD_LINK] = getFuncSelector(FILE_SYSTEM_METHOD_LINK, _hashImpl);
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
           _callParameters->m_sender == thisAddress;
}

std::shared_ptr<PrecompiledExecResult> ShardingPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    auto blockContext = _executive->blockContext().lock();
    if (!isFromThisOrSDK(_callParameters, getThisAddress(blockContext->isWasm())))
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("ShardPrecompiled")
                                 << LOG_DESC("call: request should only call from SDK")
                                 << LOG_KV("origin", _callParameters->m_origin)
                                 << LOG_KV("sender", _callParameters->m_sender);
        auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
        _callParameters->setExecResult(codec.encode(int32_t(CODE_SENDER_ERROR)));
        return _callParameters;
    }

    uint32_t func = getParamFunc(_callParameters->input());

    if (func == name2Selector[FILE_SYSTEM_METHOD_MK_SHARD])
    {
        makeShard(_executive, _callParameters);
    }
    else if (func == name2Selector[FILE_SYSTEM_METHOD_LINK])
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
        return BFSPrecompiled::call(_executive, _callParameters);
    }

    return _callParameters;
}


void ShardingPrecompiled::makeShard(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)

{
    // makeShard(string)
    std::string shardName;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), shardName);

    if (shardName.find("/") != std::string::npos)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("ShardPrecompiled")
                                 << LOG_DESC(
                                        "makeShard: Shard name should not be a path, please check")
                                 << LOG_KV("shardName", shardName);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_INVALID_TYPE)));
        return;
    }

    std::string absolutePath = std::string(USER_SHARD_PREFIX) + shardName;

    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext->number()) << LOG_BADGE("ShardPrecompiled")
                          << LOG_KV("mkShard", absolutePath);

    BFSPrecompiled::makeDirImpl(absolutePath, _executive, _callParameters);
}

void ShardingPrecompiled::linkShard(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    const PrecompiledExecResult::Ptr& _callParameters)
{
    // linkShard(string,string,string)
    std::string shardName, contractAddress, contractAbi;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), shardName, contractAddress, contractAbi);

    if (shardName.find("/") != std::string::npos)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("ShardPrecompiled")
                                 << LOG_DESC(
                                        "linkShard: Shard name should not be a path, please check")
                                 << LOG_KV("shardName", shardName);
        _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_INVALID_TYPE)));
        return;
    }

    std::string absolutePath = shardName + "/" + contractAddress;

    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext->number()) << LOG_BADGE("ShardPrecompiled")
                          << LOG_DESC("linkShard") << LOG_KV("absolutePath", absolutePath)
                          << LOG_KV("contractAddress", contractAddress)
                          << LOG_KV("contractAbiSize", contractAbi.size());

    setContractShard(
        _executive, contractAddress, shardName, _callParameters);  // will throw exception if failed

    BFSPrecompiled::linkImpl(
        absolutePath, contractAddress, contractAbi, _executive, _callParameters);
}

void ShardingPrecompiled::getContractShard(
    const std::shared_ptr<executor::TransactionExecutive>& _executive,
    PrecompiledExecResult::Ptr const& _callParameters)
{
    // (errorCode, shardName) = getContractShard(string)
    std::string contractAddress;
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), contractAddress);

    if (!checkPathValid(contractAddress))
    {
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ShardPrecompiled")
                               << LOG_DESC("getContractShard: invalid contract address")
                               << LOG_KV("contractAddress", contractAddress);

        _callParameters->setExecResult(codec.encode(int32_t(CODE_FILE_INVALID_PATH)));
        return;
    }

    PRECOMPILED_LOG(DEBUG) << BLOCK_NUMBER(blockContext->number()) << LOG_BADGE("ShardPrecompiled")
                           << "getContractShard internalCall request"
                           << LOG_KV("contractAddress", contractAddress);
    // externalRequest
    bytes params = codec.encodeWithSig(FILE_SYSTEM_GET_CONTRACT_SHARD_INTERNAL, contractAddress);
    auto thisAddress = std::string(getThisAddress(blockContext->isWasm()));
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
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), contractAddress);

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
    auto blockContext = _executive->blockContext().lock();
    PRECOMPILED_LOG(INFO) << BLOCK_NUMBER(blockContext->number()) << LOG_BADGE("ShardPrecompiled")
                          << "setContractShard internalCall request"
                          << LOG_KV("contractAddress", contractAddress)
                          << LOG_KV("shardName", shardName);
    // externalRequest
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());

    bytes params = codec.encodeWithSig(FILE_SYSTEM_SET_CONTRACT_SHARD_INTERNAL,
        std::string(contractAddress), std::string(shardName));
    auto thisAddress = std::string(getThisAddress(blockContext->isWasm()));
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
    auto blockContext = _executive->blockContext().lock();
    auto codec = CodecWrapper(blockContext->hashHandler(), blockContext->isWasm());
    codec.decode(_callParameters->params(), contractAddress, shardName);

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
