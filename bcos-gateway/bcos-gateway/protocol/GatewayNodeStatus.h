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
 * @file GatewayNodeStatus.h
 * @author: yujiechen
 * @date 2021-12-31
 */
#pragma once
#include <bcos-gateway/Common.h>
#include <bcos-tars-protocol/tars/GatewayInfo.h>
#include <memory>
namespace bcos
{
namespace gateway
{
class GroupNodeInfo
{
public:
    using Ptr = std::shared_ptr<GroupNodeInfo>;
    GroupNodeInfo(std::string const& _groupID) : m_groupID(_groupID) {}
    virtual ~GroupNodeInfo() {}

    void setGroupID(std::string const& _groupID) { m_groupID = _groupID; }
    void setNodeIDList(std::vector<std::string>&& _nodeIDList)
    {
        m_nodeIDList = std::move(_nodeIDList);
    }
    void setType(GroupType _type) { m_type = _type; }

    std::string const& groupID() const { return m_groupID; }
    // Note: externally ensure thread safety
    std::vector<std::string> const& nodeIDList() const { return m_nodeIDList; }
    int type() const { return m_type; }

private:
    std::string m_groupID;
    std::vector<std::string> m_nodeIDList;
    int m_type;
};

class GatewayNodeStatus
{
public:
    using Ptr = std::shared_ptr<GatewayNodeStatus>;
    GatewayNodeStatus();
    virtual ~GatewayNodeStatus() {}

    virtual void setUUID(std::string const& _uuid) { m_tarsStatus->uuid = _uuid; }
    virtual void setSeq(int32_t _seq) { m_tarsStatus->seq = _seq; }
    virtual void setGroupNodeInfos(std::vector<GroupNodeInfo::Ptr>&& _groupNodeInfos)
    {
        m_groupNodeInfos = std::move(_groupNodeInfos);
    }

    virtual bytesPointer encode();
    virtual void decode(bytesConstRef _data);

    virtual std::string const& uuid() const { return m_tarsStatus->uuid; }
    virtual int32_t seq() const { return m_tarsStatus->seq; }
    // Note: externally ensure thread safety
    virtual std::vector<GroupNodeInfo::Ptr> const& groupNodeInfos() const
    {
        return m_groupNodeInfos;
    }

private:
    std::shared_ptr<bcostars::GatewayNodeStatus> m_tarsStatus;
    std::vector<GroupNodeInfo::Ptr> m_groupNodeInfos;
};
}  // namespace gateway
}  // namespace bcos