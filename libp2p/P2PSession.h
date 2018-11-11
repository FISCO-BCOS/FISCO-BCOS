/*
 * P2PSession.h
 *
 *  Created on: 2018年11月7日
 *      Author: ancelmo
 */

#pragma once

#include <memory>
#include <libnetwork/Session.h>
#include <libnetwork/Common.h>

namespace dev
{
namespace p2p
{

class Service;

class P2PSession: public std::enable_shared_from_this<P2PSession> {
public:
    typedef std::shared_ptr<P2PSession> Ptr;

    P2PSession() {
        m_topics = std::make_shared<std::set<std::string> >();
    }

    virtual ~P2PSession() {};

    virtual void start();
    virtual void stop();
    virtual void heartBeat();

    virtual SessionFace::Ptr session() { return m_session; }
    virtual void setSession(std::shared_ptr<SessionFace> session) { m_session = session; }

    virtual NodeID nodeID() { return m_nodeID; }
    virtual void setNodeID(NodeID nodeID) { m_nodeID = nodeID; }

    virtual std::shared_ptr<std::set<std::string> > topics() { return m_topics; }

    virtual std::weak_ptr<Service> service() { return m_service; }
    virtual void setService(std::weak_ptr<Service> service) { m_service = service; }

private:
    SessionFace::Ptr m_session;
    NodeID m_nodeID;
    std::shared_ptr<std::set<std::string> > m_topics;
    std::weak_ptr<Service> m_service;
    uint32_t failTimes = 0;
    std::shared_ptr<boost::asio::deadline_timer> m_timer;

    const uint32_t HEARTBEAT_INTERVEL = 5000;
    const uint32_t MAX_IDLE = HEARTBEAT_INTERVEL * 10;
};

}
}
