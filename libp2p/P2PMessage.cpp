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
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/SnappyCompress.h>

using namespace dev;
using namespace dev::p2p;
using namespace dev::compress;

void P2PMessage::encode(bytes& buffer)
{
    std::shared_ptr<bytes> compressData = std::make_shared<bytes>();
    /// compress success
    if (compress(compressData))
    {
        encode(buffer, compressData);
    }
    else
    {
        encode(buffer, m_buffer);
    }
}

/**
 * @brief: encode the message into bytes to send
 * @param buffer: the encoded data
 * @param encodeBuffer: the message should be encoded
 * @param protocol: the protocol ID that should be encoded into the buffer
 */
void P2PMessage::encode(bytes& buffer, std::shared_ptr<bytes> encodeBuffer)
{
    buffer.clear();  ///< It is not allowed to be assembled outside.
    m_length = HEADER_LENGTH + encodeBuffer->size();

    uint32_t length = htonl(m_length);
    VERSION_TYPE versionType = htons(m_version);
    PROTOCOL_ID protocolID = htonl(m_protocolID);
    PACKET_TYPE packetType = htons(m_packetType);
    uint32_t seq = htonl(m_seq);

    buffer.insert(buffer.end(), (byte*)&length, (byte*)&length + sizeof(length));
    buffer.insert(buffer.end(), (byte*)&versionType, (byte*)&versionType + sizeof(versionType));
    buffer.insert(buffer.end(), (byte*)&protocolID, (byte*)&protocolID + sizeof(protocolID));
    buffer.insert(buffer.end(), (byte*)&packetType, (byte*)&packetType + sizeof(packetType));
    buffer.insert(buffer.end(), (byte*)&seq, (byte*)&seq + sizeof(seq));
    buffer.insert(buffer.end(), encodeBuffer->begin(), encodeBuffer->end());
}

/// compress the data to be sended
bool P2PMessage::compress(std::shared_ptr<bytes> compressData)
{
    if (!g_BCOSConfig.compressEnabled() || m_buffer->size() <= g_BCOSConfig.c_compressThreshold)
    {
        return false;
    }
    /// the packet has already been encoded
    if ((m_version & dev::eth::CompressFlag) == dev::eth::CompressFlag)
    {
        return false;
    }
    size_t compressSize = SnappyCompress::compress(ref(*m_buffer), *compressData);
    if (compressSize < 1)
    {
        return false;
    }
    m_version |= dev::eth::CompressFlag;
    return true;
}

ssize_t P2PMessage::decode(const byte* buffer, size_t size)
{
    if (size < HEADER_LENGTH)
    {
        return dev::network::PACKET_INCOMPLETE;
    }

    int32_t offset = 0;
    m_length = ntohl(*((uint32_t*)&buffer[offset]));

    if (size < m_length)
    {
        return dev::network::PACKET_INCOMPLETE;
    }
    /// get version
    offset += sizeof(m_length);
    m_version = ntohs(*((VERSION_TYPE*)&buffer[offset]));
    /// get protocolID
    offset += sizeof(m_version);
    m_protocolID = ntohl(*((PROTOCOL_ID*)&buffer[offset]));
    /// get pacektType
    offset += sizeof(m_protocolID);
    m_packetType = ntohs(*((PACKET_TYPE*)&buffer[offset]));
    /// get sesq
    offset += sizeof(m_packetType);
    m_seq = ntohl(*((uint32_t*)&buffer[offset]));

    /// the data has been compressed
    if (g_BCOSConfig.compressEnabled() &&
        ((m_version & dev::eth::CompressFlag) == dev::eth::CompressFlag))
    {
        /// uncompress data
        SnappyCompress::uncompress(
            bytesConstRef((const byte*)(&buffer[HEADER_LENGTH]), m_length - HEADER_LENGTH),
            *m_buffer);
    }
    else
    {
        ///< TODO: assign to std::move
        m_buffer->assign(&buffer[HEADER_LENGTH], &buffer[HEADER_LENGTH] + m_length - HEADER_LENGTH);
    }
    return m_length;
}