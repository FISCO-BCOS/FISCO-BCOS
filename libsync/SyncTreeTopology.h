/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2019 fisco-dev contributors.
 */
/**
 * @brief : SyncTreeTopology implementation
 * @file: SyncTreeTopology.h
 * @author: yujiechen
 * @date: 2019-09-19
 */
#pragma once
#include "Common.h"
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>

#define SYNCTREE_LOG(_OBV)                                                 \
    LOG(_OBV) << LOG_BADGE("SYNCTREE") << LOG_KV("nodeIndex", m_nodeIndex) \
              << LOG_KV("consIndex", m_consIndex) << LOG_KV("nodeId", m_nodeId.abridged())

namespace dev
{
namespace sync
{
class SyncTreeTopology
{
public:
    using Ptr = std::shared_ptr<SyncTreeTopology>;
    SyncTreeTopology(dev::h512 const& _nodeId, unsigned const& _treeWidth = 2)
      : m_treeWidth(_treeWidth), m_nodeId(_nodeId)
    {}

    virtual ~SyncTreeTopology() {}
    // update corresponding info when the nodes changed
    virtual void updateNodeListInfo(dev::h512s const& _nodeList);
    // update corresponding info when the consensus nodes changed
    virtual void updateConsensusNodeInfo(dev::h512s const& _consensusNodes);
    // select the nodes by tree topology
    virtual dev::h512s selectNodes(std::set<dev::h512> const& _peers);

protected:
    // select the child nodes by tree
    void recursiveSelectChildNodes(dev::h512s& _selectedNodeList, ssize_t const& _parentIndex,
        std::set<dev::h512> const& _peers);
    // select the parent nodes by tree
    void selectParentNodes(dev::h512s& _selectedNodeList, std::set<dev::h512> const& _peers,
        int64_t const& _nodeIndex);

private:
    bool getNodeIDByIndex(dev::h512& _nodeID, ssize_t const& _nodeIndex) const;
    ssize_t getNodeIndexByNodeId(dev::h512s const& _findSet, dev::h512& _nodeId);
    // update the tree-topology range the nodes located in
    void updateStartAndEndIndex();
    bool locatedInGroup();

protected:
    mutable Mutex m_mutex;
    // the nodeList include both the consensus nodes and the observer nodes
    dev::h512s m_nodeList;
    std::atomic<int64_t> m_nodeNum = {0};

    // the list of the current consensus nodes
    dev::h512s m_currentConsensusNodes;
    unsigned m_treeWidth;
    dev::h512 m_nodeId;
    std::atomic<int64_t> m_nodeIndex = {0};
    std::atomic<int64_t> m_consIndex = {0};
    std::atomic<int64_t> m_endIndex = {0};
    std::atomic<int64_t> m_startIndex = {0};
};
}  // namespace sync
}  // namespace dev
