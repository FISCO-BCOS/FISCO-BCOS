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

void SyncTreeTopology::updateNodeInfo(bcos::crypto::NodeIDs const& _nodeList)
{
    Guard lock(m_mutex);
    if (bcos::crypto::KeyCompareTools::compareTwoNodeIDs(_nodeList, *m_nodeList))
    {
        return;
    }
    // update the nodeNum
    std::int32_t nodeNum = _nodeList.size();
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
    bcos::crypto::NodeIDs const& _consensusNodes, bcos::crypto::NodeIDs const& _nodeList)
{
    Guard lock(m_mutex);
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
    if (m_consensusNodes->empty()) [[unlikely]]
    {
        return;
    }
    // the current node list hasn't been set
    if (m_nodeList->empty()) [[unlikely]]
    {
        return;
    }
    // the node doesn't locate in the group
    if (!locatedInGroup()) [[unlikely]]
    {
        return;
    }
    std::int64_t consensusNodeSize = m_consensusNodes->size();
    // max node size every consensus node responses to sync the newest block
    std::int64_t slotSize = (m_nodeNum + consensusNodeSize - 1) / consensusNodeSize;
    // calculate the node index interval([m_startIndex, m_endIndex]) every consensus node responses
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
bcos::crypto::NodeIDPtr SyncTreeTopology::getNodeIDByIndex(std::int32_t _nodeIndex) const
{
    if (_nodeIndex >= m_nodeNum || _nodeIndex < 0)
    {
        SYNCTREE_LOG(DEBUG) << LOG_DESC("getNodeIDByIndex: invalidNode")
                            << LOG_KV("nodeIndex", _nodeIndex) << LOG_KV("nodeListSize", m_nodeNum);
        return nullptr;
    }
    return m_nodeList->at(_nodeIndex);
}

// select the child nodes by tree
bcos::crypto::NodeIDSetPtr SyncTreeTopology::recursiveSelectChildNodes(
    std::int32_t _parentIndex, bcos::crypto::NodeIDSetPtr const& _peers, std::int32_t _startIndex)
{
    // if the node doesn't locate in the group
    if (!locatedInGroup())
    {
        return std::make_shared<crypto::NodeIDSet>();
    }
    return TreeTopology::recursiveSelectChildNodes(_parentIndex, _peers, _startIndex);
}

// select the parent nodes by tree
bcos::crypto::NodeIDSetPtr SyncTreeTopology::selectParentNodes(
    bcos::crypto::NodeIDSetPtr const& _peers, std::int32_t _nodeIndex, std::int32_t _startIndex,
    bool)
{
    // if the node doesn't locate in the group
    if (!locatedInGroup())
    {
        return std::make_shared<crypto::NodeIDSet>();
    }
    // push all other consensus node to the selectedNodeList if this node is the consensus node
    if (m_consIndex >= 0)
    {
        auto selectedNodeSet = std::make_shared<bcos::crypto::NodeIDSet>();
        for (auto const& [idx, consNode] : *m_consensusNodes | RANGES::views::enumerate)
        {
            if (_peers->contains(consNode) && static_cast<std::uint64_t>(m_consIndex) != idx)
            {
                selectedNodeSet->insert(consNode);
            }
        }
        return selectedNodeSet;
    }
    // if observer node
    return TreeTopology::selectParentNodes(_peers, _nodeIndex, _startIndex, false);
}

bcos::crypto::NodeIDSetPtr SyncTreeTopology::selectNodesForBlockSync(
    bcos::crypto::NodeIDSetPtr const& _peers)
{
    Guard lock(m_mutex);
    auto selectedNodeSet = std::make_shared<bcos::crypto::NodeIDSet>();
    // the node doesn't locate in the group
    if (!locatedInGroup())
    {
        return selectedNodeSet;
    }
    // here will not overflow
    // the sync-tree-topology is:
    // consensusNode(0)->{0->{2,3}, 1->{4,5}}
    // however, the tree-topology is:
    // consensusNode(0)->{1->{3,4}, 2->{5,6}}
    // so every node of tree-topology should decrease 1 to get sync-tree-topology
    std::int32_t offset = m_startIndex - 1;
    std::int32_t nodeIndex = m_nodeIndex + 1 - m_startIndex;
    selectedNodeSet = selectParentNodes(_peers, nodeIndex, offset, false);

    // if the node is the consensusNode, chose the childNode
    // the node is not the consensusNode,
    auto recursiveNodeSet =
        recursiveSelectChildNodes(m_consIndex >= 0 ? 0 : nodeIndex, _peers, offset);
    selectedNodeSet->insert(recursiveNodeSet->begin(), recursiveNodeSet->end());
    if (c_fileLogLevel <= TRACE) [[unlikely]]
    {
        std::stringstream nodeList;
        for (auto const& node : *selectedNodeSet)
        {
            nodeList << node->shortHex() << ",";
        }
        SYNCTREE_LOG(TRACE) << LOG_DESC("selectNodesForBlockSync")
                            << LOG_KV("selectSize", selectedNodeSet->size())
                            << LOG_KV("nodes", nodeList.str());
    }
    return selectedNodeSet;
}
