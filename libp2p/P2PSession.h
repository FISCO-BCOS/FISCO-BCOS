/*
 * P2PSession.h
 *
 *  Created on: 2018年11月7日
 *      Author: ancelmo
 */

#pragma once

#include <memory>
#include <libnetwork/Session.h>

namespace dev
{
namespace p2p
{

class P2PSession: public std::enable_shared_from_this<P2PSession> {
public:
    typedef std::shared_ptr<P2PSession> Ptr;

    P2PSession() {
        m_topics = std::make_shared<std::set<std::string> >();
    }

    virtual ~P2PSession() {};

    virtual Session::Ptr session() { return m_session; }
    virtual void setSession(std::shared_ptr<Session> session) { m_session = session; }

    virtual NodeID id() { return m_nodeID; }
    virtual std::shared_ptr<std::set<std::string> > topics() { return m_topics; }

private:
    Session::Ptr m_session;
    NodeID m_nodeID;
    std::shared_ptr<std::set<std::string> > m_topics;
};

}
}
