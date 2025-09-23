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
 * @file P2PMessage.cpp
 * @author: octopus
 * @date 2021-05-04
 */

#include <bcos-gateway/Common.h>
#include <bcos-gateway/libp2p/Common.h>
#include <bcos-gateway/libp2p/P2PMessage.h>
#include <bcos-utilities/ZstdCompress.h>
#include <boost/asio/detail/socket_ops.hpp>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::crypto;

bool P2PMessageOptions::encode(bytes& _buffer) const
{
    // parameters check
    if (m_groupID.size() > MAX_GROUPID_LENGTH)
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
        CHECK_OFFSET_WITH_THROW_EXCEPTION((offset + OPTIONS_MIN_LENGTH), length);

        // groupID length
        uint16_t groupIDLength =
            boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));
        offset += 2;

        // groupID
        if (groupIDLength > 0)
        {
            CHECK_OFFSET_WITH_THROW_EXCEPTION(offset + groupIDLength, length);
            m_groupID.assign(&_buffer[offset], &_buffer[offset] + groupIDLength);
            offset += groupIDLength;
        }

        // nodeID length
        CHECK_OFFSET_WITH_THROW_EXCEPTION(offset + 2, length);
        uint16_t nodeIDLength =
            boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));
        offset += 2;

        CHECK_OFFSET_WITH_THROW_EXCEPTION(offset + nodeIDLength, length);
        m_srcNodeID.clear();
        m_srcNodeID.insert(
            m_srcNodeID.begin(), (byte*)&_buffer[offset], (byte*)&_buffer[offset] + nodeIDLength);
        offset += nodeIDLength;

        CHECK_OFFSET_WITH_THROW_EXCEPTION(offset + 1, length);
        // dstNodeCount
        uint8_t dstNodeCount = *((uint8_t*)&_buffer[offset]);
        offset += 1;

        CHECK_OFFSET_WITH_THROW_EXCEPTION(offset + dstNodeCount * nodeIDLength, length);
        // dstNodeIDs
        m_dstNodeIDs.reserve(dstNodeCount);
        for (size_t i = 0; i < dstNodeCount; i++)
        {
            m_dstNodeIDs.emplace_back(
                (byte*)&_buffer[offset], (byte*)&_buffer[offset] + nodeIDLength);
            offset += nodeIDLength;
        }

        CHECK_OFFSET_WITH_THROW_EXCEPTION(offset + 2, length);

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

bool P2PMessage::encodeHeader(bytes& _buffer) const
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

bool bcos::gateway::P2PMessage::encodeHeaderImpl(bytes& _buffer) const
{
    auto offset = _buffer.size();

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
    return true;
}

bool P2PMessage::encode(EncodedMessage& _buffer) const
{
    bool isCompressSuccess = false;
    if (_buffer.compress)
    {
        bcos::bytes compressData;
        if (tryToCompressPayload(compressData))
        {
            isCompressSuccess = true;
            // set compress flag
            m_ext |= bcos::protocol::MessageExtFieldFlag::COMPRESS;
            _buffer.payload = std::move(compressData);
        }
    }

    // No data compression is performed
    if (!isCompressSuccess)
    {
        _buffer.payload = m_payload;
    }

    bytes headerBuffer;
    // encode header
    if (!encodeHeader(headerBuffer))
    {
        return false;
    }

    *(uint32_t*)headerBuffer.data() = boost::asio::detail::socket_ops::host_to_network_long(
        headerBuffer.size() + _buffer.payload.size());

    _buffer.header = std::move(headerBuffer);
    return true;
}

bool P2PMessage::encode(bcos::bytes& _buffer)
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
bool P2PMessage::tryToCompressPayload(bytes& compressData) const
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

int32_t P2PMessage::decodeHeader(const bytesConstRef& _buffer)
{
    int32_t offset = 0;

    // length field
    m_length =
        boost::asio::detail::socket_ops::network_to_host_long(*((uint32_t*)&_buffer[offset]));
    offset += 4;

    // version
    m_version =
        boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));
    offset += 2;

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

    return offset;
}

int32_t P2PMessage::decode(const bytesConstRef& _buffer)
{
    // check if packet header fully received
    if (_buffer.size() < P2PMessage::MESSAGE_HEADER_LENGTH)
    {
        return MessageDecodeStatus::MESSAGE_INCOMPLETE;
    }

    int32_t offset = decodeHeader(_buffer);
    if (offset <= 0)
    {  // The packet was not fully received by the network.
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

    uint32_t length = _buffer.size();
    CHECK_OFFSET_WITH_THROW_EXCEPTION(offset, m_length);
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
