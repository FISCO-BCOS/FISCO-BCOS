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
/** @file P2PSession.h
 *  @author monan
 *  @date 20181112
 */

#pragma once

#include <libnetwork/Common.h>
#include <libnetwork/Session.h>
#include <libp2p/Common.h>
#include <memory>

namespace dev
{
namespace p2p
{
class Service;

class P2PSession : public std::enable_shared_from_this<P2PSession>
{
public:
    typedef std::shared_ptr<P2PSession> Ptr;

    P2PSession() { m_topics = std::make_shared<std::set<std::string> >(); }

    virtual ~P2PSession(){};

    virtual void start();
    virtual void stop(dev::network::DisconnectReason reason);
    virtual bool actived() { return m_run; }
    virtual void heartBeat();

    virtual dev::network::SessionFace::Ptr session() { return m_session; }
    virtual void setSession(std::shared_ptr<dev::network::SessionFace> session) { m_session = session; }

    virtual NodeID nodeID() { return m_nodeID; }
    virtual void setNodeID(NodeID nodeID) { m_nodeID = nodeID; }

    virtual std::shared_ptr<std::set<std::string> > topics() { return m_topics; }

    virtual std::weak_ptr<Service> service() { return m_service; }
    virtual void setService(std::weak_ptr<Service> service) { m_service = service; }

    virtual void onTopicMessage(uint32_t topicSeq);

private:
    dev::network::SessionFace::Ptr m_session;
    NodeID m_nodeID;
    uint32_t m_topicSeq = 0;
    std::shared_ptr<std::set<std::string> > m_topics;
    std::weak_ptr<Service> m_service;
    uint32_t failTimes = 0;
    std::shared_ptr<boost::asio::deadline_timer> m_timer;
    bool m_run = false;

    const uint32_t HEARTBEAT_INTERVEL = 5000;
    const uint32_t MAX_IDLE = HEARTBEAT_INTERVEL * 10;
};

}  // namespace p2p
}  // namespace dev
