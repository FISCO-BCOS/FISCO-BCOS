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
    if (true == bcos::crypto::KeyCompareTools::compareTwoNodeIDs(_nodeList, *m_nodeList))
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
    if (true == bcos::crypto::KeyCompareTools::compareTwoNodeIDs(_nodeList, *m_nodeList) &&
        true == bcos::crypto::KeyCompareTools::compareTwoNodeIDs(_consensusNodes, *m_currentConsensusNodes))
    {
        return;
    }
    std::int64_t nodeNum = _nodeList.size();
    if (m_nodeNum != nodeNum)
    {
        m_nodeNum = nodeNum;
    }
    *m_nodeList = _nodeList;
    *m_currentConsensusNodes = _consensusNodes;
    /*
     * c node = 7, o node = 0
     */
    /*
     * c node = 4, o node = 7
     * c o c o c o o o c o o
     */
    m_consIndex = getNodeIndexByNodeId(m_currentConsensusNodes, m_nodeId);
    m_nodeIndex = getNodeIndexByNodeId(m_nodeList, m_nodeId);
    std::cout << __FILE__ << " " << __FUNCTION__ << " " <<  __LINE__ << " "
              << "m_nodeId:: " << m_nodeId->shortHex()  << " m_consIndex: " << m_consIndex  << " m_nodeIndex: "<< m_nodeIndex << std::endl;
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
    std::int64_t consensusNodeSize = 0;

    consensusNodeSize = m_currentConsensusNodes->size();
    // max node size every consensus node responses to sync the newest block
    /*
     * c node = 7, o node = 0
     */
    /*
     * c node = 4, o node = 7
     * (11 + 4 - 1) / 4 = (14 - 1) / 4 = 3
     */
    std::int64_t slotSize = (m_nodeNum + consensusNodeSize - 1) / consensusNodeSize;
    std::cout << __FILE__ << " " << __FUNCTION__ << " " <<  __LINE__ << " " << "slotSize: " << slotSize  << std::endl;
    // calculate the node index interval([m_startIndex, m_endIndex]) every consensus node repsonses
    // the node is the consensus node, calculate m_startIndex
    /*
     * c node = 7, o node = 0
     * 1 * 0 = 0
     * 1 * 1 = 1
     * 1 * 2 = 2
     * 1 * 3 = 3
     * 1 * 4 = 4
     * 1 * 5 = 5
     * 1 * 6 = 6
     */
    /*
     * c node = 4, o node = 7
     * c o c o c o o o c o o
     * 3 * 0 = 0
     * (1 / 3) * 3 = 0
     * 3 * 2 = 6
     * (3 / 3) * 3 = 1
     * 3 * 4 = 6
     * (5 / 3) * 3 = 3
     * (6 / 3) * 3 = 6
     * (7 / 3) * 3 = 6
     * 3 * 8 = 24, = 10
     * (9 / 3) * 3 = 9
     * (10 / 3) * 3 = 9
     */
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
    std::cout << __FILE__ << " " << __FUNCTION__ << " " <<  __LINE__ << " " << "m_startIndex: " << m_startIndex  << std::endl;
    /*
     * c node = 7, o node = 0
     * 0 + 1 - 1 = 0
     * 1 + 1 - 1 = 1
     * 2 + 1 - 1 = 2
     * 3 + 1 - 1 = 3
     * 4 + 1 - 1 = 4
     * 5 + 1 - 1 = 5
     * 6 + 1 - 1 = 6
     */
    /*
     * c node = 4, o node = 7
     * c o c o c o o o c o o
     * 3 * 0 = 0                  | 0 + 3 - 1 = 2
     * (1 / 3) * 3 = 0          | 0 + 3 - 1 = 2
     * 3 * 2 = 6                  | 6 + 3 - 1 = 8
     * (3 / 3) * 3 = 1          | 1 + 3 - 1 = 3
     * 3 * 4 = 6                  | 6 + 3 - 1 = 8
     * (5 / 3) * 3 = 3          | 3 + 3 - 1 = 5
     * (6 / 3) * 3 = 6          | 6 + 3 - 1 = 8
     * (7 / 3) * 3 = 6          | 6 + 3 - 1 = 8
     * 3 * 8 = 24, = 10      | 10 + 3 - 1 = 12, 10
     * (9 / 3) * 3 = 9          | 9 + 3 - 1 = 10
     * (10 / 3) * 3 = 9        | 9 + 3 - 1 = 10
     */
    std::int64_t endIndex = m_startIndex + slotSize - 1;
    if (endIndex > (m_nodeNum - 1))
    {
        endIndex = m_nodeNum - 1;
    }
    // start from 1, so the endIndex should be increase 1
    // 0 - 0 + 1 = 1
    // 1 - 0 + 1 = 2
    // 2 - 0 + 1 = 3
    // 3 - 3 + 1 = 4
    // 4 - 3 + 1 = 5
    // 5 - 3 + 1 = 6
    // 6 - 6 + 1 = 7
    /*
     * c node = 4, o node = 7
     * c o c o c o o o c o o
     * 3 * 0 = 0                | 0 + 3 - 1 = 2             | 2 - 0 + 1 = 3         | 0 -> 3
     * (1 / 3) * 3 = 0         | 0 + 3 - 1 = 2             | 2 - 0 + 1 = 3        | 0 -> 3
     * 3 * 2 = 6                | 6 + 3 - 1 = 8             | 8 - 6 + 1 = 3         | 6 -> 3
     * (3 / 3) * 3 = 1         | 1 + 3 - 1 = 3             | 3 - 1 + 1 = 3        | 1 -> 3
     * 3 * 4 = 6                | 6 + 3 - 1 = 8              | 8 - 6 + 1 = 3        | 6 -> 3
     * (5 / 3) * 3 = 3         | 3 + 3 - 1 = 5             | 5 - 3 + 1 = 3        | 3 -> 3
     * (6 / 3) * 3 = 6         | 6 + 3 - 1 = 8             | 8 - 6 + 1 = 3        | 6 -> 3
     * (7 / 3) * 3 = 6         | 6 + 3 - 1 = 8             | 8 - 6 + 1 = 3         | 6 -> 3
     * 3 * 8 = 24, = 10     | 10 + 3 - 1 = 12, 10   | 10 - 10 + 1 = 1     | 10 -> 1
     * (9 / 3) * 3 = 9         | 9 + 3 - 1 = 10           | 10 - 9 + 1 = 2      | 9 -> 2
     * (10 / 3) * 3 = 9       | 9 + 3 - 1 = 10           | 10 - 9 + 1 = 2      | 9 -> 2
     */
    m_endIndex = endIndex - m_startIndex + 1;
    std::cout << __FILE__ << " " << __FUNCTION__ << " " <<  __LINE__ << " " << "m_endIndex: " << m_endIndex  << std::endl;
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

bool SyncTreeTopology::locatedInGroup()
{
    return (m_consIndex != -1) || (m_nodeIndex != -1);
}

// select the child nodes by tree
void SyncTreeTopology::recursiveSelectChildNodes(bcos::crypto::NodeIDListPtr _selectedNodeList,
                                                 std::int64_t const _parentIndex, bcos::crypto::NodeIDSetPtr _peers, std::int64_t const _startIndex)
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
                                         bcos::crypto::NodeIDSetPtr _peers, std::int64_t const _nodeIndex, std::int64_t const _startIndex,
                                         bool const)
{
    // if the node doesn't locate in the group
    if (!locatedInGroup())
    {
        return;
    }
    // push all other consensus node to the selectedNodeList if this node is the consensus node
    std::cout << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << " " << "_nodeIndex: " << _nodeIndex << " _startIndex: " << _startIndex  << std::endl;
    if (m_consIndex >= 0)
    {
        for (auto const& consNode : *m_currentConsensusNodes)
        {
            if(true == _peers->contains(consNode) && false == bcos::crypto::KeyCompareTools::isNodeIDExist(consNode, *_selectedNodeList))
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
    /*
    0
    1           2
    3 4        5 6

    0
    0           1
    2 3        4 5
    */
    /*
    0
    1           2           3
    4 5 6     7 8 9

    0
    0           1           2
    3 4 5     6 7 8
    */
    // here will not overflow
    // the sync-tree-toplogy is:
    // consensusNode(0)->{0->{2,3}, 1->{4,5}}
    // however, the tree-toplogy is:
    // consensusNode(0)->{1->{3,4}, 2->{5,6}}
    // so every node of tree-toplogy should decrease 1 to get sync-tree-toplogy

    // offset = 0 - 1 = -1
    /*
     * c node = 4, o node = 7
     * c o c o c o o o c o o
     * 3 * 0 = 0                | 0 - 1 = -1
     * (1 / 3) * 3 = 0         | 0 - 1 = -1
     * 3 * 2 = 6                | 6 - 1 = 5
     * (3 / 3) * 3 = 1         | 1 - 1 = 0
     * 3 * 4 = 6                | 6 - 1 = 5
     * (5 / 3) * 3 = 3        | 3 - 1 = 2
     * (6 / 3) * 3 = 6         | 6 - 1 = 5
     * (7 / 3) * 3 = 6         | 6 - 1 = 5
     * 3 * 8 = 24, = 10      | 10 - 1 = 9
     * (9 / 3) * 3 = 9          | 9 - 1 = 8
     * (10 / 3) * 3 = 9        | 9 - 1 = 8
     */
    std::int64_t offset = m_startIndex - 1;
    std::cout << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << " " << "m_startIndex: " << m_startIndex << " offset: " << offset << std::endl;

    // nodeIndex = 0 + 1 - 0 = 1
    /*
     * c node = 4, o node = 7
     * c o c o c o o o c o o
     * 3 * 0 = 0                | 0 + 1 - 0 = 1
     * (1 / 3) * 3 = 0         | 1 + 1 - 0 = 2
     * 3 * 2 = 6                | 2 + 1 - 6 = -3
     * (3 / 3) * 3 = 1         | 3 + 1 - 1 = 3
     * 3 * 4 = 6                | 4 + 1 - 6 = -1
     * (5 / 3) * 3 = 3        | 5 + 1 - 3 = 3
     * (6 / 3) * 3 = 6         | 6 + 1 - 6 = 1
     * (7 / 3) * 3 = 6         | 7 + 1 - 6 = 2
     * 3 * 8 = 24, = 10      | 8 + 1 - 10 = -1
     * (9 / 3) * 3 = 9          | 9 + 1 - 9 = 1
     * (10 / 3) * 3 = 9        | 10 + 1 - 9 = 2
     */
    std::int64_t nodeIndex = m_nodeIndex + 1 - m_startIndex;
    std::cout << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << " " << "m_nodeIndex: " << m_nodeIndex << " nodeIndex: " << nodeIndex << std::endl;
    // find the parent nodes
    /*
     * c node = 4, o node = 7
     * c o c o c o o o c o o
     * 1 -1
     * 2 -1
     * -3 5
     * 3 0
     * -1 5
     * 3 2
     * 1 5
     * 2 5
     * -1 9
     * 1 8
     * 2 8
     */
    selectParentNodes(selectedNodeList, _peers, nodeIndex, offset);
    std::cout << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << " " << "_selectedNodeList: " << std::endl;
    for(auto const& node : *selectedNodeList)
    {
        std::cout << node->shortHex() << std::endl;
    }

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

    std::cout << "selectNodesForBlockSync: " << std::endl;
    for(auto snode : *selectedNodeList)
    {
        for(auto const& [idx, node] : *m_nodeList | RANGES::view::enumerate)
        {
            if(snode->data() == node->data())
            {
                std::cout << idx << " ";
                break;
            }
        }
    }
    std::cout << std::endl;

    return selectedNodeList;
}
