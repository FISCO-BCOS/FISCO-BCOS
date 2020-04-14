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
/** @file P2PInterface.h
 *  @author chaychen
 *  @date 20180911
 */

#pragma once

#include <libdevcore/FixedHash.h>
#include <libnetwork/SessionFace.h>
#include <libp2p/Common.h>
#include <libp2p/P2PMessage.h>
#include <memory>

namespace dev
{
namespace stat
{
class NetworkStatHandler;
}
namespace flowlimit
{
class RateLimiter;
}
namespace p2p
{
class P2PMessage;
class P2PMessageFactory;
class P2PSession;
typedef std::function<void(dev::network::NetworkException, std::shared_ptr<dev::p2p::P2PSession>,
    std::shared_ptr<dev::p2p::P2PMessage>)>
    CallbackFuncWithSession;
typedef std::function<void(const std::string&, const std::string&)> CallbackFuncForTopicVerify;
class P2PInterface
{
public:
    using Ptr = std::shared_ptr<P2PInterface>;
    virtual ~P2PInterface(){};

    virtual NodeID id() const = 0;

    virtual std::shared_ptr<P2PMessage> sendMessageByNodeID(
        NodeID nodeID, std::shared_ptr<P2PMessage> message) = 0;

    virtual void asyncSendMessageByNodeID(NodeID nodeID, std::shared_ptr<P2PMessage> message,
        CallbackFuncWithSession callback,
        dev::network::Options options = dev::network::Options()) = 0;

    virtual std::shared_ptr<P2PMessage> sendMessageByTopic(
        std::string topic, std::shared_ptr<P2PMessage> message) = 0;

    virtual void asyncSendMessageByTopic(std::string topic, std::shared_ptr<P2PMessage> message,
        CallbackFuncWithSession callback, dev::network::Options options) = 0;

    virtual bool asyncMulticastMessageByTopic(std::string topic,
        std::shared_ptr<P2PMessage> message,
        std::shared_ptr<dev::flowlimit::RateLimiter> _bandwidthLimiter = nullptr) = 0;

    virtual void asyncMulticastMessageByNodeIDList(
        NodeIDs nodeIDs, std::shared_ptr<P2PMessage> message) = 0;

    virtual void asyncBroadcastMessage(
        std::shared_ptr<P2PMessage> message, dev::network::Options options) = 0;

    virtual void registerHandlerByProtoclID(
        PROTOCOL_ID protocolID, CallbackFuncWithSession handler) = 0;

    virtual void removeHandlerByProtocolID(PROTOCOL_ID const&) {}

    virtual void registerHandlerByTopic(std::string topic, CallbackFuncWithSession handler) = 0;

    virtual P2PSessionInfos sessionInfos() = 0;
    virtual P2PSessionInfos sessionInfosByProtocolID(PROTOCOL_ID _protocolID) const = 0;

    virtual bool isConnected(NodeID const& _nodeID) const = 0;

    virtual std::set<std::string> topics() = 0;

    virtual dev::h512s getNodeListByGroupID(GROUP_ID groupID) = 0;
    virtual void setGroupID2NodeList(std::map<GROUP_ID, dev::h512s> _groupID2NodeList) = 0;
    virtual void setNodeListByGroupID(GROUP_ID _groupID, const dev::h512s& _nodeList) = 0;

    virtual void setTopics(std::shared_ptr<std::set<std::string>> _topics) = 0;

    virtual std::shared_ptr<P2PMessageFactory> p2pMessageFactory() = 0;

    virtual void setCallbackFuncForTopicVerify(CallbackFuncForTopicVerify channelRpcCallBack) = 0;

    virtual CallbackFuncForTopicVerify callbackFuncForTopicVerify() = 0;

    virtual std::shared_ptr<dev::p2p::P2PSession> getP2PSessionByNodeId(NodeID const& _nodeID) = 0;
    virtual void appendNetworkStatHandlerByGroupID(
        GROUP_ID const&, std::shared_ptr<dev::stat::NetworkStatHandler>)
    {}
    virtual void removeNetworkStatHandlerByGroupID(GROUP_ID const&) {}

    virtual void setNodeBandwidthLimiter(std::shared_ptr<dev::flowlimit::RateLimiter>) {}
    virtual void registerGroupBandwidthLimiter(
        GROUP_ID const&, std::shared_ptr<dev::flowlimit::RateLimiter>)
    {}
    virtual void removeGroupBandwidthLimiter(GROUP_ID const&) {}
};

}  // namespace p2p

}  // namespace dev
