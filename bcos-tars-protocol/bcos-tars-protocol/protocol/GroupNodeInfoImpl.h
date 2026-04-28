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
#include <bcos-framework/gateway/GroupNodeInfo.h>
#include <bcos-framework/protocol/ProtocolInfo.h>
#include <bcos-tars-protocol/tars/GatewayInfo.h>
#include <bcos-tars-protocol/tars/ProtocolInfo.h>

namespace bcostars
{
namespace protocol
{
class GroupNodeInfoImpl : public bcos::gateway::GroupNodeInfo
{
public:
    GroupNodeInfoImpl();
    explicit GroupNodeInfoImpl(std::function<bcostars::GroupNodeInfo*()> inner);
    ~GroupNodeInfoImpl() override;

    // the groupID
    void setGroupID(std::string const& _groupID) override;
    // the nodeIDList
    void setNodeIDList(std::vector<std::string>&& _nodeIDList) override;
    void setNodeTypeList(std::vector<int32_t>&& _nodeTypeList) override;
    // the groupType
    void setType(uint16_t _type) override;

    std::string const& groupID() const override;
    // Note: externally ensure thread safety
    std::vector<std::string> const& nodeIDList() const override;
    std::vector<int32_t> const& nodeTypeList() const override;
    int type() const override;
    void setNodeProtocolList(
        std::vector<bcos::protocol::ProtocolInfo::ConstPtr>&& _protocolList) override;

    std::vector<bcos::protocol::ProtocolInfo::ConstPtr> const& nodeProtocolList() const override;
    void appendNodeID(std::string const& _nodeID) override;

    void appendProtocol(bcos::protocol::ProtocolInfo::ConstPtr _protocol) override;
    bcos::protocol::ProtocolInfo::ConstPtr protocol(uint64_t _index) const override;
    const bcostars::GroupNodeInfo& inner() const;

protected:
    virtual void appendInnerProtocol(bcos::protocol::ProtocolInfo::ConstPtr const& _protocol);

    virtual void decodeInner();

private:
    std::function<bcostars::GroupNodeInfo*()> m_inner;
    std::vector<bcos::protocol::ProtocolInfo::ConstPtr> m_protocolList;
};
}  // namespace protocol
}  // namespace bcostars