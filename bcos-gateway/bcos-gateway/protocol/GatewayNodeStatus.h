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

#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include <bcos-framework/protocol/Protocol.h>
#include <bcos-gateway/Common.h>
#include <bcos-tars-protocol/protocol/GroupNodeInfoImpl.h>
#include <bcos-tars-protocol/tars/GatewayInfo.h>
#include <memory>
namespace bcos
{
namespace gateway
{
class GatewayNodeStatus
{
public:
    using Ptr = std::shared_ptr<GatewayNodeStatus>;
    using ConstPtr = std::shared_ptr<GatewayNodeStatus const>;
    GatewayNodeStatus();
    virtual ~GatewayNodeStatus() {}

    virtual void setUUID(std::string const& _uuid) { m_tarsStatus->uuid = _uuid; }
    virtual void setSeq(uint32_t _seq) { m_tarsStatus->seq = _seq; }
    virtual void setGroupNodeInfos(std::vector<GroupNodeInfo::Ptr>&& _groupNodeInfos)
    {
        m_groupNodeInfos = std::move(_groupNodeInfos);
    }

    virtual bytesPointer encode();
    virtual void decode(bytesConstRef _data);

    virtual std::string const& uuid() const { return m_tarsStatus->uuid; }
    virtual uint32_t seq() const { return m_tarsStatus->seq; }
    // Note: externally ensure thread safety
    virtual std::vector<GroupNodeInfo::Ptr> const& groupNodeInfos() const
    {
        return m_groupNodeInfos;
    }

private:
    std::shared_ptr<bcostars::GatewayNodeStatus> m_tarsStatus;
    std::vector<GroupNodeInfo::Ptr> m_groupNodeInfos;
};

class GatewayNodeStatusFactory
{
public:
    using Ptr = std::shared_ptr<GatewayNodeStatusFactory>;
    GatewayNodeStatusFactory() = default;
    virtual ~GatewayNodeStatusFactory() {}

    GatewayNodeStatus::Ptr createGatewayNodeStatus()
    {
        return std::make_shared<GatewayNodeStatus>();
    }
    GroupNodeInfo::Ptr createGroupNodeInfo()
    {
        return std::make_shared<bcostars::protocol::GroupNodeInfoImpl>();
    }
};
}  // namespace gateway
}  // namespace bcos