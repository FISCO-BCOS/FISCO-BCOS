/*
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file GatewayStatus.h
 * @author: yujiechen
 * @date 2022-1-07
 */
#pragma once
#include "bcos-gateway/Common.h"
#include <bcos-gateway/protocol/GatewayNodeStatus.h>
namespace bcos
{
namespace gateway
{
class GatewayStatus
{
public:
    using Ptr = std::shared_ptr<GatewayStatus>;
    GatewayStatus(std::string const& _uuid) : m_uuid(_uuid) {}
    virtual ~GatewayStatus() {}

    std::string const& uuid() const { return m_uuid; }

    // update the gateway info when receive new gatewayNodeStatus
    void update(std::string const& _p2pNodeID, GatewayNodeStatus::ConstPtr _nodeStatus);

    // random choose the p2pNode to send message
    bool randomChooseP2PNode(
        std::string& _p2pNodeID, uint16_t _type, std::string const& _groupID) const;

    // remove the p2p node from the gatewayInfo after the node disconnected
    void removeP2PNode(std::string const& _p2pNodeID);

protected:
    bool randomChooseNode(
        std::string& _choosedNode, GroupType _type, std::string const& _groupID) const;

    void removeP2PIDWithoutLock(std::string const& _groupID, std::string const& _p2pNodeID);

private:
    std::string m_uuid;
    // groupID => groupType => P2PNodeIDList
    std::map<std::string, std::map<GroupType, std::set<std::string>>> m_groupP2PNodeList;
    mutable SharedMutex x_groupP2PNodeList;
};

class GatewayStatusFactory
{
public:
    using Ptr = std::shared_ptr<GatewayStatusFactory>;
    GatewayStatusFactory() = default;
    virtual ~GatewayStatusFactory() {}

    virtual GatewayStatus::Ptr createGatewayInfo(std::string const& _uuid)
    {
        return std::make_shared<GatewayStatus>(_uuid);
    }
};
}  // namespace gateway
}  // namespace bcos