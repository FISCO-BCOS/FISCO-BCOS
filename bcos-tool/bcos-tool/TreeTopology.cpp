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
 * @file TreeTopology.cpp
 * @author: yujiechen
 * @date 2023-03-22
 */
#include "TreeTopology.h"

using namespace bcos;
using namespace bcos::tool;

void TreeTopology::updateConsensusNodeInfo(const bcos::crypto::NodeIDs& _consensusNodes)
{
    Guard lock(m_mutex);
    if (bcos::crypto::KeyCompareTools::compareTwoNodeIDs(*m_consensusNodes, _consensusNodes))
    {
        return;
    }
    *m_consensusNodes = _consensusNodes;
    m_consIndex = getNodeIndexByNodeId(m_consensusNodes, m_nodeId);
    updateStartAndEndIndex();
}

void TreeTopology::updateStartAndEndIndex()
{
    m_startIndex = 0;
    m_endIndex = static_cast<std::int32_t>(m_consensusNodes->size() - 1);
    m_nodeNum = static_cast<std::int32_t>(m_consensusNodes->size());
    TREE_LOG(DEBUG) << LOG_DESC("updateConsensusNodeInfo") << LOG_KV("consIndex", m_consIndex)
                    << LOG_KV("endIndex", m_endIndex) << LOG_KV("nodeNum", m_nodeNum);
}

/**
 * @brief : get node index according to given nodeID and node list
 * @return:
 *  -1: the given _nodeId doesn't exist in _findSet
 *   >=0 : the index of the given node
 */
std::int32_t TreeTopology::getNodeIndexByNodeId(
    const bcos::crypto::NodeIDListPtr& _nodeList, const bcos::crypto::NodeIDPtr& _nodeId)
{
    std::int32_t nodeIndex{-1};
    auto iter = std::find_if(_nodeList->begin(), _nodeList->end(),
        [&_nodeId](auto& _node) { return (_nodeId->data() == _node->data()); });
    return (iter != _nodeList->end()) ? (std::int32_t)std::distance(_nodeList->begin(), iter) : -1;
}

/**
 * @brief : get nodeID according to nodeIndex
 * @return:
 *  false: the given node doesn't locate in the node list
 *  true:  the given node locates in the node list, and assign its node Id to _nodeID
 */
bcos::crypto::NodeIDPtr TreeTopology::getNodeIDByIndex(std::int32_t _nodeIndex) const
{
    if (std::cmp_greater_equal(_nodeIndex, m_consensusNodes->size()) || _nodeIndex < 0)
    {
        return nullptr;
    }
    return m_consensusNodes->at(_nodeIndex);
}

/**
 * @brief : select child nodes of given node from peers recursively
 *          if the any child node doesn't exist in the peers, select the grand child nodes, etc.
 * @params :
 *  1. _selectedNodeList: return value, the selected child nodes(maybe the grand child, etc.)
 *  2. _treeIndex: the index of the consIndex in tree
 *  3. _peers: the nodeIDs of peers maintained by syncStatus
 */
bcos::crypto::NodeIDSetPtr TreeTopology::recursiveSelectChildNodes(
    std::int32_t _treeIndex, bcos::crypto::NodeIDSetPtr const& _peers, std::int32_t _consIndex)
{
    bcos::crypto::NodeIDSetPtr selectedNodeSet = std::make_shared<bcos::crypto::NodeIDSet>();
    // if the node doesn't locate in the group
    for (std::uint32_t i = 0; i < m_treeWidth; ++i)
    {
        std::int32_t expectedIndex = _treeIndex * m_treeWidth + i + 1;
        // when expectedIndex is bigger than m_consensusNodes.size(), return
        if (expectedIndex > m_endIndex)
        {
            break;
        }
        auto selectedIndex = transTreeIndexToNodeIndex(expectedIndex, _consIndex);
        auto selectedNode = getNodeIDByIndex(selectedIndex);
        if (!selectedNode || selectedNode == m_nodeId)
        {
            continue;
        }
        // the child node exists in the peers
        if (_peers->contains(selectedNode) && selectedNode->data() != m_nodeId->data())
        {
            selectedNodeSet->insert(selectedNode);
        }
        else  // the child node doesn't exist in the peers, select the grand child recursively
        {
            if (expectedIndex < m_endIndex)
            {
                auto recursiveNodeSet =
                    recursiveSelectChildNodes(expectedIndex, _peers, _consIndex);
                selectedNodeSet->insert(recursiveNodeSet->begin(), recursiveNodeSet->end());
            }
        }
        // the last node
        if (expectedIndex == m_endIndex)
        {
            break;
        }
    }
    return selectedNodeSet;
}

/**
 * @brief : select parent node of given node from peers
 *          if the parent node doesn't exist in the peers, select the grand parent, etc.
 *
 * @params :
 *  1. _selectedNodeList: return value, the selected parent node(maybe the grand parent, etc.)
 *  2. _peers: the nodeIDs of peers maintained by syncStatus
 *  3. _nodeIndex: the index of the node that need select parent from given peers
 */
bcos::crypto::NodeIDSetPtr TreeTopology::selectParentNodes(bcos::crypto::NodeIDSetPtr const& _peers,
    std::int32_t _nodeIndex, std::int32_t _startIndex, bool _selectAll)
{
    // find the parent nodes
    std::int32_t parentIndex = (_nodeIndex - 1 + m_treeWidth - 1) / m_treeWidth;
    bcos::crypto::NodeIDSetPtr selectedNodeSet = std::make_shared<bcos::crypto::NodeIDSet>();
    // the parentNode is the node-self
    if (parentIndex == _nodeIndex)
    {
        return selectedNodeSet;
    }

    while (parentIndex >= 0)
    {
        // find the parentNode from the peers
        auto selectedIndex = transTreeIndexToNodeIndex(parentIndex, _startIndex);
        auto selectedNode = getNodeIDByIndex(selectedIndex);
        if (selectedNode && _peers->contains(selectedNode) &&
            selectedNode->data() != m_nodeId->data())
        {
            selectedNodeSet->insert(selectedNode);
            if (!_selectAll)
            {
                break;
            }
        }
        if (parentIndex <= 0)
        {
            break;
        }
        parentIndex = (parentIndex - 1) / m_treeWidth;
    }
    return selectedNodeSet;
}

std::int32_t TreeTopology::getTreeIndex(std::int32_t _consIndex)
{
    std::int32_t nodeIndex = 0;
    if (m_consIndex >= _consIndex)
    {
        nodeIndex = m_consIndex - _consIndex;
    }
    else
    {
        // when the node is added into or removed from the sealerList frequently
        // the consIndex maybe higher than m_consIndex,
        // and the distance maybe higher than m_nodeNum
        nodeIndex = (m_consIndex + m_nodeNum - _consIndex % m_nodeNum) % m_nodeNum;
    }
    return nodeIndex;
}

bcos::crypto::NodeIDSetPtr TreeTopology::selectNodes(bcos::crypto::NodeIDSetPtr const& _peers,
    std::int32_t _consIndex, bool _isTheStartNode, bcos::crypto::NodeIDPtr fromNode)
{
    Guard lock(m_mutex);
    // the first node is the  observer nodes or not belong to the group
    // send messages to the given consNode or the child of the given consNode
    if (m_consIndex < 0)
    {
        bcos::crypto::NodeIDSetPtr selectedNodeSet = std::make_shared<bcos::crypto::NodeIDSet>();
        if (!_isTheStartNode)
        {
            return selectedNodeSet;
        }

        // if node index exist && node alive in group peers && node is not self
        bcos::crypto::NodeIDPtr selectedNode = getNodeIDByIndex(_consIndex);
        if (selectedNode && _peers->contains(selectedNode) &&
            selectedNode->data() != m_nodeId->data())
        {
            selectedNodeSet->insert(selectedNode);
        }
        else
        {
            // otherwise, select grand child nodes
            return recursiveSelectChildNodes(_consIndex, _peers, _consIndex);
        }
        return selectedNodeSet;
    }
    crypto::NodeIDSetPtr nodes;
    // if the consensus nodes

    if (fromNode != nullptr && !_isTheStartNode)
    {
        auto nodeIndexInConsensus = getNodeIndexByNodeId(m_consensusNodes, fromNode);
        TREE_LOG(DEBUG) << LOG_DESC("selectNodesByOtherNodeView")
                        << LOG_KV("index", nodeIndexInConsensus)
                        << LOG_KV("fromNode", fromNode->shortHex());
        if (nodeIndexInConsensus < 0)
        {
            auto nodeIndex = getTreeIndex(_consIndex);
            nodes = recursiveSelectChildNodes(nodeIndex, _peers, _consIndex);
        }
        else
        {
            nodes = recursiveSelectChildNodes(m_consIndex, _peers, nodeIndexInConsensus);
        }
    }
    else
    {
        auto nodeIndex = getTreeIndex(_consIndex);
        nodes = recursiveSelectChildNodes(nodeIndex, _peers, _consIndex);
    }
    if (c_fileLogLevel <= TRACE)
    {
        std::stringstream nodeList;
        std::for_each((*nodes).begin(), (*nodes).end(),
            [&](const auto& item) { nodeList << item->shortHex() << ","; });
        TREE_LOG(TRACE) << LOG_DESC("selectNodes") << LOG_KV("nodeSize", nodes->size())
                        << LOG_KV("node", nodeList.str());
    }
    return nodes;
}

bcos::crypto::NodeIDSetPtr TreeTopology::selectParent(
    bcos::crypto::NodeIDSetPtr const& _peers, std::int32_t _consIndex, bool _selectAll)
{
    Guard lock(m_mutex);
    if (m_consIndex < 0)
    {
        return std::make_shared<crypto::NodeIDSet>();
    }
    // if the consensus nodes
    auto nodeIndex = getTreeIndex(_consIndex);
    return selectParentNodes(_peers, nodeIndex, _consIndex, _selectAll);
}

bcos::crypto::NodeIDSetPtr TreeTopology::selectNodesByNodeID(
    bcos::crypto::NodeIDSetPtr const& _peers, bcos::crypto::NodeIDPtr const& _nodeID,
    bool _isTheStartNode)
{
    auto consIndex = getNodeIndexByNodeId(m_consensusNodes, _nodeID);
    TREE_LOG(DEBUG) << LOG_DESC("selectNodesByNodeID") << LOG_KV("consIndex", consIndex)
                    << LOG_KV("nodeID", _nodeID->shortHex());
    return selectNodes(_peers, consIndex, _isTheStartNode);
}

bcos::crypto::NodeIDSetPtr TreeTopology::selectParentByNodeID(
    bcos::crypto::NodeIDSetPtr const& _peers, bcos::crypto::NodeIDPtr const& _nodeID)
{
    auto consIndex = getNodeIndexByNodeId(m_consensusNodes, _nodeID);
    return selectParent(_peers, consIndex, false);
}