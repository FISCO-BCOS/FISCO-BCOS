
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

using namespace bcos;
using namespace bcos::p2p;

void P2PMessage::encode(bytes& buffer)
{
    buffer.clear();  ///< It is not allowed to be assembled outside.
    m_length = HEADER_LENGTH + m_buffer->size();

    uint32_t length = htonl(m_length);
    PROTOCOL_ID protocolID = htons(m_protocolID);
    PACKET_TYPE packetType = htons(m_packetType);
    uint32_t seq = htonl(m_seq);

    buffer.insert(buffer.end(), (byte*)&length, (byte*)&length + 4);
    buffer.insert(buffer.end(), (byte*)&protocolID, (byte*)&protocolID + 2);
    buffer.insert(buffer.end(), (byte*)&packetType, (byte*)&packetType + 2);
    buffer.insert(buffer.end(), (byte*)&seq, (byte*)&seq + 4);
    buffer.insert(buffer.end(), m_buffer->begin(), m_buffer->end());
}

ssize_t P2PMessage::decode(const byte* buffer, size_t size)
{
    if (size < HEADER_LENGTH)
    {
        return bcos::network::PACKET_INCOMPLETE;
    }

    int32_t offset = 0;
    m_length = ntohl(*((uint32_t*)&buffer[offset]));

    /*if (m_length > MAX_LENGTH)
    {
        return PACKET_ERROR;
    }*/

    if (size < m_length)
    {
        return bcos::network::PACKET_INCOMPLETE;
    }

    offset += 4;
    m_protocolID = ntohs(*((PROTOCOL_ID*)&buffer[offset]));
    offset += 2;
    m_packetType = ntohs(*((PACKET_TYPE*)&buffer[offset]));
    offset += 2;
    m_seq = ntohl(*((uint32_t*)&buffer[offset]));
    ///< TODO: assign to std::move
    m_buffer->assign(&buffer[HEADER_LENGTH], &buffer[HEADER_LENGTH] + m_length - HEADER_LENGTH);

    return m_length;
}
