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

namespace bcos::tool
{
class TreeTopology
{
public:
    using Ptr = std::shared_ptr<TreeTopology>;

    explicit TreeTopology(bcos::crypto::NodeIDPtr _nodeId, std::uint32_t const _treeWidth = 3)
      : m_nodeId(std::move(_nodeId)), m_treeWidth(_treeWidth)
    {
        m_consensusNodes = std::make_shared<bcos::crypto::NodeIDs>();
    }

    virtual ~TreeTopology() = default;
    TreeTopology(TreeTopology const&) = delete;
    TreeTopology& operator=(TreeTopology const&) = delete;
    TreeTopology(TreeTopology&&) = delete;
    TreeTopology& operator=(TreeTopology&&) = delete;

    // update corresponding info when the consensus nodes changed
    virtual void updateConsensusNodeInfo(const bcos::crypto::NodeIDs& _consensusNodes);

    // select the nodes by tree topology
    [[nodiscard]] virtual bcos::crypto::NodeIDSetPtr selectNodes(
        bcos::crypto::NodeIDSetPtr const& _peers, std::int32_t _consIndex = 0,
        bool _isTheStartNode = false, bcos::crypto::NodeIDPtr fromNode = nullptr);

    [[nodiscard]] virtual bcos::crypto::NodeIDSetPtr selectNodesByNodeID(
        bcos::crypto::NodeIDSetPtr const& _peers, bcos::crypto::NodeIDPtr const& _nodeID,
        bool _isTheStartNode);

    [[nodiscard]] virtual bcos::crypto::NodeIDSetPtr selectParent(
        bcos::crypto::NodeIDSetPtr const& _peers, std::int32_t _consIndex, bool _selectAll);

    [[nodiscard]] virtual bcos::crypto::NodeIDSetPtr selectParentByNodeID(
        bcos::crypto::NodeIDSetPtr const& _peers, bcos::crypto::NodeIDPtr const& _nodeID);

    virtual inline std::int32_t consIndex() const
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
    [[nodiscard]] virtual bcos::crypto::NodeIDSetPtr recursiveSelectChildNodes(
        std::int32_t _treeIndex, bcos::crypto::NodeIDSetPtr const& _peers, std::int32_t _consIndex);
    // select the parent nodes by tree
    // _selectAll is true:
    // select all the parent(include the grandparent) for the given node
    [[nodiscard]] virtual bcos::crypto::NodeIDSetPtr selectParentNodes(
        bcos::crypto::NodeIDSetPtr const& _peers, std::int32_t _nodeIndex, std::int32_t _startIndex,
        bool _selectAll);

    // starting from this node, calculate the index of other nodes in a ring
    // 0 1 2 3 4 5 6 7 8 9
    // assume this node is 4: then pass in 5, return 1; pass in 9, return 5; pass in 0, return 6;
    // pass in 3, return 9; pass in 4, return 0
    std::int32_t getTreeIndex(std::int32_t _consIndex);

    [[nodiscard]] virtual bcos::crypto::NodeIDPtr getNodeIDByIndex(std::int32_t _nodeIndex) const;

    virtual inline std::int32_t transTreeIndexToNodeIndex(
        std::int32_t _selectedIndex, std::int32_t _offset)
    {
        return (_selectedIndex + _offset) % m_nodeNum;
    }

    static std::int32_t getNodeIndexByNodeId(
        const bcos::crypto::NodeIDListPtr& _nodeList, const bcos::crypto::NodeIDPtr& _nodeId);

    mutable Mutex m_mutex;
    // the list of the current consensus nodes
    bcos::crypto::NodeIDListPtr m_consensusNodes;
    bcos::crypto::NodeIDPtr m_nodeId;

    std::atomic_int32_t m_nodeNum{0};
    std::uint32_t m_treeWidth;
    // index of consensus, if observer than -1
    std::atomic_int32_t m_consIndex{0};

    // node select range, [startIndex, endIndex]
    std::atomic_int32_t m_startIndex{0};
    std::atomic_int32_t m_endIndex{0};
};

}  // namespace bcos::tool
