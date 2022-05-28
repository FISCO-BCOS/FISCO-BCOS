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
 * @file WsMessage.h
 * @author: octopus
 * @date 2021-07-28
 */
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <boost/asio/detail/socket_ops.hpp>
#include <iterator>

using namespace bcos;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;

// version(2) + type(2) + status(2) + seqLength(2) + ext(2) + payload(N)
const size_t WsMessage::MESSAGE_MIN_LENGTH;

bool WsMessage::encode(bytes& _buffer)
{
    _buffer.clear();

    uint16_t version = boost::asio::detail::socket_ops::host_to_network_short(m_version);
    uint16_t type = boost::asio::detail::socket_ops::host_to_network_short(m_packetType);
    int16_t status = boost::asio::detail::socket_ops::host_to_network_short(m_status);
    uint16_t seqLength = boost::asio::detail::socket_ops::host_to_network_short(m_seq.size());
    uint16_t ext = boost::asio::detail::socket_ops::host_to_network_short(m_ext);

    _buffer.insert(_buffer.end(), (byte*)&version, (byte*)&version + 2);
    _buffer.insert(_buffer.end(), (byte*)&type, (byte*)&type + 2);
    _buffer.insert(_buffer.end(), (byte*)&status, (byte*)&status + 2);
    _buffer.insert(_buffer.end(), (byte*)&seqLength, (byte*)&seqLength + 2);
    _buffer.insert(_buffer.end(), m_seq.begin(), m_seq.end());
    _buffer.insert(_buffer.end(), (byte*)&ext, (byte*)&ext + 2);
    _buffer.insert(_buffer.end(), m_payload->begin(), m_payload->end());

    return true;
}

int64_t WsMessage::decode(bytesConstRef _buffer)
{
    std::size_t size = _buffer.size();
    if (size < MESSAGE_MIN_LENGTH)
    {
        return -1;
    }

    m_seq.clear();
    m_payload->clear();

    auto dataBuffer = _buffer.data();
    auto p = _buffer.data();

    // version field
    m_version = boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)p));
    p += 2;

    // type field
    m_packetType = boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)p));
    p += 2;

    // status field
    m_status = boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)p));
    p += 2;

    // seqLength
    uint16_t seqLength = boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)p));
    p += 2;

    // seq field
    m_seq.insert(m_seq.begin(), p, p + seqLength);
    p += seqLength;

    // ext field
    m_ext = boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)p));
    p += 2;

    // data field
    m_payload->insert(m_payload->begin(), p, dataBuffer + size);

    return size;
}