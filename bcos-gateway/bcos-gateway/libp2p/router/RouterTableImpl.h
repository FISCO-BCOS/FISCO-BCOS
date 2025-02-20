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
 * @file RouterTableImpl.h
 * @author: yujiechen
 * @date 2022-5-24
 */
#pragma once

#include "RouterTableInterface.h"
#include <bcos-tars-protocol/tars/RouterTable.h>
#include <memory>


namespace bcos::gateway
{
class RouterTableEntry : public RouterTableEntryInterface
{
public:
    using Ptr = std::shared_ptr<RouterTableEntry>;
    RouterTableEntry()
      : m_inner([m_entry = bcostars::RouterTableEntry()]() mutable { return &m_entry; })
    {}
    RouterTableEntry(std::function<bcostars::RouterTableEntry*()> _inner)
      : m_inner(std::move(_inner))
    {}
    RouterTableEntry(RouterTableEntry&&) = delete;
    RouterTableEntry(const RouterTableEntry&) = delete;
    RouterTableEntry& operator=(const RouterTableEntry&) = delete;
    RouterTableEntry& operator=(RouterTableEntry&&) = delete;
    ~RouterTableEntry() override = default;

    void setDstNode(std::string const& _dstNode) override { m_inner()->dstNode = _dstNode; }
    void setNextHop(std::string const& _nextHop) override { m_inner()->nextHop = _nextHop; }
    void clearNextHop() override { m_inner()->nextHop = std::string(); }
    void setDistance(int32_t _distance) override { m_inner()->distance = _distance; }
    void incDistance(int32_t _deltaDistance) override { m_inner()->distance += _deltaDistance; }

    // Note: for compatibility, use long p2p-id
    std::string const& dstNode() const override { return m_inner()->dstNode; }
    // Note: for compatibility, use long p2p-id
    std::string const& nextHop() const override { return m_inner()->nextHop; }
    int32_t distance() const override { return m_inner()->distance; }

    bcostars::RouterTableEntry const& inner() const { return *(m_inner()); }

    // set the dstNodeInfo
    void setDstNodeInfo(P2PInfo const& _dstNodeInfo) override
    {
        assignNodeIDInfo(m_inner()->dstNodeInfo, _dstNodeInfo);
    }

    void resetDstNodeInfo(P2PInfo const& _dstNodeInfo) override
    {
        if (!m_inner()->dstNodeInfo.p2pID.empty())
        {
            return;
        }
        if (_dstNodeInfo.p2pID.empty())
        {
            return;
        }
        setDstNodeInfo(_dstNodeInfo);
    }

    // the short p2p id
    P2PInfo dstNodeInfo() const override { return {m_inner()->dstNodeInfo.p2pID, dstNode()}; }

private:
    static void assignNodeIDInfo(bcostars::NodeIDInfo& nodeIDInfo, P2PInfo const& routerNodeID)
    {
        nodeIDInfo.p2pID = routerNodeID.p2pID;
    }

    std::function<bcostars::RouterTableEntry*()> m_inner;
};

class RouterTable : public RouterTableInterface
{
public:
    using Ptr = std::shared_ptr<RouterTable>;
    RouterTable() : m_inner([m_table = bcostars::RouterTable()]() mutable { return &m_table; }) {}
    RouterTable(bytesConstRef _decodedData) : RouterTable() { decode(_decodedData); }
    RouterTable(RouterTable&&) = delete;
    RouterTable(const RouterTable&) = delete;
    RouterTable& operator=(const RouterTable&) = delete;
    RouterTable& operator=(RouterTable&&) = delete;
    ~RouterTable() override = default;

    void encode(bcos::bytes& _encodedData) override;
    void decode(bcos::bytesConstRef _decodedData) override;

    std::map<std::string, RouterTableEntryInterface::Ptr> const& routerEntries() override
    {
        return m_routerEntries;
    }
    // append the unreachableNodes into param _unreachableNodes
    bool update(std::set<std::string>& _unreachableNodes, std::string const& _generatedFrom,
        RouterTableEntryInterface::Ptr _entry) override;
    // append the unreachableNodes into param _unreachableNodes
    bool erase(std::set<std::string>& _unreachableNodes, std::string const& _p2pNodeID) override;

    void setNodeID(std::string const& _nodeID) override { m_nodeID = _nodeID; }
    std::string const& nodeID() const override { return m_nodeID; }

    void setUnreachableDistance(int _unreachableDistance) override
    {
        m_unreachableDistance = _unreachableDistance;
    }

    std::string getNextHop(std::string const& _nodeID) override;
    std::set<std::string> getAllReachableNode() override;

    bool updateDstNodeEntry(
        std::string const& _generatedFrom, RouterTableEntryInterface::Ptr _entry);
    void updateDistanceForAllRouterEntries(std::set<std::string>& _unreachableNodes,
        std::string const& _nextHop, int32_t _newDistance);

private:
    std::string m_nodeID;
    std::function<bcostars::RouterTable*()> m_inner;
    std::map<std::string, RouterTableEntryInterface::Ptr> m_routerEntries;
    mutable SharedMutex x_routerEntries;

    int m_unreachableDistance = 10;
};

class RouterTableFactoryImpl : public RouterTableFactory
{
public:
    using Ptr = std::shared_ptr<RouterTableFactoryImpl>;
    RouterTableInterface::Ptr createRouterTable() override
    {
        return std::make_shared<RouterTable>();
    }
    RouterTableInterface::Ptr createRouterTable(bcos::bytesConstRef _decodedData) override
    {
        return std::make_shared<RouterTable>(_decodedData);
    }

    RouterTableEntryInterface::Ptr createRouterEntry() override
    {
        return std::make_shared<RouterTableEntry>();
    }
};

}  // namespace bcos::gateway
