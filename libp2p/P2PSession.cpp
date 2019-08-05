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
 * (c) 2016-2019 fisco-dev contributors.
 */
/** @file P2PSession.cpp
 *  @author monan
 *  @date 20181112
 *  @darrenyin
 *  @date 20190801
 */

#include "P2PSession.h"
#include "P2PMessage.h"
#include "Service.h"
#include "libchannelserver/ChannelMessage.h"
#include "libnetwork/ASIOInterface.h"
#include <json/json.h>
#include <libdevcore/Common.h>
#include <libnetwork/Common.h>
#include <libnetwork/Host.h>
#include <boost/algorithm/string.hpp>

using namespace dev;
using namespace dev::p2p;
using namespace dev::channel;

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
                               << LOG_KV("name", m_session->nodeIPEndpoint().name())
                               << LOG_KV("seq", service->topicSeq());
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
                        [self, this](NetworkException e, dev::network::Message::Ptr response) {
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
                                    auto topicList =
                                        std::make_shared<std::vector<dev::p2p::TopicItem> >();
                                    dev::p2p::TopicItem item;
                                    for (uint32_t i = 0; i < topics.size(); ++i)
                                    {
                                        if (i == 0)
                                        {
                                            topicSeq = boost::lexical_cast<uint32_t>(topics[i]);
                                        }
                                        else
                                        {
                                            if (i % 2 == 1)
                                            {
                                                item.topic = topics[i];
                                            }
                                            else
                                            {
                                                item.topicStatus =
                                                    boost::lexical_cast<uint32_t>(topics[i]);
                                                topicList->push_back(std::move(item));
                                            }
                                        }
                                    }

                                    session->setTopics(topicSeq, topicList);
                                    for (auto topicIt : *topicList)
                                    {
                                        if (topicIt.topicStatus == TopicStatus::INIT_STATUS)
                                        {
                                            this->requestRandValue(session, topicIt.topic, e);
                                        }
                                    }
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
                        s.append(it.topic);
                        s.append("\t");
                        s.append(boost::lexical_cast<std::string>(it.topicStatus));
                    }

                    SESSION_LOG(TRACE) << "Receive request topics, reponse topics content:" << s;

                    buffer->assign(s.begin(), s.end());

                    responseTopics->setBuffer(buffer);
                    responseTopics->setSeq(message->seq());

                    m_session->asyncSendMessage(
                        responseTopics, dev::network::Options(), CallbackFunc());
                }

                break;
            }
            case AMOPPacketType::RequestSign:
            {
                /*request sign */
                std::string seq(32, '2');
                signForAmop(message, seq, (uint16_t)0x38);
                break;
            }

            case AMOPPacketType::RequestCheckSign:
            {
                /*request check sign */
                std::string seq(32, '3');
                signForAmop(message, seq, (uint16_t)0x39);
                break;
            }
            case AMOPPacketType::UpdateTopicStatus:
            {
                std::string seq(32, '4');
                signForAmop(message, seq, (uint16_t)0x3a);
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


void P2PSession::signForAmop(P2PMessage::Ptr message, const std::string& seq, uint16_t cmdType)
{
    SESSION_LOG(DEBUG)
        << LOG_DESC("get request sign type[4 RequestSign 5 RequestCheckSign 6 UpdateTopicStatus]")
        << LOG_KV("type", message->packetType());
    auto self = std::weak_ptr<P2PSession>(shared_from_this());
    auto session = self.lock();
    auto service = session->service().lock();
    CallbackFuncWithSession callback = service->getHandlerByprotocolID(dev::eth::ProtocolID::AMOP);
    auto requestData =
        std::dynamic_pointer_cast<P2PMessage>(service->p2pMessageFactory()->buildMessage());
    requestData->setProtocolID(dev::eth::ProtocolID::AMOP);
    std::shared_ptr<ChannelMessage> channelMessage = std::make_shared<ChannelMessage>();

    ssize_t result = channelMessage->decode(message->buffer()->data(), message->buffer()->size());

    if (result <= 0)
    {
        CHANNEL_LOG(ERROR) << "onNodeChannelRequest decode error"
                           << LOG_KV(" package size", message->buffer()->size());
        return;
    }

    CHANNEL_LOG(DEBUG) << LOG_KV("length", message->buffer()->size())
                       << LOG_KV("type", channelMessage->type())
                       << LOG_KV("seq", channelMessage->seq())
                       << LOG_KV("result", channelMessage->result());

    channelMessage->setType(cmdType);
    channelMessage->setSeq(seq);

    std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
    channelMessage->encode(*buffer);
    requestData->setBuffer(buffer);
    requestData->setSeq(message->seq());
    SESSION_LOG(DEBUG) << "new seq:" << requestData->seq();
    NetworkException e;
    callback(e, session, requestData);
}

void P2PSession::requestRandValue(
    dev::p2p::P2PSession::Ptr session, const std::string& topicToSend, NetworkException e)
{
    auto service = session->service().lock();
    CallbackFuncWithSession callback = service->getHandlerByprotocolID(dev::eth::ProtocolID::AMOP);
    auto requestData =
        std::dynamic_pointer_cast<P2PMessage>(service->p2pMessageFactory()->buildMessage());
    requestData->setProtocolID(dev::eth::ProtocolID::AMOP);
    std::shared_ptr<ChannelMessage> channelMessage = std::make_shared<ChannelMessage>();
    std::shared_ptr<bytes> data = std::make_shared<bytes>();
    uint8_t topiclen = (uint8_t)(topicToSend.length() + 1);
    data->insert(data->end(), (byte*)&topiclen, (byte*)&topiclen + sizeof(topiclen));
    data->insert(
        data->end(), (byte*)topicToSend.c_str(), (byte*)topicToSend.c_str() + topicToSend.length());

    channelMessage->setType(0x37);
    channelMessage->setData(data->data(), data->size());
    uint32_t seq = service->p2pMessageFactory()->newSeq();
    channelMessage->setSeq(std::string(32, '1'));
    std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
    channelMessage->encode(*buffer);
    requestData->setBuffer(buffer);
    requestData->setSeq(service->p2pMessageFactory()->newSeq());
    SESSION_LOG(DEBUG) << "new seq:" << requestData->seq();
    callback(e, session, requestData);
}
