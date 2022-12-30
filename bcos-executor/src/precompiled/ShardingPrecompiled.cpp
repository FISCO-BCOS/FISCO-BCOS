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

ShardingPrecompiled::ShardingPrecompiled(crypto::Hash::Ptr _hashImpl) : BFSPrecompiled(_hashImpl)
{
    name2Selector[FILE_SYSTEM_METHOD_MK_SHARD] =
        getFuncSelector(FILE_SYSTEM_METHOD_MK_SHARD, _hashImpl);
    name2Selector[FILE_SYSTEM_METHOD_LINK] = getFuncSelector(FILE_SYSTEM_METHOD_LINK, _hashImpl);
}

std::shared_ptr<PrecompiledExecResult> ShardingPrecompiled::call(
    std::shared_ptr<executor::TransactionExecutive> _executive,
    PrecompiledExecResult::Ptr _callParameters)
{
    uint32_t func = getParamFunc(_callParameters->input());
    uint32_t version = _executive->blockContext().lock()->blockVersion();

    if (func == name2Selector[FILE_SYSTEM_METHOD_MK_SHARD])
    {
        // mkdir(string) => int32
        makeShard(_executive, _callParameters);
    }
    else if (func == name2Selector[FILE_SYSTEM_METHOD_LINK])
    {
        // link(absolutePath, address, abi) => int32
        linkShard(_executive, _callParameters);
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

    // TODO: add to s_shards


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

    // TODO: update contract table by internal call ShardInternalPrecompiled

    BFSPrecompiled::linkImpl(
        absolutePath, contractAddress, contractAbi, _executive, _callParameters);
}
