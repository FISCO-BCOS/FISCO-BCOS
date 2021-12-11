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
 *  m_limitations under the License.
 *
 * @file WsTools.h
 * @author: octopus
 * @date 2021-10-10
 */
#pragma once
#include "bcos-boostssl/websocket/WsConfig.h"
#include <bcos-boostssl/utilities/Common.h>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/thread/thread.hpp>
#include <atomic>
#include <unordered_map>

namespace bcos
{
namespace boostssl
{
namespace ws
{
class WsTools
{
public:
    static bool validIP(const std::string& _ip)
    {
        boost::system::error_code ec;
        boost::asio::ip::address::from_string(_ip, ec);
        if (ec)
        {
            return false;
        }
        return true;
    }

    static bool validPort(uint16_t _port) { return _port > 1024; }

    static bool stringToEndPoint(const std::string& peer, EndPoint& _endpoint);

    static void close(boost::asio::ip::tcp::socket& skt);
};
}  // namespace ws
}  // namespace boostssl
}  // namespace bcos