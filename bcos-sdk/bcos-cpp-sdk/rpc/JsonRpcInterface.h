/*
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
 * @file JsonRPCInterface.h
 * @author: octopus
 * @date 2021-05-24
 */

#pragma once
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <functional>
#include <memory>

namespace bcos
{
namespace cppsdk
{
namespace jsonrpc
{
using RespFunc = std::function<void(bcos::Error::Ptr, std::shared_ptr<bcos::bytes>)>;

class JsonRpcInterface
{
public:
    using Ptr = std::shared_ptr<JsonRpcInterface>;
    using UniquePtr = std::unique_ptr<JsonRpcInterface>;

    JsonRpcInterface() = default;
    virtual ~JsonRpcInterface() {}

public:
    virtual void start() = 0;
    virtual void stop() = 0;

public:
    //-------------------------------------------------------------------------------------
    virtual void genericMethod(const std::string& _data, RespFunc _respFunc) = 0;
    virtual void genericMethod(
        const std::string& _groupID, const std::string& _data, RespFunc _respFunc) = 0;
    virtual void genericMethod(const std::string& _groupID, const std::string& _nodeName,
        const std::string& _data, RespFunc _respFunc) = 0;
    //-------------------------------------------------------------------------------------

    virtual void call(const std::string& _groupID, const std::string& _nodeName,
        const std::string& _to, const std::string& _data, RespFunc _respFunc) = 0;

    virtual void sendTransaction(const std::string& _groupID, const std::string& _nodeName,
        const std::string& _data, bool _requireProof, RespFunc _respFunc) = 0;

    virtual void getTransaction(const std::string& _groupID, const std::string& _nodeName,
        const std::string& _txHash, bool _requireProof, RespFunc _respFunc) = 0;

    virtual void getTransactionReceipt(const std::string& _groupID, const std::string& _nodeName,
        const std::string& _txHash, bool _requireProof, RespFunc _respFunc) = 0;

    virtual void getBlockByHash(const std::string& _groupID, const std::string& _nodeName,
        const std::string& _blockHash, bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc) = 0;

    virtual void getBlockByNumber(const std::string& _groupID, const std::string& _nodeName,
        int64_t _blockNumber, bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc) = 0;

    virtual void getBlockHashByNumber(const std::string& _groupID, const std::string& _nodeName,
        int64_t _blockNumber, RespFunc _respFunc) = 0;

    virtual void getBlockNumber(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) = 0;

    virtual void getCode(const std::string& _groupID, const std::string& _nodeName,
        const std::string _contractAddress, RespFunc _respFunc) = 0;

    virtual void getSealerList(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) = 0;

    virtual void getObserverList(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) = 0;

    virtual void getPbftView(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) = 0;

    virtual void getPendingTxSize(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) = 0;

    virtual void getSyncStatus(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) = 0;

    virtual void getConsensusStatus(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) = 0;

    virtual void getSystemConfigByKey(const std::string& _groupID, const std::string& _nodeName,
        const std::string& _keyValue, RespFunc _respFunc) = 0;

    virtual void getTotalTransactionCount(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) = 0;

    virtual void getGroupPeers(std::string const& _groupID, RespFunc _respFunc) = 0;

    virtual void getPeers(RespFunc _respFunc) = 0;

    virtual void getGroupList(RespFunc _respFunc) = 0;

    virtual void getGroupInfo(const std::string& _groupID, RespFunc _respFunc) = 0;

    virtual void getGroupInfoList(RespFunc _respFunc) = 0;

    virtual void getGroupNodeInfo(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) = 0;
};

}  // namespace jsonrpc
}  // namespace cppsdk
}  // namespace bcos