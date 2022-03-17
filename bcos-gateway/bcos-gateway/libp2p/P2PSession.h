/** @file P2PSession.h
 *  @author monan
 *  @date 20181112
 */

#pragma once

#include <bcos-framework/interfaces/protocol/ProtocolInfo.h>
#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-gateway/libp2p/Common.h>
#include <bcos-gateway/libp2p/P2PMessage.h>
#include <bcos-boostssl/interfaces/NodeInfo.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <memory>

namespace bcos
{
namespace gateway
{
class P2PMessage;
class Service;

class P2PSession : public std::enable_shared_from_this<P2PSession>
{
public:
    using Ptr = std::shared_ptr<P2PSession>;

    P2PSession();

    virtual ~P2PSession();

    virtual void start();
    virtual void stop(DisconnectReason reason);
    virtual bool actived() { return m_run; }
    virtual void heartBeat();

    virtual boostssl::ws::WsSession::Ptr session() { return m_session; }
    virtual void setSession(std::shared_ptr<boostssl::ws::WsSession> session) { m_session = session; }

    virtual boostssl::nodeID p2pID() { return m_p2pInfo->nodeID; }
    // Note: the p2pInfo must be setted after session setted
    virtual void setP2PInfo(boostssl::NodeInfo const& p2pInfo)
    {
        *m_p2pInfo = p2pInfo;
        auto& stream = m_session->wsStreamDelegate()->tcpStream();
        auto endpoint = stream.socket().remote_endpoint();
        m_p2pInfo->nodeIPEndpoint = boostssl::NodeIPEndpoint(endpoint);
    }
    virtual boostssl::NodeInfo const& p2pInfo() const& { return *m_p2pInfo; }
    virtual std::shared_ptr<boostssl::NodeInfo> mutableP2pInfo() { return m_p2pInfo; }

    virtual std::weak_ptr<Service> service() { return m_service; }
    virtual void setService(std::weak_ptr<Service> service) { m_service = service; }

    virtual void setProtocolInfo(bcos::protocol::ProtocolInfo::ConstPtr _protocolInfo)
    {
        WriteGuard l(x_protocolInfo);
        m_protocolInfo = _protocolInfo;
    }
    // empty when negotiate failed or negotiate unfinished
    virtual bcos::protocol::ProtocolInfo::ConstPtr protocolInfo() const
    {
        ReadGuard l(x_protocolInfo);
        return m_protocolInfo;
    }

private:
    boostssl::ws::WsSession::Ptr m_session;
    /// gateway p2p info
    std::shared_ptr<boostssl::NodeInfo> m_p2pInfo;
    std::weak_ptr<Service> m_service;
    std::shared_ptr<boost::asio::deadline_timer> m_timer;
    bool m_run = false;
    const static uint32_t HEARTBEAT_INTERVEL = 5000;

    bcos::protocol::ProtocolInfo::ConstPtr m_protocolInfo = nullptr;
    mutable bcos::SharedMutex x_protocolInfo;
};

}  // namespace gateway
}  // namespace bcos
