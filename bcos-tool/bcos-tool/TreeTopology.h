/**
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
 * @brief TreeTopology implementation
 * @file TreeTopology.h
 * @author: yujiechen
 * @date 2023-03-22
 */
#pragma once

#include "bcos-crypto/bcos-crypto/KeyCompareTools.h"
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>

#define TREE_LOG(LEVEL)                                                      \
    BCOS_LOG(LEVEL) << LOG_BADGE("TREE") << LOG_KV("consIndex", m_consIndex) \
                    << LOG_KV("nodeId", m_nodeId->shortHex())

namespace bcos
{
namespace tool
{

class TreeTopology
{
public:
    using Ptr = std::shared_ptr<TreeTopology>;

    TreeTopology(bcos::crypto::NodeIDPtr _nodeId, std::uint32_t const _treeWidth = 3)
      : m_nodeId(std::move(_nodeId)), m_treeWidth(_treeWidth)
    {
        m_consensusNodes = std::make_shared<bcos::crypto::NodeIDs>();
    }

    virtual ~TreeTopology() = default;
    // update corresponding info when the consensus nodes changed
    virtual void updateConsensusNodeInfo(const bcos::crypto::NodeIDs& _consensusNodes);

    // select the nodes by tree topology
    virtual bcos::crypto::NodeIDListPtr selectNodes(bcos::crypto::NodeIDSetPtr _peers,
        std::int64_t const _consIndex = 0, bool const _isTheStartNode = false);

    virtual bcos::crypto::NodeIDListPtr selectNodesByNodeID(bcos::crypto::NodeIDSetPtr _peers,
        bcos::crypto::NodeIDPtr _nodeID, bool const _isTheStartNode = false);

    virtual bcos::crypto::NodeIDListPtr selectParent(bcos::crypto::NodeIDSetPtr _peers,
        std::int64_t const _consIndex = 0, bool const _selectAll = false);

    virtual bcos::crypto::NodeIDListPtr selectParentByNodeID(
        bcos::crypto::NodeIDSetPtr _peers, bcos::crypto::NodeIDPtr _nodeID);

    virtual std::int64_t consIndex() const
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
    virtual void recursiveSelectChildNodes(bcos::crypto::NodeIDListPtr _selectedNodeList,
        std::int64_t const _treeIndex, bcos::crypto::NodeIDSetPtr _peers,
        std::int64_t const _consIndex);
    // select the parent nodes by tree
    // _selectAll is true:
    // select all the parent(include the grandparent) for the given node
    virtual void selectParentNodes(bcos::crypto::NodeIDListPtr _selectedNodeList,
        bcos::crypto::NodeIDSetPtr _peers, std::int64_t const _nodeIndex,
        std::int64_t const _startIndex, bool const _selectAll = false);

    // starting from this node, calculate the index of other nodes in a ring
    // 0 1 2 3 4 5 6 7 8 9
    // suppost this node is 4: then pass in 5, return 1; pass in 9, return 5; pass in 0, return 6;
    // pass in 3, return 9; pass in 4, return 0
    std::int64_t getTreeIndex(std::int64_t const _consIndex);

    virtual bool getNodeIDByIndex(
        bcos::crypto::NodeIDPtr& _nodeID, std::int64_t const _nodeIndex) const;

    virtual std::int64_t transTreeIndexToNodeIndex(
        std::int64_t const _selectedIndex, std::int64_t const _offset);

    std::int64_t getNodeIndexByNodeId(
        bcos::crypto::NodeIDListPtr _nodeList, bcos::crypto::NodeIDPtr _nodeId);

protected:
    mutable Mutex m_mutex;
    // the list of the current consensus nodes
    bcos::crypto::NodeIDListPtr m_consensusNodes;
    std::atomic_int64_t m_nodeNum{0};

    bcos::crypto::NodeIDPtr m_nodeId;
    std::uint32_t m_treeWidth;
    std::atomic_int64_t m_consIndex{0};

    std::atomic_int64_t m_endIndex{0};
    std::atomic_int64_t m_startIndex{0};
};

}  // namespace tool
}  // namespace bcos
