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
 * @brief interface for RPC
 * @file Rpc.h
 * @author: octopus
 * @date 2021-07-15
 */

#pragma once
#include "bcos-rpc/groupmgr/GroupManager.h"
#include "bcos-rpc/web3jsonrpc/Web3Subscribe.h"
#include "web3jsonrpc/Web3JsonRpcImpl.h"

#include <bcos-framework/rpc/RPCInterface.h>
#include <bcos-rpc/amop/AMOPClient.h>
#include <bcos-rpc/event/EventSub.h>
#include <bcos-rpc/jsonrpc/JsonRpcImpl_2_0.h>
namespace bcos
{
namespace boostssl
{
namespace ws
{
class WsSession;
class WsService;
}  // namespace ws
}  // namespace boostssl
namespace rpc
{
class Rpc : public RPCInterface, public std::enable_shared_from_this<Rpc>
{
public:
    using Ptr = std::shared_ptr<Rpc>;
    Rpc(std::shared_ptr<boostssl::ws::WsService> _wsService,
        bcos::rpc::JsonRpcImpl_2_0::Ptr _jsonRpcImpl, bcos::event::EventSub::Ptr _eventSub,
        AMOPClient::Ptr _amopClient);

    virtual ~Rpc() { stop(); }

    void start() override;
    void stop() override;

    /**
     * @brief: notify blockNumber to rpc
     * @param _blockNumber: blockNumber
     * @param _callback: resp callback
     * @return void
     */
    void asyncNotifyBlockNumber(std::string const& _groupID, std::string const& _nodeName,
        bcos::protocol::BlockNumber _blockNumber,
        std::function<void(Error::Ptr)> _callback) override;

    void asyncNotifyGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo,
        std::function<void(Error::Ptr&&)> _callback) override;

    std::shared_ptr<boostssl::ws::WsService> wsService() const { return m_wsService; }
    bcos::rpc::JsonRpcImpl_2_0::Ptr jsonRpcImpl() const { return m_jsonRpcImpl; }
    bcos::event::EventSub::Ptr eventSub() const { return m_eventSub; }

    void asyncNotifyAMOPMessage(int16_t _type, std::string const& _topic,
        bytesConstRef _requestData,
        std::function<void(Error::Ptr&& _error, bytesPointer _responseData)> _callback) override
    {
        m_amopClient->asyncNotifyAMOPMessage(_type, _topic, _requestData, _callback);
    }

    void setClientID(std::string const& _clientID)
    {
        m_jsonRpcImpl->setClientID(_clientID);
        m_amopClient->setClientID(_clientID);
    }
    void asyncNotifySubscribeTopic(
        std::function<void(Error::Ptr&& _error, std::string)> _callback) override
    {
        m_amopClient->asyncNotifySubscribeTopic(_callback);
    }

    void setWeb3Service(boostssl::ws::WsService::Ptr _web3Service)
    {
        m_web3Service = std::move(_web3Service);
    }

    void setWeb3JsonRpcImpl(bcos::rpc::Web3JsonRpcImpl::Ptr _web3JsonRpcImpl)
    {
        m_web3JsonRpcImpl = std::move(_web3JsonRpcImpl);
    }

    void setWeb3Subscribe(bcos::rpc::Web3Subscribe::Ptr _web3Subscribe)
    {
        m_web3Subscribe = std::move(_web3Subscribe);
    }

    bcos::rpc::Web3Subscribe::Ptr web3Subscribe() const { return m_web3Subscribe; }

    void setOnNewBlock(
        std::function<void(std::string const& _groupID, bcos::protocol::BlockNumber _blockNumber)>
            _onNewBlock)
    {
        m_onNewBlock = std::move(_onNewBlock);
    }

    void onNewBlock(std::string const& _groupID, bcos::protocol::BlockNumber _blockNumber)
    {
        if (m_onNewBlock)
        {
            m_onNewBlock(_groupID, _blockNumber);
        }
    }

    GroupManager::Ptr groupManager() { return m_groupManager; }

    bcos::rpc::Web3JsonRpcImpl::Ptr web3JsonRpc() const { return m_web3JsonRpcImpl; }

protected:
    virtual void notifyGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo);

    virtual void onRecvHandshakeRequest(std::shared_ptr<boostssl::MessageFace> _msg,
        std::shared_ptr<boostssl::ws::WsSession> _session);

    virtual bool negotiatedVersion(std::shared_ptr<boostssl::MessageFace> _msg,
        std::shared_ptr<boostssl::ws::WsSession> _session);

private:
    std::shared_ptr<boostssl::ws::WsService> m_wsService;
    bcos::rpc::JsonRpcImpl_2_0::Ptr m_jsonRpcImpl;
    bcos::event::EventSub::Ptr m_eventSub;
    AMOPClient::Ptr m_amopClient;
    GroupManager::Ptr m_groupManager;
    boostssl::ws::WsService::Ptr m_web3Service = nullptr;
    bcos::rpc::Web3JsonRpcImpl::Ptr m_web3JsonRpcImpl = nullptr;
    bcos::rpc::Web3Subscribe::Ptr m_web3Subscribe = nullptr;


    bcos::protocol::ProtocolInfo::ConstPtr m_localProtocol;

    // callback for new block
    std::function<void(std::string const& _groupID, bcos::protocol::BlockNumber _blockNumber)>
        m_onNewBlock;
};

}  // namespace rpc
}  // namespace bcos