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
 * @file FrontMessage.cpp
 * @author: octopus
 * @date 2021-04-20
 */

#include <bcos-front/FrontMessage.h>
#include <boost/asio.hpp>

using namespace bcos;
using namespace front;

bool FrontMessage::encode(bytes& _buffer)
{
    _buffer.clear();

    /// moduleID          :2 bytes
    /// UUID length       :1 bytes
    /// UUID              :UUID length bytes
    /// ext               :2 bytes
    /// payload

    uint16_t moduleID = boost::asio::detail::socket_ops::host_to_network_short(m_moduleID);
    uint16_t ext = boost::asio::detail::socket_ops::host_to_network_short(m_ext);

    size_t uuidLength = m_uuid->size();
    // uuid length should not be greater than 256
    if (uuidLength > MAX_MESSAGE_UUID_SIZE)
    {
        return false;
    }

    _buffer.insert(_buffer.end(), (byte*)&moduleID, (byte*)&moduleID + 2);
    _buffer.insert(_buffer.end(), (byte*)&uuidLength, (byte*)&uuidLength + 1);
    if (uuidLength > 0)
    {
        _buffer.insert(_buffer.end(), m_uuid->begin(), m_uuid->end());
    }
    _buffer.insert(_buffer.end(), (byte*)&ext, (byte*)&ext + 2);

    _buffer.insert(_buffer.end(), m_payload.begin(), m_payload.end());
    return true;
}

ssize_t FrontMessage::decode(bytesConstRef _buffer)
{
    if (_buffer.size() < HEADER_MIN_LENGTH)
    {
        return MessageDecodeStatus::MESSAGE_ERROR;
    }

    m_uuid->clear();
    m_payload.reset();

    int32_t offset = 0;
    m_moduleID =
        boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));
    offset += 2;

    uint8_t uuidLength = *((uint8_t*)&_buffer[offset]);
    offset += 1;

    if (uuidLength > 0)
    {
        m_uuid->assign(&_buffer[offset], &_buffer[offset] + uuidLength);
        offset += uuidLength;
    }

    m_ext = boost::asio::detail::socket_ops::network_to_host_short(*((uint16_t*)&_buffer[offset]));
    offset += 2;

    m_payload = _buffer.getCroppedData(offset);

    return MessageDecodeStatus::MESSAGE_COMPLETE;
}
