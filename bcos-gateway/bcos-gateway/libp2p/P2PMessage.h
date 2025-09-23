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
#include <boost/throw_exception.hpp>
#include <utility>
#include <vector>

void CHECK_OFFSET_WITH_THROW_EXCEPTION(auto offset, auto length)
{
    if (std::cmp_greater((offset), (length)))
    {
        BOOST_THROW_EXCEPTION(
            std::out_of_range("Out of range error, offset:" + std::to_string(offset) +
                              " ,length: " + std::to_string(length) + " ,file: " + __FILE__ +
                              " ,func: " + __func__ + " ,line: " + std::to_string(__LINE__)));
    }
}

namespace bcos::gateway
{
/// Options format definition
///   options(default version):
///       groupID length    :1 bytes
///       groupID           : bytes
///       nodeID length     :2 bytes
///       src nodeID        : bytes
///       src nodeID count  :1 bytes
///       dst nodeIDs       : bytes
///       moduleID          : 2 bytes
class P2PMessageOptions
{
public:
    using Ptr = std::shared_ptr<P2PMessageOptions>;
    /// groupID length(2) + nodeID length(2) + dst nodeID count(1) + moduleID(2)
    const static size_t OPTIONS_MIN_LENGTH = 7;

    P2PMessageOptions() = default;
    P2PMessageOptions(const P2PMessageOptions&) = default;
    P2PMessageOptions(P2PMessageOptions&&) = default;
    P2PMessageOptions& operator=(const P2PMessageOptions&) = default;
    P2PMessageOptions& operator=(P2PMessageOptions&&) = default;

    virtual ~P2PMessageOptions() = default;

    /// The maximum gateway transport protocol supported groupID length  65535
    constexpr static size_t MAX_GROUPID_LENGTH = 65535;
    /// The maximum gateway transport protocol supported nodeID length  65535
    constexpr static size_t MAX_NODEID_LENGTH = 65535;
    /// The maximum gateway transport protocol supported dst nodeID count  127
    constexpr static size_t MAX_DST_NODEID_COUNT = 255;

    bool encode(bytes& _buffer) const;
    int32_t decode(const bytesConstRef& _buffer);

    uint16_t moduleID() const { return m_moduleID; }
    void setModuleID(uint16_t _moduleID) { m_moduleID = _moduleID; }

    std::string groupID() const { return m_groupID; }
    void setGroupID(const std::string& _groupID) { m_groupID = _groupID; }

    bytesConstRef srcNodeID() const { return ref(m_srcNodeID); }
    void setSrcNodeID(bytes _srcNodeID) { m_srcNodeID = std::move(_srcNodeID); }

    const std::vector<bytes>& dstNodeIDs() const { return m_dstNodeIDs; }
    void setDstNodeIDs(std::vector<bytes> _dstNodeIDs) { m_dstNodeIDs = std::move(_dstNodeIDs); }
    std::vector<bytes>& mutableDstNodeIDs() { return m_dstNodeIDs; }

protected:
    std::string m_groupID;
    bytes m_srcNodeID;
    std::vector<bytes> m_dstNodeIDs;
    uint16_t m_moduleID = 0;
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
///       moduleID          : 2 bytes
///   payload           :X bytes
class P2PMessage : public Message
{
public:
    using Ptr = std::shared_ptr<P2PMessage>;

    /// length(4) + version(2) + packetType(2) + seq(4) + ext(2)
    constexpr static size_t MESSAGE_HEADER_LENGTH = 14;

    /// For RSA public key, the prefix length is 18 in hex, used for print log graciously
    constexpr static size_t RSA_PUBLIC_KEY_PREFIX = 18;
    constexpr static size_t RSA_PUBLIC_KEY_TRUNC = 8;
    constexpr static size_t RSA_PUBLIC_KEY_TRUNC_LENGTH = 26;

    P2PMessage() = default;

    // ~P2PMessage() override = default;

    uint32_t lengthDirect() const override { return m_length; }
    uint32_t length() const override
    {
        // The length value has been set
        if (m_length > 0)
        {
            return m_length;
        }

        // estimate the length of msg to be encoded
        int64_t length = (int64_t)payload().size() + (int64_t)P2PMessage::MESSAGE_HEADER_LENGTH;
        if (hasOptions() && options().srcNodeID())
        {
            length += P2PMessageOptions::OPTIONS_MIN_LENGTH;
            length += (int64_t)(options().srcNodeID().size() * (1 + options().dstNodeIDs().size()));
        }
        return length;
    }
    // virtual void setLength(uint32_t length) { m_length = length; }

    uint16_t version() const override { return m_version; }
    virtual void setVersion(uint16_t version) { m_version = version; }

    uint16_t packetType() const override { return m_packetType; }
    virtual void setPacketType(uint16_t packetType) { m_packetType = packetType; }

    uint32_t seq() const override { return m_seq; }
    virtual void setSeq(uint32_t seq) { m_seq = seq; }

    uint16_t ext() const override { return m_ext; }
    virtual void setExt(uint16_t _ext) { m_ext |= _ext; }

    const P2PMessageOptions& options() const { return m_options; }
    void setOptions(P2PMessageOptions _options) { m_options = std::move(_options); }

    bytesConstRef payload() const { return bcos::ref(m_payload); }
    void setPayload(bytes _payload) { m_payload = std::move(_payload); }

    void setRespPacket() { m_ext |= bcos::protocol::MessageExtFieldFlag::RESPONSE; }
    bool encode(bytes& _buffer) override;
    bool encode(EncodedMessage& _buffer) const override;
    int32_t decode(const bytesConstRef& _buffer) override;
    bool isRespPacket() const override
    {
        return (m_ext & bcos::protocol::MessageExtFieldFlag::RESPONSE) != 0;
    }

    // compress payload if payload need to be compressed
    bool tryToCompressPayload(bytes& compressData) const;

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

    // Note: only for log
    std::string_view srcP2PNodeIDView() const { return printP2PIDElegantly(m_srcP2PNodeID); }
    // Note: only for log
    std::string_view dstP2PNodeIDView() const { return printP2PIDElegantly(m_dstP2PNodeID); }

    virtual void setExtAttributes(std::any _extAttr) { m_extAttr = std::move(_extAttr); }
    const std::any& extAttributes() const override { return m_extAttr; }

    static inline std::string_view printP2PIDElegantly(std::string_view p2pId) noexcept
    {
        if (p2pId.length() < RSA_PUBLIC_KEY_TRUNC_LENGTH)
        {
            return p2pId;
        }
        return p2pId.substr(RSA_PUBLIC_KEY_PREFIX, RSA_PUBLIC_KEY_TRUNC);
    }

    bool encodeHeader(bytes& _buffer) const override;

protected:
    virtual bool encodeHeaderImpl(bytes& _buffer) const;
    virtual int32_t decodeHeader(const bytesConstRef& _buffer);

    mutable uint32_t m_length = 0;
    uint16_t m_version = (uint16_t)(bcos::protocol::ProtocolVersion::V0);
    uint16_t m_packetType = 0;
    uint32_t m_seq = 0;
    mutable uint16_t m_ext = 0;

    // the src p2pNodeID, for message forward, only encode into the P2PMessageV2
    std::string m_srcP2PNodeID;
    // the dst p2pNodeID, for message forward, only encode into the P2PMessageV2
    std::string m_dstP2PNodeID;

    P2PMessageOptions m_options;  ///< options fields
    bytes m_payload;              ///< payload data

    std::any m_extAttr = nullptr;  ///< message additional attributes
};

class P2PMessageFactory : public MessageFactory
{
public:
    using Ptr = std::shared_ptr<P2PMessageFactory>;
    // virtual ~P2PMessageFactory() = default;

    Message::Ptr buildMessage() override
    {
        auto message = std::make_shared<P2PMessage>();
        return message;
    }
};

inline std::ostream& operator<<(std::ostream& _out, const P2PMessage& _p2pMessage)
{
    _out << "P2PMessage {" << " length: " << _p2pMessage.length()
         << " version: " << _p2pMessage.version() << " packetType: " << _p2pMessage.packetType()
         << " seq: " << _p2pMessage.seq() << " ext: " << _p2pMessage.ext() << " }";
    return _out;
}

inline std::ostream& operator<<(std::ostream& _out, P2PMessage::Ptr& _p2pMessage)
{
    _out << (*_p2pMessage.get());
    return _out;
}

}  // namespace bcos::gateway
