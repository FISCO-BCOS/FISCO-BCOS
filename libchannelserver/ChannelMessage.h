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
/**
 * @file: ChannelMessage.h
 * @author: monan
 * @date: 2018
 */

#pragma once

#include "ChannelException.h"
#include "Message.h"
#include "http_parser.h"
#include <arpa/inet.h>
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/easylog.h>
#include <boost/lexical_cast.hpp>
#include <queue>
#include <string>
#include <thread>

namespace dev
{
namespace channel
{
class ChannelMessage : public Message
{
public:
    typedef std::shared_ptr<ChannelMessage> Ptr;

    ChannelMessage() : Message() {}
    virtual ~ChannelMessage() {}

    virtual void encode(bytes& buffer)
    {
        uint32_t lengthN = htonl(HEADER_LENGTH + _data->size());
        uint16_t typeN = htons(_type);
        int32_t resultN = htonl(_result);

        buffer.insert(buffer.end(), (byte*)&lengthN, (byte*)&lengthN + sizeof(lengthN));
        buffer.insert(buffer.end(), (byte*)&typeN, (byte*)&typeN + sizeof(typeN));
        buffer.insert(buffer.end(), _seq.data(), _seq.data() + _seq.size());
        buffer.insert(buffer.end(), (byte*)&resultN, (byte*)&resultN + sizeof(resultN));

        buffer.insert(buffer.end(), _data->begin(), _data->end());
    }

    virtual ssize_t decode(const byte* buffer, size_t size) { return decodeAMOP(buffer, size); }

    virtual ssize_t decodeAMOP(const byte* buffer, size_t size)
    {
        if (size < HEADER_LENGTH)
        {
            return 0;
        }

        _length = ntohl(*((uint32_t*)&buffer[0]));

        if (_length > MAX_LENGTH)
        {
            return -1;
        }

        if (size < _length)
        {
            return 0;
        }

        _type = ntohs(*((uint16_t*)&buffer[4]));
        _seq.assign(&buffer[6], &buffer[6] + 32);
        _result = ntohl(*((uint32_t*)&buffer[38]));

        _data->assign(&buffer[HEADER_LENGTH], &buffer[HEADER_LENGTH] + _length - HEADER_LENGTH);

        return _length;
    }

protected:
    const static size_t MIN_HEADER_LENGTH = 4;

    const static size_t HEADER_LENGTH = 4 + 2 + 32 + 4;
    const static size_t MAX_LENGTH = 10 * 1024 * 1024;  // max 10M
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
        _length = message->length();
        _type = message->type();
        _seq = message->seq();
        _result = message->result();

        _data = std::make_shared<bytes>(message->data(), message->data() + message->dataSize());
    }

    virtual ~TopicChannelMessage() {}

    virtual std::string topic()
    {
        if (!(_type == 0x30 || _type == 0x31 || _type == 0x1001))
        {
            throw(ChannelException(
                -1, "type: " + boost::lexical_cast<std::string>(_type) +
                        " Not ChannelMessage, ChannelMessage type must be 0x30, 0x31 or 0x1001"));
        }

        if (_data->size() < 1)
        {
            throw(ChannelException(-1, "ERROR, message length: 0"));
        }

        uint8_t topicLen = *((uint8_t*)_data->data());

        if (_data->size() < topicLen)
        {
            throw(ChannelException(
                -1, "ERROR, topic length topicLen: " + boost::lexical_cast<std::string>(topicLen) +
                        " size:" + boost::lexical_cast<std::string>(_data->size())));
        }

        std::string topic((char*)_data->data() + 1, topicLen - 1);

        return topic;
    }

    virtual void setTopic(const std::string& topic)
    {
        if (_data->size() > 0)
        {
            throw(ChannelException(-1, "Already have data, can't set topic."));
        }

        _data->push_back((char)topic.size() + 1);
        _data->insert(_data->end(), topic.begin(), topic.end());
    }

    virtual byte* data() override
    {
        std::string topic = this->topic();

        return _data->data() + 1 + topic.size();
    }

    virtual size_t dataSize() override
    {
        std::string topic = this->topic();

        return _data->size() - 1 - topic.size();
    }

    virtual void setData(const byte* p, size_t size) override
    {
        if (_data->empty())
        {
            throw(ChannelException(-1, "Topic not set, can't set data."));
        }

        _data->insert(_data->end(), p, p + size);
    }
};

}  // namespace channel

}  // namespace dev
