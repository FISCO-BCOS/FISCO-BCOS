/** @file P2PSession.h
 *  @author monan
 *  @date 20181112
 */

#pragma once

#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-framework/interfaces/protocol/ProtocolInfo.h>
#include <bcos-gateway/libp2p/Common.h>
#include <bcos-gateway/libp2p/P2PMessage.h>
#include <memory>

namespace bcos
{
namespace gateway
{
class P2PMessage;
class Service;

class P2PSession : public boostssl::ws::WsSession
{
public:
    using Ptr = std::shared_ptr<P2PSession>;

    P2PSession(std::string _moduleName) : WsSession(_moduleName)
    {
        P2PSESSION_LOG(INFO) << "[P2PSession::P2PSession] this=" << this;
    }
    virtual ~P2PSession();

    virtual void start();

    virtual P2pID p2pID() { return m_p2pInfo->p2pID; }
    // Note: the p2pInfo must be setted after session setted
    virtual void initP2PInfo()
    {
        m_p2pInfo = std::make_shared<P2pInfo>();
        m_p2pInfo->p2pID = m_nodeId;
        auto& stream = m_wsStreamDelegate->tcpStream();
        auto endpoint = stream.socket().remote_endpoint();
        m_p2pInfo->hostIp = endpoint.address().to_string();
        m_p2pInfo->hostPort = std::to_string(endpoint.port());
    }
    virtual P2pInfo const& p2pInfo() const& { return *m_p2pInfo; }
    virtual std::shared_ptr<P2pInfo> mutableP2pInfo() { return m_p2pInfo; }

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
    /// gateway p2p info
    std::shared_ptr<P2pInfo> m_p2pInfo;
    std::weak_ptr<Service> m_service;
    bool m_run = false;

    bcos::protocol::ProtocolInfo::ConstPtr m_protocolInfo = nullptr;
    mutable bcos::SharedMutex x_protocolInfo;
};

class P2pSessionFactory : public boostssl::ws::WsSessionFactory
// class P2pSessionFactory
{
public:
    using Ptr = std::shared_ptr<P2pSessionFactory>;
    P2pSessionFactory() = default;
    virtual ~P2pSessionFactory() {}

public:
    virtual boostssl::ws::WsSession::Ptr createSession(std::string _moduleName) override
    {
        auto session = std::make_shared<P2PSession>(_moduleName);
        return session;
    }
};

}  // namespace gateway
}  // namespace bcos
