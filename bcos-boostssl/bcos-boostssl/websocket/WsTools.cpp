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
 * @file WsTools.cpp
 * @author: octopus
 * @date 2021-10-19
 */
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsConfig.h>
#include <bcos-boostssl/websocket/WsTools.h>
#include <boost/algorithm/string.hpp>

using namespace bcos;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;

bool WsTools::hostAndPort2Endpoint(const std::string& _host, NodeIPEndpoint& _endpoint)
{
    std::string ip;
    uint16_t port = 0;

    std::vector<std::string> result;
    try
    {
        boost::split(result, _host, boost::is_any_of("]"), boost::token_compress_on);
        if (result.size() == 2)
        {  // ipv6 format is [IP]:Port
            ip = result[0].substr(1);
            int tempPort = std::stoi(result[1].substr(1));
            if (tempPort < 0 || tempPort > UINT16_MAX)
            {
                return false;
            }
            port = static_cast<uint16_t>(tempPort);
        }
        else if (result.size() == 1)
        {  // ipv4 format is IP:Port
            std::vector<std::string> v;
            boost::split(v, _host, boost::is_any_of(":"), boost::token_compress_on);
            if (v.size() < 2)
            {
                return false;
            }
            ip = v[0];
            int tempPort = std::stoi(v[1]);
            if (tempPort < 0 || tempPort > UINT16_MAX)
            {
                return false;
            }
            port = static_cast<uint16_t>(tempPort);
        }
        else
        {
            return false;
        }
    }
    catch (...)
    {
        return false;
    }

    if (!validPort(port))
    {
        return false;
    }

    boost::system::error_code ec;
    boost::asio::ip::address ip_address;
    // ip
    if (isIPAddress(ip))
    {
        ip_address = boost::asio::ip::make_address(ip, ec);
    }
    // hostname
    else if (isHostname(ip))
    {
        boost::asio::io_context io_context;
        boost::asio::ip::tcp::resolver resolver(io_context);
        boost::asio::ip::tcp::resolver::query query(boost::asio::ip::tcp::v4(), ip, "80");
        boost::asio::ip::tcp::resolver::results_type results = resolver.resolve(query, ec);
        if (!ec)
        {
            ip_address = results.begin()->endpoint().address();
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
    if (ec.value() != 0)
    {
        return false;
    }

    _endpoint = NodeIPEndpoint{ip_address, port};
    return true;
}


void WsTools::close(boost::asio::ip::tcp::socket& _socket)
{
    try
    {
        boost::beast::error_code ec;
        _socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        if (_socket.is_open())
        {
            _socket.close();
        }
    }
    catch (std::exception const& e)
    {
        WEBSOCKET_TOOL(WARNING) << LOG_DESC("WsTools close exception")
                                << LOG_KV("message", boost::diagnostic_information(e));
    }
}