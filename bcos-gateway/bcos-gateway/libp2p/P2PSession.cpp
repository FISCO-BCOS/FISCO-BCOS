/** @file P2PSession.cpp
 *  @author monan
 *  @date 20181112
 */

#include <bcos-gateway/Gateway.h>
#include <bcos-gateway/libnetwork/ASIOInterface.h>
#include <bcos-gateway/libnetwork/Host.h>
#include <bcos-gateway/libp2p/P2PMessage.h>
#include <bcos-gateway/libp2p/P2PSession.h>
#include <bcos-gateway/libp2p/Service.h>

#include <bcos-utilities/Common.h>
#include <boost/algorithm/string.hpp>

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
            auto message =
                std::dynamic_pointer_cast<P2PMessage>(service->messageFactory()->buildMessage());
            message->setPacketType(GatewayMessageType::Heartbeat);
            if (c_fileLogLevel <= TRACE) [[unlikely]]
            {
                P2PSESSION_LOG(TRACE) << LOG_DESC("P2PSession onHeartBeat")
                                      << LOG_KV("p2pid", printShortP2pID(m_p2pInfo->p2pID))
                                      << LOG_KV("endpoint", m_session->nodeIPEndpoint());
            }
            asyncSendP2PMessage(message, Options());
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
    m_session->asyncSendMessage(message, options, callback);
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
    co_return co_await m_session->fastSendMessage(message, std::move(payloads), options);
}