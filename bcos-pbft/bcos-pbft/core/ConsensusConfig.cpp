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
 * @brief Base implementation of consensus Config
 * @file ConsensusConfig.cpp
 * @author: yujiechen
 * @date 2021-04-09
 */
#include "ConsensusConfig.h"

using namespace bcos;
using namespace bcos::crypto;
using namespace bcos::consensus;
// the consensus node list
// Note: copy here to ensure thread safety
// And the cost of copying the pointer is more efficient
ConsensusNodeList ConsensusConfig::consensusNodeList() const
{
    ReadGuard lock(x_consensusNodeList);
    return *m_consensusNodeList;
}

NodeIDs ConsensusConfig::consensusNodeIDList(bool _excludeSelf) const
{
    ReadGuard lock(x_consensusNodeList);
    std::vector<PublicPtr> nodeIDList;
    for (const auto& node : *m_consensusNodeList)
    {
        if (_excludeSelf && node->nodeID()->data() == nodeID()->data())
        {
            continue;
        }
        nodeIDList.push_back(node->nodeID());
    }
    return nodeIDList;
}

bool ConsensusConfig::compareConsensusNode(
    ConsensusNodeList const& _left, ConsensusNodeList const& _right)
{
    if (_left.size() != _right.size())
    {
        return false;
    }
    size_t i = 0;
    for (auto const& node : _left)
    {
        const auto& compareNode = _right[i];
        if (node->nodeID()->data() != compareNode->nodeID()->data() ||
            node->weight() != compareNode->weight())
        {
            return false;
        }
        i++;
    }
    return true;
}


bool ConsensusConfig::isNodeExist(
    ConsensusNodeInterface::Ptr const& _node, ConsensusNodeList const& _nodeList)
{
    auto iter = std::find_if(_nodeList.begin(), _nodeList.end(),
        [_node](const ConsensusNodeInterface::Ptr& _consensusNode) {
            return _node->nodeID()->data() == _consensusNode->nodeID()->data() &&
                   _node->weight() == _consensusNode->weight();
        });
    return !(_nodeList.end() == iter);
}

void ConsensusConfig::setObserverNodeList(ConsensusNodeList& _observerNodeList)
{
    std::sort(_observerNodeList.begin(), _observerNodeList.end(), ConsensusNodeComparator());
    // update the observer list
    {
        UpgradableGuard lock(x_observerNodeList);
        // consensus node list have not been changed
        if (compareConsensusNode(_observerNodeList, *m_observerNodeList))
        {
            m_observerNodeListUpdated = false;
            return;
        }
        UpgradeGuard ul(lock);
        // consensus node list have been changed
        *m_observerNodeList = _observerNodeList;
        m_observerNodeListUpdated = true;
    }
}

void ConsensusConfig::setConsensusNodeList(ConsensusNodeList& _consensusNodeList)
{
    if (_consensusNodeList.empty())
    {
        BOOST_THROW_EXCEPTION(InitConsensusException()
                              << errinfo_comment("Must contain at least one consensus node"));
    }

    std::sort(_consensusNodeList.begin(), _consensusNodeList.end(), ConsensusNodeComparator());
    // update the consensus list
    {
        UpgradableGuard lock(x_consensusNodeList);
        // consensus node list have not been changed
        if (compareConsensusNode(_consensusNodeList, *m_consensusNodeList))
        {
            m_consensusNodeListUpdated = false;
            return;
        }
        UpgradeGuard ul(lock);
        // consensus node list have been changed
        *m_consensusNodeList = _consensusNodeList;
        m_consensusNodeListUpdated = true;
    }
    {
        // update the consensusNodeNum
        ReadGuard lock(x_consensusNodeList);
        m_consensusNodeNum.store(m_consensusNodeList->size());
    }
    // update the nodeIndex
    auto nodeIndex = getNodeIndexByNodeID(m_keyPair->publicKey());
    if (nodeIndex != m_nodeIndex)
    {
        m_nodeIndex.store(nodeIndex);
    }
    // update quorum
    updateQuorum();
    CONSENSUS_LOG(INFO) << METRIC << LOG_DESC("updateConsensusNodeList")
                        << LOG_KV("nodeNum", m_consensusNodeNum) << LOG_KV("nodeIndex", nodeIndex)
                        << LOG_KV("committedIndex",
                               (committedProposal() ? committedProposal()->index() : 0))
                        << decsConsensusNodeList(_consensusNodeList);
}

IndexType ConsensusConfig::getNodeIndexByNodeID(bcos::crypto::PublicPtr _nodeID)
{
    ReadGuard lock(x_consensusNodeList);
    IndexType nodeIndex = NON_CONSENSUS_NODE;
    IndexType i = 0;
    for (const auto& _consensusNode : *m_consensusNodeList)
    {
        if (_consensusNode->nodeID()->data() == _nodeID->data())
        {
            nodeIndex = i;
            break;
        }
        i++;
    }
    return nodeIndex;
}

ConsensusNodeInterface::Ptr ConsensusConfig::getConsensusNodeByIndex(IndexType _nodeIndex)
{
    ReadGuard lock(x_consensusNodeList);
    if (_nodeIndex < m_consensusNodeList->size())
    {
        return (*m_consensusNodeList)[_nodeIndex];
    }
    return nullptr;
}