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

void TreeTopology::updateConsensusNodeInfo(bcos::crypto::NodeIDListPtr _consensusNodes)
{
    Guard l(m_mutex);
    if (true == compareTwoNodeIDs(*m_currentConsensusNodes, *_consensusNodes))
    {
        return;
    }
    *m_currentConsensusNodes = *_consensusNodes;
    m_consIndex = getNodeIndexByNodeId(m_currentConsensusNodes, m_nodeId);
    updateStartAndEndIndex();
}

void TreeTopology::updateStartAndEndIndex()
{
    m_startIndex = 0;
    m_endIndex = m_currentConsensusNodes->size() - 1;
    m_nodeNum = m_currentConsensusNodes->size();
    TREE_LOG(DEBUG) << LOG_DESC("updateConsensusNodeInfo") << LOG_KV("consIndex", m_consIndex)
                    << LOG_KV("endIndex", m_endIndex) << LOG_KV("nodeNum", m_nodeNum);
}

/**
 * @brief : get node index according to given nodeID and node list
 * @return:
 *  -1: the given _nodeId doesn't exist in _findSet
 *   >=0 : the index of the given node
 */
ssize_t TreeTopology::getNodeIndexByNodeId(bcos::crypto::NodeIDListPtr _findSet, bcos::crypto::NodeIDPtr  _nodeId)
{
    ssize_t nodeIndex{-1};
    for (ssize_t i = 0; i < static_cast<ssize_t>(_findSet->size()); ++i)
    {
        if (_nodeId->data() == (*_findSet)[i]->data())
        {
            nodeIndex = i;
            break;
        }
    }
    return nodeIndex;
}

/**
 * @brief : get nodeID according to nodeIndex
 * @return:
 *  false: the given node doesn't locate in the node list
 *  true:  the given node locates in the node list, and assign its node Id to _nodeID
 */
bool TreeTopology::getNodeIDByIndex(bcos::crypto::NodeIDPtr& _nodeID, ssize_t const _nodeIndex) const
{
    if (_nodeIndex >= static_cast<ssize_t>(m_currentConsensusNodes->size()))
    {
        return false;
    }
    _nodeID = (*m_currentConsensusNodes)[_nodeIndex];
    return true;
}

ssize_t TreeTopology::getSelectedNodeIndex(ssize_t const _selectedIndex, ssize_t const _offset)
{
    return (_selectedIndex + _offset) % m_nodeNum;
}

/**
 * @brief : select child nodes of given node from peers recursively
 *          if the any child node doesn't exist in the peers, select the grand child nodes, etc.
 * @params :
 *  1. _selectedNodeList: return value, the selected child nodes(maybe the grand child, etc.)
 *  2. _parentIndex: the index of the node who needs select child nodes from the given peers
 *  3. _peers: the nodeIDs of peers maintained by syncStatus
 */
void TreeTopology::recursiveSelectChildNodes(bcos::crypto::NodeIDListPtr _selectedNodeList,
                                             ssize_t const _parentIndex, bcos::crypto::NodeIDSetPtr _peers,
                                             std::int64_t const _startIndex)
{
    std::vector<bcos::bytes> peersBytes;
    extractNodeIDsBytes(*_peers, peersBytes);

    // if the node doesn't locate in the group
    bcos::crypto::NodeIDPtr selectedNode;
    for (ssize_t i = 0; i < m_treeWidth; ++i)
    {
        ssize_t expectedIndex = _parentIndex * m_treeWidth + i + 1;
        // when expectedIndex is bigger than m_currentConsensusNodes.size(), return
        if (expectedIndex > m_endIndex)
        {
            break;
        }
        auto selectedIndex = getSelectedNodeIndex(expectedIndex, _startIndex);
        if (!getNodeIDByIndex(selectedNode, selectedIndex))
        {
            continue;
        }
        // the child node exists in the peers
        if (peersBytes.end() != std::find(peersBytes.begin(), peersBytes.end(), selectedNode->data()))
        {
            _selectedNodeList->emplace_back(selectedNode);
        }
            // the child node doesn't exist in the peers, select the grand child recursively
        else
        {
            if (expectedIndex < m_endIndex)
            {
                recursiveSelectChildNodes(_selectedNodeList, expectedIndex, _peers, _startIndex);
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
void TreeTopology::selectParentNodes(bcos::crypto::NodeIDListPtr _selectedNodeList,
                                     bcos::crypto::NodeIDSetPtr _peers, std::int64_t const _nodeIndex,
                                     std::int64_t const _startIndex, bool const _selectAll)
{
    ssize_t parentIndex = (_nodeIndex - 1) / m_treeWidth;
    // the parentNode is the node-slef
    if (parentIndex == _nodeIndex)
    {
        return;
    }

    std::vector<bcos::bytes> peersBytes;
    extractNodeIDsBytes(*_peers, peersBytes);

    bcos::crypto::NodeIDPtr selectedNode;
    while (parentIndex >= 0)
    {
        // find the parentNode from the peers
        auto selectedIndex = getSelectedNodeIndex(parentIndex, _startIndex);
        if (getNodeIDByIndex(selectedNode, selectedIndex) && peersBytes.end() != std::find(peersBytes.begin(), peersBytes.end(), selectedNode->data()))
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

std::int64_t TreeTopology::getNodeIndex(std::int64_t const _consIndex)
{
    std::int64_t nodeIndex = 0;
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

bcos::crypto::NodeIDListPtr TreeTopology::selectNodes(bcos::crypto::NodeIDSetPtr _peers,
                                                      std::int64_t const _consIndex, bool const _isTheStartNode)
{
    Guard l(m_mutex);
    bcos::crypto::NodeIDListPtr selectedNodeList = std::make_shared<bcos::crypto::NodeIDs>();
    // the first node is the  observer nodes or not belong to the group
    // send messages to the given consNode or the child of the given consNode
    if (m_consIndex < 0)
    {
        if (!_isTheStartNode)
        {
            return selectedNodeList;
        }

        std::vector<bcos::bytes> peersBytes;
        extractNodeIDsBytes(*_peers, peersBytes);

        bcos::crypto::NodeIDPtr selectedNode;
        if (getNodeIDByIndex(selectedNode, _consIndex) && peersBytes.end() != std::find(peersBytes.begin(), peersBytes.end(), selectedNode->data()))
        {
            selectedNodeList->emplace_back(selectedNode);
        }
        else
        {
            recursiveSelectChildNodes(selectedNodeList, _consIndex, _peers, _consIndex);
        }
        return selectedNodeList;
    }
    else    // the consensus nodes
    {
        auto nodeIndex = getNodeIndex(_consIndex);
        recursiveSelectChildNodes(selectedNodeList, nodeIndex, _peers, _consIndex);
    }
    return selectedNodeList;
}

bcos::crypto::NodeIDListPtr TreeTopology::selectParent(
        bcos::crypto::NodeIDSetPtr _peers, std::int64_t const _consIndex, bool const _selectAll)
{
    Guard l(m_mutex);
    bcos::crypto::NodeIDListPtr selectedParentNodeList = std::make_shared<bcos::crypto::NodeIDs>();
    if (m_consIndex < 0)
    {
        return selectedParentNodeList;
    }
    else
    {
        auto nodeIndex = getNodeIndex(_consIndex);
        selectParentNodes(selectedParentNodeList, _peers, nodeIndex, _consIndex, _selectAll);
    }
    return selectedParentNodeList;
}

bcos::crypto::NodeIDListPtr TreeTopology::selectNodesByNodeID(
        bcos::crypto::NodeIDSetPtr _peers, bcos::crypto::NodeIDPtr _nodeID,
        bool const _isTheStartNode)
{
    auto consIndex = getNodeIndexByNodeId(m_currentConsensusNodes, _nodeID);
    TREE_LOG(DEBUG) << LOG_DESC("selectNodesByNodeID") << LOG_KV("consIndex", consIndex)
                    << LOG_KV("nodeID", _nodeID->shortHex());
    return selectNodes(_peers, consIndex, _isTheStartNode);
}

bcos::crypto::NodeIDListPtr TreeTopology::selectParentByNodeID(
        bcos::crypto::NodeIDSetPtr _peers, bcos::crypto::NodeIDPtr _nodeID)
{
    auto consIndex = getNodeIndexByNodeId(m_currentConsensusNodes, _nodeID);
    return selectParent(_peers, consIndex);
}