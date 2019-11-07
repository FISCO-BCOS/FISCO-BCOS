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
 * @file: SyncTreeTopology.cpp
 * @author: yujiechen
 * @date: 2019-09-19
 */
#include "SyncTreeTopology.h"

using namespace dev;
using namespace dev::sync;

void SyncTreeTopology::updateNodeListInfo(dev::h512s const& _nodeList)
{
    Guard l(m_mutex);

    if (_nodeList == m_nodeList)
    {
        return;
    }
    // update the nodeNum
    int64_t nodeNum = _nodeList.size();
    if (m_nodeNum != nodeNum)
    {
        m_nodeNum = nodeNum;
    }
    m_nodeList = _nodeList;
    // update the nodeIndex
    m_nodeIndex = getNodeIndexByNodeId(m_nodeList, m_nodeId);
    updateStartAndEndIndex();
}

void SyncTreeTopology::updateConsensusNodeInfo(dev::h512s const& _consensusNodes)
{
    Guard l(m_mutex);

    if (m_currentConsensusNodes == _consensusNodes)
    {
        return;
    }
    m_currentConsensusNodes = _consensusNodes;

    m_consIndex = getNodeIndexByNodeId(m_currentConsensusNodes, m_nodeId);
    updateStartAndEndIndex();
}

void SyncTreeTopology::updateStartAndEndIndex()
{
    // the currentConsensus node list hasn't been set
    if (m_currentConsensusNodes.size() == 0)
    {
        return;
    }
    // the current node list hasn't been set
    if (m_nodeList.size() == 0)
    {
        return;
    }
    // the node doesn't locate in the group
    if (!locatedInGroup())
    {
        return;
    }
    int64_t consensusNodeSize = 0;

    consensusNodeSize = m_currentConsensusNodes.size();
    // max node size every consensus node responses to sync the newest block
    int64_t slotSize = (m_nodeNum + consensusNodeSize - 1) / consensusNodeSize;
    // calculate the node index interval([m_startIndex, m_endIndex]) every consensus node repsonses
    // the node is the consensus node, calculate m_startIndex
    if (m_consIndex >= 0)
    {
        m_startIndex = slotSize * m_consIndex.load();
    }
    // the node is the observer node, calculate m_startIndex
    else
    {
        m_startIndex = (m_nodeIndex / slotSize) * slotSize;
    }
    int64_t endIndex = m_startIndex + slotSize - 1;
    if (endIndex > (m_nodeNum - 1))
    {
        endIndex = m_nodeNum - 1;
    }
    m_endIndex = endIndex;
#if 0
    SYNCTREE_LOG(DEBUG) << LOG_DESC("updateStartAndEndIndex") << LOG_KV("startIndex", m_startIndex)
                        << LOG_KV("endIndex", m_endIndex) << LOG_KV("slotSize", slotSize)
                        << LOG_KV("nodeNum", m_nodeNum) << LOG_KV("consNum", consensusNodeSize);
#endif
}

/**
 * @brief : get node index according to given nodeID and node list
 * @return:
 *  -1: the given _nodeId doesn't exist in _findSet
 *   >=0 : the index of the given node
 */
ssize_t SyncTreeTopology::getNodeIndexByNodeId(dev::h512s const& _findSet, dev::h512& _nodeId)
{
    ssize_t nodeIndex = -1;
    for (ssize_t i = 0; i < (ssize_t)_findSet.size(); i++)
    {
        if (_nodeId == _findSet[i])
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
bool SyncTreeTopology::getNodeIDByIndex(h512& _nodeID, ssize_t const& _nodeIndex) const
{
    if (_nodeIndex >= m_nodeNum)
    {
#if 0
        SYNCTREE_LOG(DEBUG) << LOG_DESC("getNodeIDByIndex: invalidNode")
                            << LOG_KV("nodeIndex", _nodeIndex) << LOG_KV("nodeListSize", m_nodeNum);
#endif
        return false;
    }
    _nodeID = m_nodeList[_nodeIndex];
    return true;
}

/**
 * @brief : select child nodes of given node from peers recursively
 *          if the any child node doesn't exist in the peers, select the grand child nodes, etc.
 * @params :
 *  1. _selectedNodeList: return value, the selected child nodes(maybe the grand child, etc.)
 *  2. _parentIndex: the index of the node who needs select child nodes from the given peers
 *  3. _peers: the nodeIDs of peers maintained by syncStatus
 */
void SyncTreeTopology::recursiveSelectChildNodes(std::shared_ptr<h512s> _selectedNodeList,
    ssize_t const& _parentIndex, std::shared_ptr<std::set<dev::h512>> _peers)
{
    // if the node doesn't locate in the group
    // (both m_consIndex and m_nodeIndex are -1), return empty node list
    if (!locatedInGroup())
    {
        return;
    }
    dev::h512 selectedNode;
    for (ssize_t i = 0; i < m_treeWidth; i++)
    {
        // the index of the child node:
        // {_parentIndex - m_startIndex} is the ID of the parent node in the {m_startIndex}th tree
        // {(_parentIndex - m_startIndex) * m_treeWidth + i} is the ID of the child node in the
        // {m_startIndex}th tree, expectedIndex is the node index of the child node
        ssize_t expectedIndex = (_parentIndex - m_startIndex) * m_treeWidth + i + m_startIndex;
        // when expectedIndex is bigger than m_endIndex, return
        if (expectedIndex > m_endIndex)
        {
            break;
        }
        // the child node exists in the peers
        if (getNodeIDByIndex(selectedNode, expectedIndex) && _peers->count(selectedNode))
        {
#if 0
            SYNCTREE_LOG(DEBUG) << LOG_DESC("recursiveSelectChildNodes")
                                << LOG_KV("selectedNode", selectedNode.abridged())
                                << LOG_KV("selectedIndex", expectedIndex);
#endif
            _selectedNodeList->push_back(selectedNode);
        }
        // the child node doesn't exit in the peers, select the grand child recursively
        else
        {
            recursiveSelectChildNodes(_selectedNodeList, expectedIndex + 1, _peers);
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
void SyncTreeTopology::selectParentNodes(std::shared_ptr<dev::h512s> _selectedNodeList,
    std::shared_ptr<std::set<dev::h512>> _peers, int64_t const& _nodeIndex)
{
    if (!locatedInGroup())
    {
        return;
    }
    // push all other consensus node to the selectedNodeList if this node is the consensus node
    if (m_consIndex >= 0)
    {
        for (auto const& consNode : m_currentConsensusNodes)
        {
            if (_peers->count(consNode))
            {
                _selectedNodeList->push_back(consNode);
            }
        }
        return;
    }
    // find the parentNode if this node is not the consensus node:
    // {_nodeIndex - m_startIndex} is the ID of the given node in the {m_startIndex}th tree
    // {(_nodeIndex - m_startIndex) / m_treeWidth} is the parent ID of the given node in the
    // {m_startIndex}th tree, parentIndex is the nodeIndex of the parentNode
    ssize_t parentIndex = (_nodeIndex - m_startIndex) / m_treeWidth + m_startIndex - 1;
    // the parentNode is the node-slef
    if (parentIndex == _nodeIndex)
    {
        return;
    }
    dev::h512 selectedNode;
    while (parentIndex >= m_startIndex)
    {
        // find the parentNode from the peers
        if (getNodeIDByIndex(selectedNode, parentIndex) && _peers->count(selectedNode))
        {
            _selectedNodeList->push_back(selectedNode);
#if 0
            SYNCTREE_LOG(DEBUG) << LOG_DESC("selectParentNodes")
                                << LOG_KV("parentIndex", parentIndex)
                                << LOG_KV("selectedNode", selectedNode.abridged())
                                << LOG_KV("idx", m_nodeIndex);
#endif
            break;
        }
        parentIndex = (parentIndex - m_startIndex) / m_treeWidth + m_startIndex - 1;
    }
}

bool SyncTreeTopology::locatedInGroup()
{
    return (m_consIndex != -1) || (m_nodeIndex != -1);
}

std::shared_ptr<dev::h512s> SyncTreeTopology::selectNodes(
    std::shared_ptr<std::set<dev::h512>> _peers)
{
    Guard l(m_mutex);
    std::shared_ptr<dev::h512s> selectedNodeList = std::make_shared<dev::h512s>();
    // the node doesn't locate in the group
    if (!locatedInGroup())
    {
        return selectedNodeList;
    }


    // the node is the consensusNode, chose the childNode
    if (m_consIndex >= 0)
    {
        recursiveSelectChildNodes(selectedNodeList, m_startIndex, _peers);
    }
    // the node is not the consensusNode
    else
    {
        recursiveSelectChildNodes(selectedNodeList, m_nodeIndex + 1, _peers);
    }
    // find the parent nodes
    selectParentNodes(selectedNodeList, _peers, m_nodeIndex);
    return selectedNodeList;
}
