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
/**
 * @file: ChannelMessage.h
 * @author: monan
 * @date: 2018
 *
 * @file: ChannelMessage.h
 * @author: darrenyin
 * @date: 2019
 */

#pragma once

#include "ChannelException.h"
#include "Message.h"

#include <arpa/inet.h>
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <boost/lexical_cast.hpp>
#include <climits>
#include <queue>
#include <string>
#include <thread>

namespace dev
{
namespace channel
{
enum ChannelMessageType
{
    CHANNEL_RPC_REQUEST = 0x12,           // type for rpc request
    CLIENT_HEARTBEAT = 0x13,              // type for heart beat for sdk
    CLIENT_HANDSHAKE = 0x14,              // type for hand shake
    CLIENT_REGISTER_EVENT_LOG = 0x15,     // type for event log filter register request and response
    AMOP_REQUEST = 0x30,                  // type for request from sdk
    AMOP_RESPONSE = 0x31,                 // type for response to sdk
    AMOP_CLIENT_SUBSCRIBE_TOPICS = 0x32,  // type for topic request
    AMOP_MULBROADCAST = 0x35,             // type for mult broadcast
    REQUEST_TOPICCERT = 0x37,             // type request verify
    UPDATE_TOPIICSTATUS = 0x38,           // type for update status
    TRANSACTION_NOTIFY = 0x1000,          // type for  transaction notify
    BLOCK_NOTIFY = 0x1001,                // type for  block notify
    EVENT_LOG_PUSH = 0x1002               // type for event log push
};

class ChannelMessage : public Message
{
public:
    typedef std::shared_ptr<ChannelMessage> Ptr;

    ChannelMessage() : Message() {}
    virtual ~ChannelMessage() {}

    virtual void encode(bytes& buffer)
    {
        uint32_t lengthN = htonl(HEADER_LENGTH + m_data->size());
        uint16_t typeN = htons(m_type);
        int32_t resultN = htonl(m_result);

        buffer.insert(buffer.end(), (byte*)&lengthN, (byte*)&lengthN + sizeof(lengthN));
        buffer.insert(buffer.end(), (byte*)&typeN, (byte*)&typeN + sizeof(typeN));
        buffer.insert(buffer.end(), m_seq.data(), m_seq.data() + m_seq.size());
        buffer.insert(buffer.end(), (byte*)&resultN, (byte*)&resultN + sizeof(resultN));

        buffer.insert(buffer.end(), m_data->begin(), m_data->end());
    }

    virtual ssize_t decode(const byte* buffer, size_t size) { return decodeAMOP(buffer, size); }

    virtual ssize_t decodeAMOP(const byte* buffer, size_t size)
    {
        if (size < HEADER_LENGTH)
        {
            return 0;
        }

        m_length = ntohl(*((uint32_t*)&buffer[0]));

#if 0
        if (_length > MAX_LENGTH)
        {
            return -1;
        }
#endif

        if (size < m_length)
        {
            return 0;
        }

        m_type = ntohs(*((uint16_t*)&buffer[4]));
        m_seq.assign(&buffer[6], &buffer[6] + 32);
        m_result = ntohl(*((uint32_t*)&buffer[38]));

        m_data->assign(&buffer[HEADER_LENGTH], &buffer[HEADER_LENGTH] + m_length - HEADER_LENGTH);

        return m_length;
    }

protected:
    const static size_t MIN_HEADER_LENGTH = 4;

    const static size_t HEADER_LENGTH = 4 + 2 + 32 + 4;
    const static size_t MAX_LENGTH = ULONG_MAX;  // max 4G
};

class ChannelMessageFactory : public MessageFactory
{
public:
    virtual ~ChannelMessageFactory(){};
    virtual Message::Ptr buildMessage() override { return std::make_shared<ChannelMessage>(); }
};

class TopicChannelMessage : public ChannelMessage
{
public:
    typedef std::shared_ptr<TopicChannelMessage> Ptr;

    TopicChannelMessage() : ChannelMessage(){};

    TopicChannelMessage(Message* message)
    {
        m_length = message->length();
        m_type = message->type();
        m_seq = message->seq();
        m_result = message->result();
        m_data = std::make_shared<bytes>(message->data(), message->data() + message->dataSize());
    }

    virtual ~TopicChannelMessage() {}

    virtual std::string topic()
    {
        if (!(m_type == AMOP_REQUEST || m_type == AMOP_RESPONSE || m_type == TRANSACTION_NOTIFY ||
                m_type == CLIENT_REGISTER_EVENT_LOG))
        {
            throw(ChannelException(-1, "type: " + boost::lexical_cast<std::string>(m_type) +
                                           " Not ChannelMessage, ChannelMessage type must be 0x30, "
                                           "0x31 or 0x1001 or 0x15"));
        }

        if (m_data->size() < 1)
        {
            throw(ChannelException(-1, "ERROR, message length: 0"));
        }

        uint8_t topicLen = *((uint8_t*)m_data->data());

        if (m_data->size() < topicLen)
        {
            throw(ChannelException(
                -1, "ERROR, topic length topicLen: " + boost::lexical_cast<std::string>(topicLen) +
                        " size:" + boost::lexical_cast<std::string>(m_data->size())));
        }

        std::string topic((char*)m_data->data() + 1, topicLen - 1);

        return topic;
    }

    virtual void setTopicData(const std::string& topic, const byte* p, size_t size)
    {
        if (m_data->size() > 0)
        {
            throw(ChannelException(-1, "Already have data, can't set topic."));
        }

        m_data->push_back((char)topic.size() + 1);
        m_data->insert(m_data->end(), topic.begin(), topic.end());
        m_data->insert(m_data->end(), p, p + size);
    }

    virtual byte* data() override
    {
        std::string topic = this->topic();

        return m_data->data() + 1 + topic.size();
    }

    virtual size_t dataSize() override
    {
        std::string topic = this->topic();

        return m_data->size() - 1 - topic.size();
    }
    void setData(const byte*, size_t) override { assert(false); };
};


class TopicVerifyChannelMessage : public ChannelMessage
{
public:
    typedef std::shared_ptr<TopicVerifyChannelMessage> Ptr;

    TopicVerifyChannelMessage() : ChannelMessage(){};

    virtual ~TopicVerifyChannelMessage() {}
    void setData(const std::string& content)
    {
        if (m_data->size() > 0)
        {
            throw(ChannelException(-1, "Already have data, can't set data."));
        }
        m_data->insert(
            m_data->end(), (byte*)content.c_str(), (byte*)content.c_str() + content.length());
        m_type = REQUEST_TOPICCERT;
        m_seq = dev::newSeq();
    }

    void setData(const byte*, size_t) override { assert(false); };
};


}  // namespace channel

}  // namespace dev
