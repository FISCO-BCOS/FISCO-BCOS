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
 * @file GroupNodeInfoImpl.h
 * @author: yujiechen
 * @date 2022-3-8
 */
#pragma once

#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include <bcos-framework//gateway/GroupNodeInfo.h>
#include <bcos-framework//protocol/ProtocolInfo.h>
#include <bcos-tars-protocol/tars/GatewayInfo.h>
#include <bcos-tars-protocol/tars/ProtocolInfo.h>

namespace bcostars
{
namespace protocol
{
class GroupNodeInfoImpl : public bcos::gateway::GroupNodeInfo
{
public:
    GroupNodeInfoImpl()
      : m_inner(
            [m_groupNodeInfo = bcostars::GroupNodeInfo()]() mutable { return &m_groupNodeInfo; })
    {
        decodeInner();
    }
    explicit GroupNodeInfoImpl(std::function<bcostars::GroupNodeInfo*()> inner) : m_inner(inner)
    {
        decodeInner();
    }
    ~GroupNodeInfoImpl() override {}

    // the groupID
    void setGroupID(std::string const& _groupID) override { m_inner()->groupID = _groupID; }
    // the nodeIDList
    void setNodeIDList(std::vector<std::string>&& _nodeIDList) override
    {
        m_inner()->nodeIDList = std::move(_nodeIDList);
    }
    // the groupType
    void setType(uint16_t _type) override { m_inner()->type = _type; }

    std::string const& groupID() const override { return m_inner()->groupID; }
    // Note: externally ensure thread safety
    std::vector<std::string> const& nodeIDList() const override { return m_inner()->nodeIDList; }
    int type() const override { return m_inner()->type; }
    void setNodeProtocolList(
        std::vector<bcos::protocol::ProtocolInfo::ConstPtr>&& _protocolList) override
    {
        // encode protocolList into m_inner()
        for (auto const& protocol : _protocolList)
        {
            appendInnerProtocol(protocol);
        }
        m_protocolList = std::move(_protocolList);
    }

    std::vector<bcos::protocol::ProtocolInfo::ConstPtr> const& nodeProtocolList() const override
    {
        return m_protocolList;
    }
    void appendNodeID(std::string const& _nodeID) override
    {
        m_inner()->nodeIDList.emplace_back(_nodeID);
    }

    void appendProtocol(bcos::protocol::ProtocolInfo::ConstPtr _protocol) override
    {
        m_protocolList.emplace_back(_protocol);
        appendInnerProtocol(_protocol);
    }
    bcos::protocol::ProtocolInfo::ConstPtr protocol(uint64_t _index) const override
    {
        if (m_protocolList.size() <= _index)
        {
            return nullptr;
        }
        return m_protocolList.at(_index);
    }
    const bcostars::GroupNodeInfo& inner() const { return *m_inner(); }

protected:
    virtual void appendInnerProtocol(bcos::protocol::ProtocolInfo::ConstPtr _protocol)
    {
        bcostars::ProtocolInfo tarsProtocol;
        tarsProtocol.moduleID = _protocol->protocolModuleID();
        tarsProtocol.minVersion = _protocol->minVersion();
        tarsProtocol.maxVersion = _protocol->maxVersion();
        m_inner()->protocolList.emplace_back(std::move(tarsProtocol));
    }

    virtual void decodeInner()
    {
        // recover m_protocolList
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

private:
    std::function<bcostars::GroupNodeInfo*()> m_inner;
    std::vector<bcos::protocol::ProtocolInfo::ConstPtr> m_protocolList;
};
}  // namespace protocol
}  // namespace bcostars