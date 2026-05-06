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
#include "../interfaces/NodeInfoDef.h"
#include <bcos-utilities/Common.h>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/regex.hpp>
#include <boost/thread/thread.hpp>

namespace bcos::boostssl::ws
{
class WsTools
{
public:
    static bool isIPAddress(std::string const& _input);
    static bool validIP(const std::string& _ip);

    static bool isHostname(const std::string& _input);

    static bool validPort(uint16_t _port);
    static bool hostAndPort2Endpoint(const std::string& _host, NodeIPEndpoint& _endpoint);

    static void close(boost::asio::ip::tcp::socket& skt);
};
}  // namespace bcos::boostssl::ws