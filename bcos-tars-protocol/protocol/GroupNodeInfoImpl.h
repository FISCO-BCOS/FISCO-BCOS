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

#include <bcos-framework/interfaces/gateway/GroupNodeInfo.h>
#include <bcos-tars-protocol/tars/GatewayInfo.h>
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
    {}
    explicit GroupNodeInfoImpl(std::function<bcostars::GroupNodeInfo*()> inner) : m_inner(inner) {}
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
    const bcostars::GroupNodeInfo& inner() const { return *m_inner(); }

private:
    std::function<bcostars::GroupNodeInfo*()> m_inner;
};
}  // namespace protocol
}  // namespace bcostars