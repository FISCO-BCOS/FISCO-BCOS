/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file P2PInterface.h
 *  @author chaychen
 *  @date 20180911
 */

#pragma once
#include <libnetwork/Common.h>
#include <libnetwork/SessionFace.h>
#include <libdevcore/FixedHash.h>
#include <memory>
#include "P2PSession.h"
#include "P2PMessage.h"

#define CallbackFuncWithSession std::function<void(dev::p2p::NetworkException, std::shared_ptr<dev::p2p::P2PSession>, dev::p2p::P2PMessage::Ptr)>

namespace dev
{
namespace p2p
{
class Host;
class P2PInterface
{
public:
    virtual ~P2PInterface() {};

    virtual P2PMessage::Ptr sendMessageByNodeID(NodeID nodeID, P2PMessage::Ptr message) = 0;

    virtual void asyncSendMessageByNodeID(NodeID nodeID, P2PMessage::Ptr message,
        CallbackFuncWithSession callback, Options options = Options()) = 0;

    virtual P2PMessage::Ptr sendMessageByTopic(std::string topic, P2PMessage::Ptr message) = 0;

    virtual void asyncSendMessageByTopic(std::string topic, P2PMessage::Ptr message,
        CallbackFuncWithSession callback, Options options) = 0;

    virtual void asyncMulticastMessageByTopic(std::string topic, P2PMessage::Ptr message) = 0;

    virtual void asyncMulticastMessageByNodeIDList(NodeIDs nodeIDs, P2PMessage::Ptr message) = 0;

    virtual void asyncBroadcastMessage(P2PMessage::Ptr message, Options options) = 0;

    virtual void registerHandlerByProtoclID(PROTOCOL_ID protocolID, CallbackFuncWithSession handler) = 0;

    virtual void registerHandlerByTopic(std::string topic, CallbackFuncWithSession handler) = 0;

    virtual SessionInfos sessionInfos() = 0;
    virtual SessionInfos sessionInfosByProtocolID(PROTOCOL_ID _protocolID) = 0;

    virtual bool isConnected(NodeID _nodeID) = 0;

    virtual std::shared_ptr<std::vector<std::string>> topics() = 0;

    virtual dev::h512s getNodeListByGroupID(GROUP_ID groupID) = 0;
    virtual void setGroupID2NodeList(std::map<GROUP_ID, dev::h512s> _groupID2NodeList) = 0;

    virtual void setTopics(std::shared_ptr<std::vector<std::string>> _topics) = 0;

    virtual std::shared_ptr<P2PMessageFactory> p2pMessageFactory() = 0;
};

}  // namespace p2p

}  // namespace dev
