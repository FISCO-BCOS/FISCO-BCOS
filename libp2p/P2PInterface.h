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
#include "Common.h"

namespace dev
{
namespace p2p
{
class P2PInterface
{
public:
    /// < protocolID stored in Message struct
    virtual Message::Ptr sendMessageByNodeID(NodeID const& nodeID, Message::Ptr message) = 0;

    virtual void asyncSendMessageByNodeID(NodeID const& nodeID, Message::Ptr message,
        CallbackFunc callback = [](P2PException e, Message::Ptr msg) {},
        Options const& options = Options()) = 0;

    virtual Message::Ptr sendMessageByTopic(std::string const& topic, Message::Ptr message) = 0;

    virtual void asyncSendMessageByTopic(std::string const& topic, Message::Ptr message,
        CallbackFunc callback, Options const& options) = 0;

    virtual void asyncMulticastMessageByTopic(std::string const& topic, Message::Ptr message) = 0;

    virtual void asyncMulticastMessageByNodeIDList(
        NodeIDs const& nodeIDs, Message::Ptr message) = 0;

    virtual void asyncBroadcastMessage(Message::Ptr message, Options const& options) = 0;

    virtual void registerHandlerByProtoclID(
        PROTOCOL_ID protocolID, CallbackFuncWithSession handler) = 0;

    virtual void registerHandlerByTopic(
        std::string const& topic, CallbackFuncWithSession handler) = 0;

    virtual void setTopicsByNode(
        NodeID const& _nodeID, std::shared_ptr<std::vector<std::string>> _topics) = 0;

    virtual std::shared_ptr<std::vector<std::string>> getTopicsByNode(NodeID const& _nodeID) = 0;

    virtual SessionInfos sessionInfos() const = 0;

    ///< Get connecting sessions by topicID which is groupID and can be got by protocolID.
    ///< TODO: Whether the session list needs caching, especially when the consensus module calls.

    virtual SessionInfos sessionInfosByProtocolID(PROTOCOL_ID _protocolID) const = 0;

    ///< Quickly determine whether to connect to a particular node.
    virtual bool isConnected(NodeID const& _nodeID) const = 0;

    ///< One-time loading the list of node members for a group.
    virtual void setGroupID2NodeList(std::map<int32_t, h512s> const& _groupID2NodeList) = 0;
};

}  // namespace p2p

}  // namespace dev
