/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "FrontServiceInfo.h"
#include "fisco-bcos-tars-service/Common/TarsUtils.h"

bcos::gateway::FrontServiceInfo::FrontServiceInfo(std::string _nodeID,
    bcos::front::FrontServiceInterface::Ptr _frontService, bcos::protocol::NodeType _type,
    bcostars::FrontServicePrx _frontServicePrx)
  : m_nodeID(std::move(_nodeID)),
    m_nodeType(_type),
    m_frontService(std::move(_frontService)),
    m_frontServicePrx(std::move(_frontServicePrx))
{}

bcos::front::FrontServiceInterface::Ptr bcos::gateway::FrontServiceInfo::frontService()
{
    return m_frontService;
}

bcostars::FrontServicePrx bcos::gateway::FrontServiceInfo::frontServicePrx()
{
    return m_frontServicePrx;
}

bool bcos::gateway::FrontServiceInfo::unreachable()
{
    if (!m_frontServicePrx)
    {
        return false;
    }

    return !bcostars::checkConnection("FrontService", "unreachable", m_frontServicePrx, nullptr, false);
}

std::string const& bcos::gateway::FrontServiceInfo::nodeID() const
{
    return m_nodeID;
}

bcos::protocol::NodeType bcos::gateway::FrontServiceInfo::nodeType() const
{
    return m_nodeType;
}

void bcos::gateway::FrontServiceInfo::setProtocolInfo(
    bcos::protocol::ProtocolInfo::ConstPtr _protocolInfo)
{
    m_protocolInfo = std::move(_protocolInfo);
}

bcos::protocol::ProtocolInfo::ConstPtr bcos::gateway::FrontServiceInfo::protocolInfo() const
{
    return m_protocolInfo;
}