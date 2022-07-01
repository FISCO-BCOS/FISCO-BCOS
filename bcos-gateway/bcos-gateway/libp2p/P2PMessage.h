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
 * @file P2PMessage.h
 * @author: octopus
 * @date 2021-05-04
 */

#pragma once

#include <bcos-framework/protocol/Protocol.h>
#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-gateway/libnetwork/Message.h>
#include <bcos-utilities/Common.h>
#include <vector>

#define CHECK_OFFSET_WITH_THROW_EXCEPTION(offset, length)                                    \
    do                                                                                       \
    {                                                                                        \
        if ((offset) > (length))                                                             \
        {                                                                                    \
            throw std::out_of_range("Out of range error, offset:" + std::to_string(offset) + \
                                    " ,length: " + std::to_string(length));                  \
        }                                                                                    \
    } while (0);

namespace bcos
{
namespace gateway
{
/// Options format definition
///   options(default version):
///       groupID length    :1 bytes
///       groupID           : bytes
///       nodeID length     :2 bytes
///       src nodeID        : bytes
///       src nodeID count  :1 bytes
///       dst nodeIDs       : bytes
class P2PMessageOptions
{
public:
    using Ptr = std::shared_ptr<P2PMessageOptions>;
    /// groupID length(1) + nodeID length(2) + dst nodeID count(1)
    const static size_t OPTIONS_MIN_LENGTH = 5;

public:
    P2PMessageOptions() { m_srcNodeID = std::make_shared<bytes>(); }

    virtual ~P2PMessageOptions() {}

    /// The maximum gateway transport protocol supported groupID length  65535
    const static size_t MAX_GROUPID_LENGTH = 65535;
    /// The maximum gateway transport protocol supported nodeID length  65535
    const static size_t MAX_NODEID_LENGTH = 65535;
    /// The maximum gateway transport protocol supported dst nodeID count  127
    const static size_t MAX_DST_NODEID_COUNT = 255;

    bool encode(bytes& _buffer);
    ssize_t decode(bytesConstRef _buffer);

public:
    std::string groupID() const { return m_groupID; }
    void setGroupID(const std::string& _groupID) { m_groupID = _groupID; }

    std::shared_ptr<bytes> srcNodeID() const { return m_srcNodeID; }
    void setSrcNodeID(std::shared_ptr<bytes> _srcNodeID) { m_srcNodeID = _srcNodeID; }

    std::vector<std::shared_ptr<bytes>>& dstNodeIDs() { return m_dstNodeIDs; }
    void setDstNodeIDs(const std::vector<std::shared_ptr<bytes>>& _dstNodeIDs)
    {
        m_dstNodeIDs = _dstNodeIDs;
    }

protected:
    std::string m_groupID;
    std::shared_ptr<bytes> m_srcNodeID;
    std::vector<std::shared_ptr<bytes>> m_dstNodeIDs;
};

/// Message format definition of gateway P2P network
///
/// fields:
///   length            :4 bytes
///   version           :2 bytes
///   packet type       :2 bytes
///   seq               :4 bytes
///   ext               :2 bytes
///   options(default version):
///       groupID length    :1 bytes
///       groupID           : bytes
///       nodeID length     :2 bytes
///       src nodeID        : bytes
///       src nodeID count  :1 bytes
///       dst nodeIDs       : bytes
///   payload           :X bytes
class P2PMessage : public Message
{
public:
    using Ptr = std::shared_ptr<P2PMessage>;

    /// length(4) + version(2) + packetType(2) + seq(4) + ext(2)
    const static size_t MESSAGE_HEADER_LENGTH = 14;
    const static size_t MAX_MESSAGE_LENGTH =
        100 * 1024 * 1024;  ///< The maximum length of data is 100M.
public:
    P2PMessage()
    {
        m_payload = std::make_shared<bytes>();
        m_options = std::make_shared<P2PMessageOptions>();
    }

    virtual ~P2PMessage() {}

public:
    uint32_t length() const override { return m_length; }
    virtual void setLength(uint32_t length) { m_length = length; }

    uint16_t version() const override { return m_version; }
    virtual void setVersion(uint16_t version) { m_version = version; }

    uint16_t packetType() const override { return m_packetType; }
    virtual void setPacketType(uint16_t packetType) { m_packetType = packetType; }

    uint32_t seq() const override { return m_seq; }
    virtual void setSeq(uint32_t seq) { m_seq = seq; }

    uint16_t ext() const override { return m_ext; }
    virtual void setExt(uint16_t _ext) { m_ext = _ext; }

    P2PMessageOptions::Ptr options() const { return m_options; }
    void setOptions(P2PMessageOptions::Ptr _options) { m_options = _options; }

    std::shared_ptr<bytes> payload() const { return m_payload; }
    void setPayload(std::shared_ptr<bytes> _payload) { m_payload = _payload; }

    void setRespPacket() { m_ext |= bcos::protocol::MessageExtFieldFlag::Response; }
    bool encode(bytes& _buffer) override;
    ssize_t decode(bytesConstRef _buffer) override;
    bool isRespPacket() const override
    {
        return (m_ext & bcos::protocol::MessageExtFieldFlag::Response) != 0;
    }
    bool hasOptions() const
    {
        return (m_packetType == GatewayMessageType::PeerToPeerMessage) ||
               (m_packetType == GatewayMessageType::BroadcastMessage);
    }

    virtual void setSrcP2PNodeID(std::string const& _srcP2PNodeID)
    {
        m_srcP2PNodeID = _srcP2PNodeID;
    }
    virtual void setDstP2PNodeID(std::string const& _dstP2PNodeID)
    {
        m_dstP2PNodeID = _dstP2PNodeID;
    }

    std::string const& srcP2PNodeID() const override { return m_srcP2PNodeID; }
    std::string const& dstP2PNodeID() const override { return m_dstP2PNodeID; }

    void appendRateLimiter(ratelimit::BWRateLimiterInterface::Ptr _rateLimiterInterface)
    {
        m_rateLimiters.push_back(_rateLimiterInterface);
    }

    // For ratelimit token revert
    virtual const std::vector<ratelimit::BWRateLimiterInterface::Ptr>& rateLimiters() const override
    {
        return m_rateLimiters;
    }

    bool countForBWLimit() const { return m_countForBWLimit; }
    void setCountForBWLimit(bool _countForBWLimit) { m_countForBWLimit = _countForBWLimit; }

protected:
    virtual ssize_t decodeHeader(bytesConstRef _buffer);
    virtual bool encodeHeader(bytes& _buffer);

protected:
    uint32_t m_length;
    uint16_t m_version = (uint16_t)(bcos::protocol::ProtocolVersion::V0);
    uint16_t m_packetType = 0;
    uint32_t m_seq = 0;
    uint16_t m_ext = 0;

    // the src p2pNodeID, for message forward, only encode into the P2PMessageV2
    std::string m_srcP2PNodeID;
    // the dst p2pNodeID, for message forward, only encode into the P2PMessageV2
    std::string m_dstP2PNodeID;

    P2PMessageOptions::Ptr m_options;  ///< options fields

    std::shared_ptr<bytes> m_payload;  ///< payload data

private:
    bool m_countForBWLimit =
        false;  // For bandwidth limits, if this message count for bandwidth limits
    //  For ratelimit token revert
    std::vector<ratelimit::BWRateLimiterInterface::Ptr> m_rateLimiters{4};
};

class P2PMessageFactory : public MessageFactory
{
public:
    using Ptr = std::shared_ptr<P2PMessageFactory>;
    virtual ~P2PMessageFactory() {}

public:
    virtual Message::Ptr buildMessage()
    {
        auto message = std::make_shared<P2PMessage>();
        return message;
    }
};

inline std::ostream& operator<<(std::ostream& _out, const P2PMessage _p2pMessage)
{
    _out << "P2PMessage {"
         << " length: " << _p2pMessage.length() << " version: " << _p2pMessage.version()
         << " packetType: " << _p2pMessage.packetType() << " seq: " << _p2pMessage.seq()
         << " ext: " << _p2pMessage.ext() << " }";
    return _out;
}

inline std::ostream& operator<<(std::ostream& _out, P2PMessage::Ptr _p2pMessage)
{
    _out << (*_p2pMessage.get());
    return _out;
}

}  // namespace gateway
}  // namespace bcos
