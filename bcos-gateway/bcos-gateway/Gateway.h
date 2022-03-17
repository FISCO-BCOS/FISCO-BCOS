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
 * @file Gateway.h
 * @author: octopus
 * @date 2021-04-19
 */

#pragma once

#include <bcos-framework/interfaces/front/FrontServiceInterface.h>
#include <bcos-framework/interfaces/gateway/GatewayInterface.h>
#include <bcos-boostssl/interfaces/MessageFace.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-gateway/Common.h>
#include <bcos-gateway/gateway/GatewayNodeManager.h>
#include <bcos-gateway/libamop/AMOPImpl.h>
#include <bcos-gateway/libp2p/Service.h>

namespace bcos
{
namespace gateway
{
class Gateway : public GatewayInterface, public std::enable_shared_from_this<Gateway>
{
public:
    using Ptr = std::shared_ptr<Gateway>;
    Gateway(std::string const& _chainID, P2PInterface::Ptr _p2pInterface,
        GatewayNodeManager::Ptr _gatewayNodeManager, bcos::amop::AMOPImpl::Ptr _amop)
      : m_chainID(_chainID),
        m_p2pInterface(_p2pInterface),
        m_gatewayNodeManager(_gatewayNodeManager),
        m_amop(_amop)
    {
        m_p2pInterface->registerHandlerByMsgType(GatewayMessageType::PeerToPeerMessage,
            boost::bind(&Gateway::onReceiveP2PMessage, this, boost::placeholders::_1,
                boost::placeholders::_2));

        m_p2pInterface->registerHandlerByMsgType(GatewayMessageType::BroadcastMessage,
            boost::bind(&Gateway::onReceiveBroadcastMessage, this, boost::placeholders::_1,
                boost::placeholders::_2));
    }
    virtual ~Gateway() { stop(); }

    void start() override;
    void stop() override;

    /**
     * @brief: get connected peers
     * @param _peerRespFunc:
     * @return void
     */
    void asyncGetPeers(
        std::function<void(Error::Ptr, GatewayInfo::Ptr, GatewayInfosPtr)> _onGetPeers) override;
    /**
     * @brief: get nodeIDs from gateway
     * @param _groupID:
     * @param _onGetGroupNodeInfo: get nodeIDs callback
     * @return void
     */
    void asyncGetGroupNodeInfo(
        const std::string& _groupID, GetGroupNodeInfoFunc _onGetGroupNodeInfo) override;
    /**
     * @brief: send message
     * @param _groupID: groupID
     * @param _srcNodeID: the sender nodeID
     * @param _dstNodeID: the receiver nodeID
     * @param _payload: message payload
     * @param _errorRespFunc: error func
     * @return void
     */
    void asyncSendMessageByNodeID(const std::string& _groupID, bcos::crypto::NodeIDPtr _srcNodeID,
        bcos::crypto::NodeIDPtr _dstNodeID, bytesConstRef _payload,
        ErrorRespFunc _errorRespFunc) override;

    /**
     * @brief: send message to multiple nodes
     * @param _groupID: groupID
     * @param _srcNodeID: the sender nodeID
     * @param _nodeIDs: the receiver nodeIDs
     * @param _payload: message payload
     * @return void
     */
    void asyncSendMessageByNodeIDs(const std::string& _groupID, bcos::crypto::NodeIDPtr _srcNodeID,
        const bcos::crypto::NodeIDs& _nodeIDs, bytesConstRef _payload) override;

    /**
     * @brief: send broadcast message
     * @param _groupID: groupID
     * @param _srcNodeID: the sender nodeID
     * @param _payload: message payload
     * @return void
     */
    void asyncSendBroadcastMessage(uint16_t _type, const std::string& _groupID,
        bcos::crypto::NodeIDPtr _srcNodeID, bytesConstRef _payload) override;

    /**
     * @brief: receive p2p message
     * @param _groupID: groupID
     * @param _srcNodeID: the sender nodeID
     * @param _dstNodeID: the receiver nodeID
     * @param _payload: message content
     * @param _errorRespFunc: error func
     * @return void
     */
    virtual void onReceiveP2PMessage(const std::string& _groupID,
        bcos::crypto::NodeIDPtr _srcNodeID, bcos::crypto::NodeIDPtr _dstNodeID,
        bytesConstRef _payload, ErrorRespFunc _errorRespFunc = ErrorRespFunc());


    P2PInterface::Ptr p2pInterface() const { return m_p2pInterface; }
    GatewayNodeManager::Ptr gatewayNodeManager() { return m_gatewayNodeManager; }
    /**
     * @brief receive the latest group information notification from the GroupManagerInterface
     *
     * @param _groupInfo the latest group information
     */
    void asyncNotifyGroupInfo(
        bcos::group::GroupInfo::Ptr, std::function<void(Error::Ptr&&)>) override;

    /// for AMOP
    void asyncSendMessageByTopic(const std::string& _topic, bcos::bytesConstRef _data,
        std::function<void(bcos::Error::Ptr&&, int16_t, bytesPointer)> _respFunc) override
    {
        m_amop->asyncSendMessageByTopic(_topic, _data, _respFunc);
    }
    void asyncSendBroadbastMessageByTopic(
        const std::string& _topic, bcos::bytesConstRef _data) override
    {
        m_amop->asyncSendBroadbastMessageByTopic(_topic, _data);
    }

    void asyncSubscribeTopic(std::string const& _clientID, std::string const& _topicInfo,
        std::function<void(Error::Ptr&&)> _callback) override
    {
        m_amop->asyncSubscribeTopic(_clientID, _topicInfo, _callback);
    }

    void asyncRemoveTopic(std::string const& _clientID, std::vector<std::string> const& _topicList,
        std::function<void(Error::Ptr&&)> _callback) override
    {
        m_amop->asyncRemoveTopic(_clientID, _topicList, _callback);
    }

    bcos::amop::AMOPImpl::Ptr amop() { return m_amop; }

    bool registerNode(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::protocol::NodeType _nodeType, bcos::front::FrontServiceInterface::Ptr _frontService,
        bcos::protocol::ProtocolInfo::ConstPtr _protocolInfo) override
    {
        return m_gatewayNodeManager->registerNode(
            _groupID, _nodeID, _nodeType, _frontService, _protocolInfo);
    }

protected:
    // for UT
    Gateway() {}
    virtual void onReceiveP2PMessage(
        std::shared_ptr<boostssl::MessageFace> _msg, std::shared_ptr<boostssl::ws::WsSession> _session);

    /**
     * @brief: receive group broadcast message
     * @param _groupID: groupID
     * @param _srcNodeID: the sender nodeID
     * @param _payload: message content
     * @return void
     */
    virtual void onReceiveBroadcastMessage(
        std::shared_ptr<boostssl::MessageFace> _msg, std::shared_ptr<boostssl::ws::WsSession> _session);

private:
    std::string m_chainID;
    // p2p service interface
    P2PInterface::Ptr m_p2pInterface;
    // GatewayNodeManager
    GatewayNodeManager::Ptr m_gatewayNodeManager;
    bcos::amop::AMOPImpl::Ptr m_amop;
};
}  // namespace gateway
}  // namespace bcos
