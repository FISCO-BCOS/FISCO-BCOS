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
#include <boost/asio/detail/socket_ops.hpp>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::crypto;

#define CHECK_OFFSET_WITH_THROW_EXCEPTION(offset, length)                                    \
    do                                                                                       \
    {                                                                                        \
        if ((offset) > (length))                                                             \
        {                                                                                    \
            throw std::out_of_range("Out of range error, offset:" + std::to_string(offset) + \
                                    " ,length: " + std::to_string(length));                  \
        }                                                                                    \
    } while (0);

bool P2PMessageOptions::encode(bytes& _buffer)
{
    // parameters check
    if (m_groupID.size() > MAX_GROUPID_LENGTH)
    {
        P2PMSG_LOG(ERROR) << LOG_DESC("groupID length overflow")
                          << LOG_KV("groupID length", m_groupID.size());
        return false;
    }
    if (!m_srcNodeID || m_srcNodeID->empty() || (m_srcNodeID->size() > MAX_NODEID_LENGTH))
    {
        P2PMSG_LOG(ERROR) << LOG_DESC("srcNodeID length valid")
                          << LOG_KV("srcNodeID length", (m_srcNodeID ? m_srcNodeID->size() : 0));
        return false;
    }
    if (m_dstNodeIDs.size() > MAX_DST_NODEID_COUNT)
    {
        P2PMSG_LOG(ERROR) << LOG_DESC("dstNodeID amount overfow")
                          << LOG_KV("dstNodeID size", m_dstNodeIDs.size());
        return false;
    }

    for (auto dstNodeID : m_dstNodeIDs)
    {
        if (!dstNodeID || dstNodeID->empty() || (dstNodeID->size() > MAX_NODEID_LENGTH))
        {
            P2PMSG_LOG(ERROR) << LOG_DESC("dstNodeID length valid")
                              << LOG_KV("dstNodeID length", (dstNodeID ? dstNodeID->size() : 0));
            return false;
        }
    }

    // groupID length
    uint16_t groupIDLength =
        boost::asio::detail::socket_ops::host_to_network_short((uint16_t)m_groupID.size());
    _buffer.insert(_buffer.end(), (byte*)&groupIDLength, (byte*)&groupIDLength + 2);
    // groupID
    _buffer.insert(_buffer.end(), m_groupID.begin(), m_groupID.end());

    // nodeID length
    uint16_t nodeIDLength =
        boost::asio::detail::socket_ops::host_to_network_short((uint16_t)m_srcNodeID->size());
    _buffer.insert(_buffer.end(), (byte*)&nodeIDLength, (byte*)&nodeIDLength + 2);
    // srcNodeID
    _buffer.insert(_buffer.end(), m_srcNodeID->begin(), m_srcNodeID->end());

    // dstNodeID count
    uint8_t dstNodeIDCount = (uint8_t)m_dstNodeIDs.size();
    _buffer.insert(_buffer.end(), (byte*)&dstNodeIDCount, (byte*)&dstNodeIDCount + 1);

    // dstNodeIDs
    for (const auto& nodeID : m_dstNodeIDs)
    {
        _buffer.insert(_buffer.end(), nodeID->begin(), nodeID->end());
    }

    return true;
}

///       groupID length    :1 bytes
///       groupID           : bytes
///       nodeID length     :2 bytes
///       src nodeID        : bytes
///       src nodeID count  :1 bytes
///       dst nodeIDs       : bytes
int64_t P2PMessageOptions::decode(bytesConstRef _buffer)
{
    size_t offset = 0;
    size_t length = _buffer.size();

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
        m_srcNodeID->insert(
            m_srcNodeID->begin(), (byte*)&_buffer[offset], (byte*)&_buffer[offset] + nodeIDLength);
        offset += nodeIDLength;

        CHECK_OFFSET_WITH_THROW_EXCEPTION(offset + 1, length);
        // dstNodeCount
        uint8_t dstNodeCount = *((uint8_t*)&_buffer[offset]);
        offset += 1;

        CHECK_OFFSET_WITH_THROW_EXCEPTION(offset + dstNodeCount * nodeIDLength, length);
        // dstNodeIDs
        m_dstNodeIDs.resize(dstNodeCount);
        for (size_t i = 0; i < dstNodeCount; i++)
        {
            m_dstNodeIDs[i] = std::make_shared<bytes>(
                (byte*)&_buffer[offset], (byte*)&_buffer[offset] + nodeIDLength);

            offset += nodeIDLength;
        }
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

bool P2PMessage::encode(bytes& _buffer)
{
    _buffer.clear();

    // set length to zero first
    uint32_t length = 0;
    uint16_t version = boost::asio::detail::socket_ops::host_to_network_short(m_version);
    uint16_t packetType = boost::asio::detail::socket_ops::host_to_network_short(m_packetType);
    uint16_t ext = boost::asio::detail::socket_ops::host_to_network_short(m_ext);
    uint16_t seqLength = boost::asio::detail::socket_ops::host_to_network_short(m_seq.size());
    // uint16_t seqLength = m_seq.size();
    // auto seq_size = m_seq.size();
    // std::cout << "seqlength type: " << typeid(seq_size).name() << endl;

    _buffer.insert(_buffer.end(), (byte*)&length, (byte*)&length + 4);
    _buffer.insert(_buffer.end(), (byte*)&version, (byte*)&version + 2);
    _buffer.insert(_buffer.end(), (byte*)&packetType, (byte*)&packetType + 2);
    _buffer.insert(_buffer.end(), (byte*)&seqLength, (byte*)&seqLength + 2);
    _buffer.insert(_buffer.end(), m_seq.begin(), m_seq.end());
    _buffer.insert(_buffer.end(), (byte*)&ext, (byte*)&ext + 2);

    // encode options
    if (hasOptions() && !m_options->encode(_buffer))
    {
        return false;
    }

    _buffer.insert(_buffer.end(), m_payload->begin(), m_payload->end());

    // calc total length and modify the length value in the buffer
    length = boost::asio::detail::socket_ops::host_to_network_long((uint32_t)_buffer.size());

    std::copy((byte*)&length, (byte*)&length + 4, _buffer.data());
    return true;
}

ssize_t P2PMessage::decodeHeader(bytesConstRef _buffer)
{
    std::size_t size = _buffer.size();
    int32_t offset = 0;

    m_seq.clear();
    auto p = _buffer.data();

    // length field
    m_length = boost::asio::detail::socket_ops::network_to_host_long(*((uint32_t*)p));
    p += 4;
    offset += 4;

    // version
    m_version = boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)p));
    p += 2;
    offset += 2;

    // packetType
    m_packetType = boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)p));
    p += 2;
    offset += 2;

    // seqLength
    uint16_t seqLength = boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)p));
    // uint16_t seqLength = *((uint16_t*)&p);
    p += 2;
    offset += 2;

    // seq
    m_seq.insert(m_seq.begin(), p, p + seqLength);
    offset += seqLength;
    p += seqLength;

    // ext
    m_ext = boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)p));
    offset += 2;

    return offset;
}

int64_t P2PMessage::decode(bytesConstRef _buffer)
{
    // check if packet header fully received
    if (_buffer.size() < P2PMessage::MESSAGE_HEADER_LENGTH)
    {
        return MessageDecodeStatus::MESSAGE_INCOMPLETE;
    }

    int32_t offset = decodeHeader(_buffer);

    // check if packet header fully received
    if (_buffer.size() < m_length)
    {
        return MessageDecodeStatus::MESSAGE_INCOMPLETE;
    }
    if (m_length > P2PMessage::MAX_MESSAGE_LENGTH)
    {
        P2PMSG_LOG(WARNING) << LOG_DESC("Illegal p2p message packet") << LOG_KV("length", m_length)
                            << LOG_KV("maxLen", P2PMessage::MAX_MESSAGE_LENGTH);
        return MessageDecodeStatus::MESSAGE_ERROR;
    }
    if (hasOptions())
    {
        // encode options
        auto optionsOffset = m_options->decode(_buffer.getCroppedData(offset));
        if (optionsOffset < 0)
        {
            return MessageDecodeStatus::MESSAGE_ERROR;
        }
        offset += optionsOffset;
    }

    auto data = _buffer.getCroppedData(offset, m_length - offset);
    // payload
    m_payload = std::make_shared<bytes>(data.begin(), data.end());

    return m_length;
}
