/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file Service.h
 *  @author monan
 *  @modify first draft
 *  @date 20180910
 *  @author chaychen
 *  @modify realize encode and decode, add timeout, code format
 *  @date 20180911
 */

#pragma once
#include "P2PInterface.h"
#include "P2PMessageFactory.h"
#include "P2PSession.h"
#include <libdevcore/Common.h>
#include <libdevcore/Exceptions.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/TopicInfo.h>
#include <libnetwork/Host.h>
#include <libnetwork/PeerWhitelist.h>
#include <map>
#include <memory>
#include <unordered_map>

namespace dev
{
namespace p2p
{
class P2PMessage;
class Service : public P2PInterface, public std::enable_shared_from_this<Service>
{
public:
    Service();
    virtual ~Service() { stop(); }

    typedef std::shared_ptr<Service> Ptr;

    virtual void start();
    virtual void stop();
    virtual void heartBeat();

    virtual void setWhitelist(PeerWhitelist::Ptr _whitelist);

    virtual bool actived() { return m_run; }
    NodeID id() const override { return m_alias.pub(); }

    virtual void onConnect(dev::network::NetworkException e, dev::network::NodeInfo const& nodeInfo,
        std::shared_ptr<dev::network::SessionFace> session);
    virtual void onDisconnect(dev::network::NetworkException e, P2PSession::Ptr p2pSession);
    virtual void onMessage(dev::network::NetworkException e, dev::network::SessionFace::Ptr session,
        dev::network::Message::Ptr message, P2PSession::Ptr p2pSession);

    std::shared_ptr<P2PMessage> sendMessageByNodeID(
        NodeID nodeID, std::shared_ptr<P2PMessage> message) override;
    void asyncSendMessageByNodeID(NodeID nodeID, std::shared_ptr<P2PMessage> message,
        CallbackFuncWithSession callback,
        dev::network::Options options = dev::network::Options()) override;

    std::shared_ptr<P2PMessage> sendMessageByTopic(
        std::string topic, std::shared_ptr<P2PMessage> message) override;
    void asyncSendMessageByTopic(std::string topic, std::shared_ptr<P2PMessage> message,
        CallbackFuncWithSession callback, dev::network::Options options) override;

    void asyncMulticastMessageByTopic(
        std::string topic, std::shared_ptr<P2PMessage> message) override;
    void asyncMulticastMessageByNodeIDList(
        NodeIDs nodeIDs, std::shared_ptr<P2PMessage> message) override;
    void asyncBroadcastMessage(
        std::shared_ptr<P2PMessage> message, dev::network::Options options) override;

    void registerHandlerByProtoclID(
        PROTOCOL_ID protocolID, CallbackFuncWithSession handler) override;

    void removeHandlerByProtocolID(PROTOCOL_ID const& _protocolID) override;

    void registerHandlerByTopic(std::string topic, CallbackFuncWithSession handler) override;

    virtual std::map<dev::network::NodeIPEndpoint, NodeID> staticNodes() { return m_staticNodes; }
    virtual void setStaticNodes(std::map<dev::network::NodeIPEndpoint, NodeID> staticNodes)
    {
        m_staticNodes = staticNodes;
    }

    P2PSessionInfos sessionInfos() override;  ///< Only connected node

    P2PSessionInfos sessionInfosByProtocolID(PROTOCOL_ID _protocolID) const override;

    bool isConnected(NodeID const& nodeID) const override;

    h512s getNodeListByGroupID(GROUP_ID groupID) override { return m_groupID2NodeList[groupID]; }
    void setGroupID2NodeList(std::map<GROUP_ID, h512s> _groupID2NodeList) override
    {
        RecursiveGuard l(x_nodeList);
        m_groupID2NodeList = _groupID2NodeList;
    }

    void setNodeListByGroupID(GROUP_ID _groupID, const h512s& _nodeList) override
    {
        RecursiveGuard l(x_nodeList);
        m_groupID2NodeList[_groupID] = _nodeList;
    }

    virtual uint32_t topicSeq() { return m_topicSeq; }
    virtual void increaseTopicSeq() { ++m_topicSeq; }

    std::set<std::string> topics() override
    {
        RecursiveGuard l(x_topics);
        return *m_topics;
    }
    void setTopics(std::shared_ptr<std::set<std::string>> _topics) override
    {
        RecursiveGuard l(x_topics);
        m_topics = _topics;
        ++m_topicSeq;
    }

    virtual std::shared_ptr<dev::network::Host> host() { return m_host; }
    virtual void setHost(std::shared_ptr<dev::network::Host> host) { m_host = host; }

    std::shared_ptr<P2PMessageFactory> p2pMessageFactory() override { return m_p2pMessageFactory; }
    virtual void setP2PMessageFactory(std::shared_ptr<P2PMessageFactory> _p2pMessageFactory)
    {
        m_p2pMessageFactory = _p2pMessageFactory;
    }

    virtual KeyPair keyPair() { return m_alias; }
    virtual void setKeyPair(KeyPair keyPair) { m_alias = keyPair; }
    void updateStaticNodes(
        std::shared_ptr<dev::network::SocketFace> const& _s, NodeID const& nodeId);

    void setCallbackFuncForTopicVerify(CallbackFuncForTopicVerify channelRpcCallBack) override
    {
        m_channelRpcCallBack = channelRpcCallBack;
    }

    CallbackFuncForTopicVerify callbackFuncForTopicVerify() override
    {
        return m_channelRpcCallBack;
    }

    std::shared_ptr<dev::p2p::P2PSession> getP2PSessionByNodeId(NodeID const& _nodeID) override
    {
        RecursiveGuard l(x_sessions);
        auto it = m_sessions.find(_nodeID);
        if (it != m_sessions.end())
        {
            return it->second;
        }
        return nullptr;
    }

    void appendNetworkStatHandlerByGroupID(
        GROUP_ID const& _groupID, std::shared_ptr<dev::stat::NetworkStatHandler> _handler) override;

private:
    NodeIDs getPeersByTopic(std::string const& topic);
    void checkWhitelistAndClearSession();
    void updateIncomingTraffic(P2PMessage::Ptr _msg);
    void updateOutcomingTraffic(P2PMessage::Ptr _msg);

private:
    std::map<dev::network::NodeIPEndpoint, NodeID> m_staticNodes;
    RecursiveMutex x_nodes;

    std::shared_ptr<dev::network::Host> m_host;

    std::unordered_map<NodeID, P2PSession::Ptr> m_sessions;
    mutable RecursiveMutex x_sessions;

    std::atomic<uint32_t> m_topicSeq = {0};
    std::shared_ptr<std::set<std::string>> m_topics;
    RecursiveMutex x_topics;

    ///< key is the group that the node joins
    ///< value is the list of node members for the group
    ///< the data is currently statically loaded and not synchronized between nodes
    mutable RecursiveMutex x_nodeList;
    std::map<GROUP_ID, h512s> m_groupID2NodeList;

    std::shared_ptr<std::unordered_map<uint32_t, CallbackFuncWithSession>> m_protocolID2Handler;
    RecursiveMutex x_protocolID2Handler;

    ///< A call B, the function to call after the request is received by B in topic.
    std::shared_ptr<std::unordered_map<std::string, CallbackFuncWithSession>> m_topic2Handler;
    RecursiveMutex x_topic2Handler;

    std::shared_ptr<P2PMessageFactory> m_p2pMessageFactory;
    KeyPair m_alias;

    std::shared_ptr<boost::asio::deadline_timer> m_timer;


    CallbackFuncForTopicVerify m_channelRpcCallBack;

    bool m_run = false;

    PeerWhitelist::Ptr m_whitelist;

    // for network statistics
    std::shared_ptr<std::map<GROUP_ID, std::shared_ptr<dev::stat::NetworkStatHandler>>>
        m_group2NetworkStatHandler;
    mutable SharedMutex x_group2NetworkStatHandler;
};

}  // namespace p2p

}  // namespace dev
