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
#include "bcos-gateway/libnetwork/RcuSnapshot.h"
#include "bcos-tars-protocol/tars/RouterTable.h"
#include <memory>


namespace bcos::gateway
{
class RouterTableEntry : public RouterTableEntryInterface
{
public:
    using Ptr = std::shared_ptr<RouterTableEntry>;
    RouterTableEntry();
    RouterTableEntry(std::function<bcostars::RouterTableEntry*()> _inner);
    RouterTableEntry(RouterTableEntry&&) = delete;
    RouterTableEntry(const RouterTableEntry&) = delete;
    RouterTableEntry& operator=(const RouterTableEntry&) = delete;
    RouterTableEntry& operator=(RouterTableEntry&&) = delete;
    ~RouterTableEntry() override = default;

    /// Deep copy, including the tars entry the inner accessor points at. The copy constructor is
    /// deleted (the inner tars object is owned by the accessor lambda), so the snapshot tables need
    /// an explicit deep clone for copy-on-write.
    std::shared_ptr<RouterTableEntry> clone() const
    {
        auto copy = std::make_shared<RouterTableEntry>([inner = *(m_inner())]() mutable {
            return &inner;
        });
        return copy;
    }

    void setDstNode(std::string const& _dstNode) override;
    void setNextHop(std::string const& _nextHop) override;
    void clearNextHop() override;
    void setDistance(int32_t _distance) override;
    void incDistance(int32_t _deltaDistance) override;

    // Note: for compatibility, use long p2p-id
    std::string const& dstNode() const override;
    // Note: for compatibility, use long p2p-id
    std::string const& nextHop() const override;
    int32_t distance() const override;

    bcostars::RouterTableEntry const& inner() const;

    // set the dstNodeInfo
    void setDstNodeInfo(P2PInfo const& _dstNodeInfo) override;

    void resetDstNodeInfo(P2PInfo const& _dstNodeInfo) override;

    // the short p2p id
    P2PInfo dstNodeInfo() const override;

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
    RouterTable();
    RouterTable(bytesConstRef _decodedData);
    RouterTable(RouterTable&&) = delete;
    RouterTable(const RouterTable&) = delete;
    RouterTable& operator=(const RouterTable&) = delete;
    RouterTable& operator=(RouterTable&&) = delete;
    ~RouterTable() override = default;

    void encode(bcos::bytes& _encodedData) override;
    void decode(bcos::bytesConstRef _decodedData) override;

    std::map<std::string, RouterTableEntryInterface::Ptr> const& routerEntries() override;
    // append the unreachableNodes into param _unreachableNodes
    bool update(std::set<std::string>& _unreachableNodes, std::string const& _generatedFrom,
        RouterTableEntryInterface::Ptr _entry) override;
    // append the unreachableNodes into param _unreachableNodes
    bool erase(std::set<std::string>& _unreachableNodes, std::string const& _p2pNodeID) override;

    void setNodeID(std::string const& _nodeID) override;
    std::string const& nodeID() const override;

    void setUnreachableDistance(int _unreachableDistance) override;

    std::string getNextHop(std::string const& _nodeID) override;
    std::set<std::string> getAllReachableNode() override;

    using Entries = std::map<std::string, RouterTableEntryInterface::Ptr>;

    bool updateDstNodeEntry(
        Entries& entries, std::string const& _generatedFrom, RouterTableEntryInterface::Ptr _entry);
    /// Convenience overload: apply the entry update in its own snapshot publish.
    bool updateDstNodeEntry(
        std::string const& _generatedFrom, RouterTableEntryInterface::Ptr _entry);
    void updateDistanceForAllRouterEntries(std::set<std::string>& _unreachableNodes,
        std::string const& _nextHop, int32_t _newDistance);
    void updateDistanceForAllRouterEntries(std::set<std::string>& _unreachableNodes,
        std::string const& _nextHop, int32_t _newDistance, Entries& entries);

private:
    /// Deep copy: the concrete entries are tars-backed and neither copyable nor movable, so a
    /// snapshot clone has to rebuild each one.
    static Entries cloneEntries(Entries const& entries);

    std::string m_nodeID;
    std::function<bcostars::RouterTable*()> m_inner;

    // Read-mostly routing table: every forwarded message reads it, topology changes write it.
    // Copy-on-write with a grace period removes the read-side lock (see RcuSnapshot.h).
    RcuSnapshot<Entries> m_entries;

    // encode() materialises m_inner from the snapshot; it is the only user of m_inner and is not
    // called concurrently with itself, but guard it anyway since it mutates shared state
    std::mutex x_encode;

    int m_unreachableDistance = 10;
};

class RouterTableFactoryImpl : public RouterTableFactory
{
public:
    using Ptr = std::shared_ptr<RouterTableFactoryImpl>;
    RouterTableInterface::Ptr createRouterTable() override;
    RouterTableInterface::Ptr createRouterTable(bcos::bytesConstRef _decodedData) override;

    RouterTableEntryInterface::Ptr createRouterEntry() override;
};

}  // namespace bcos::gateway
