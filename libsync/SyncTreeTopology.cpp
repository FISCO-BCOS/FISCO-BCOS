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

    if (_nodeList == *m_nodeList)
    {
        return;
    }
    // update the nodeNum
    int64_t nodeNum = _nodeList.size();
    if (m_nodeNum != nodeNum)
    {
        m_nodeNum = nodeNum;
    }
    *m_nodeList = _nodeList;
    // update the nodeIndex
    m_nodeIndex = getNodeIndexByNodeId(m_nodeList, m_nodeId);
    updateStartAndEndIndex();
}

void SyncTreeTopology::updateAllNodeInfo(
    dev::h512s const& _consensusNodes, dev::h512s const& _nodeList)
{
    Guard l(m_mutex);
    if (_nodeList == *m_nodeList && _consensusNodes == *m_currentConsensusNodes)
    {
        return;
    }
    int64_t nodeNum = _nodeList.size();
    if (m_nodeNum != nodeNum)
    {
        m_nodeNum = nodeNum;
    }
    *m_nodeList = _nodeList;
    *m_currentConsensusNodes = _consensusNodes;
    m_consIndex = getNodeIndexByNodeId(m_currentConsensusNodes, m_nodeId);
    m_nodeIndex = getNodeIndexByNodeId(m_nodeList, m_nodeId);
    updateStartAndEndIndex();
}

void SyncTreeTopology::updateStartAndEndIndex()
{
    // the currentConsensus node list hasn't been set
    if (m_currentConsensusNodes->size() == 0)
    {
        return;
    }
    // the current node list hasn't been set
    if (m_nodeList->size() == 0)
    {
        return;
    }
    // the node doesn't locate in the group
    if (!locatedInGroup())
    {
        return;
    }
    int64_t consensusNodeSize = 0;

    consensusNodeSize = m_currentConsensusNodes->size();
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
    if (m_startIndex > (m_nodeNum - 1))
    {
        m_startIndex = m_nodeNum - 1;
    }
    int64_t endIndex = m_startIndex + slotSize - 1;
    if (endIndex > (m_nodeNum - 1))
    {
        endIndex = m_nodeNum - 1;
    }
    // start from 1, so the endIndex should be increase 1
    m_endIndex = endIndex - m_startIndex + 1;
    SYNCTREE_LOG(DEBUG) << LOG_DESC("updateStartAndEndIndex") << LOG_KV("startIndex", m_startIndex)
                        << LOG_KV("endIndex", m_endIndex) << LOG_KV("slotSize", slotSize)
                        << LOG_KV("nodeNum", m_nodeNum) << LOG_KV("consNum", consensusNodeSize);
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
        SYNCTREE_LOG(DEBUG) << LOG_DESC("getNodeIDByIndex: invalidNode")
                            << LOG_KV("nodeIndex", _nodeIndex) << LOG_KV("nodeListSize", m_nodeNum);
        return false;
    }
    _nodeID = (*m_nodeList)[_nodeIndex];
    return true;
}

bool SyncTreeTopology::locatedInGroup()
{
    return (m_consIndex != -1) || (m_nodeIndex != -1);
}

// select the child nodes by tree
void SyncTreeTopology::recursiveSelectChildNodes(std::shared_ptr<dev::h512s> _selectedNodeList,
    ssize_t const& _parentIndex, std::shared_ptr<std::set<dev::h512>> _peers,
    int64_t const& _startIndex)
{
    // if the node doesn't locate in the group
    if (!locatedInGroup())
    {
        return;
    }
    return TreeTopology::recursiveSelectChildNodes(
        _selectedNodeList, _parentIndex, _peers, _startIndex);
}

// select the parent nodes by tree
void SyncTreeTopology::selectParentNodes(std::shared_ptr<dev::h512s> _selectedNodeList,
    std::shared_ptr<std::set<dev::h512>> _peers, int64_t const& _nodeIndex,
    int64_t const& _startIndex)
{
    // if the node doesn't locate in the group
    if (!locatedInGroup())
    {
        return;
    }
    // push all other consensus node to the selectedNodeList if this node is the consensus node
    // return TreeTopology::selectParentNodes(_selectedNodeList, _peers, _nodeIndex);
    if (m_consIndex >= 0)
    {
        for (auto const& consNode : *m_currentConsensusNodes)
        {
            if (_peers->count(consNode))
            {
                _selectedNodeList->push_back(consNode);
            }
        }
        return;
    }
    return TreeTopology::selectParentNodes(_selectedNodeList, _peers, _nodeIndex, _startIndex);
}

std::shared_ptr<dev::h512s> SyncTreeTopology::selectNodesForBlockSync(
    std::shared_ptr<std::set<dev::h512>> _peers)
{
    Guard l(m_mutex);
    std::shared_ptr<dev::h512s> selectedNodeList = std::make_shared<dev::h512s>();
    // the node doesn't locate in the group
    if (!locatedInGroup())
    {
        return selectedNodeList;
    }
    // here will not overflow
    // the sync-tree-toplogy is:
    // consensusNode(0)->{0->{2,3}, 1->{4,5}}
    // however, the tree-toplogy is:
    // consensusNode(0)->{1->{3,4}, 2->{5,6}}
    // so every node of tree-toplogy should decrease 1 to get sync-tree-toplogy
    int64_t offset = m_startIndex - 1;

    int64_t nodeIndex = m_nodeIndex + 1 - m_startIndex;
    // find the parent nodes
    selectParentNodes(selectedNodeList, _peers, nodeIndex, offset);

    // the node is the consensusNode, chose the childNode
    if (m_consIndex >= 0)
    {
        recursiveSelectChildNodes(selectedNodeList, 0, _peers, offset);
    }
    // the node is not the consensusNode
    else
    {
        recursiveSelectChildNodes(selectedNodeList, nodeIndex, _peers, offset);
    }

    return selectedNodeList;
}
