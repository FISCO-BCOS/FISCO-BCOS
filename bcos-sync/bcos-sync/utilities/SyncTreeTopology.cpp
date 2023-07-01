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
 * @brief SyncTreeTopology implementation
 * @file SyncTreeTopology.cpp
 * @author: yujiechen
 * @date 2023-03-22
 */
#include "bcos-sync/bcos-sync/utilities/SyncTreeTopology.h"

using namespace bcos;
using namespace bcos::sync;

void SyncTreeTopology::updateNodeInfo(bcos::crypto::NodeIDs _nodeList)
{
    Guard l(m_mutex);
    if (bcos::crypto::KeyCompareTools::compareTwoNodeIDs(_nodeList, *m_nodeList))
    {
        return;
    }
    // update the nodeNum
    std::int64_t nodeNum = _nodeList.size();
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
    bcos::crypto::NodeIDs _consensusNodes, bcos::crypto::NodeIDs _nodeList)
{
    Guard l(m_mutex);
    if (bcos::crypto::KeyCompareTools::compareTwoNodeIDs(_nodeList, *m_nodeList) &&
            bcos::crypto::KeyCompareTools::compareTwoNodeIDs(_consensusNodes, *m_consensusNodes))
    {
        return;
    }
    std::int64_t nodeNum = _nodeList.size();
    if (m_nodeNum != nodeNum)
    {
        m_nodeNum = nodeNum;
    }
    *m_nodeList = _nodeList;
    *m_consensusNodes = _consensusNodes;
    m_consIndex = getNodeIndexByNodeId(m_consensusNodes, m_nodeId);
    m_nodeIndex = getNodeIndexByNodeId(m_nodeList, m_nodeId);
    updateStartAndEndIndex();
}

void SyncTreeTopology::updateStartAndEndIndex()
{
    // the currentConsensus node list hasn't been set
    if (m_consensusNodes->size() == 0)
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
    std::int64_t consensusNodeSize = m_consensusNodes->size();
    // max node size every consensus node responses to sync the newest block
    std::int64_t slotSize = (m_nodeNum + consensusNodeSize - 1) / consensusNodeSize;
    // calculate the node index interval([m_startIndex, m_endIndex]) every consensus node repsonses
    // the node is the consensus node, calculate m_startIndex
    if (m_consIndex >= 0)
    {
        m_startIndex = slotSize * m_consIndex.load();
    }
    else  // the node is the observer node, calculate m_startIndex
    {
        m_startIndex = (m_nodeIndex / slotSize) * slotSize;
    }
    if (m_startIndex > (m_nodeNum - 1))
    {
        m_startIndex = m_nodeNum - 1;
    }
    std::int64_t endIndex = m_startIndex + slotSize - 1;
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
bool SyncTreeTopology::getNodeIDByIndex(
    bcos::crypto::NodeIDPtr& _nodeID, std::int64_t const _nodeIndex) const
{
    if (_nodeIndex >= m_nodeNum || _nodeIndex < 0)
    {
        SYNCTREE_LOG(DEBUG) << LOG_DESC("getNodeIDByIndex: invalidNode")
                            << LOG_KV("nodeIndex", _nodeIndex) << LOG_KV("nodeListSize", m_nodeNum);
        return false;
    }
    _nodeID = (*m_nodeList)[_nodeIndex];
    return true;
}

// select the child nodes by tree
void SyncTreeTopology::recursiveSelectChildNodes(bcos::crypto::NodeIDListPtr _selectedNodeList,
    std::int64_t const _parentIndex, bcos::crypto::NodeIDSetPtr _peers,
    std::int64_t const _startIndex)
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
void SyncTreeTopology::selectParentNodes(bcos::crypto::NodeIDListPtr _selectedNodeList,
    bcos::crypto::NodeIDSetPtr _peers, std::int64_t const _nodeIndex,
    std::int64_t const _startIndex, bool const)
{
    // if the node doesn't locate in the group
    if (!locatedInGroup())
    {
        return;
    }
    // push all other consensus node to the selectedNodeList if this node is the consensus node
    if (m_consIndex >= 0)
    {
        for (auto const& [idx, consNode] : *m_consensusNodes | RANGES::view::enumerate)
        {
            if (_peers->contains(consNode) &&
                !bcos::crypto::KeyCompareTools::isNodeIDExist(consNode, *_selectedNodeList) &&
                static_cast<std::uint64_t>(m_consIndex) != idx)
            {
                _selectedNodeList->emplace_back(consNode);
            }
        }
        return;
    }
    return TreeTopology::selectParentNodes(_selectedNodeList, _peers, _nodeIndex, _startIndex);
}

bcos::crypto::NodeIDListPtr SyncTreeTopology::selectNodesForBlockSync(
    bcos::crypto::NodeIDSetPtr _peers)
{
    Guard l(m_mutex);
    bcos::crypto::NodeIDListPtr selectedNodeList = std::make_shared<bcos::crypto::NodeIDs>();
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
    std::int64_t offset = m_startIndex - 1;
    std::int64_t nodeIndex = m_nodeIndex + 1 - m_startIndex;
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
