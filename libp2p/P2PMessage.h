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
/** @file P2PMessage.h
 *  @author yujiechen
 *  @date 2019.03.26
 */

#pragma once
#include <libdevcore/FixedHash.h>
#include <libethcore/Protocol.h>
#include <libnetwork/Common.h>
#include <memory>


namespace dev
{
namespace p2p
{
class P2PMessage : public dev::network::Message
{
public:
    typedef std::shared_ptr<P2PMessage> Ptr;

    /// m_length(4bytes) + m_version(2bytes) + m_protocolID(2bytes) + m_groupID(2bytes) +
    /// m_packetType(2bytes) + m_seq(4bytes)
    const static size_t HEADER_LENGTH = 16;
    const static size_t MAX_LENGTH = 1024 * 1024;  ///< The maximum length of data is 1M.

    P2PMessage() { m_buffer = std::make_shared<bytes>(); }

    virtual ~P2PMessage() {}

    virtual void encode(bytes& buffer);
    /// < If the decoding is successful, the length of the decoded data is returned; otherwise, 0 is
    /// returned.
    virtual ssize_t decode(const byte* buffer, size_t size);

    virtual void setVersion(VERSION_TYPE const& version) { m_version = version; }
    virtual VERSION_TYPE version() const { return m_version; }

    virtual uint32_t length() override { return m_length; }
    virtual void setLength(uint32_t _length) { m_length = _length; }

    virtual PROTOCOL_ID protocolID() { return m_protocolID; }
    virtual void setProtocolID(PROTOCOL_ID _protocolID) { m_protocolID = _protocolID; }
    virtual PACKET_TYPE packetType() { return m_packetType; }
    virtual void setPacketType(PACKET_TYPE _packetType) { m_packetType = _packetType; }

    virtual uint32_t seq() override { return m_seq; }
    virtual void setSeq(uint32_t _seq) { m_seq = _seq; }

    virtual std::shared_ptr<bytes> buffer() { return m_buffer; }
    virtual void setBuffer(std::shared_ptr<bytes> _buffer)
    {
        m_buffer.reset();
        m_buffer = _buffer;
    }

    virtual bool isRequestPacket() override { return (m_protocolID > 0); }
    virtual PROTOCOL_ID getResponceProtocolID()
    {
        if (isRequestPacket())
            return -m_protocolID;
        else
            return 0;
    }

protected:
    virtual void encode(bytes& buffer, std::shared_ptr<bytes> encodeBuffer);

    VERSION_TYPE m_version = 0;
    uint32_t m_length = 0;            ///< m_length = HEADER_LENGTH + length(m_buffer)
    PROTOCOL_ID m_protocolID = 0;     ///< message type, the first two bytes of information, when
                                      ///< greater than 0 is the ID of the request package.
    PACKET_TYPE m_packetType = 0;     ///< message sub type, the second two bytes of information
    uint32_t m_seq = 0;               ///< the message identify
    std::shared_ptr<bytes> m_buffer;  ///< message data

private:
    /// compress the data to be sended
    bool compress(std::shared_ptr<bytes>);
};
}  // namespace p2p
}  // namespace dev
