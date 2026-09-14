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
 * @file Message.h
 * @author: octopus
 * @date 2021-05-06
 */

#pragma once

#include "bcos-framework/protocol/Protocol.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Exceptions.h"
#include <boost/asio/buffer.hpp>
#include <boost/throw_exception.hpp>
#include <any>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

void checkOffset(auto offset, auto length)
{
    if (std::cmp_greater((offset), (length)))
    {
        bcos::throwTrace(std::out_of_range("Out of range error, offset:" + std::to_string(offset) +
                                           " ,length: " + std::to_string(length)));
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

    ~P2PMessageOptions() = default;

    /// The maximum gateway transport protocol supported groupID length  65535
    constexpr static size_t MAX_GROUPID_LENGTH = 65535;
    /// The maximum gateway transport protocol supported nodeID length  65535
    constexpr static size_t MAX_NODEID_LENGTH = 65535;
    /// The maximum gateway transport protocol supported dst nodeID count  127
    constexpr static size_t MAX_DST_NODEID_COUNT = 255;

    bool encode(bytes& _buffer) const;
    int32_t decode(const bytesConstRef& _buffer);

    uint16_t moduleID() const;
    void setModuleID(uint16_t _moduleID);

    std::string groupID() const;
    void setGroupID(const std::string& _groupID);

    bytesConstRef srcNodeID() const;
    void setSrcNodeID(bytes _srcNodeID);

    const std::vector<bytes>& dstNodeIDs() const;
    void setDstNodeIDs(std::vector<bytes> _dstNodeIDs);
    std::vector<bytes>& mutableDstNodeIDs();

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
///   extended header (version > V0 only):
///       ttl               :2 bytes
///       srcP2PNodeID len  :2 bytes
///       srcP2PNodeID      : bytes
///       dstP2PNodeID len  :2 bytes
///       dstP2PNodeID      : bytes
///   options(default version):
///       groupID length    :1 bytes
///       groupID           : bytes
///       nodeID length     :2 bytes
///       src nodeID        : bytes
///       src nodeID count  :1 bytes
///       dst nodeIDs       : bytes
///       moduleID          : 2 bytes
///   payload           :X bytes
///
/// Concrete, non-polymorphic message type: the wire-format differences between protocol
/// versions are data-driven (the version field branches inside encode/decode), so no virtual
/// dispatch is needed anywhere in the gateway.
class Message
{
public:
    using Ptr = std::shared_ptr<Message>;
    using ConstPtr = std::shared_ptr<const Message>;

    /// length(4) + version(2) + packetType(2) + seq(4) + ext(2)
    constexpr static size_t MESSAGE_HEADER_LENGTH = 14;

    /// For RSA public key, the prefix length is 18 in hex, used for print log graciously
    constexpr static size_t RSA_PUBLIC_KEY_PREFIX = 18;
    constexpr static size_t RSA_PUBLIC_KEY_TRUNC = 8;
    constexpr static size_t RSA_PUBLIC_KEY_TRUNC_LENGTH = 26;

    Message() = default;
    Message(const Message&) = default;
    Message(Message&&) = default;
    Message& operator=(Message&&) = default;
    Message& operator=(const Message&) = default;
    ~Message() noexcept = default;

    uint32_t lengthDirect() const;
    uint32_t length() const;

    uint16_t version() const;
    void setVersion(uint16_t version);

    uint16_t packetType() const;
    void setPacketType(uint16_t packetType);

    uint32_t seq() const;
    void setSeq(uint32_t seq);

    uint16_t ext() const;
    void setExt(uint16_t _ext);

    // only meaningful when version > V0 (encoded into the extended header)
    int16_t ttl() const;
    void setTTL(int16_t _ttl);

    const P2PMessageOptions& options() const;
    void setOptions(P2PMessageOptions _options);

    bytesConstRef payload() const;
    void setPayload(bytes _payload);

    void setRespPacket();
    // Deprecated: the send path encodes the header with encodeHeader and passes the payload as
    // views (see Session::fastSendMessage); kept for tests and legacy callers.
    bool encode(bytes& _buffer);
    int32_t decode(const bytesConstRef& _buffer);
    bool isRespPacket() const;

    // compress payload if payload need to be compressed
    bool tryToCompressPayload(bytes& compressData) const;

    bool hasOptions() const;

    void setSrcP2PNodeID(std::string const& _srcP2PNodeID);
    void setDstP2PNodeID(std::string const& _dstP2PNodeID);

    std::string const& srcP2PNodeID() const;
    std::string const& dstP2PNodeID() const;

    // Note: only for log
    std::string printSrcP2PNodeID() const;
    // Note: only for log
    std::string printDstP2PNodeID() const;

    void setExtAttributes(std::any _extAttr);
    const std::any& extAttributes() const;

    bool encodeHeader(bytes& _buffer) const;

protected:
    bool encodeHeaderImpl(bytes& _buffer) const;
    int32_t decodeHeader(const bytesConstRef& _buffer);

    mutable uint32_t m_length = 0;
    uint16_t m_version = (uint16_t)(bcos::protocol::ProtocolVersion::V0);
    uint16_t m_packetType = 0;
    uint32_t m_seq = 0;
    mutable uint16_t m_ext = 0;
    // time-to-live for forwarded messages, encoded only when version > V0
    int16_t m_ttl = 10;

    // the src p2pNodeID, for message forward, encoded only when version > V0
    std::string m_srcP2PNodeID;
    // the dst p2pNodeID, for message forward, encoded only when version > V0
    std::string m_dstP2PNodeID;

    P2PMessageOptions m_options;  ///< options fields
    bytes m_payload;              ///< payload data

    std::any m_extAttr = nullptr;  ///< message additional attributes
};

class MessageFactory
{
public:
    using Ptr = std::shared_ptr<MessageFactory>;

    MessageFactory() = default;
    MessageFactory(const MessageFactory&) = delete;
    MessageFactory(MessageFactory&&) = delete;
    MessageFactory& operator=(const MessageFactory&) = delete;
    MessageFactory&& operator=(MessageFactory&&) = delete;

    ~MessageFactory() = default;
    Message::Ptr buildMessage();

    uint32_t newSeq();

private:
    std::atomic<uint32_t> m_seq = {1};
};

std::ostream& operator<<(std::ostream& _out, const Message& _message);

std::ostream& operator<<(std::ostream& _out, Message::Ptr& _message);

}  // namespace bcos::gateway
