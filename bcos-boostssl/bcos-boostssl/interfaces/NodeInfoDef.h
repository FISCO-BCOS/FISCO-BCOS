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
#include <utility>

namespace bcos::boostssl
{

/**
 * @brief client end endpoint. Node will connect to NodeIPEndpoint.
 */
struct NodeIPEndpoint
{
    using Ptr = std::shared_ptr<NodeIPEndpoint>;
    NodeIPEndpoint() = default;
        NodeIPEndpoint(std::string _host, uint16_t _port);
        NodeIPEndpoint(const boost::asio::ip::address& _addr, uint16_t _port);
    NodeIPEndpoint(const NodeIPEndpoint& _nodeIPEndpoint) = default;
    NodeIPEndpoint(NodeIPEndpoint&& _nodeIPEndpoint) noexcept = default;
    NodeIPEndpoint& operator=(const NodeIPEndpoint& _nodeIPEndpoint) = default;
    NodeIPEndpoint& operator=(NodeIPEndpoint&& _nodeIPEndpoint) noexcept = default;

    virtual ~NodeIPEndpoint() = default;
    NodeIPEndpoint(const boost::asio::ip::tcp::endpoint& _endpoint);
    bool operator<(const NodeIPEndpoint& rhs) const;
    bool operator==(const NodeIPEndpoint& rhs) const;
    operator boost::asio::ip::tcp::endpoint() const;

    // Get the port associated with the endpoint.
    uint16_t port() const;

    // Get the IP address associated with the endpoint.
    std::string address() const;
    bool isIPv6() const;

    std::string m_host;
    uint16_t m_port = 0;
    bool m_ipv6 = false;
};

std::ostream& operator<<(std::ostream& _out, NodeIPEndpoint const& _endpoint);

}  // namespace bcos::boostssl