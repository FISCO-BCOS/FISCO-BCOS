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
/** @file P2PMessage.cpp
 *  @author monan
 *  @date 20181112
 */

#include "P2PMessage.h"
#include "Common.h"

using namespace dev;
using namespace dev::p2p;

void P2PMessage::encode(bytes& buffer)
{
    buffer.clear();  ///< It is not allowed to be assembled outside.
    m_length = HEADER_LENGTH + m_buffer->size();

    uint32_t length = htonl(m_length);
    PROTOCOL_ID protocolID = htons(m_protocolID);
    PACKET_TYPE packetType = htons(m_packetType);
    uint32_t seq = htonl(m_seq);

    buffer.insert(buffer.end(), (byte*)&length, (byte*)&length + sizeof(length));
    buffer.insert(buffer.end(), (byte*)&protocolID, (byte*)&protocolID + sizeof(protocolID));
    buffer.insert(buffer.end(), (byte*)&packetType, (byte*)&packetType + sizeof(packetType));
    buffer.insert(buffer.end(), (byte*)&seq, (byte*)&seq + sizeof(seq));
    buffer.insert(buffer.end(), m_buffer->begin(), m_buffer->end());
}

ssize_t P2PMessage::decode(const byte* buffer, size_t size)
{
    if (size < HEADER_LENGTH)
    {
        return dev::network::PACKET_INCOMPLETE;
    }

    int32_t offset = 0;
    m_length = ntohl(*((uint32_t*)&buffer[offset]));

    /*if (m_length > MAX_LENGTH)
    {
        return PACKET_ERROR;
    }*/

    if (size < m_length)
    {
        return dev::network::PACKET_INCOMPLETE;
    }

    offset += sizeof(m_length);
    m_protocolID = ntohs(*((PROTOCOL_ID*)&buffer[offset]));
    offset += sizeof(m_protocolID);
    m_packetType = ntohs(*((PACKET_TYPE*)&buffer[offset]));
    offset += sizeof(m_packetType);
    m_seq = ntohl(*((uint32_t*)&buffer[offset]));
    ///< TODO: assign to std::move
    m_buffer->assign(&buffer[HEADER_LENGTH], &buffer[HEADER_LENGTH] + m_length - HEADER_LENGTH);

    return m_length;
}

void P2PMessage::encodeAMOPBuffer(std::string const& topic)
{
    ///< check protocolID is AMOP message or not
    if (dev::eth::ProtocolID::Topic != abs(m_protocolID))
    {
        return;
    }

    ///< new buffer format:topic lenght + topic data + ori buffer data
    m_buffer->insert(m_buffer->begin(), topic.begin(), topic.end());
    uint32_t topicLen = htonl(topic.size());
    m_buffer->insert(m_buffer->begin(), (byte*)&topicLen, (byte*)&topicLen + sizeof(topicLen));
}

ssize_t P2PMessage::decodeAMOPBuffer(std::shared_ptr<bytes> buffer, std::string& topic)
{
    ///< check protocolID is AMOP message or not
    if (dev::eth::ProtocolID::Topic != abs(m_protocolID))
    {
        return dev::network::PACKET_ERROR;
    }

    uint32_t topicLen = ntohl(*((uint32_t*)m_buffer->data()));
    P2PMSG_LOG(TRACE) << "Message::decodeAMOPBuffer topic len=" << topicLen
                      << ", buffer size=" << m_buffer->size();
    if (topicLen + 4 > m_buffer->size())
    {
        return dev::network::PACKET_ERROR;
    }
    topic = std::string((char*)(m_buffer->data()) + 4, topicLen);
    buffer->insert(buffer->end(), m_buffer->begin() + 4 + topicLen, m_buffer->end());

    return buffer->size();
}
