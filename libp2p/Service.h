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
/** @file Service.h
 *  @author molan
 *  @modify first draft
 *  @date 20180910
 *  @author chaychen
 *  @modify realize encode and decode, add timeout, code format
 *  @date 20180911
 */

#pragma once
#include "Host.h"
#include "P2PMsgHandler.h"
#include <libdevcore/Common.h>
#include <libdevcore/Exceptions.h>
#include <libdevcore/FixedHash.h>
#include <memory>

namespace dev
{
namespace p2p
{
class P2PInterfase
{
public:
    virtual Message::Ptr sendMessageByNodeID(
        dev::h256 const& nodeID, uint32_t protocolID, Message::Ptr message) = 0;

    virtual void asyncSendMessageByNodeID(dev::h256 const& nodeID, uint32_t protocolID,
        Message::Ptr message, std::function<void(dev::Exception, Message::Ptr)> callback,
        Options options) = 0;

    virtual Message::Ptr sendMessageByTopic(
        std::string const& topic, uint32_t protocolID, Message::Ptr message) = 0;

    virtual void asyncSendMessageByTopic(std::string const& topic, uint32_t protocolID,
        Message::Ptr message, std::function<void(dev::Exception, Message::Ptr)> callback,
        Options options) = 0;

    virtual void asyncMulticastMessageByTopic(
        std::string const& topic, uint32_t protocolID, Message::Ptr message) = 0;

    virtual void asyncBroadcastMessage(
        uint32_t protocolID, Message::Ptr message, Options options) = 0;

    virtual void registerHandlerByProtoclID(uint32_t protocolID,
        std::function<void(dev::Exception, std::shared_ptr<Session>, Message::Ptr)> handler) = 0;

    virtual void registerHandlerByTopic(std::string const& topic,
        std::function<void(dev::Exception, std::shared_ptr<Session>, Message::Ptr)> handler,
        Options options) = 0;
};

class Service : public P2PInterfase
{
public:
    Service(std::shared_ptr<Host> _host) : m_host(_host)
    {
        m_p2pMsgHandler = std::make_shared<P2PMsgHandler>();
        // set m_p2pMsgHandler to session?
    }

    Message::Ptr sendMessageByNodeID(
        dev::h256 const& nodeID, uint32_t protocolID, Message::Ptr message) override;

    void asyncSendMessageByNodeID(dev::h256 const& nodeID, uint32_t protocolID,
        Message::Ptr message, std::function<void(dev::Exception, Message::Ptr)> callback,
        Options options) override;

    Message::Ptr sendMessageByTopic(
        std::string const& topic, uint32_t protocolID, Message::Ptr message) override;

    void asyncSendMessageByTopic(std::string const& topic, uint32_t protocolID,
        Message::Ptr message, std::function<void(dev::Exception, Message::Ptr)> callback,
        Options options) override;

    void asyncMulticastMessageByTopic(
        std::string const& topic, uint32_t protocolID, Message::Ptr message) override;

    void asyncBroadcastMessage(uint32_t protocolID, Message::Ptr message, Options options) override;

    void registerHandlerByProtoclID(uint32_t protocolID,
        std::function<void(dev::Exception, std::shared_ptr<Session>, Message::Ptr)> handler)
        override;

    void registerHandlerByTopic(std::string const& topic,
        std::function<void(dev::Exception, std::shared_ptr<Session>, Message::Ptr)> handler,
        Options options) override;

private:
    std::shared_ptr<Host> m_host;

    std::shared_ptr<P2PMsgHandler> m_p2pMsgHandler;
    std::atomic<uint32_t> m_seq = {0};
};

}  // namespace p2p

}  // namespace dev
