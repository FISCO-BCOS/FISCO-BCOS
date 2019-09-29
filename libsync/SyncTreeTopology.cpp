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
    {
        ReadGuard l(x_nodeList);
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
    }
    {
        WriteGuard l(x_nodeList);
        m_nodeList = _nodeList;
    }
    // update the nodeIndex
    m_nodeIndex = getNodeIndexByNodeId(m_nodeList, m_nodeId, x_nodeList);
    updateStartAndEndIndex();
}

void SyncTreeTopology::updateConsensusNodeInfo(dev::h512s const& _consensusNodes)
{
    {
        ReadGuard l(x_currentConsensusNodes);
        if (m_currentConsensusNodes == _consensusNodes)
        {
            return;
        }
    }
    {
        WriteGuard l(x_currentConsensusNodes);
        m_currentConsensusNodes = _consensusNodes;
    }
    m_consIndex = getNodeIndexByNodeId(m_currentConsensusNodes, m_nodeId, x_currentConsensusNodes);
    updateStartAndEndIndex();
}

void SyncTreeTopology::updateStartAndEndIndex()
{
    {
        ReadGuard l(x_currentConsensusNodes);
        if (m_currentConsensusNodes.size() == 0)
        {
            return;
        }
    }
    {
        ReadGuard l(x_nodeList);
        if (m_nodeList.size() == 0)
        {
            return;
        }
    }
    if (!locatedInGroup())
    {
        return;
    }
    int64_t consensusNodeSize = 0;
    {
        ReadGuard l(x_currentConsensusNodes);
        consensusNodeSize = m_currentConsensusNodes.size();
    }
    int64_t slotSize = (m_nodeNum + consensusNodeSize - 1) / consensusNodeSize;
    // the consensus node
    if (m_consIndex > 0)
    {
        m_startIndex = slotSize * m_consIndex.load();
    }
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
    SYNCTREE_LOG(DEBUG) << LOG_DESC("updateStartAndEndIndex") << LOG_KV("startIndex", m_startIndex)
                        << LOG_KV("endIndex", m_endIndex) << LOG_KV("slotSize", slotSize)
                        << LOG_KV("nodeNum", m_nodeNum) << LOG_KV("consNum", consensusNodeSize);
}

ssize_t SyncTreeTopology::getNodeIndexByNodeId(
    dev::h512s const& _findSet, dev::h512& _nodeId, SharedMutex& _mutex)
{
    ssize_t nodeIndex = -1;
    ReadGuard l(_mutex);
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

bool SyncTreeTopology::getNodeIDByIndex(h512& _nodeID, ssize_t const& _nodeIndex) const
{
    if (_nodeIndex >= m_nodeNum)
    {
        SYNCTREE_LOG(DEBUG) << LOG_DESC("getNodeIDByIndex: invalidNode")
                            << LOG_KV("nodeIndex", _nodeIndex) << LOG_KV("nodeListSize", m_nodeNum);
        return false;
    }
    ReadGuard l(x_nodeList);
    _nodeID = m_nodeList[_nodeIndex];
    return true;
}

void SyncTreeTopology::recursiveSelectChildNodes(
    h512s& _selectedNodeList, ssize_t const& _parentIndex, std::set<dev::h512> const& _peers)
{
    if (!locatedInGroup())
    {
        _selectedNodeList = h512s();
        return;
    }
    dev::h512 selectedNode;
    for (ssize_t i = 0; i < m_treeWidth; i++)
    {
        ssize_t expectedIndex = (_parentIndex - m_startIndex) * m_treeWidth + i + m_startIndex;
        if (expectedIndex > m_endIndex)
        {
            break;
        }
        // the child node exists in the peers
        if (getNodeIDByIndex(selectedNode, expectedIndex) && _peers.count(selectedNode))
        {
            SYNCTREE_LOG(DEBUG) << LOG_DESC("recursiveSelectChildNodes")
                                << LOG_KV("selectedNode", selectedNode.abridged())
                                << LOG_KV("selectedIndex", expectedIndex);
            _selectedNodeList.push_back(selectedNode);
        }
        // the child node doesn't exit in the peers, select the grand child recursively
        else
        {
            recursiveSelectChildNodes(_selectedNodeList, expectedIndex + 1, _peers);
        }
    }
}

void SyncTreeTopology::selectParentNodes(
    dev::h512s& _selectedNodeList, std::set<dev::h512> const& _peers, int64_t const& _nodeIndex)
{
    if (!locatedInGroup())
    {
        _selectedNodeList = h512s();
        return;
    }
    // push all other consensus node to the selectedNodeList if this node is the consensus node
    if (m_consIndex > 0)
    {
        ReadGuard l(x_currentConsensusNodes);
        for (auto const& consNode : m_currentConsensusNodes)
        {
            if (_peers.count(consNode))
            {
                _selectedNodeList.push_back(consNode);
            }
        }
        return;
    }
    // find the parentNode if this node is not the consensus node
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
        if (getNodeIDByIndex(selectedNode, parentIndex) && _peers.count(selectedNode))
        {
            _selectedNodeList.push_back(selectedNode);
            SYNCTREE_LOG(DEBUG) << LOG_DESC("selectParentNodes")
                                << LOG_KV("parentIndex", parentIndex)
                                << LOG_KV("selectedNode", selectedNode.abridged())
                                << LOG_KV("idx", m_nodeIndex);
            break;
        }
        parentIndex = (parentIndex - m_startIndex) / m_treeWidth + m_startIndex - 1;
    }
}

bool SyncTreeTopology::locatedInGroup()
{
    return (m_consIndex != -1) || (m_nodeIndex != -1);
}
