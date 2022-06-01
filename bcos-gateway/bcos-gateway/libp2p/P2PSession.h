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

    P2PSession(std::string _moduleName, std::string const& _hostNodeID)
      : WsSession(_moduleName),
        m_protocolInfo(std::make_shared<bcos::protocol::ProtocolInfo>()),
        m_hostNodeID(_hostNodeID)
    {
        // init with the minVersion
        m_protocolInfo->setVersion(m_protocolInfo->minVersion());
        P2PSESSION_LOG(INFO) << "[P2PSession::P2PSession] this=" << this;
    }
    virtual ~P2PSession();

    virtual P2pID p2pID()
    {
        if (!m_p2pInfo)
        {
            return P2pID();
        }
        return m_p2pInfo->p2pID;
    }

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
        *m_protocolInfo = *_protocolInfo;
    }
    // empty when negotiate failed or negotiate unfinished
    virtual bcos::protocol::ProtocolInfo::ConstPtr protocolInfo() const
    {
        ReadGuard l(x_protocolInfo);
        return m_protocolInfo;
    }

    void onMessage(bcos::boostssl::MessageFace::Ptr _message) override;

private:
    /// gateway p2p info
    std::shared_ptr<P2pInfo> m_p2pInfo;
    std::weak_ptr<Service> m_service;

    bcos::protocol::ProtocolInfo::Ptr m_protocolInfo = nullptr;
    mutable bcos::SharedMutex x_protocolInfo;

    std::string m_hostNodeID;
};

class P2pSessionFactory : public boostssl::ws::WsSessionFactory
{
public:
    using Ptr = std::shared_ptr<P2pSessionFactory>;
    P2pSessionFactory(std::string const& _hostNodeID) : m_hostNodeID(_hostNodeID) {}
    virtual ~P2pSessionFactory() {}
    virtual boostssl::ws::WsSession::Ptr createSession(std::string _moduleName) override
    {
        auto session = std::make_shared<P2PSession>(_moduleName, m_hostNodeID);
        session->setNeedCheckRspPacket(true);
        return session;
    }

private:
    std::string m_hostNodeID;
};

}  // namespace gateway
}  // namespace bcos
