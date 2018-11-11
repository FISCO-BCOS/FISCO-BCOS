/*
 * P2PMessage.h
 *
 *  Created on: 2018年11月11日
 *      Author: monan
 */

#pragma once

#include <libnetwork/Common.h>
#include <memory>
#include <libdevcore/FixedHash.h>

namespace dev {
namespace p2p {

class P2PMessage : public Message
{
public:
    typedef std::shared_ptr<P2PMessage> Ptr;

    const static size_t HEADER_LENGTH = 12;
    const static size_t MAX_LENGTH = 1024 * 1024;  ///< The maximum length of data is 1M.

    P2PMessage() { m_buffer = std::make_shared<bytes>(); }

    virtual ~P2PMessage() {}

    virtual uint32_t length() override { return m_length; }
    virtual void setLength(uint32_t _length) { m_length = _length; }

    virtual PROTOCOL_ID protocolID() { return m_protocolID; }
    virtual void setProtocolID(PROTOCOL_ID _protocolID) { m_protocolID = _protocolID; }
    virtual PACKET_TYPE packetType() { return m_packetType; }
    virtual void setPacketType(PACKET_TYPE _packetType) { m_packetType = _packetType; }

    virtual uint32_t seq() override { return m_seq; }
    virtual void setSeq(uint32_t _seq) { m_seq = _seq; }

    virtual std::shared_ptr<bytes> buffer() { return m_buffer; }
    virtual void setBuffer(std::shared_ptr<bytes> _buffer) { m_buffer = _buffer; }

    virtual bool isRequestPacket() { return (m_protocolID > 0); }
    virtual PROTOCOL_ID getResponceProtocolID()
    {
        if (isRequestPacket())
            return -m_protocolID;
        else
            return 0;
    }

    virtual void encode(bytes& buffer) override;

    /// < If the decoding is successful, the length of the decoded data is returned; otherwise, 0 is
    /// returned.
    virtual ssize_t decode(const byte* buffer, size_t size) override;

    ///< This buffer param is the m_buffer member stored in struct Messger, and the topic info will
    ///< be encoded in buffer.
    void encodeAMOPBuffer(std::string const& topic);
    virtual ssize_t decodeAMOPBuffer(std::shared_ptr<bytes> buffer, std::string& topic);

    void printMsgWithPrefix(std::string const& strPrefix)
    {
        std::stringstream strMsg;
        strMsg << strPrefix << "Message(" << m_length << "," << m_protocolID << "," << m_packetType
               << "," << m_seq << ",";
        if (dev::eth::ProtocolID::Topic != abs(m_protocolID))
        {
            strMsg << std::string((const char*)m_buffer->data(), m_buffer->size());
        }
        else
        {
            std::string topic;
            std::shared_ptr<bytes> temp = std::make_shared<bytes>();
            decodeAMOPBuffer(temp, topic);
            strMsg << topic << "," << std::string((const char*)temp->data(), temp->size());
        }
        strMsg << ")";
        LOG(INFO) << strMsg.str();
    }

private:
    uint32_t m_length = 0;            ///< m_length = HEADER_LENGTH + length(m_buffer)
    PROTOCOL_ID m_protocolID = 0;     ///< message type, the first two bytes of information, when
                                      ///< greater than 0 is the ID of the request package.
    PACKET_TYPE m_packetType = 0;     ///< message sub type, the second two bytes of information
    uint32_t m_seq = 0;               ///< the message identify
    std::shared_ptr<bytes> m_buffer;  ///< message data
};

enum AMOPPacketType
{
    SendTopicSeq = 1,
    RequestTopics = 2,
    SendTopics = 3
};

class P2PMessageFactory : public MessageFactory
{
public:
    virtual ~P2PMessageFactory() {}
    virtual Message::Ptr buildMessage() override
    {
        auto message = std::make_shared<P2PMessage>();
        uint32_t seq = ++m_seq;
        message->setSeq(seq);

        return message;
    }

    std::atomic<uint32_t> m_seq = {1};
};

}
}
