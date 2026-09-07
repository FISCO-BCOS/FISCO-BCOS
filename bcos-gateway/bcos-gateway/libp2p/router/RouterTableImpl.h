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

#include "bcos-framework/gateway/GatewayTypeDef.h"
#include "bcos-tars-protocol/tars/RouterTable.h"
#include "bcos-utilities/Common.h"
#include <memory>
#include <set>


namespace bcos::gateway
{
class RouterTableEntry
{
public:
    using Ptr = std::shared_ptr<RouterTableEntry>;
    RouterTableEntry();
    RouterTableEntry(std::function<bcostars::RouterTableEntry*()> _inner);
    RouterTableEntry(RouterTableEntry&&) = delete;
    RouterTableEntry(const RouterTableEntry&) = delete;
    RouterTableEntry& operator=(const RouterTableEntry&) = delete;
    RouterTableEntry& operator=(RouterTableEntry&&) = delete;
    ~RouterTableEntry() = default;

    void setDstNode(std::string const& _dstNode);
    void setNextHop(std::string const& _nextHop);
    void clearNextHop();
    void setDistance(int32_t _distance);
    void incDistance(int32_t _deltaDistance);

    // Note: for compatibility, use long p2p-id
    std::string const& dstNode() const;
    // Note: for compatibility, use long p2p-id
    std::string const& nextHop() const;

    std::string printDstNode() const;
    std::string printNextHop() const;

    int32_t distance() const;

    bcostars::RouterTableEntry const& inner() const;

    // set the dstNodeInfo
    void setDstNodeInfo(P2PInfo const& _dstNodeInfo);

    void resetDstNodeInfo(P2PInfo const& _dstNodeInfo);

    // the short p2p id
    P2PInfo dstNodeInfo() const;

private:
    static void assignNodeIDInfo(bcostars::NodeIDInfo& nodeIDInfo, P2PInfo const& routerNodeID)
    {
        nodeIDInfo.p2pID = routerNodeID.p2pID;
    }

    std::function<bcostars::RouterTableEntry*()> m_inner;
};

class RouterTable
{
public:
    using Ptr = std::shared_ptr<RouterTable>;
    RouterTable();
    RouterTable(bytesConstRef _decodedData);
    RouterTable(RouterTable&&) = delete;
    RouterTable(const RouterTable&) = delete;
    RouterTable& operator=(const RouterTable&) = delete;
    RouterTable& operator=(RouterTable&&) = delete;
    ~RouterTable() = default;

    void encode(bcos::bytes& _encodedData);
    void decode(bcos::bytesConstRef _decodedData);

    std::map<std::string, RouterTableEntry::Ptr> const& routerEntries();
    // append the unreachableNodes into param _unreachableNodes
    bool update(std::set<std::string>& _unreachableNodes, std::string const& _generatedFrom,
        RouterTableEntry::Ptr _entry);
    // append the unreachableNodes into param _unreachableNodes
    bool erase(std::set<std::string>& _unreachableNodes, std::string const& _p2pNodeID);

    void setNodeID(std::string const& _nodeID);
    std::string const& nodeID() const;

    void setUnreachableDistance(int _unreachableDistance);

    std::string getNextHop(std::string const& _nodeID);
    std::set<std::string> getAllReachableNode();

    bool updateDstNodeEntry(std::string const& _generatedFrom, RouterTableEntry::Ptr _entry);
    void updateDistanceForAllRouterEntries(std::set<std::string>& _unreachableNodes,
        std::string const& _nextHop, int32_t _newDistance);

private:
    std::string m_nodeID;
    std::function<bcostars::RouterTable*()> m_inner;
    std::map<std::string, RouterTableEntry::Ptr> m_routerEntries;
    mutable SharedMutex x_routerEntries;

    int m_unreachableDistance = 10;
};

}  // namespace bcos::gateway
