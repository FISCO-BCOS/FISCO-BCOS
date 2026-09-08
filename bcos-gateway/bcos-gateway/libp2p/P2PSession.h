/** @file P2PSession.h
 *  @author monan
 *  @date 20181112
 */

#pragma once

#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-framework/protocol/ProtocolInfo.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include <bcos-task/Task.h>
#include <boost/asio/steady_timer.hpp>
#include <memory>
#include <range/v3/view/any_view.hpp>
#include <utility>


namespace bcos::gateway
{
class P2PMessage;
class Service;
class Session;

class P2PSession : public std::enable_shared_from_this<P2PSession>
{
public:
    using Ptr = std::shared_ptr<P2PSession>;

    P2PSession();

    ~P2PSession();

    void start();
    void stop(DisconnectReason reason);
    bool active();
    void heartBeat();

    std::shared_ptr<Session> session();
    void setSession(std::shared_ptr<Session> session);

    P2pID p2pID();
    std::string printP2pID();
    // Note: the p2pInfo must be setted after session setted
    void setP2PInfo(P2PInfo const& p2pInfo);
    P2PInfo const& p2pInfo() const& { return *m_p2pInfo; }
    std::shared_ptr<P2PInfo> mutableP2pInfo();

    std::weak_ptr<Service> service();
    void setService(std::weak_ptr<Service> service);

    void setProtocolInfo(bcos::protocol::ProtocolInfo::ConstPtr _protocolInfo);
    // empty when negotiate failed or negotiate unfinished
    bcos::protocol::ProtocolInfo::ConstPtr protocolInfo() const;

    task::Task<Message::Ptr> fastSendP2PMessage(
        P2PMessage& message, ::ranges::any_view<bytesConstRef> payloads, Options options);

private:
    std::shared_ptr<Session> m_session;
    /// gateway p2p info
    std::shared_ptr<P2PInfo> m_p2pInfo;
    std::weak_ptr<Service> m_service;
    std::optional<boost::asio::steady_timer> m_timer;
    bool m_run = false;
    const static uint32_t HEARTBEAT_INTERVEL = 5000;

    bcos::protocol::ProtocolInfo::Ptr m_protocolInfo = nullptr;
    mutable bcos::SharedMutex x_protocolInfo;
};

}  // namespace bcos::gateway
