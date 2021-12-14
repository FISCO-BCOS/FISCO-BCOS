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
 * @brief interface for JsonRPC
 * @file JsonRpcInterface.h
 * @author: octopus
 * @date 2021-07-09
 */

#pragma once

#include <bcos-framework/interfaces/multigroup/GroupInfo.h>
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <bcos-framework/libutilities/Error.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <json/json.h>
#include <functional>

namespace bcos
{
namespace rpc
{
using Sender = std::function<void(const std::string&)>;
using RespFunc = std::function<void(bcos::Error::Ptr, Json::Value&)>;

class JsonRpcInterface
{
public:
    using Ptr = std::shared_ptr<JsonRpcInterface>;
    virtual ~JsonRpcInterface() {}

public:
    virtual void onRPCRequest(const std::string& _requestBody, Sender _sender) = 0;

public:
    virtual void call(std::string const& _groupID, std::string const& _nodeName,
        const std::string& _to, const std::string& _data, RespFunc _respFunc) = 0;

    virtual void sendTransaction(std::string const& _groupID, std::string const& _nodeName,
        const std::string& _data, bool _requireProof, RespFunc _respFunc) = 0;

    virtual void getTransaction(std::string const& _groupID, std::string const& _nodeName,
        const std::string& _txHash, bool _requireProof, RespFunc _respFunc) = 0;

    virtual void getTransactionReceipt(std::string const& _groupID, std::string const& _nodeName,
        const std::string& _txHash, bool _requireProof, RespFunc _respFunc) = 0;

    virtual void getBlockByHash(std::string const& _groupID, std::string const& _nodeName,
        const std::string& _blockHash, bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc) = 0;

    virtual void getBlockByNumber(std::string const& _groupID, std::string const& _nodeName,
        int64_t _blockNumber, bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc) = 0;

    virtual void getBlockHashByNumber(std::string const& _groupID, std::string const& _nodeName,
        int64_t _blockNumber, RespFunc _respFunc) = 0;

    virtual void getBlockNumber(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) = 0;

    virtual void getCode(std::string const& _groupID, std::string const& _nodeName,
        const std::string _contractAddress, RespFunc _respFunc) = 0;

    virtual void getSealerList(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) = 0;

    virtual void getObserverList(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) = 0;

    virtual void getPbftView(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) = 0;

    virtual void getPendingTxSize(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) = 0;

    virtual void getSyncStatus(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) = 0;
    virtual void getConsensusStatus(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) = 0;

    virtual void getSystemConfigByKey(std::string const& _groupID, std::string const& _nodeName,
        const std::string& _keyValue, RespFunc _respFunc) = 0;

    virtual void getTotalTransactionCount(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) = 0;

    virtual void getGroupPeers(std::string const& _groupID, RespFunc _respFunc) = 0;
    virtual void getPeers(RespFunc _respFunc) = 0;
    // get all the groupID list
    virtual void getGroupList(RespFunc _respFunc) = 0;
    // get the group information of the given group
    virtual void getGroupInfo(std::string const& _groupID, RespFunc _respFunc) = 0;
    // get all the group info list
    virtual void getGroupInfoList(RespFunc _respFunc) = 0;
    // get the information of a given node
    virtual void getGroupNodeInfo(
        std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc) = 0;
};

}  // namespace rpc
}  // namespace bcos
