/*
 *  Copyright (C) 2020 FISCO BCOS.
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
 */
/**
 * @brief : TreeTopology implementation
 * @file: TreeTopology.h
 * @author: yujiechen
 * @date: 2019-09-19
 */
#pragma once

#include "FixedHash.h"
#include <libutilities/Common.h>

#define TREE_LOG(_OBV)                                                 \
    LOG(_OBV) << LOG_BADGE("TREE") << LOG_KV("consIndex", m_consIndex) \
              << LOG_KV("nodeId", m_nodeId.abridged())

namespace bcos
{
namespace sync
{
class TreeTopology
{
public:
    using Ptr = std::shared_ptr<TreeTopology>;
    TreeTopology(bcos::h512 const& _nodeId, unsigned const& _treeWidth = 3)
      : m_treeWidth(_treeWidth), m_nodeId(_nodeId)
    {
        m_currentConsensusNodes = std::make_shared<bcos::h512s>();
    }

    virtual ~TreeTopology() {}
    // update corresponding info when the consensus nodes changed
    virtual void updateConsensusNodeInfo(bcos::h512s const& _consensusNodes);

    // select the nodes by tree topology
    virtual std::shared_ptr<bcos::h512s> selectNodes(std::shared_ptr<std::set<bcos::h512>> _peers,
        int64_t const& _consIndex = 0, bool const& _isTheStartNode = false);

    virtual std::shared_ptr<bcos::h512s> selectNodesByNodeID(
        std::shared_ptr<std::set<bcos::h512>> _peers, bcos::h512 const& _nodeID,
        bool const& _isTheStartNode = false);

    virtual std::shared_ptr<bcos::h512s> selectParentByNodeID(
        std::shared_ptr<std::set<bcos::h512>> _peers, bcos::h512 const& _nodeID);

    virtual std::shared_ptr<bcos::h512s> selectParent(std::shared_ptr<std::set<bcos::h512>> _peers,
        int64_t const& _consIndex = 0, bool const& _selectAll = false);

    virtual int64_t consIndex() const
    {
        if (m_consIndex == -1)
        {
            std::srand(utcTime());
            return std::rand() % m_nodeNum;
        }
        return m_consIndex;
    }

protected:
    virtual void updateStartAndEndIndex();

    // select the child nodes by tree
    virtual void recursiveSelectChildNodes(std::shared_ptr<bcos::h512s> _selectedNodeList,
        ssize_t const& _parentIndex, std::shared_ptr<std::set<bcos::h512>> _peers,
        int64_t const& _startIndex);
    // select the parent nodes by tree
    // _selectAll is true:
    // select all the parent(include the grandparent) for the given node
    virtual void selectParentNodes(std::shared_ptr<bcos::h512s> _selectedNodeList,
        std::shared_ptr<std::set<bcos::h512>> _peers, int64_t const& _nodeIndex,
        int64_t const& _startIndex, bool const& _selectAll = false);

    int64_t getNodeIndex(int64_t const& _consIndex);

    virtual bool getNodeIDByIndex(bcos::h512& _nodeID, ssize_t const& _nodeIndex) const;

    virtual ssize_t getSelectedNodeIndex(ssize_t const& _selectedIndex, ssize_t const& _offset);

    ssize_t getNodeIndexByNodeId(std::shared_ptr<bcos::h512s> _findSet, bcos::h512 const& _nodeId);

protected:
    mutable Mutex m_mutex;
    // the list of the current consensus nodes
    std::shared_ptr<bcos::h512s> m_currentConsensusNodes;
    std::atomic<int64_t> m_nodeNum = {0};

    unsigned m_treeWidth;
    bcos::h512 m_nodeId;
    std::atomic<int64_t> m_consIndex = {0};

    std::atomic<int64_t> m_endIndex = {0};
    std::atomic<int64_t> m_startIndex = {0};
};
}  // namespace sync
}  // namespace bcos
