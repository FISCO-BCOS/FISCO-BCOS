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
 * @brief : TreeTopology implementation
 * @file: TreeTopology.h
 * @author: yujiechen
 * @date: 2019-09-19
 */
#pragma once

#include "Common.h"
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>

#define TREE_LOG(_OBV)                                                 \
    LOG(_OBV) << LOG_BADGE("TREE") << LOG_KV("consIndex", m_consIndex) \
              << LOG_KV("nodeId", m_nodeId.abridged())

namespace dev
{
namespace sync
{
class TreeTopology
{
public:
    using Ptr = std::shared_ptr<TreeTopology>;
    TreeTopology(dev::h512 const& _nodeId, unsigned const& _treeWidth = 3)
      : m_treeWidth(_treeWidth), m_nodeId(_nodeId)
    {
        m_currentConsensusNodes = std::make_shared<dev::h512s>();
    }

    virtual ~TreeTopology() {}
    // update corresponding info when the consensus nodes changed
    virtual void updateConsensusNodeInfo(dev::h512s const& _consensusNodes);

    // select the nodes by tree topology
    virtual std::shared_ptr<dev::h512s> selectNodes(std::shared_ptr<std::set<dev::h512>> _peers);

protected:
    virtual void updateStartAndEndIndex();

    // select the child nodes by tree
    virtual void recursiveSelectChildNodes(std::shared_ptr<dev::h512s> _selectedNodeList,
        ssize_t const& _parentIndex, std::shared_ptr<std::set<dev::h512>> _peers);
    // select the parent nodes by tree
    virtual void selectParentNodes(std::shared_ptr<dev::h512s> _selectedNodeList,
        std::shared_ptr<std::set<dev::h512>> _peers, int64_t const& _nodeIndex);

    virtual bool getNodeIDByIndex(dev::h512& _nodeID, ssize_t const& _nodeIndex) const;

    // get the child node index
    virtual ssize_t getChildNodeIndex(ssize_t const& _parentIndex, ssize_t const& _offset);
    // get the parent node index
    virtual ssize_t getParentNodeIndex(ssize_t const& _nodeIndex);

    ssize_t getNodeIndexByNodeId(std::shared_ptr<dev::h512s> _findSet, dev::h512& _nodeId);

protected:
    mutable Mutex m_mutex;
    // the list of the current consensus nodes
    std::shared_ptr<dev::h512s> m_currentConsensusNodes;

    unsigned m_treeWidth;
    dev::h512 m_nodeId;
    std::atomic<int64_t> m_consIndex = {0};

    std::atomic<int64_t> m_endIndex = {0};
    std::atomic<int64_t> m_startIndex = {0};

    ssize_t m_childOffset = 0;
};
}  // namespace sync
}  // namespace dev
