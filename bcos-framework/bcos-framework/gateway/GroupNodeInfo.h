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
 * @file GroupNodeInfo.h
 * @author: yujiechen
 * @date 2022-3-8
 */
#pragma once
#include <bcos-framework//protocol/Protocol.h>
#include <bcos-framework//protocol/ProtocolInfo.h>
#include <memory>
#include <vector>
namespace bcos
{
namespace gateway
{
class GroupNodeInfo
{
public:
    using Ptr = std::shared_ptr<GroupNodeInfo>;
    GroupNodeInfo() = default;
    virtual ~GroupNodeInfo() {}
    // the groupID
    virtual void setGroupID(std::string const& _groupID) = 0;
    // the nodeIDList
    virtual void setNodeIDList(std::vector<std::string>&& _nodeIDList) = 0;
    virtual void appendNodeID(std::string const& _nodeID) = 0;
    virtual void appendProtocol(bcos::protocol::ProtocolInfo::ConstPtr _protocol) = 0;
    // the groupType
    virtual void setType(uint16_t _type) = 0;

    virtual std::string const& groupID() const = 0;
    // Note: externally ensure thread safety
    virtual std::vector<std::string> const& nodeIDList() const = 0;
    virtual int type() const = 0;

    virtual void setNodeProtocolList(
        std::vector<bcos::protocol::ProtocolInfo::ConstPtr>&& _protocolList) = 0;
    virtual std::vector<bcos::protocol::ProtocolInfo::ConstPtr> const& nodeProtocolList() const = 0;
    virtual bcos::protocol::ProtocolInfo::ConstPtr protocol(uint64_t _index) const = 0;
};
}  // namespace gateway
}  // namespace bcos