/*
 * P2PSession.cpp
 *
 *  Created on: 2018年11月11日
 *      Author: monan
 */

#include "P2PSession.h"
#include <libnetwork/Common.h>
#include <libnetwork/Host.h>
#include <libdevcore/Common.h>
#include "Service.h"

using namespace dev;
using namespace dev::p2p;

void P2PSession::start() {
    m_run = true;
    m_session->start();
    heartBeat();
}

void P2PSession::stop() {
    m_run = false;
    m_session->disconnect(UserReason);
}

void P2PSession::heartBeat() {
    auto service = m_service.lock();
    if(service && service->actived()) {
        if(m_session->isConnected()) {
            auto message = std::dynamic_pointer_cast<P2PMessage>(service->p2pMessageFactory()->buildMessage());

            message->setProtocolID(dev::eth::ProtocolID::AMOP);
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
            if(e) {
                LOG(TRACE) << "Timer canceled: " << e;
                return;
            }

            auto s = self.lock();
            if(s) {
                s->heartBeat();
            }
        });
    }
}
