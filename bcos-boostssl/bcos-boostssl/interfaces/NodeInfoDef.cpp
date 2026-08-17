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
 */

#include <bcos-boostssl/interfaces/NodeInfoDef.h>

bcos::boostssl::NodeIPEndpoint::NodeIPEndpoint(std::string _host, uint16_t _port)
  : m_host(std::move(_host)), m_port(_port)
{}

bcos::boostssl::NodeIPEndpoint::NodeIPEndpoint(
    const boost::asio::ip::address& _addr, uint16_t _port)
  : m_host(_addr.to_string()), m_port(_port), m_ipv6(_addr.is_v6())
{}

bcos::boostssl::NodeIPEndpoint::NodeIPEndpoint(
    const boost::asio::ip::tcp::endpoint& _endpoint)
  : m_host(_endpoint.address().to_string()),
    m_port(_endpoint.port()),
    m_ipv6(_endpoint.address().is_v6())
{}

bool bcos::boostssl::NodeIPEndpoint::operator<(const NodeIPEndpoint& rhs) const
{
    if (m_host != rhs.m_host)
    {
        return m_host < rhs.m_host;
    }
    return m_port < rhs.m_port;
}

bool bcos::boostssl::NodeIPEndpoint::operator==(const NodeIPEndpoint& rhs) const
{
    return m_host == rhs.m_host && m_port == rhs.m_port;
}

bcos::boostssl::NodeIPEndpoint::operator boost::asio::ip::tcp::endpoint() const
{
    return {boost::asio::ip::make_address(m_host), m_port};
}

uint16_t bcos::boostssl::NodeIPEndpoint::port() const
{
    return m_port;
}

std::string bcos::boostssl::NodeIPEndpoint::address() const
{
    return m_host;
}

bool bcos::boostssl::NodeIPEndpoint::isIPv6() const
{
    return m_ipv6;
}

std::ostream& bcos::boostssl::operator<<(std::ostream& _out, NodeIPEndpoint const& _endpoint)
{
    _out << _endpoint.address() << ":" << _endpoint.port();
    return _out;
}