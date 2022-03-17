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
using namespace bcos::boostssl;

P2PSession::P2PSession()
{
    m_p2pInfo = std::make_shared<NodeInfo>();
    P2PSESSION_LOG(INFO) << "[P2PSession::P2PSession] this=" << this;
}

P2PSession::~P2PSession()
{
    P2PSESSION_LOG(INFO) << "[P2PSession::~P2PSession] this=" << this;
}

void P2PSession::start()
{
    if (!m_run && m_session)
    {
        m_run = true;

        heartBeat();
    }
}

void P2PSession::stop(DisconnectReason reason)
{
    if (m_run)
    {
        m_run = false;
        if (m_session && m_session->isConnected())
        {
            m_session->drop(reason);
        }
    }
}

void P2PSession::heartBeat()
{
    auto service = m_service.lock();
    if (service && service->isConnected())
    {
        if (m_session && m_session->isConnected())
        {
            auto message =
                std::dynamic_pointer_cast<P2PMessage>(service->messageFactory()->buildMessage());
            message->setPacketType(GatewayMessageType::Heartbeat);
            P2PSESSION_LOG(DEBUG) << LOG_DESC("P2PSession onHeartBeat")
                                  << LOG_KV("p2pid", m_p2pInfo->p2pID)
                                  << LOG_KV("endpoint", m_session->endPoint());

            m_session->asyncSendMessage(message);
        }

        auto self = std::weak_ptr<P2PSession>(shared_from_this());
        m_timer = service->host()->asioInterface()->newTimer(HEARTBEAT_INTERVEL);
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