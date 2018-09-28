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
/** @file P2PMsgHandler.cpp
 *  @author chaychen
 *  @date 20180916
 */

#include "P2PMsgHandler.h"

namespace dev
{
namespace p2p
{
bool P2PMsgHandler::addProtocolID2Handler(
    int16_t protocolID, CallbackFuncWithSession const& callback)
{
    RecursiveGuard l(x_protocolID2Handler);
    if (m_protocolID2Handler->find(protocolID) == m_protocolID2Handler->end())
    {
        m_protocolID2Handler->insert(std::make_pair(protocolID, callback));
        return true;
    }
    else
    {
        LOG(ERROR) << "Handler repetition! ProtocolID = " << protocolID;
        return false;
    }
}

bool P2PMsgHandler::getHandlerByProtocolID(int16_t protocolID, CallbackFuncWithSession& callback)
{
    RecursiveGuard l(x_protocolID2Handler);
    auto it = m_protocolID2Handler->find(protocolID);
    if (it != m_protocolID2Handler->end())
    {
        callback = it->second;
        return true;
    }
    else
    {
        LOG(ERROR) << "Handler not found! ProtocolID = " << protocolID;
        return false;
    }
}

bool P2PMsgHandler::eraseHandlerByProtocolID(int16_t protocolID)
{
    RecursiveGuard l(x_protocolID2Handler);
    if (m_protocolID2Handler->find(protocolID) != m_protocolID2Handler->end())
    {
        m_protocolID2Handler->erase(protocolID);
        return true;
    }
    else
    {
        LOG(ERROR) << "Handler not found! ProtocolID = " << protocolID;
        return false;
    }
}

bool P2PMsgHandler::addTopic2Handler(
    std::string const& topic, CallbackFuncWithSession const& callback)
{
    RecursiveGuard l(x_topic2Handler);
    if (m_topic2Handler->find(topic) == m_topic2Handler->end())
    {
        m_topic2Handler->insert(std::make_pair(topic, callback));
        return true;
    }
    else
    {
        LOG(ERROR) << "Handler repetition! Topic = " << topic;
        return false;
    }
}

bool P2PMsgHandler::getHandlerByTopic(std::string const& topic, CallbackFuncWithSession& callback)
{
    RecursiveGuard l(x_topic2Handler);
    auto it = m_topic2Handler->find(topic);
    if (it != m_topic2Handler->end())
    {
        callback = it->second;
        return true;
    }
    else
    {
        LOG(ERROR) << "Handler not found! Topic = " << topic;
        return false;
    }
}

bool P2PMsgHandler::eraseHandlerByTopic(std::string const& topic)
{
    RecursiveGuard l(x_topic2Handler);
    if (m_topic2Handler->find(topic) != m_topic2Handler->end())
    {
        m_topic2Handler->erase(topic);
        return true;
    }
    else
    {
        LOG(ERROR) << "Handler not found! Topic = " << topic;
        return false;
    }
}

void P2PMsgHandler::registerAMOP()
{
    addProtocolID2Handler(dev::eth::ProtocolID::AMOP,
        [](P2PException e, std::shared_ptr<Session> s, Message::Ptr msg) {
            msg->printMsgWithPrefix("Message received in AMOP handler,");
            switch (msg->packetType())
            {
            case AMOPPacketType::SendTopicSeq:
            {
                uint32_t msgSeq = std::stoul(
                    std::string((const char*)msg->buffer()->data(), msg->buffer()->size()));
                uint32_t recordSeq = s->peerTopicSeq(s->id());
                LOG(INFO) << "HandleSendTopicSeq recordSeq:" << recordSeq << ", msgSeq:" << msgSeq;
                if (recordSeq < msgSeq)
                {
                    Message::Ptr retMsg = std::make_shared<Message>();
                    retMsg->setProtocolID(dev::eth::ProtocolID::AMOP);
                    retMsg->setPacketType(AMOPPacketType::RequestTopics);
                    retMsg->setLength(Message::HEADER_LENGTH + retMsg->buffer()->size());
                    std::shared_ptr<bytes> msgBuf = std::make_shared<bytes>();
                    retMsg->encode(*msgBuf);
                    s->send(msgBuf);
                }
                break;
            }
            case AMOPPacketType::RequestTopics:
            {
                Message::Ptr retMsg = std::make_shared<Message>();
                retMsg->setProtocolID(dev::eth::ProtocolID::AMOP);
                retMsg->setPacketType(AMOPPacketType::SendTopics);
                stringstream str;
                str << to_string(s->host()->topicSeq()) << "|";
                std::shared_ptr<std::vector<std::string>> topics = s->host()->topics();
                if (topics->size() > 0)
                {
                    for (auto const& topic : *topics)
                    {
                        str << topic << "|";
                    }
                }
                std::shared_ptr<bytes> buffer = std::make_shared<bytes>();
                std::string content = str.str();
                LOG(INFO) << "HandleRequestTopics, the buffer content of message sent:" << content;
                buffer->assign(content.begin(), content.end());
                retMsg->setBuffer(buffer);
                retMsg->setLength(Message::HEADER_LENGTH + retMsg->buffer()->size());
                std::shared_ptr<bytes> msgBuf = std::make_shared<bytes>();
                retMsg->encode(*msgBuf);
                s->send(msgBuf);
                break;
            }
            case AMOPPacketType::SendTopics:
            {
                std::string buffer =
                    std::string((const char*)msg->buffer()->data(), msg->buffer()->size());
                std::shared_ptr<std::vector<std::string>> topics =
                    std::make_shared<std::vector<std::string>>();
                uint32_t topicSeq = 0;
                ///< split implement
                auto last = 0;
                auto index = buffer.find_first_of("|", last);
                while (index != std::string::npos)
                {
                    if (0 != topicSeq)
                    {
                        topics->push_back(buffer.substr(last, index - last));
                    }
                    else
                    {
                        topicSeq = std::stoul(buffer.substr(last, index - last));
                    }
                    last = index + 1;
                    index = buffer.find_first_of("|", last);
                }
                LOG(INFO) << "HandleSendTopics, counter node topicSeq:" << topicSeq
                          << ", topics size:" << topics->size();
                s->setTopicsAndTopicSeq(s->id(), topics, topicSeq);
                break;
            }
            default:
            {
                LOG(INFO) << "Invalid packetType in AMOP message.";
                break;
            }
            }
        });
}

}  // namespace p2p

}  // namespace dev
