/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file AMOPRequest.h
 * @author: octopus
 * @date 2021-08-23
 */
#pragma once
#include <bcos-utilities/Common.h>
#include <boost/asio/detail/socket_ops.hpp>

// check offset length overflow when decode message
#define OFFSET_CHECK(offset, step, length)                                                  \
    do                                                                                      \
    {                                                                                       \
        if (offset + step > length)                                                         \
        {                                                                                   \
            throw std::runtime_error("offset overflow, offset: " + std::to_string(offset) + \
                                     ",step: " + std::to_string(step) +                     \
                                     " ,length: " + std::to_string(length));                \
        }                                                                                   \
    } while (0)

namespace bcos
{
namespace protocol
{
class AMOPRequest
{
public:
    using Ptr = std::shared_ptr<AMOPRequest>;
    AMOPRequest() = default;
    AMOPRequest(bcos::bytesConstRef _data) { decode(_data); }
    virtual ~AMOPRequest() {}

    // topic field length
    const static size_t TOPIC_MAX_LENGTH = 65535;
    const static size_t MESSAGE_MIN_LENGTH = 2;


    std::string topic() const { return m_topic; }
    void setTopic(const std::string& _topic) { m_topic = _topic; }
    void setData(bcos::bytesConstRef _data) { m_data = _data; }
    bcos::bytesConstRef data() const { return m_data; }

    virtual bool encode(bcos::bytes& _buffer)
    {
        if (m_topic.size() > TOPIC_MAX_LENGTH)
        {
            return false;
        }
        // the version
        auto version = boost::asio::detail::socket_ops::host_to_network_long(m_version);
        auto versionLen = sizeof(m_version) / sizeof(uint8_t);
        _buffer.insert(_buffer.end(), (byte*)&version, (byte*)&version + versionLen);
        // the topic length
        uint16_t length =
            boost::asio::detail::socket_ops::host_to_network_short((uint16_t)m_topic.size());
        _buffer.insert(_buffer.end(), (byte*)&length, (byte*)&length + 2);
        // the topic data
        _buffer.insert(_buffer.end(), m_topic.begin(), m_topic.end());
        // the data
        _buffer.insert(_buffer.end(), m_data.begin(), m_data.end());
        return true;
    }
    virtual int64_t decode(bcos::bytesConstRef _data)
    {
        if (_data.size() < MESSAGE_MIN_LENGTH)
        {
            return -1;
        }

        try
        {
            std::size_t length = _data.size();
            std::size_t offset = 0;
            // decode version
            auto versionLen = sizeof(m_version) / sizeof(uint8_t);
            OFFSET_CHECK(offset, versionLen, length);
            offset += versionLen;
            m_version =
                boost::asio::detail::socket_ops::network_to_host_long(*((uint32_t*)_data.data()));

            // decode topicLength
            OFFSET_CHECK(offset, 2, length);
            uint16_t topicLen = boost::asio::detail::socket_ops::network_to_host_short(
                *((uint16_t*)(_data.data() + versionLen)));
            offset += 2;

            // decode topic
            OFFSET_CHECK(offset, topicLen, length);
            m_topic = std::string(_data.data() + offset, _data.data() + offset + topicLen);
            offset += topicLen;

            // decode data
            m_data = _data.getCroppedData(offset);
            return _data.size();
        }
        catch (const std::string&)
        {
            return -1;
        }
    }

    virtual uint32_t version() const { return m_version; }
    virtual void setVersion(uint32_t _version) { m_version = _version; }

private:
    std::string m_topic;
    bcos::bytesConstRef m_data = bcos::bytesConstRef();
    uint32_t m_version = 0;
};

class AMOPRequestFactory
{
public:
    using Ptr = std::shared_ptr<AMOPRequestFactory>;
    AMOPRequestFactory() = default;
    virtual ~AMOPRequestFactory() {}

    std::shared_ptr<AMOPRequest> buildRequest() { return std::make_shared<AMOPRequest>(); }
    std::shared_ptr<AMOPRequest> buildRequest(bcos::bytesConstRef _data)
    {
        return std::make_shared<AMOPRequest>(_data);
    }
};

}  // namespace protocol
}  // namespace bcos
