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
 * @file RpcInterface.h
 * @author: octopus
 * @date 2021-08-10
 */

#pragma once
#include <bcos-cpp-sdk/rpc/JsonRpcInterface.h>
#include <bcos-cpp-sdk/rpc/JsonRpcRequest.h>
#include <bcos-cpp-sdk/ws/Service.h>
#include <bcos-framework/multigroup/GroupInfoCodec.h>
#include <functional>
#include <utility>

namespace bcos::cppsdk::jsonrpc
{
using JsonRpcSendFunc =
    std::function<void(const std::string&, const std::string&, const std::string&, RespFunc)>;

class JsonRpcImpl : public JsonRpcInterface, public std::enable_shared_from_this<JsonRpcImpl>
{
public:
    using Ptr = std::shared_ptr<JsonRpcImpl>;
    using UniquePtr = std::unique_ptr<JsonRpcImpl>;

public:
    explicit JsonRpcImpl(bcos::group::GroupInfoCodec::Ptr _groupInfoCodec)
      : m_groupInfoCodec(std::move(_groupInfoCodec))
    {}

    ~JsonRpcImpl() override
    {
        stop();
        // BCOS_LOG(INFO) << LOG_KV("[DELOBJ][JsonRpcImpl]", this);
    }

public:
    void start() override;
    void stop() override;

public:
    //-------------------------------------------------------------------------------------
    void genericMethod(const std::string& _data, RespFunc _respFunc) override;

    void genericMethod(
        const std::string& _groupID, const std::string& _data, RespFunc _respFunc) override;

    void genericMethod(const std::string& _groupID, const std::string& _nodeName,
        const std::string& _data, RespFunc _respFunc) override;
    //-------------------------------------------------------------------------------------

    void call(const std::string& _groupID, const std::string& _nodeName, const std::string& _to,
        const std::string& _data, RespFunc _respFunc) override;

    void sendTransaction(const std::string& _groupID, const std::string& _nodeName,
        const std::string& _data, bool _requireProof, RespFunc _respFunc) override;

    void getTransaction(const std::string& _groupID, const std::string& _nodeName,
        const std::string& _txHash, bool _requireProof, RespFunc _respFunc) override;

    void getTransactionReceipt(const std::string& _groupID, const std::string& _nodeName,
        const std::string& _txHash, bool _requireProof, RespFunc _respFunc) override;

    void getBlockByHash(const std::string& _groupID, const std::string& _nodeName,
        const std::string& _blockHash, bool _onlyHeader, bool _onlyTxHash,
        RespFunc _respFunc) override;

    void getBlockByNumber(const std::string& _groupID, const std::string& _nodeName,
        int64_t _blockNumber, bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc) override;

    void getBlockHashByNumber(const std::string& _groupID, const std::string& _nodeName,
        int64_t _blockNumber, RespFunc _respFunc) override;

    void getBlockNumber(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) override;

    void getCode(const std::string& _groupID, const std::string& _nodeName,
        const std::string& _contractAddress, RespFunc _respFunc) override;

    void getSealerList(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) override;

    void getObserverList(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) override;

    void getPbftView(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) override;

    void getPendingTxSize(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) override;

    void getSyncStatus(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) override;

    void getConsensusStatus(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) override;

    void getSystemConfigByKey(const std::string& _groupID, const std::string& _nodeName,
        const std::string& _keyValue, RespFunc _respFunc) override;

    void getTotalTransactionCount(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) override;

    void getGroupPeers(std::string const& _groupID, RespFunc _respFunc) override;

    void getPeers(RespFunc _respFunc) override;

    void getGroupList(RespFunc _respFunc) override;

    void getGroupInfo(const std::string& _groupID, RespFunc _respFunc) override;

    void getGroupInfoList(RespFunc _respFunc) override;

    void getGroupNodeInfo(
        const std::string& _groupID, const std::string& _nodeName, RespFunc _respFunc) override;

    JsonRpcRequestFactory::Ptr factory() const { return m_factory; }
    void setFactory(JsonRpcRequestFactory::Ptr _factory) { m_factory = std::move(_factory); }

    JsonRpcSendFunc sender() const { return m_sender; }
    void setSender(JsonRpcSendFunc _sender) { m_sender = std::move(_sender); }

    std::shared_ptr<bcos::cppsdk::service::Service> service() const { return m_service; }
    void setService(std::shared_ptr<bcos::cppsdk::service::Service> _service)
    {
        m_service = std::move(_service);
    }

    void setSendRequestToHighestBlockNode(bool _sendRequestToHighestBlockNode)
    {
        m_sendRequestToHighestBlockNode = _sendRequestToHighestBlockNode;
    }

    bool sendRequestToHighestBlockNode() const { return m_sendRequestToHighestBlockNode; }

private:
    std::shared_ptr<bcos::cppsdk::service::Service> m_service;
    JsonRpcRequestFactory::Ptr m_factory;
    std::function<void(const std::string&, const std::string&, const std::string&, RespFunc)>
        m_sender;
    bcos::group::GroupInfoCodec::Ptr m_groupInfoCodec;

    bool m_sendRequestToHighestBlockNode = false;
};

}  // namespace bcos::cppsdk::jsonrpc
