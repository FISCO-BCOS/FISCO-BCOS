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
 * @file: TreeTopology.cpp
 * @author: yujiechen
 * @date: 2019-09-19
 */
#include "TreeTopology.h"

using namespace dev;
using namespace dev::sync;
void TreeTopology::updateConsensusNodeInfo(dev::h512s const& _consensusNodes)
{
    Guard l(m_mutex);
    if (*m_currentConsensusNodes == _consensusNodes)
    {
        return;
    }
    *m_currentConsensusNodes = _consensusNodes;
    m_consIndex = getNodeIndexByNodeId(m_currentConsensusNodes, m_nodeId);
    updateStartAndEndIndex();
}

void TreeTopology::updateStartAndEndIndex()
{
    m_startIndex = 0;
    m_endIndex = (ssize_t)(m_currentConsensusNodes->size() - 1);
    m_nodeNum = m_currentConsensusNodes->size();
}

/**
 * @brief : get node index according to given nodeID and node list
 * @return:
 *  -1: the given _nodeId doesn't exist in _findSet
 *   >=0 : the index of the given node
 */
ssize_t TreeTopology::getNodeIndexByNodeId(std::shared_ptr<dev::h512s> _findSet, dev::h512& _nodeId)
{
    ssize_t nodeIndex = -1;
    for (ssize_t i = 0; i < (ssize_t)_findSet->size(); i++)
    {
        if (_nodeId == (*_findSet)[i])
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
bool TreeTopology::getNodeIDByIndex(h512& _nodeID, ssize_t const& _nodeIndex) const
{
    if (_nodeIndex >= (ssize_t)m_currentConsensusNodes->size())
    {
        return false;
    }
    _nodeID = (*m_currentConsensusNodes)[_nodeIndex];
    return true;
}

ssize_t TreeTopology::getSelectedNodeIndex(ssize_t const& _selectedIndex, ssize_t const& _offset)
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
void TreeTopology::recursiveSelectChildNodes(std::shared_ptr<h512s> _selectedNodeList,
    ssize_t const& _parentIndex, std::shared_ptr<std::set<dev::h512>> _peers,
    int64_t const& _startIndex)
{
    // if the node doesn't locate in the group
    dev::h512 selectedNode;
    for (ssize_t i = 0; i < m_treeWidth; i++)
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
        if (_peers->count(selectedNode))
        {
            _selectedNodeList->push_back(selectedNode);
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
void TreeTopology::selectParentNodes(std::shared_ptr<dev::h512s> _selectedNodeList,
    std::shared_ptr<std::set<dev::h512>> _peers, int64_t const& _nodeIndex,
    int64_t const& _startIndex)
{
    ssize_t parentIndex = (_nodeIndex - 1) / m_treeWidth;
    // the parentNode is the node-slef
    if (parentIndex == _nodeIndex)
    {
        return;
    }
    dev::h512 selectedNode;
    while (parentIndex > 0)
    {
        // find the parentNode from the peers
        auto selectedIndex = getSelectedNodeIndex(parentIndex, _startIndex);
        if (getNodeIDByIndex(selectedNode, selectedIndex) && _peers->count(selectedNode))
        {
            _selectedNodeList->push_back(selectedNode);
            break;
        }
        if (parentIndex <= 0)
        {
            break;
        }
        parentIndex = (parentIndex - 1) / m_treeWidth;
    }
}

std::shared_ptr<dev::h512s> TreeTopology::selectNodes(
    std::shared_ptr<std::set<dev::h512>> _peers, int64_t const& _consIndex)
{
    Guard l(m_mutex);
    std::shared_ptr<dev::h512s> selectedNodeList = std::make_shared<dev::h512s>();
    int64_t nodeIndex = 0;
    // the observer nodes
    if (m_consIndex < 0)
    {
        dev::h512 selectedNode;
        if (getNodeIDByIndex(selectedNode, _consIndex) && _peers->count(selectedNode))
        {
            selectedNodeList->push_back(selectedNode);
        }
        else
        {
            recursiveSelectChildNodes(selectedNodeList, _consIndex, _peers, _consIndex);
        }
        return selectedNodeList;
    }
    // the consensus nodes
    else
    {
        if (m_consIndex >= _consIndex)
        {
            nodeIndex = m_consIndex - _consIndex;
        }
        else
        {
            nodeIndex = (m_consIndex + m_nodeNum - _consIndex) % m_nodeNum;
        }
    }
    recursiveSelectChildNodes(selectedNodeList, nodeIndex, _peers, _consIndex);

    // find the parent nodes
    selectParentNodes(selectedNodeList, _peers, nodeIndex, _consIndex);
    return selectedNodeList;
}