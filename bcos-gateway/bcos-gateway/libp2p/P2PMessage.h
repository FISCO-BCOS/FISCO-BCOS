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

#include "bcos-utilities/ObjectCounter.h"
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-gateway/libnetwork/Common.h>
#include <bcos-gateway/libnetwork/Message.h>
#include <bcos-utilities/Common.h>
#include <vector>

#define CHECK_OFFSET_WITH_THROW_EXCEPTION(offset, length)                                    \
    do                                                                                       \
    {                                                                                        \
        if (std::cmp_greater((offset), (length)))                                            \
        {                                                                                    \
            throw std::out_of_range("Out of range error, offset:" + std::to_string(offset) + \
                                    " ,length: " + std::to_string(length) +                  \
                                    " ,file: " + __FILE__ + " ,func: " + __func__ +          \
                                    " ,line: " + std::to_string(__LINE__));                  \
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
///       moduleID          : 2 bytes
class P2PMessageOptions : public bcos::ObjectCounter<P2PMessageOptions>
{
public:
    using Ptr = std::shared_ptr<P2PMessageOptions>;
    /// groupID length(2) + nodeID length(2) + dst nodeID count(1) + moduleID(2)
    const static size_t OPTIONS_MIN_LENGTH = 7;

    P2PMessageOptions() { m_srcNodeID = std::make_shared<bytes>(); }
    P2PMessageOptions(const P2PMessageOptions&) = delete;
    P2PMessageOptions(P2PMessageOptions&&) = delete;
    P2PMessageOptions& operator=(const P2PMessageOptions&) = delete;
    P2PMessageOptions& operator=(P2PMessageOptions&&) = delete;

    virtual ~P2PMessageOptions() = default;

    /// The maximum gateway transport protocol supported groupID length  65535
    constexpr static size_t MAX_GROUPID_LENGTH = 65535;
    /// The maximum gateway transport protocol supported nodeID length  65535
    constexpr static size_t MAX_NODEID_LENGTH = 65535;
    /// The maximum gateway transport protocol supported dst nodeID count  127
    constexpr static size_t MAX_DST_NODEID_COUNT = 255;

    bool encode(bytes& _buffer);
    int32_t decode(const bytesConstRef& _buffer);

public:
    uint16_t moduleID() const { return m_moduleID; }
    void setModuleID(uint16_t _moduleID) { m_moduleID = _moduleID; }

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
class P2PMessage : public Message, public bcos::ObjectCounter<P2PMessage>
{
public:
    using Ptr = std::shared_ptr<P2PMessage>;

    /// length(4) + version(2) + packetType(2) + seq(4) + ext(2)
    constexpr static size_t MESSAGE_HEADER_LENGTH = 14;
    constexpr static size_t MAX_MESSAGE_LENGTH =
        100 * 1024 * 1024;  ///< The maximum length of data is 100M.

    /// For RSA public key, the prefix length is 18 in hex, used for print log graciously
    constexpr static size_t RSA_PUBLIC_KEY_PREFIX = 18;
    constexpr static size_t RSA_PUBLIC_KEY_TRUNC = 8;
    constexpr static size_t RSA_PUBLIC_KEY_TRUNC_LENGTH = 26;

public:
    P2PMessage()
    {
        m_payload = std::make_shared<bytes>();
        m_options = std::make_shared<P2PMessageOptions>();
        m_srcP2PNodeID.reserve(512);
        m_dstP2PNodeID.reserve(512);
    }

    // ~P2PMessage() override = default;

public:
    uint32_t lengthDirect() const override { return m_length; }
    uint32_t length() const override
    {
        // The length value has been set
        if (m_length > 0)
        {
            return m_length;
        }

        // estimate the length of msg to be encoded
        int64_t length = (int64_t)payload()->size() + (int64_t)P2PMessage::MESSAGE_HEADER_LENGTH;
        if (hasOptions() && options() && options()->srcNodeID())
        {
            length += P2PMessageOptions::OPTIONS_MIN_LENGTH;
            length +=
                (int64_t)(options()->srcNodeID()->size() * (1 + options()->dstNodeIDs().size()));
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

    P2PMessageOptions::Ptr options() const { return m_options; }
    void setOptions(P2PMessageOptions::Ptr _options) { m_options = _options; }

    std::shared_ptr<bytes> payload() const { return m_payload; }
    void setPayload(std::shared_ptr<bytes> _payload) { m_payload = _payload; }

    void setRespPacket() { m_ext |= bcos::protocol::MessageExtFieldFlag::Response; }
    bool encode(bytes& _buffer) override;
    bool encode(EncodedMessage& _buffer) override;
    int32_t decode(const bytesConstRef& _buffer) override;
    bool isRespPacket() const override
    {
        return (m_ext & bcos::protocol::MessageExtFieldFlag::Response) != 0;
    }

    // compress payload if payload need to be compressed
    bool tryToCompressPayload(bytes& compressData);

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

    virtual void setExtAttributes(MessageExtAttributes::Ptr _extAttr)
    {
        m_extAttr = std::move(_extAttr);
    }
    MessageExtAttributes::Ptr extAttributes() override { return m_extAttr; }

    static inline std::string_view printP2PIDElegantly(std::string_view p2pId) noexcept
    {
        if (p2pId.length() < RSA_PUBLIC_KEY_TRUNC_LENGTH)
        {
            return p2pId;
        }
        return p2pId.substr(RSA_PUBLIC_KEY_PREFIX, RSA_PUBLIC_KEY_TRUNC);
    }

protected:
    virtual int32_t decodeHeader(const bytesConstRef& _buffer);
    virtual bool encodeHeader(bytes& _buffer);

protected:
    uint32_t m_length = 0;
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

    MessageExtAttributes::Ptr m_extAttr = nullptr;  ///< message additional attributes
};

class P2PMessageFactory : public MessageFactory, public bcos::ObjectCounter<P2PMessageFactory>
{
public:
    using Ptr = std::shared_ptr<P2PMessageFactory>;
    // virtual ~P2PMessageFactory() = default;

public:
    Message::Ptr buildMessage() override
    {
        auto message = std::make_shared<P2PMessage>();
        return message;
    }
};

inline std::ostream& operator<<(std::ostream& _out, const P2PMessage& _p2pMessage)
{
    _out << "P2PMessage {"
         << " length: " << _p2pMessage.length() << " version: " << _p2pMessage.version()
         << " packetType: " << _p2pMessage.packetType() << " seq: " << _p2pMessage.seq()
         << " ext: " << _p2pMessage.ext() << " }";
    return _out;
}

inline std::ostream& operator<<(std::ostream& _out, P2PMessage::Ptr& _p2pMessage)
{
    _out << (*_p2pMessage.get());
    return _out;
}

}  // namespace gateway
}  // namespace bcos
