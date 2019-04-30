/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file P2PSession.cpp
 *  @author monan
 *  @date 20181112
 */

#include "P2PSession.h"
#include "P2PMessage.h"
#include "Service.h"
#include <libdevcore/Common.h>
#include <libnetwork/Common.h>
#include <libnetwork/Host.h>
#include <boost/algorithm/string.hpp>

using namespace dev;
using namespace dev::p2p;

void P2PSession::start()
{
    if (!m_run && m_session)
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
        if (m_session && m_session->actived())
        {
            m_session->disconnect(reason);
        }
    }
}

void P2PSession::heartBeat()
{
    auto service = m_service.lock();
    if (service && service->actived())
    {
        if (m_session && m_session->actived())
        {
            SESSION_LOG(TRACE) << LOG_DESC("P2PSession onHeartBeat")
                               << LOG_KV("nodeID", m_nodeInfo.nodeID.abridged())
                               << LOG_KV("name", m_session->nodeIPEndpoint().name());
            auto message =
                std::dynamic_pointer_cast<P2PMessage>(service->p2pMessageFactory()->buildMessage());

            message->setProtocolID(dev::eth::ProtocolID::Topic);
            message->setPacketType(AMOPPacketType::SendTopicSeq);
            std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
            std::string s = boost::lexical_cast<std::string>(service->topicSeq());
            buffer->assign(s.begin(), s.end());
            message->setBuffer(buffer);
            std::shared_ptr<bytes> msgBuf = std::make_shared<bytes>();
            m_session->asyncSendMessage(message);
        }

        auto self = std::weak_ptr<P2PSession>(shared_from_this());
        m_timer = service->host()->asioInterface()->newTimer(HEARTBEAT_INTERVEL);
        m_timer->async_wait([self](boost::system::error_code e) {
            if (e)
            {
                SESSION_LOG(TRACE) << "Timer canceled: " << e.message();
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

void P2PSession::onTopicMessage(P2PMessage::Ptr message)
{
    auto service = m_service.lock();

    if (service && service->actived())
    {
        try
        {
            switch (message->packetType())
            {
            case AMOPPacketType::SendTopicSeq:
            {
                std::string s((const char*)message->buffer()->data(), message->buffer()->size());
                auto topicSeq = boost::lexical_cast<uint32_t>(s);

                if (m_topicSeq != topicSeq)
                {
                    SESSION_LOG(TRACE) << LOG_DESC("Remote seq not equal to local seq update")
                                       << topicSeq << "!=" << m_topicSeq;

                    auto requestTopics = std::dynamic_pointer_cast<P2PMessage>(
                        service->p2pMessageFactory()->buildMessage());

                    requestTopics->setProtocolID(dev::eth::ProtocolID::Topic);
                    requestTopics->setPacketType(AMOPPacketType::RequestTopics);
                    std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
                    requestTopics->setBuffer(buffer);
                    requestTopics->setSeq(service->p2pMessageFactory()->newSeq());

                    auto self = std::weak_ptr<P2PSession>(shared_from_this());
                    dev::network::Options option;
                    option.timeout = 5 * 1000;  // 5 seconds timeout
                    m_session->asyncSendMessage(requestTopics, option,
                        [self](NetworkException e, dev::network::Message::Ptr response) {
                            try
                            {
                                if (e.errorCode())
                                {
                                    SESSION_LOG(ERROR) << LOG_DESC("Error while requesting topic")
                                                       << LOG_KV("errorCode", e.errorCode())
                                                       << LOG_KV("message", e.what());
                                    return;
                                }

                                std::vector<std::string> topics;

                                auto p2pResponse = std::dynamic_pointer_cast<P2PMessage>(response);
                                std::string s((const char*)p2pResponse->buffer()->data(),
                                    p2pResponse->buffer()->size());

                                auto session = self.lock();
                                if (session)
                                {
                                    SESSION_LOG(INFO) << "Received topic: [" << s << "] from "
                                                      << session->nodeID().hex();
                                    boost::split(topics, s, boost::is_any_of("\t"));

                                    uint32_t topicSeq = 0;
                                    auto topicList = std::make_shared<std::set<std::string> >();
                                    for (uint32_t i = 0; i < topics.size(); ++i)
                                    {
                                        if (i == 0)
                                        {
                                            topicSeq = boost::lexical_cast<uint32_t>(topics[i]);
                                        }
                                        else
                                        {
                                            topicList->insert(topics[i]);
                                        }
                                    }

                                    session->setTopics(topicSeq, topicList);
                                }
                            }
                            catch (std::exception& e)
                            {
                                SESSION_LOG(ERROR)
                                    << "Parse topics error: " << boost::diagnostic_information(e);
                            }
                        });
                }
                break;
            }
            case AMOPPacketType::RequestTopics:
            {
                SESSION_LOG(TRACE) << "Receive request topics, reponse topics";

                auto responseTopics = std::dynamic_pointer_cast<P2PMessage>(
                    service->p2pMessageFactory()->buildMessage());

                responseTopics->setProtocolID(-((PROTOCOL_ID)dev::eth::ProtocolID::Topic));
                responseTopics->setPacketType(AMOPPacketType::SendTopics);
                std::shared_ptr<bytes> buffer = std::make_shared<bytes>();

                auto service = m_service.lock();
                if (service)
                {
                    std::string s = boost::lexical_cast<std::string>(service->topicSeq());
                    for (auto& it : service->topics())
                    {
                        s.append("\t");
                        s.append(it);
                    }

                    buffer->assign(s.begin(), s.end());

                    responseTopics->setBuffer(buffer);
                    responseTopics->setSeq(message->seq());

                    m_session->asyncSendMessage(
                        responseTopics, dev::network::Options(), CallbackFunc());
                }

                break;
            }
            default:
            {
                SESSION_LOG(ERROR) << LOG_DESC("Unknown topic packet type")
                                   << LOG_KV("type", message->packetType());
                break;
            }
            }
        }
        catch (std::exception& e)
        {
            SESSION_LOG(ERROR) << "Error onTopicMessage: " << boost::diagnostic_information(e);
        }
    }
}
