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
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <memory>
namespace bcos
{
namespace gateway
{
// Message type definition
enum GatewayMessageType : int16_t
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
};

/// node info obtained from the certificate
struct P2pInfo
{
    using Ptr = std::shared_ptr<P2pInfo>;
    P2pInfo() = default;
    ~P2pInfo() {}
    std::string p2pID;
    std::string agencyName;
    std::string nodeName;
    std::string nodeIPEndpointStr;
    // TODO: add ip and port here in the future after the PR merged
    std::string hostIp;
    std::string hostPort;
    std::string nodeIPEndpoint() { return hostIp + ":" + hostPort; }
};
using P2pInfos = std::vector<P2pInfo>;
using P2pID = std::string;

class GatewayInfo
{
public:
    using Ptr = std::shared_ptr<GatewayInfo>;
    // groupID=>nodeList
    using NodeIDInfoType = std::map<std::string, std::set<std::string>>;
    GatewayInfo()
      : m_p2pInfo(std::make_shared<P2pInfo>()), m_nodeIDInfo(std::make_shared<NodeIDInfoType>())
    {}
    explicit GatewayInfo(P2pInfo const& _p2pInfo) : GatewayInfo() { *m_p2pInfo = _p2pInfo; }

    virtual ~GatewayInfo() {}
    void setNodeIDInfo(NodeIDInfoType&& _nodeIDInfo)
    {
        WriteGuard l(x_nodeIDInfo);
        *m_nodeIDInfo = std::move(_nodeIDInfo);
    }
    void setP2PInfo(P2pInfo&& _p2pInfo)
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
    P2pInfo p2pInfo()
    {
        ReadGuard l(x_p2pInfo);
        return *m_p2pInfo;
    }

private:
    std::shared_ptr<P2pInfo> m_p2pInfo;
    SharedMutex x_p2pInfo;

    std::shared_ptr<NodeIDInfoType> m_nodeIDInfo;
    SharedMutex x_nodeIDInfo;
};
using GatewayInfos = std::vector<GatewayInfo::Ptr>;
using GatewayInfosPtr = std::shared_ptr<GatewayInfos>;
}  // namespace gateway
}  // namespace bcos
