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
 * @file P2PMessageV2.cpp
 * @brief: extend srcNodeID and dstNodeID for message forward
 * @author: yujiechen
 * @date 2021-05-04
 */

#include "P2PMessageV2.h"
#include "Common.h"
using namespace bcos;
using namespace bcos::gateway;

bool P2PMessageV2::encodeHeaderImpl(bytes& _buffer) const
{
    auto ret = P2PMessage::encodeHeaderImpl(_buffer);
    if (m_version <= (uint16_t)(bcos::protocol::ProtocolVersion::V0))
    {
        return ret;
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
    // ecode ttl
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

int32_t P2PMessageV2::decodeHeader(const bytesConstRef& _buffer)
{
    int32_t offset = P2PMessage::decodeHeader(_buffer);
    if (m_version <= bcos::protocol::ProtocolVersion::V0)
    {
        return offset;
    }

    // The packet was not fully received by the network.
    if (_buffer.size() < m_length)
    {
        return MessageDecodeStatus::MESSAGE_INCOMPLETE;
    }

    auto length = static_cast<int32_t>(_buffer.size());
    // decode ttl
    CHECK_OFFSET_WITH_THROW_EXCEPTION(offset + 2, length);
    m_ttl = boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));

    offset += 2;
    CHECK_OFFSET_WITH_THROW_EXCEPTION(offset + 2, length);
    // decode srcP2PNodeID, the length of srcP2PNodeID is 2-bytes
    uint16_t srcP2PNodeIDLen =
        boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));

    offset += 2;
    CHECK_OFFSET_WITH_THROW_EXCEPTION(offset + srcP2PNodeIDLen, length);
    if (srcP2PNodeIDLen > 0)
    {
        m_srcP2PNodeID.assign(&_buffer[offset], &_buffer[offset] + srcP2PNodeIDLen);
    }
    offset += srcP2PNodeIDLen;
    // decode dstP2PNodeID, the length of dstP2PNodeID is 2-bytes
    uint16_t dstP2PNodeIDLen =
        boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));
    offset += 2;
    CHECK_OFFSET_WITH_THROW_EXCEPTION(offset + dstP2PNodeIDLen, length);
    if (dstP2PNodeIDLen > 0)
    {
        m_dstP2PNodeID.assign(&_buffer[offset], &_buffer[offset] + dstP2PNodeIDLen);
    }
    offset += dstP2PNodeIDLen;
    return offset;
}
