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
        CallbackFunc callback, Options const& options) = 0;

    virtual void asyncSendMessageByNodeID(NodeID const& nodeID, Message::Ptr message) = 0;

    virtual Message::Ptr sendMessageByTopic(std::string const& topic, Message::Ptr message) = 0;

    virtual void asyncSendMessageByTopic(std::string const& topic, Message::Ptr message,
        CallbackFunc callback, Options const& options) = 0;

    virtual void asyncMulticastMessageByTopic(std::string const& topic, Message::Ptr message) = 0;

    virtual void asyncBroadcastMessage(Message::Ptr message, Options const& options) = 0;

    virtual void registerHandlerByProtoclID(
        int16_t protocolID, CallbackFuncWithSession handler) = 0;

    virtual void registerHandlerByTopic(
        std::string const& topic, CallbackFuncWithSession handler) = 0;
};

}  // namespace p2p

}  // namespace dev
