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
 * @file NodeInfoDef.h
 * @author: lucasli
 * @date 2022-04-02
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <memory>

namespace bcos
{
namespace boostssl
{

/**
 * @brief client end endpoint. Node will connect to NodeIPEndpoint.
 */
struct NodeIPEndpoint
{
    using Ptr = std::shared_ptr<NodeIPEndpoint>;
    NodeIPEndpoint() = default;
    NodeIPEndpoint(std::string const& _host, uint16_t _port) : m_host(_host), m_port(_port) {}
    NodeIPEndpoint(const NodeIPEndpoint& _nodeIPEndpoint) = default;
    NodeIPEndpoint(boost::asio::ip::address _addr, uint16_t _port)
      : m_host(_addr.to_string()), m_port(_port), m_ipv6(_addr.is_v6())
    {}

    virtual ~NodeIPEndpoint() = default;
    NodeIPEndpoint(const boost::asio::ip::tcp::endpoint& _endpoint)
    {
        m_host = _endpoint.address().to_string();
        m_port = _endpoint.port();
        m_ipv6 = _endpoint.address().is_v6();
    }
    bool operator<(const NodeIPEndpoint& rhs) const
    {
        if (m_host + std::to_string(m_port) < rhs.m_host + std::to_string(rhs.m_port))
        {
            return true;
        }
        return false;
    }
    bool operator==(const NodeIPEndpoint& rhs) const
    {
        return (m_host + std::to_string(m_port) == rhs.m_host + std::to_string(rhs.m_port));
    }
    operator boost::asio::ip::tcp::endpoint() const
    {
        return boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address(m_host), m_port);
    }

    // Get the port associated with the endpoint.
    uint16_t port() const { return m_port; };

    // Get the IP address associated with the endpoint.
    std::string address() const { return m_host; };
    bool isIPv6() const { return m_ipv6; }

    std::string m_host;
    uint16_t m_port;
    bool m_ipv6 = false;
};

inline std::ostream& operator<<(std::ostream& _out, NodeIPEndpoint const& _endpoint)
{
    _out << _endpoint.address() << ":" << _endpoint.port();
    return _out;
}

}  // namespace boostssl
}  // namespace bcos