/**
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
 * @brief typedef for gateway
 * @file GatewayTypeDef.h
 * @author: octopus
 * @date 2021-04-19
 */
#pragma once
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <memory>
#include <utility>

namespace bcos::gateway
{
constexpr static size_t HASH_NODEID_MAX_SIZE = 33;
/// For RSA public key, the prefix length is 18 in hex, used for print log graciously
constexpr static size_t RSA_PUBLIC_KEY_PREFIX = 18;
constexpr static size_t RSA_PUBLIC_KEY_TRUNC = 8;

// Message type definition
enum GatewayMessageType : uint16_t
{
    Heartbeat = 0x1,
    Handshake = 0x2,
    RequestNodeStatus = 0x3,  // for request the gateway nodeinfo
    ResponseNodeStatus = 0x4,
    PeerToPeerMessage = 0x5,
    BroadcastMessage = 0x6,
    AMOPMessageType = 0x7,
    WSMessageType = 0x8,
    SyncNodeSeq = 0x9,
    RouterTableSyncSeq = 0xa,
    RouterTableResponse = 0xb,
    RouterTableRequest = 0xc,
    ForwardMessage = 0xd,
    All = 0xff
};
/**
 * @brief client end endpoint. Node will connect to NodeIPEndpoint.
 */
struct NodeIPEndpoint
{
    using Ptr = std::shared_ptr<NodeIPEndpoint>;
    NodeIPEndpoint() = default;
    NodeIPEndpoint(NodeIPEndpoint&&) noexcept = default;
    NodeIPEndpoint& operator=(const NodeIPEndpoint&) = default;
    NodeIPEndpoint& operator=(NodeIPEndpoint&&) noexcept = default;
    NodeIPEndpoint(std::string _host, uint16_t _port) : m_host(std::move(_host)), m_port(_port) {}
    NodeIPEndpoint(const NodeIPEndpoint& _nodeIPEndpoint) = default;
    NodeIPEndpoint(const boost::asio::ip::address& _addr, uint16_t _port)
      : m_host(_addr.to_string()), m_port(_port), m_ipv6(_addr.is_v6())
    {}
    NodeIPEndpoint(const boost::asio::ip::tcp::endpoint& _endpoint) : m_port(_endpoint.port())
    {
        m_host = _endpoint.address().to_string();
        m_ipv6 = _endpoint.address().is_v6();
    }

    virtual ~NodeIPEndpoint() = default;

    bool operator<(const NodeIPEndpoint& rhs) const
    {
        return m_host + std::to_string(m_port) < rhs.m_host + std::to_string(rhs.m_port);
    }
    bool operator==(const NodeIPEndpoint& rhs) const
    {
        return (m_host + std::to_string(m_port) == rhs.m_host + std::to_string(rhs.m_port));
    }
    operator boost::asio::ip::tcp::endpoint() const
    {
        return {boost::asio::ip::make_address(m_host), m_port};
    }

    // Get the port associated with the endpoint.
    uint16_t port() const { return m_port; };

    // Get the IP address associated with the endpoint.
    std::string address() const { return m_host; };
    bool isIPv6() const { return m_ipv6; }

    std::string m_host;
    uint16_t m_port{};
    bool m_ipv6 = false;
};

inline std::ostream& operator<<(std::ostream& _out, NodeIPEndpoint const& _endpoint)
{
    _out << _endpoint.address() << ":" << _endpoint.port();
    return _out;
}

/// node info obtained from the certificate
struct P2PInfo
{
    using Ptr = std::shared_ptr<P2PInfo>;
    P2PInfo() = default;
    P2PInfo(const P2PInfo&) = default;
    P2PInfo(P2PInfo&&) noexcept = default;
    P2PInfo& operator=(const P2PInfo&) = default;
    P2PInfo& operator=(P2PInfo&&) noexcept = default;
    P2PInfo(std::string _p2pID, std::string _rawP2pID)
      : rawP2pID(std::move(_rawP2pID)), p2pID(std::move(_p2pID))
    {}
    ~P2PInfo() noexcept = default;
    // the raw-p2p-nodeID
    std::string rawP2pID;
    // hash(rawP2pID)
    std::string p2pID;
    std::string p2pIDWithoutExtInfo;
    std::string agencyName;
    std::string nodeName;
    NodeIPEndpoint nodeIPEndpoint;
};
using P2PInfos = std::vector<P2PInfo>;

class GatewayInfo
{
public:
    using Ptr = std::shared_ptr<GatewayInfo>;
    // groupID=>nodeList
    using NodeIDInfoType = std::map<std::string, std::map<std::string, uint32_t>>;
    GatewayInfo()
      : m_p2pInfo(std::make_shared<P2PInfo>()), m_nodeIDInfo(std::make_shared<NodeIDInfoType>())
    {}
    GatewayInfo(const GatewayInfo&) = delete;
    GatewayInfo(GatewayInfo&&) = delete;
    GatewayInfo& operator=(const GatewayInfo&) = delete;
    GatewayInfo& operator=(GatewayInfo&&) = delete;
    explicit GatewayInfo(P2PInfo const& _p2pInfo) : GatewayInfo() { *m_p2pInfo = _p2pInfo; }

    virtual ~GatewayInfo() {}
    void setNodeIDInfo(NodeIDInfoType&& _nodeIDInfo)
    {
        WriteGuard l(x_nodeIDInfo);
        *m_nodeIDInfo = std::move(_nodeIDInfo);
    }
    void setP2PInfo(P2PInfo&& _p2pInfo)
    {
        WriteGuard l(x_p2pInfo);
        *m_p2pInfo = std::move(_p2pInfo);
    }
    // Note: copy here to ensure thread-safe,  use std::move out to ensure performance
    NodeIDInfoType nodeIDInfo()
    {
        ReadGuard l(x_nodeIDInfo);
        return *m_nodeIDInfo;
    }
    // Note: copy here to ensure thread-safe, use std::move out to ensure performance
    P2PInfo p2pInfo()
    {
        ReadGuard l(x_p2pInfo);
        return *m_p2pInfo;
    }

private:
    std::shared_ptr<P2PInfo> m_p2pInfo;
    SharedMutex x_p2pInfo;

    std::shared_ptr<NodeIDInfoType> m_nodeIDInfo;
    SharedMutex x_nodeIDInfo;
};
using GatewayInfos = std::vector<GatewayInfo::Ptr>;
using GatewayInfosPtr = std::shared_ptr<GatewayInfos>;

inline std::string printShortP2pID(std::string const& data)
{
    if (data.length() == 0)
    {
        return "empty";
    }
    if (data.length() <= HASH_NODEID_MAX_SIZE)
    {
        auto startIt = data.begin();
        auto endIt = data.end();
        if (data.size() > 4)
        {
            endIt = startIt + 4 * sizeof(byte);
        }
        return *toHexString(startIt, endIt, "hash-");
    }
    return data.substr(RSA_PUBLIC_KEY_PREFIX, RSA_PUBLIC_KEY_TRUNC);
}

}  // namespace bcos::gateway
