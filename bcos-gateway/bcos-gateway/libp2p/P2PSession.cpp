/** @file P2PSession.cpp
 *  @author monan
 *  @date 20181112
 */

#include "bcos-gateway/libp2p/P2PSession.h"
#include "bcos-gateway/libnetwork/ASIOInterface.h"
#include "bcos-gateway/libp2p/Common.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-gateway/libp2p/P2PMessageV2.h"
#include "bcos-gateway/libp2p/Service.h"
#include "bcos-utilities/Common.h"
#include <bcos-task/Wait.h>

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

SessionFace::Ptr P2PSession::session()
{
    return m_session;
}

void P2PSession::setSession(std::shared_ptr<SessionFace> session)
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
                    P2PMessageV2 message;
                    message.setPacketType(GatewayMessageType::Heartbeat);
                    ::ranges::any_view<bytesConstRef> emptyPayloads;
                    co_await _self->fastSendP2PMessage(
                        message, std::move(emptyPayloads), Options{});
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

void P2PSession::asyncSendP2PMessage(
    P2PMessage::Ptr message, Options options, SessionCallbackFunc callback)
{
    if (!m_session || !m_session->active()) [[unlikely]]
    {
        P2PSESSION_LOG(WARNING) << LOG_DESC("asyncSendP2PMessage failed for invalid session")
                                << LOG_KV("from", message->printSrcP2PNodeID())
                                << LOG_KV("dst", message->printDstP2PNodeID());
        return;
    }
    auto service = m_service.lock();
    if (!service)
    {
        return;
    }
    // reset message using original long nodeID or short nodeID according to the protocol version
    // Note: m_protocolInfo be setted when create P2PSession
    service->resetP2pID(*message, (ProtocolVersion)m_protocolInfo->version());
    // route through the coroutine fast path: the message (shared_ptr) and the callback are passed as
    // coroutine parameters so they are copied into the frame and stay alive for the whole (possibly
    // deferred) send; response/error is delivered to callback
    auto self = shared_from_this();
    task::wait([](std::shared_ptr<P2PSession> _self, P2PMessage::Ptr _message, Options _options,
                   SessionCallbackFunc _callback) mutable -> task::Task<void> {
        Options sendOptions{_options.timeout, _callback ? true : false};
        try
        {
            auto resp = co_await _self->fastSendP2PMessage(
                *_message, ::ranges::views::single(_message->payload()), sendOptions);
            if (_callback)
            {
                _callback(NetworkException(), resp);
            }
        }
        catch (NetworkException const& e)
        {
            if (_callback)
            {
                _callback(e, nullptr);
            }
        }
    }(self, message, options, callback));
}

bcos::task::Task<Message::Ptr> P2PSession::fastSendP2PMessage(
    P2PMessage& message, ::ranges::any_view<bytesConstRef> payloads, Options options)
{
    if (!m_session || !m_session->active()) [[unlikely]]
    {
        P2PSESSION_LOG(WARNING) << LOG_DESC("fastSendP2PMessage failed for invalid session")
                                << LOG_KV("from", message.printSrcP2PNodeID())
                                << LOG_KV("dst", message.printDstP2PNodeID());
        co_return {};
    }
    auto service = m_service.lock();
    if (!service)
    {
        co_return {};
    }
    // reset message using original long nodeID or short nodeID according to the protocol version
    // Note: m_protocolInfo be setted when create P2PSession
    service->resetP2pID(message, (ProtocolVersion)m_protocolInfo->version());
    // the p2p message version must match the negotiated protocol version of this session: the
    // encodeHeaderImpl of P2PMessageV2 only encodes the ttl/src/dst routing fields for version > V0,
    // so sending with the default (V0) version would silently drop the V2 routing fields and break
    // multi-hop forwarding through ServiceV2 router tables
    message.setVersion((uint16_t)m_protocolInfo->version());
    co_return co_await m_session->fastSendMessage(message, std::move(payloads), options);
}