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
 * @file AMOPMessage.cpp
 * @author: octopus
 * @date 2021-06-21
 */

#include <bcos-framework/interfaces/Common.h>
#include <bcos-gateway/libamop/AMOPMessage.h>
#include <bcos-gateway/libamop/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Error.h>
#include <boost/asio.hpp>

using namespace bcos;
using namespace bcos::amop;

const size_t AMOPMessage::HEADER_LENGTH;

bool AMOPMessage::encode(bcos::bytes& _buffer)
{
    uint16_t type = boost::asio::detail::socket_ops::host_to_network_short(m_type);
    _buffer.insert(_buffer.end(), (byte*)&type, (byte*)&type + 2);
    uint16_t status = boost::asio::detail::socket_ops::host_to_network_short(m_status);
    _buffer.insert(_buffer.end(), (byte*)&status, (byte*)&status + 2);
    _buffer.insert(_buffer.end(), m_data.begin(), m_data.end());
    return true;
}

ssize_t AMOPMessage::decode(bcos::bytesConstRef _buffer)
{
    if (_buffer.size() < HEADER_LENGTH)
    {
        AMOP_MSG_LOG(ERROR)
            << LOG_BADGE("decode")
            << LOG_DESC("the topic length smaller than the minimum length(2), data size:" +
                        std::to_string(_buffer.size()))
            << LOG_KV("data", *toHexString(_buffer));
        return -1;
    }
    std::size_t offset = 0;
    m_type = boost::asio::detail::socket_ops::network_to_host_short(
        *((uint16_t*)(_buffer.data() + offset)));
    offset += 2;
    m_status = boost::asio::detail::socket_ops::network_to_host_short(
        *((uint16_t*)(_buffer.data() + offset)));
    offset += 2;
    m_data = _buffer.getCroppedData(offset);

    return _buffer.size();
}