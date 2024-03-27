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
#include <bcos-utilities/Common.h>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/thread/thread.hpp>
#include <atomic>
#include <regex>
#include <unordered_map>
#include <utility>

namespace bcos::boostssl::ws
{
static std::string m_moduleName = "DEFAULT";
class WsTools
{
public:
    static bool isIPAddress(std::string const& _input)
    {
        const std::regex ipv4_regex("^([0-9]{1,3}\\.){3}[0-9]{1,3}$");
        const std::regex ipv6_regex(
            "^(([0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}|:|((([0-9a-fA-F]{1,4}:){0,6}[0-9a-fA-F]{1,4})"
            "?::("
            "([0-9a-fA-F]{1,4}:){0,6}[0-9a-fA-F]{1,4})?))$");

        return std::regex_match(_input, ipv4_regex) || std::regex_match(_input, ipv6_regex);
    }
    static bool validIP(const std::string& _ip)
    {
        boost::system::error_code ec;
        boost::asio::ip::make_address(_ip, ec);
        return !static_cast<bool>(ec);
    }

    static bool isHostname(const std::string& _input)
    {
        if (_input.empty())
        {
            return false;
        }
        boost::asio::io_context io_context;
        boost::asio::ip::tcp::resolver resolver(io_context);

        try
        {
            boost::asio::ip::tcp::resolver::results_type results = resolver.resolve(_input, "80");
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    static bool validPort(uint16_t _port) { return _port > 1024; }
    static bool hostAndPort2Endpoint(const std::string& _host, NodeIPEndpoint& _endpoint);

    static void close(boost::asio::ip::tcp::socket& skt);

    static std::string moduleName() { return m_moduleName; }
    static void setModuleName(std::string _moduleName) { m_moduleName = std::move(_moduleName); }
};
}  // namespace bcos::boostssl::ws