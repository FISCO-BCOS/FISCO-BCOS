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
 * @file FrontServiceInfo.h
 * @author: octopus
 * @date 2021-05-13
 */
#pragma once
#include <bcos-framework//front/FrontServiceInterface.h>
#include <bcos-framework//multigroup/ChainNodeInfo.h>
#include <bcos-framework//protocol/ProtocolInfo.h>
#include <bcos-framework//protocol/ProtocolTypeDef.h>
#include <bcos-tars-protocol/client/FrontServiceClient.h>
namespace bcos
{
namespace gateway
{
class FrontServiceInfo
{
public:
    using Ptr = std::shared_ptr<FrontServiceInfo>;
    FrontServiceInfo(std::string _nodeID, bcos::front::FrontServiceInterface::Ptr _frontService,
        bcos::protocol::NodeType _type, bcostars::FrontServicePrx _frontServicePrx)
      : m_nodeID(_nodeID),
        m_nodeType(_type),
        m_frontService(_frontService),
        m_frontServicePrx(_frontServicePrx)
    {}
    bcos::front::FrontServiceInterface::Ptr frontService() { return m_frontService; }
    bcostars::FrontServicePrx frontServicePrx() { return m_frontServicePrx; }

    bool unreachable()
    {
        if (!m_frontServicePrx)
        {
            return false;
        }
        vector<EndpointInfo> activeEndPoints;
        vector<EndpointInfo> nactiveEndPoints;
        m_frontServicePrx->tars_endpointsAll(activeEndPoints, nactiveEndPoints);
        return (activeEndPoints.size() == 0);
    }

    std::string const& nodeID() const { return m_nodeID; }

    bcos::protocol::NodeType nodeType() const { return m_nodeType; }

    // the protocolInfo of the nodeService
    void setProtocolInfo(bcos::protocol::ProtocolInfo::ConstPtr _protocolInfo)
    {
        m_protocolInfo = _protocolInfo;
    }
    bcos::protocol::ProtocolInfo::ConstPtr protocolInfo() const { return m_protocolInfo; }

private:
    std::string m_nodeID;
    bcos::protocol::NodeType m_nodeType;
    bcos::front::FrontServiceInterface::Ptr m_frontService;
    bcostars::FrontServicePrx m_frontServicePrx;

    bcos::protocol::ProtocolInfo::ConstPtr m_protocolInfo;
};
}  // namespace gateway
}  // namespace bcos