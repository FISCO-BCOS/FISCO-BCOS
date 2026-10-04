/** @file P2PSession.cpp
 *  @author monan
 *  @date 20181112
 */

#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libp2p/Message.h"
#include "bcos-gateway/libp2p/Common.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-utilities/Common.h"
#include <bcos-task/Wait.h>
#include <range/v3/view/empty.hpp>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::protocol;

P2PSession::P2PSession()
  : m_p2pInfo(std::make_shared<P2PInfo>()),
    m_protocolInfo(std::make_shared<bcos::protocol::ProtocolInfo>())
{
    // init with the minVersion
    m_protocolInfo->setVersion(m_protocolInfo->minVersion());
    P2PSESSION_LOG(INFO) << "[P2PSession::P2PSession] this=" << this;
}

P2PSession::~P2PSession()
{
    P2PSESSION_LOG(INFO) << "[P2PSession::~P2PSession] this=" << this;
}

bool P2PSession::active()
{
    return m_run;
}

Session::Ptr P2PSession::session()
{
    return m_session;
}

void P2PSession::setSession(Session::Ptr session)
{
    m_session = std::move(session);
}

P2pID P2PSession::p2pID()
{
    return m_p2pInfo->rawP2pID;
}

std::string P2PSession::printP2pID()
{
    return printShortP2pID(m_p2pInfo->rawP2pID);
}

void P2PSession::setP2PInfo(P2PInfo const& p2pInfo)
{
    *m_p2pInfo = p2pInfo;
    m_p2pInfo->nodeIPEndpoint = m_session->nodeIPEndpoint();
}

std::shared_ptr<P2PInfo> P2PSession::mutableP2pInfo()
{
    return m_p2pInfo;
}

std::weak_ptr<Service> P2PSession::service()
{
    return m_service;
}

void P2PSession::setService(std::weak_ptr<Service> service)
{
    m_service = service;
}

void P2PSession::setProtocolInfo(bcos::protocol::ProtocolInfo::ConstPtr _protocolInfo)
{
    WriteGuard guard(x_protocolInfo);
    *m_protocolInfo = *_protocolInfo;
}

bcos::protocol::ProtocolInfo::ConstPtr P2PSession::protocolInfo() const
{
    ReadGuard guard(x_protocolInfo);
    return m_protocolInfo;
}

void P2PSession::start()
{
    P2PSESSION_LOG(INFO) << "[P2PSession::start] this=" << this;
    if (!m_run && m_session)
    {
        m_run = true;

        m_session->start();
        heartBeat();
    }
}

void P2PSession::stop(DisconnectReason reason)
{
    if (m_run)
    {
        m_run = false;
        if (m_session && m_session->active())
        {
            m_session->disconnect(reason);
        }
    }
}

void P2PSession::heartBeat()
{
    auto service = m_service.lock();
    if (service && service->active())
    {
        if (m_session && m_session->active())
        {
            if (c_fileLogLevel <= TRACE) [[unlikely]]
            {
                P2PSESSION_LOG(TRACE) << LOG_DESC("P2PSession onHeartBeat")
                                      << LOG_KV("p2pid", printShortP2pID(m_p2pInfo->p2pID))
                                      << LOG_KV("endpoint", m_session->nodeIPEndpoint());
            }
            // value message in frame, sent through the fast path (zero-copy). The service shared_ptr
            // is passed as a coroutine parameter so it is copied into the frame and kept alive for
            // the whole (possibly deferred) send. The pre-send checks (outgoing rate limit / max
            // size) run synchronously on the caller thread and may throw — catch so the heartbeat
            // timer below is always re-armed (otherwise this session would be dropped by the peer's
            // idle timeout).
            auto self = shared_from_this();
            try
            {
                task::wait([](std::shared_ptr<P2PSession> _self) -> task::Task<void> {
                    Message message;
                    message.setPacketType(GatewayMessageType::Heartbeat);
                    co_await _self->fastSendP2PMessage(
                        message, ::ranges::views::empty<bytesConstRef>, Options{});
                }(self));
            }
            catch (std::exception const& e)
            {
                P2PSESSION_LOG(WARNING) << LOG_DESC("heartBeat send exception")
                                        << LOG_KV("p2pid", printShortP2pID(m_p2pInfo->p2pID))
                                        << LOG_KV("what", boost::diagnostic_information(e));
            }
        }

        auto self = std::weak_ptr<P2PSession>(shared_from_this());
        m_timer.emplace(service->host()->asioInterface()->newTimer(HEARTBEAT_INTERVEL));
        m_timer->async_wait([self](boost::system::error_code e) {
            if (e)
            {
                P2PSESSION_LOG(TRACE) << "Timer canceled: " << e.message();
                return;
            }

            auto s = self.lock();
            if (s)
            {
                s->heartBeat();
            }
        });
    }
}