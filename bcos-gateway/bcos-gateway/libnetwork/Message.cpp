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
 * @file Message.cpp
 * @author: octopus
 * @date 2021-05-04
 */

#include "bcos-gateway/libnetwork/Message.h"
#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-gateway/Common.h"
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/ZstdCompress.h"
#include <boost/exception/diagnostic_information.hpp>
#include <utility>

// same log tag as the former libp2p message implementation
#define P2PMSG_LOG(LEVEL) BCOS_LOG(LEVEL) << "[P2PService][P2PMessage]"

using namespace bcos;
using namespace bcos::gateway;

bool P2PMessageOptions::encode(bytes& _buffer) const
{
    // parameters check
    if (m_groupID.size() > MAX_GROUPID_LENGTH) [[unlikely]]
    {
        P2PMSG_LOG(ERROR) << LOG_DESC("groupID length overflow")
                          << LOG_KV("groupID length", m_groupID.size());
        return false;
    }
    if (m_srcNodeID.empty() || (m_srcNodeID.size() > MAX_NODEID_LENGTH))
    {
        P2PMSG_LOG(ERROR) << LOG_DESC("srcNodeID length valid")
                          << LOG_KV("srcNodeID length", m_srcNodeID.size());
        return false;
    }
    if (m_dstNodeIDs.size() > MAX_DST_NODEID_COUNT)
    {
        P2PMSG_LOG(ERROR) << LOG_DESC("dstNodeID amount overfow")
                          << LOG_KV("dstNodeID size", m_dstNodeIDs.size());
        return false;
    }

    for (const auto& dstNodeID : m_dstNodeIDs)
    {
        if (dstNodeID.empty() || (dstNodeID.size() > MAX_NODEID_LENGTH))
        {
            P2PMSG_LOG(ERROR) << LOG_DESC("dstNodeID length valid")
                              << LOG_KV("dstNodeID length", dstNodeID.size());
            return false;
        }
    }

    // groupID length
    uint16_t groupIDLength =
        boost::asio::detail::socket_ops::host_to_network_short((uint16_t)m_groupID.size());
    _buffer.insert(
        _buffer.end(), (byte*)&groupIDLength, (byte*)&groupIDLength + sizeof(groupIDLength));
    // groupID
    _buffer.insert(_buffer.end(), m_groupID.begin(), m_groupID.end());

    // nodeID length
    uint16_t nodeIDLength =
        boost::asio::detail::socket_ops::host_to_network_short((uint16_t)m_srcNodeID.size());
    _buffer.insert(
        _buffer.end(), (byte*)&nodeIDLength, (byte*)&nodeIDLength + sizeof(nodeIDLength));
    // srcNodeID
    _buffer.insert(_buffer.end(), m_srcNodeID.begin(), m_srcNodeID.end());

    // dstNodeID count
    auto dstNodeIDCount = static_cast<uint8_t>(m_dstNodeIDs.size());
    _buffer.insert(
        _buffer.end(), (byte*)&dstNodeIDCount, (byte*)&dstNodeIDCount + sizeof(dstNodeIDCount));

    // dstNodeIDs
    for (const auto& nodeID : m_dstNodeIDs)
    {
        _buffer.insert(_buffer.end(), nodeID.begin(), nodeID.end());
    }

    // moduleID
    uint16_t moduleID = boost::asio::detail::socket_ops::host_to_network_short(m_moduleID);
    _buffer.insert(_buffer.end(), (byte*)&moduleID, (byte*)&moduleID + sizeof(moduleID));

    return true;
}

///       groupID length    :1 bytes
///       groupID           : bytes
///       nodeID length     :2 bytes
///       src nodeID        : bytes
///       src nodeID count  :1 bytes
///       dst nodeIDs       : bytes
int32_t P2PMessageOptions::decode(const bytesConstRef& _buffer)
{
    int32_t offset = 0;
    std::size_t length = _buffer.size();

    try
    {
        checkOffset((offset + OPTIONS_MIN_LENGTH), length);

        // groupID length
        uint16_t groupIDLength =
            boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));
        offset += 2;

        // FIB-67: validate groupID length against protocol maximum
        if (groupIDLength > MAX_GROUPID_LENGTH)
        {
            P2PMSG_LOG(ERROR) << LOG_DESC("decode: groupID length overflow")
                              << LOG_KV("groupIDLength", groupIDLength);
            return MessageDecodeStatus::MESSAGE_ERROR;
        }

        // groupID
        if (groupIDLength > 0)
        {
            checkOffset(offset + groupIDLength, length);
            m_groupID.assign(&_buffer[offset], &_buffer[offset] + groupIDLength);
            offset += groupIDLength;
        }

        // nodeID length
        checkOffset(offset + 2, length);
        uint16_t nodeIDLength =
            boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));
        offset += 2;

        // FIB-67: validate nodeID length against protocol maximum
        if (nodeIDLength > MAX_NODEID_LENGTH) [[unlikely]]
        {
            P2PMSG_LOG(ERROR) << LOG_DESC("decode: nodeID length overflow")
                              << LOG_KV("nodeIDLength", nodeIDLength);
            return MessageDecodeStatus::MESSAGE_ERROR;
        }

        checkOffset(offset + nodeIDLength, length);
        m_srcNodeID.clear();
        m_srcNodeID.insert(
            m_srcNodeID.begin(), (byte*)&_buffer[offset], (byte*)&_buffer[offset] + nodeIDLength);
        offset += nodeIDLength;

        checkOffset(offset + 1, length);
        // dstNodeCount
        uint8_t dstNodeCount = *((uint8_t*)&_buffer[offset]);
        offset += 1;

        // FIB-67: validate dstNodeCount against protocol maximum
        if (dstNodeCount > MAX_DST_NODEID_COUNT) [[unlikely]]
        {
            P2PMSG_LOG(ERROR) << LOG_DESC("decode: dstNodeID count overflow")
                              << LOG_KV("dstNodeCount", dstNodeCount);
            return MessageDecodeStatus::MESSAGE_ERROR;
        }

        checkOffset(offset + (static_cast<size_t>(dstNodeCount) * nodeIDLength), length);
        // dstNodeIDs
        m_dstNodeIDs.reserve(dstNodeCount);
        for (size_t i = 0; i < dstNodeCount; i++)
        {
            m_dstNodeIDs.emplace_back(
                (byte*)&_buffer[offset], (byte*)&_buffer[offset] + nodeIDLength);
            offset += nodeIDLength;
        }

        checkOffset(offset + 2, length);

        uint16_t moduleID =
            boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));
        offset += 2;

        m_moduleID = moduleID;
    }
    catch (const std::exception& e)
    {
        P2PMSG_LOG(ERROR) << LOG_DESC("decode message error")
                          << LOG_KV("e", boost::diagnostic_information(e));
        // invalid packet?
        return MessageDecodeStatus::MESSAGE_ERROR;
    }

    return offset;
}

bool Message::encodeHeader(bytes& _buffer) const
{
    if (auto result = encodeHeaderImpl(_buffer); !result)
    {
        return result;
    }

    // encode options
    if (hasOptions())
    {
        return m_options.encode(_buffer);
    }
    return true;
}

bool bcos::gateway::Message::encodeHeaderImpl(bytes& _buffer) const
{
    // set length to zero first
    uint32_t length = 0;
    uint16_t version = boost::asio::detail::socket_ops::host_to_network_short(m_version);
    uint16_t packetType = boost::asio::detail::socket_ops::host_to_network_short(m_packetType);
    uint32_t seq = boost::asio::detail::socket_ops::host_to_network_long(m_seq);
    uint16_t ext = boost::asio::detail::socket_ops::host_to_network_short(m_ext);

    _buffer.insert(_buffer.end(), (byte*)&length, (byte*)&length + 4);
    _buffer.insert(_buffer.end(), (byte*)&version, (byte*)&version + 2);
    _buffer.insert(_buffer.end(), (byte*)&packetType, (byte*)&packetType + 2);
    _buffer.insert(_buffer.end(), (byte*)&seq, (byte*)&seq + 4);
    _buffer.insert(_buffer.end(), (byte*)&ext, (byte*)&ext + 2);

    if (m_version <= (uint16_t)(bcos::protocol::ProtocolVersion::V0))
    {
        return true;
    }
    if (m_srcP2PNodeID.size() > P2PMessageOptions::MAX_NODEID_LENGTH)
    {
        P2PMSG_LOG(ERROR) << LOG_DESC("srcP2PNodeID length valid")
                          << LOG_KV("srcP2PNodeID length", m_srcP2PNodeID.size());
        return false;
    }
    if (m_dstP2PNodeID.size() > P2PMessageOptions::MAX_NODEID_LENGTH)
    {
        P2PMSG_LOG(ERROR) << LOG_DESC("dstP2PNodeID length valid")
                          << LOG_KV("dstP2PNodeID length", m_dstP2PNodeID.size());
        return false;
    }
    // encode ttl
    auto ttlData = boost::asio::detail::socket_ops::host_to_network_short(m_ttl);
    _buffer.insert(_buffer.end(), (byte*)&ttlData, (byte*)&ttlData + 2);

    // encode srcP2PNodeID
    auto srcP2PNodeIDLen =
        boost::asio::detail::socket_ops::host_to_network_short(m_srcP2PNodeID.size());
    _buffer.insert(_buffer.end(), (byte*)&srcP2PNodeIDLen, (byte*)&srcP2PNodeIDLen + 2);
    _buffer.insert(_buffer.end(), m_srcP2PNodeID.begin(), m_srcP2PNodeID.end());

    // encode dstP2PNodeID
    auto dstP2PNodeIDLen =
        boost::asio::detail::socket_ops::host_to_network_short(m_dstP2PNodeID.size());
    _buffer.insert(_buffer.end(), (byte*)&dstP2PNodeIDLen, (byte*)&dstP2PNodeIDLen + 2);
    _buffer.insert(_buffer.end(), m_dstP2PNodeID.begin(), m_dstP2PNodeID.end());
    return true;
}

bool Message::encode(bcos::bytes& _buffer)
{
    bytes emptyBuffer;
    _buffer.swap(emptyBuffer);

    // compress payload
    bcos::bytes compressData;
    bool isCompressSuccess = false;
    if (tryToCompressPayload(compressData))
    {
        isCompressSuccess = true;
        // set compress flag
        m_ext |= bcos::protocol::MessageExtFieldFlag::COMPRESS;
    }

    if (!encodeHeader(_buffer))
    {
        return false;
    }

    // encode payload
    if (isCompressSuccess)
    {
        P2PMSG_LOG(TRACE) << LOG_DESC("compress payload success")
                          << LOG_KV("compressedData", (char*)compressData.data())
                          << LOG_KV("packageType", m_packetType) << LOG_KV("ext", m_ext)
                          << LOG_KV("seq", m_seq);
        _buffer.insert(_buffer.end(), compressData.begin(), compressData.end());
    }
    else
    {
        _buffer.insert(_buffer.end(), m_payload.begin(), m_payload.end());
    }
    *(uint32_t*)_buffer.data() =
        boost::asio::detail::socket_ops::host_to_network_long(_buffer.size());

    m_length = _buffer.size();
    return true;
}

/// compress the payload data to be sended
bool Message::tryToCompressPayload(bytes& compressData) const
{
    if (m_payload.size() <= bcos::gateway::c_compressThreshold)
    {
        return false;
    }

    if (m_version < (uint16_t)(bcos::protocol::ProtocolVersion::V2))
    {
        return false;
    }

    bool isCompressSuccess =
        ZstdCompress::compress(ref(m_payload), compressData, bcos::gateway::c_zstdCompressLevel);
    return isCompressSuccess;
}

int32_t Message::decodeHeader(const bytesConstRef& _buffer)
{
    int32_t offset = 0;

    // length field
    m_length =
        boost::asio::detail::socket_ops::network_to_host_long(*((uint32_t*)&_buffer[offset]));
    offset += 4;

    // FIB-66: reject frames where length is less than header size
    if (m_length < MESSAGE_HEADER_LENGTH) [[unlikely]]
    {
        P2PMSG_LOG(WARNING) << LOG_DESC("Invalid frame: length less than header")
                            << LOG_KV("length", m_length)
                            << LOG_KV("headerLen", MESSAGE_HEADER_LENGTH);
        return MessageDecodeStatus::MESSAGE_ERROR;
    }

    // version
    m_version =
        boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));
    offset += 2;

    // FIB-66: validate version range
    if (m_version > static_cast<uint16_t>(bcos::protocol::ProtocolVersion::V3))
    {
        P2PMSG_LOG(WARNING) << LOG_DESC("Invalid frame: unsupported version")
                            << LOG_KV("version", m_version);
        return MessageDecodeStatus::MESSAGE_ERROR;
    }

    // packetType
    m_packetType =
        boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));
    offset += 2;

    // seq
    m_seq = boost::asio::detail::socket_ops::network_to_host_long(*((uint32_t*)&_buffer[offset]));
    offset += 4;

    // ext
    m_ext = boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));
    offset += 2;

    if (m_version <= (uint16_t)(bcos::protocol::ProtocolVersion::V0))
    {
        return offset;
    }

    // extended header: ttl + srcP2PNodeID + dstP2PNodeID (version > V0)

    // The packet was not fully received by the network.
    if (_buffer.size() < m_length)
    {
        return MessageDecodeStatus::MESSAGE_INCOMPLETE;
    }

    auto length = static_cast<int32_t>(_buffer.size());
    // decode ttl
    checkOffset(offset + 2, length);
    m_ttl = boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));

    offset += 2;
    checkOffset(offset + 2, length);
    // decode srcP2PNodeID, the length of srcP2PNodeID is 2-bytes
    uint16_t srcP2PNodeIDLen =
        boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));

    offset += 2;
    checkOffset(offset + srcP2PNodeIDLen, length);
    if (srcP2PNodeIDLen > 0)
    {
        m_srcP2PNodeID.assign(&_buffer[offset], &_buffer[offset] + srcP2PNodeIDLen);
    }
    offset += srcP2PNodeIDLen;
    // decode dstP2PNodeID, the length of dstP2PNodeID is 2-bytes
    uint16_t dstP2PNodeIDLen =
        boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));
    offset += 2;
    checkOffset(offset + dstP2PNodeIDLen, length);
    if (dstP2PNodeIDLen > 0)
    {
        m_dstP2PNodeID.assign(&_buffer[offset], &_buffer[offset] + dstP2PNodeIDLen);
    }
    offset += dstP2PNodeIDLen;
    return offset;
}

int32_t Message::decode(const bytesConstRef& _buffer)
{
    // check if packet header fully received
    if (_buffer.size() < Message::MESSAGE_HEADER_LENGTH)
    {
        return MessageDecodeStatus::MESSAGE_INCOMPLETE;
    }

    int32_t offset = decodeHeader(_buffer);
    if (offset < 0)
    {
        return MessageDecodeStatus::MESSAGE_ERROR;
    }
    if (offset == 0)
    {
        return MessageDecodeStatus::MESSAGE_INCOMPLETE;
    }

    // check if packet header fully received
    if (_buffer.size() < m_length)
    {
        return MessageDecodeStatus::MESSAGE_INCOMPLETE;
    }
    if (m_length > MAX_MESSAGE_LENGTH)
    {
        P2PMSG_LOG(WARNING) << LOG_DESC("Illegal p2p message packet") << LOG_KV("length", m_length)
                            << LOG_KV("maxLen", MAX_MESSAGE_LENGTH);
        return MessageDecodeStatus::MESSAGE_ERROR;
    }
    if (hasOptions())
    {
        // encode options
        auto optionsOffset = m_options.decode(_buffer.getCroppedData(offset));
        if (optionsOffset < 0)
        {
            return MessageDecodeStatus::MESSAGE_ERROR;
        }
        offset += optionsOffset;
    }

    // FIB-66: verify offset does not exceed m_length to prevent unsigned underflow
    if (std::cmp_greater(offset, m_length)) [[unlikely]]
    {
        P2PMSG_LOG(WARNING) << LOG_DESC("Invalid frame: offset exceeds length")
                            << LOG_KV("offset", offset) << LOG_KV("length", m_length);
        return MessageDecodeStatus::MESSAGE_ERROR;
    }
    auto data = _buffer.getCroppedData(offset, m_length - offset);
    // raw data cropped from buffer, maybe be compressed or not

    // uncompress payload
    // payload has been compressed
    if ((m_ext & bcos::protocol::MessageExtFieldFlag::COMPRESS) ==
        bcos::protocol::MessageExtFieldFlag::COMPRESS)
    {
        bool isUncompressSuccess = ZstdCompress::uncompress(data, m_payload);
        if (!isUncompressSuccess)
        {
            P2PMSG_LOG(ERROR) << LOG_DESC("ZstdCompress decode message error, uncompress failed")
                              << LOG_KV("packageType", m_packetType) << LOG_KV("ext", m_ext)
                              << LOG_KV("seq", m_seq);
            // invalid packet?
            return MessageDecodeStatus::MESSAGE_ERROR;
        }
        if (c_fileLogLevel <= TRACE) [[unlikely]]
        {
            P2PMSG_LOG(TRACE) << LOG_DESC("zstd uncompress success")
                              << LOG_KV("packetType", m_packetType) << LOG_KV("ext", m_ext)
                              << LOG_KV("rawDataSize", data.size()) << LOG_KV("seq", m_seq);
        }
        // reset ext
        m_ext &= (~bcos::protocol::MessageExtFieldFlag::COMPRESS);
    }
    else
    {
        m_payload.assign(data.begin(), data.end());
    }

    return (int32_t)m_length;
}
void bcos::gateway::P2PMessageOptions::setModuleID(uint16_t _moduleID)
{
    m_moduleID = _moduleID;
}
uint16_t bcos::gateway::P2PMessageOptions::moduleID() const
{
    return m_moduleID;
}
std::string bcos::gateway::P2PMessageOptions::groupID() const
{
    return m_groupID;
}
void bcos::gateway::P2PMessageOptions::setGroupID(const std::string& _groupID)
{
    m_groupID = _groupID;
}
bcos::bytesConstRef bcos::gateway::P2PMessageOptions::srcNodeID() const
{
    return ref(m_srcNodeID);
}
void bcos::gateway::P2PMessageOptions::setSrcNodeID(bytes _srcNodeID)
{
    m_srcNodeID = std::move(_srcNodeID);
}
const std::vector<bytes>& bcos::gateway::P2PMessageOptions::dstNodeIDs() const
{
    return m_dstNodeIDs;
}
void bcos::gateway::P2PMessageOptions::setDstNodeIDs(std::vector<bytes> _dstNodeIDs)
{
    m_dstNodeIDs = std::move(_dstNodeIDs);
}
std::vector<bytes>& bcos::gateway::P2PMessageOptions::mutableDstNodeIDs()
{
    return m_dstNodeIDs;
}
uint32_t bcos::gateway::Message::lengthDirect() const
{
    return m_length;
}
uint32_t bcos::gateway::Message::length() const
{
    // The length value has been set
    if (m_length > 0)
    {
        return m_length;
    }

    // estimate the length of msg to be encoded
    int64_t length = (int64_t)payload().size() + (int64_t)Message::MESSAGE_HEADER_LENGTH;
    if (hasOptions() && options().srcNodeID())
    {
        length += P2PMessageOptions::OPTIONS_MIN_LENGTH;
        length += (int64_t)(options().srcNodeID().size() * (1 + options().dstNodeIDs().size()));
    }
    return length;
}
uint16_t bcos::gateway::Message::version() const
{
    return m_version;
}
void bcos::gateway::Message::setVersion(uint16_t version)
{
    m_version = version;
}
uint16_t bcos::gateway::Message::packetType() const
{
    return m_packetType;
}
void bcos::gateway::Message::setPacketType(uint16_t packetType)
{
    m_packetType = packetType;
}
uint32_t bcos::gateway::Message::seq() const
{
    return m_seq;
}
void bcos::gateway::Message::setSeq(uint32_t seq)
{
    m_seq = seq;
}
uint16_t bcos::gateway::Message::ext() const
{
    return m_ext;
}
int16_t bcos::gateway::Message::ttl() const
{
    return m_ttl;
}
void bcos::gateway::Message::setTTL(int16_t _ttl)
{
    m_ttl = _ttl;
}
void bcos::gateway::Message::setExt(uint16_t _ext)
{
    // Accumulate (OR) semantics: the fast send path no longer touches this setter (the COMPRESS
    // flag is stamped onto the encoded wire header, not the message), so callers that want to
    // clear bits must do so explicitly — this matches the pre-PR base behaviour.
    m_ext |= _ext;
}
const bcos::gateway::P2PMessageOptions& bcos::gateway::Message::options() const
{
    return m_options;
}
void bcos::gateway::Message::setOptions(P2PMessageOptions _options)
{
    m_options = std::move(_options);
}
bcos::bytesConstRef bcos::gateway::Message::payload() const
{
    return bcos::ref(m_payload);
}
void bcos::gateway::Message::setPayload(bytes _payload)
{
    m_payload = std::move(_payload);
}
void bcos::gateway::Message::setRespPacket()
{
    m_ext |= bcos::protocol::MessageExtFieldFlag::RESPONSE;
}
bool bcos::gateway::Message::isRespPacket() const
{
    return (m_ext & bcos::protocol::MessageExtFieldFlag::RESPONSE) != 0;
}
bool bcos::gateway::Message::hasOptions() const
{
    return (m_packetType == GatewayMessageType::PeerToPeerMessage) ||
           (m_packetType == GatewayMessageType::BroadcastMessage);
}
void bcos::gateway::Message::setSrcP2PNodeID(std::string const& _srcP2PNodeID)
{
    if (m_srcP2PNodeID == _srcP2PNodeID)
    {
        return;
    }
    m_srcP2PNodeID = _srcP2PNodeID;
}
void bcos::gateway::Message::setDstP2PNodeID(std::string const& _dstP2PNodeID)
{
    if (m_dstP2PNodeID == _dstP2PNodeID)
    {
        return;
    }
    m_dstP2PNodeID = _dstP2PNodeID;
}
std::string const& bcos::gateway::Message::srcP2PNodeID() const
{
    return m_srcP2PNodeID;
}
std::string const& bcos::gateway::Message::dstP2PNodeID() const
{
    return m_dstP2PNodeID;
}
std::string bcos::gateway::Message::printSrcP2PNodeID() const
{
    return printShortP2pID(m_srcP2PNodeID);
}
std::string bcos::gateway::Message::printDstP2PNodeID() const
{
    return printShortP2pID(m_dstP2PNodeID);
}
void bcos::gateway::Message::setExtAttributes(std::any _extAttr)
{
    m_extAttr = std::move(_extAttr);
}
const std::any& bcos::gateway::Message::extAttributes() const
{
    return m_extAttr;
}
bcos::gateway::Message::Ptr bcos::gateway::MessageFactory::buildMessage()
{
    auto message = std::make_shared<Message>();
    return message;
}
uint32_t bcos::gateway::MessageFactory::newSeq()
{
    uint32_t seq = ++m_seq;
    return seq;
}
std::ostream& bcos::gateway::operator<<(std::ostream& _out, const Message& _message)
{
    _out << "Message {" << " length: " << _message.length()
         << " version: " << _message.version() << " packetType: " << _message.packetType()
         << " seq: " << _message.seq() << " ext: " << _message.ext() << " }";
    return _out;
}
std::ostream& bcos::gateway::operator<<(std::ostream& _out, Message::Ptr& _message)
{
    _out << (*_message.get());
    return _out;
}
