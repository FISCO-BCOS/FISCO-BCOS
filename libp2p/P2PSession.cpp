/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file P2PSession.cpp
 *  @author monan
 *  @date 20181112
 */

#include "P2PSession.h"
#include "Service.h"
#include <libdevcore/Common.h>
#include <libnetwork/Common.h>
#include <libnetwork/Host.h>
#include "Service.h"

using namespace dev;
using namespace dev::p2p;

void P2PSession::start()
{
    if (!m_run)
    {
        m_run = true;

        m_session->start();
        heartBeat();
    }
}

void P2PSession::stop(dev::network::DisconnectReason reason)
{
    if (m_run)
    {
        m_run = false;
        if (m_session->actived())
        {
            m_session->disconnect(reason);
        }
    }
}

void P2PSession::heartBeat()
{
    SESSION_LOG(TRACE) << "P2PSession onHeartBeat: " << m_nodeID << "@"
                       << m_session->nodeIPEndpoint().name();
    auto service = m_service.lock();
    if (service && service->actived())
    {
        if (m_session->isConnected())
        {
            auto message = std::dynamic_pointer_cast<P2PMessage>(service->p2pMessageFactory()->buildMessage());

            message->setProtocolID(dev::eth::ProtocolID::Topic);
            message->setPacketType(AMOPPacketType::SendTopicSeq);
            std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
            std::string s = boost::lexical_cast<std::string>(service->topicSeq());
            buffer->assign(s.begin(), s.end());
            message->setBuffer(buffer);
            message->setLength(P2PMessage::HEADER_LENGTH + message->buffer()->size());
            std::shared_ptr<bytes> msgBuf = std::make_shared<bytes>();

            m_session->asyncSendMessage(message);
        }

        auto self = std::weak_ptr<P2PSession>(shared_from_this());
        m_timer = service->host()->asioInterface()->newTimer(HEARTBEAT_INTERVEL);
        m_timer->async_wait([self](boost::system::error_code e) {
            if (e)
            {
                SESSION_LOG(TRACE) << "Timer canceled: " << e;
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

void P2PSession::onTopicMessage(uint32_t topicSeq) {
	if(topicSeq != m_topicSeq) {
		SESSION_LOG(TRACE) << "Remote seq: " << topicSeq << " not equal to local seq: " << m_topicSeq << ", update";

		auto service = m_service.lock();
		if (service && service->actived())
		{
			auto message = std::dynamic_pointer_cast<P2PMessage>(service->p2pMessageFactory()->buildMessage());

			message->setProtocolID(dev::eth::ProtocolID::Topic);
			message->setPacketType(AMOPPacketType::RequestTopics);
			std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
			std::string s = boost::lexical_cast<std::string>(service->topicSeq());
			buffer->assign(s.begin(), s.end());
			message->setBuffer(buffer);
			message->setLength(P2PMessage::HEADER_LENGTH + message->buffer()->size());
			m_session->asyncSendMessage(message);
		}
	}
}
