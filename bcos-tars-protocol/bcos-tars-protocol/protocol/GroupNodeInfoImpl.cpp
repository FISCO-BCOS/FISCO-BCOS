/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "GroupNodeInfoImpl.h"

bcostars::protocol::GroupNodeInfoImpl::GroupNodeInfoImpl()
  : m_inner([m_groupNodeInfo = bcostars::GroupNodeInfo()]() mutable { return &m_groupNodeInfo; })
{
    decodeInner();
}

bcostars::protocol::GroupNodeInfoImpl::GroupNodeInfoImpl(
    std::function<bcostars::GroupNodeInfo*()> inner)
  : m_inner(std::move(inner))
{
    decodeInner();
}

bcostars::protocol::GroupNodeInfoImpl::~GroupNodeInfoImpl() = default;

void bcostars::protocol::GroupNodeInfoImpl::setGroupID(std::string const& _groupID)
{
    m_inner()->groupID = _groupID;
}

void bcostars::protocol::GroupNodeInfoImpl::setNodeIDList(std::vector<std::string>&& _nodeIDList)
{
    m_inner()->nodeIDList = std::move(_nodeIDList);
}

void bcostars::protocol::GroupNodeInfoImpl::setNodeTypeList(std::vector<int32_t>&& _nodeTypeList)
{
    m_inner()->nodeTypeList = std::move(_nodeTypeList);
}

void bcostars::protocol::GroupNodeInfoImpl::setType(uint16_t _type)
{
    m_inner()->type = _type;
}

std::string const& bcostars::protocol::GroupNodeInfoImpl::groupID() const
{
    return m_inner()->groupID;
}

std::vector<std::string> const& bcostars::protocol::GroupNodeInfoImpl::nodeIDList() const
{
    return m_inner()->nodeIDList;
}

std::vector<int32_t> const& bcostars::protocol::GroupNodeInfoImpl::nodeTypeList() const
{
    return m_inner()->nodeTypeList;
}

int bcostars::protocol::GroupNodeInfoImpl::type() const
{
    return m_inner()->type;
}

void bcostars::protocol::GroupNodeInfoImpl::setNodeProtocolList(
    std::vector<bcos::protocol::ProtocolInfo::ConstPtr>&& _protocolList)
{
    m_inner()->protocolList.clear();
    for (auto const& protocol : _protocolList)
    {
        appendInnerProtocol(protocol);
    }
    m_protocolList = std::move(_protocolList);
}

std::vector<bcos::protocol::ProtocolInfo::ConstPtr> const&
bcostars::protocol::GroupNodeInfoImpl::nodeProtocolList() const
{
    return m_protocolList;
}

void bcostars::protocol::GroupNodeInfoImpl::appendNodeID(std::string const& _nodeID)
{
    m_inner()->nodeIDList.emplace_back(_nodeID);
}

void bcostars::protocol::GroupNodeInfoImpl::appendProtocol(
    bcos::protocol::ProtocolInfo::ConstPtr _protocol)
{
    m_protocolList.emplace_back(_protocol);
    appendInnerProtocol(_protocol);
}

bcos::protocol::ProtocolInfo::ConstPtr bcostars::protocol::GroupNodeInfoImpl::protocol(
    uint64_t _index) const
{
    if (m_protocolList.size() <= _index)
    {
        return nullptr;
    }
    return m_protocolList.at(_index);
}

bcostars::GroupNodeInfo const& bcostars::protocol::GroupNodeInfoImpl::inner() const
{
    return *m_inner();
}

void bcostars::protocol::GroupNodeInfoImpl::appendInnerProtocol(
    bcos::protocol::ProtocolInfo::ConstPtr const& _protocol)
{
    bcostars::ProtocolInfo tarsProtocol;
    tarsProtocol.moduleID = _protocol->protocolModuleID();
    tarsProtocol.minVersion = static_cast<tars::Int32>(_protocol->minVersion());
    tarsProtocol.maxVersion = static_cast<tars::Int32>(_protocol->maxVersion());
    m_inner()->protocolList.emplace_back(std::move(tarsProtocol));
}

void bcostars::protocol::GroupNodeInfoImpl::decodeInner()
{
    auto const& tarsProtocols = m_inner()->protocolList;
    for (auto const& protocol : tarsProtocols)
    {
        auto protocolInfo = std::make_shared<bcos::protocol::ProtocolInfo>(
            (bcos::protocol::ProtocolModuleID)protocol.moduleID,
            (bcos::protocol::ProtocolVersion)protocol.minVersion,
            (bcos::protocol::ProtocolVersion)protocol.maxVersion);
        m_protocolList.emplace_back(protocolInfo);
    }
}
