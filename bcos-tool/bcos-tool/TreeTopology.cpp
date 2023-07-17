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
bool TreeTopology::getNodeIDByIndex(bcos::crypto::NodeIDPtr& _nodeID, std::int32_t _nodeIndex) const
{
    if (_nodeIndex >= static_cast<std::int32_t>(m_consensusNodes->size()) || _nodeIndex < 0)
    {
        return false;
    }
    _nodeID = (*m_consensusNodes)[_nodeIndex];
    return true;
}

/**
 * @brief : select child nodes of given node from peers recursively
 *          if the any child node doesn't exist in the peers, select the grand child nodes, etc.
 * @params :
 *  1. _selectedNodeList: return value, the selected child nodes(maybe the grand child, etc.)
 *  2. _treeIndex: the index of the consIndex in tree
 *  3. _peers: the nodeIDs of peers maintained by syncStatus
 */
void TreeTopology::recursiveSelectChildNodes(bcos::crypto::NodeIDListPtr const& _selectedNodeList,
    std::int32_t _treeIndex, bcos::crypto::NodeIDSetPtr const& _peers, std::int32_t _consIndex)
{
    // if the node doesn't locate in the group
    bcos::crypto::NodeIDPtr selectedNode;
    for (std::uint32_t i = 0; i < m_treeWidth; ++i)
    {
        std::int32_t expectedIndex = _treeIndex * m_treeWidth + i + 1;
        // when expectedIndex is bigger than m_consensusNodes.size(), return
        if (expectedIndex > m_endIndex)
        {
            break;
        }
        auto selectedIndex = transTreeIndexToNodeIndex(expectedIndex, _consIndex);
        if (!getNodeIDByIndex(selectedNode, selectedIndex))
        {
            continue;
        }
        // the child node exists in the peers
        if (_peers->contains(selectedNode) &&
            !bcos::crypto::KeyCompareTools::isNodeIDExist(selectedNode, *_selectedNodeList) &&
            selectedNode->data() != m_nodeId->data())
        {
            _selectedNodeList->emplace_back(selectedNode);
        }
        else  // the child node doesn't exist in the peers, select the grand child recursively
        {
            if (expectedIndex < m_endIndex)
            {
                recursiveSelectChildNodes(_selectedNodeList, expectedIndex, _peers, _consIndex);
            }
        }
        // the last node
        if (expectedIndex == m_endIndex)
        {
            break;
        }
    }
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
void TreeTopology::selectParentNodes(bcos::crypto::NodeIDListPtr const& _selectedNodeList,
    bcos::crypto::NodeIDSetPtr const& _peers, std::int32_t _nodeIndex, std::int32_t _startIndex,
    bool _selectAll)
{
    // find the parent nodes
    std::int32_t parentIndex = (_nodeIndex - 1 + m_treeWidth - 1) / m_treeWidth;
    // the parentNode is the node-self
    if (parentIndex == _nodeIndex)
    {
        return;
    }

    bcos::crypto::NodeIDPtr selectedNode;
    while (parentIndex >= 0)
    {
        // find the parentNode from the peers
        auto selectedIndex = transTreeIndexToNodeIndex(parentIndex, _startIndex);
        if (getNodeIDByIndex(selectedNode, selectedIndex) && _peers->contains(selectedNode) &&
            !bcos::crypto::KeyCompareTools::isNodeIDExist(selectedNode, *_selectedNodeList) &&
            selectedNode->data() != m_nodeId->data())
        {
            _selectedNodeList->emplace_back(selectedNode);
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

bcos::crypto::NodeIDListPtr TreeTopology::selectNodes(
    bcos::crypto::NodeIDSetPtr const& _peers, std::int32_t _consIndex, bool _isTheStartNode)
{
    Guard lock(m_mutex);
    bcos::crypto::NodeIDListPtr selectedNodeList = std::make_shared<bcos::crypto::NodeIDs>();
    // the first node is the  observer nodes or not belong to the group
    // send messages to the given consNode or the child of the given consNode
    if (m_consIndex < 0)
    {
        if (!_isTheStartNode)
        {
            return selectedNodeList;
        }

        bcos::crypto::NodeIDPtr selectedNode;
        if (getNodeIDByIndex(selectedNode, _consIndex) && _peers->contains(selectedNode) &&
            !bcos::crypto::KeyCompareTools::isNodeIDExist(selectedNode, *selectedNodeList) &&
            selectedNode->data() != m_nodeId->data())
        {
            selectedNodeList->emplace_back(selectedNode);
        }
        else
        {
            recursiveSelectChildNodes(selectedNodeList, _consIndex, _peers, _consIndex);
        }
        return selectedNodeList;
    }
    else  // the consensus nodes
    {
        auto nodeIndex = getTreeIndex(_consIndex);
        recursiveSelectChildNodes(selectedNodeList, nodeIndex, _peers, _consIndex);
    }
    return selectedNodeList;
}

bcos::crypto::NodeIDListPtr TreeTopology::selectParent(
    bcos::crypto::NodeIDSetPtr const& _peers, std::int32_t _consIndex, bool _selectAll)
{
    Guard lock(m_mutex);
    bcos::crypto::NodeIDListPtr selectedParentNodeList = std::make_shared<bcos::crypto::NodeIDs>();
    if (m_consIndex < 0)
    {
        return selectedParentNodeList;
    }
    else
    {
        auto nodeIndex = getTreeIndex(_consIndex);
        selectParentNodes(selectedParentNodeList, _peers, nodeIndex, _consIndex, _selectAll);
    }
    return selectedParentNodeList;
}

bcos::crypto::NodeIDListPtr TreeTopology::selectNodesByNodeID(
    bcos::crypto::NodeIDSetPtr const& _peers, bcos::crypto::NodeIDPtr const& _nodeID,
    bool _isTheStartNode)
{
    auto consIndex = getNodeIndexByNodeId(m_consensusNodes, _nodeID);
    TREE_LOG(DEBUG) << LOG_DESC("selectNodesByNodeID") << LOG_KV("consIndex", consIndex)
                    << LOG_KV("nodeID", _nodeID->shortHex());
    return selectNodes(_peers, consIndex, _isTheStartNode);
}

bcos::crypto::NodeIDListPtr TreeTopology::selectParentByNodeID(
    bcos::crypto::NodeIDSetPtr const& _peers, bcos::crypto::NodeIDPtr const& _nodeID)
{
    auto consIndex = getNodeIndexByNodeId(m_consensusNodes, _nodeID);
    return selectParent(_peers, consIndex, false);
}