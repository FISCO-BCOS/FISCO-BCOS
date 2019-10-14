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
 * @brief : Implementation of Rotating PBFT Engine
 * @file: RotatingPBFTEngine.cpp
 * @author: yujiechen
 * @date: 2019-09-11
 */
#include "RotatingPBFTEngine.h"
using namespace dev::consensus;
using namespace dev::p2p;
using namespace dev::network;
std::pair<bool, IDXTYPE> RotatingPBFTEngine::getLeader() const
{
    // this node doesn't located in the chosed consensus node list
    if (!locatedInChosedConsensensusNodes())
    {
        return std::make_pair(false, MAXIDX);
    }
    // invalid state
    if (m_cfgErr || m_leaderFailed || m_highestBlock.sealer() == Invalid256 || m_nodeNum == 0)
    {
        return std::make_pair(false, MAXIDX);
    }
    auto leaderIdx = VRFSelection();
    return std::make_pair(true, leaderIdx);
}

// TODO: chose leader by VRF algorithm
IDXTYPE RotatingPBFTEngine::VRFSelection() const
{
    size_t index = (m_view + m_highestBlock.number()) % m_groupSize;
    return (IDXTYPE)((m_startNodeIdx.load() + index) % m_sealersNum);
}

bool RotatingPBFTEngine::locatedInChosedConsensensusNodes() const
{
    return m_locatedInConsensusNodes;
}

void RotatingPBFTEngine::resetConfig()
{
    ConsensusEngineBase::resetConfig();
    // update fault tolerance
    m_f = (m_groupSize - 1) / 3;
    // if the whole consensus node list has been changed, reset the chosen consensus node list
    resetChosedConsensusNodes();
    // when reach the m_rotatingInterval, update the chosen consensus node list
    chooseConsensusNodes();
    // update consensusInfo when send block status by tree-topology
    if (m_blockSync->syncTreeRouterEnabled())
    {
        updateConsensusInfoForTreeRouter();
    }
    resetLocatedInConsensusNodes();
}

void RotatingPBFTEngine::resetChosedConsensusNodes()
{
    if (!m_sealerListUpdated)
    {
        return;
    }
    {
        ReadGuard l(m_sealerListMutex);
        m_sealersNum = m_sealerList.size();
    }
    assert(m_sealersNum != 0);
    // the rotatingInterval or groupSize hasn't been set
    if (m_rotatingInterval == -1 || m_groupSize == -1)
    {
        return;
    }
    // first set up
    auto blockNumber = m_blockChain->number();
    if (m_startNodeIdx == -1)
    {
        m_rotatingRound = blockNumber / m_rotatingInterval;
        m_startNodeIdx = m_rotatingRound % m_sealersNum;
    }
    int64_t idx = m_startNodeIdx;
    std::set<dev::h512> chosedConsensusNodes;

    ReadGuard l(m_sealerListMutex);
    for (auto i = 0; i < m_groupSize; i++)
    {
        chosedConsensusNodes.insert(m_sealerList[idx]);
        idx = (idx + 1) % m_sealersNum;
    }
    // update m_chosedConsensusNodes
    UpgradableGuard lock(x_chosedConsensusNodes);
    if (chosedConsensusNodes != m_chosedConsensusNodes)
    {
        UpgradeGuard ul(lock);
        m_chosedConsensusNodes = chosedConsensusNodes;
    }
    RPBFTENGINE_LOG(DEBUG) << LOG_DESC("resetChosedConsensusNodes for sealerList changed")
                           << LOG_KV("updatedSealersNum", m_sealersNum)
                           << LOG_KV("updatedStartNodeIdx", m_startNodeIdx)
                           << LOG_KV("chosedConsensusNodesNum", m_chosedConsensusNodes.size())
                           << LOG_KV("blockNumber", blockNumber)
                           << LOG_KV("rotatingRound", m_rotatingRound) << LOG_KV("idx", m_idx)
                           << LOG_KV("nodeId", m_keyPair.pub().abridged());
}

void RotatingPBFTEngine::chooseConsensusNodes()
{
    // the rotatingInterval or groupSize hasn't been set
    if (m_rotatingInterval == -1 || m_groupSize == -1)
    {
        return;
    }
    auto blockNumber = m_blockChain->number();
    // don't need to switch the consensus node for not reach the rotatingInterval
    if (blockNumber - m_rotatingRound * m_rotatingInterval < m_rotatingInterval)
    {
        return;
    }
    // remove one consensus Node
    NodeID chosedOutNodeId;
    WriteGuard l(x_chosedConsensusNodes);
    auto chosedOutIdx = m_startNodeIdx.load();
    if (!getNodeIDByIndex(chosedOutNodeId, chosedOutIdx))
    {
        RPBFTENGINE_LOG(FATAL) << LOG_DESC("chooseConsensusNodes:chosed out node is not a sealer")
                               << LOG_KV("rotatingInterval", m_rotatingInterval)
                               << LOG_KV("blockNumber", blockNumber)
                               << LOG_KV("chosedOutNodeIdx", chosedOutIdx)
                               << LOG_KV("chosedOutNode", chosedOutNodeId.abridged())
                               << LOG_KV("rotatingRound", m_rotatingRound)
                               << LOG_KV("chosedConsensusNodesNum", m_chosedConsensusNodes.size())
                               << LOG_KV("sealersNum", m_sealersNum) << LOG_KV("idx", m_idx)
                               << LOG_KV("nodeId", m_keyPair.pub().abridged());
    }
    m_chosedConsensusNodes.erase(chosedOutNodeId);
    // update the startIndex
    m_rotatingRound += 1;
    m_startNodeIdx = (m_startNodeIdx.load() + 1) % m_sealersNum;
    IDXTYPE chosedInNodeIdx = (m_startNodeIdx.load() + m_groupSize - 1) % m_sealersNum;
    NodeID chosedInNodeId;
    if (!getNodeIDByIndex(chosedInNodeId, chosedInNodeIdx))
    {
        RPBFTENGINE_LOG(FATAL) << LOG_DESC("chooseConsensusNodes: chosed out node is not a sealer")
                               << LOG_KV("chosedOutNodeIdx", chosedOutIdx)
                               << LOG_KV("rotatingInterval", m_rotatingInterval)
                               << LOG_KV("blockNumber", blockNumber)
                               << LOG_KV("chosedInNodeIdx", chosedInNodeIdx)
                               << LOG_KV("chosedInNode", chosedInNodeId.abridged())
                               << LOG_KV("rotatingRound", m_rotatingRound)
                               << LOG_KV("sealersNum", m_sealersNum) << LOG_KV("idx", m_idx)
                               << LOG_KV("nodeId", m_keyPair.pub().abridged());
    }
    m_chosedConsensusNodes.insert(chosedInNodeId);

    // noteNewTransaction to send the remaining transactions to the inserted consensus nodes
    if ((m_idx == chosedOutIdx) && (chosedOutIdx != chosedInNodeIdx))
    {
        RPBFTENGINE_LOG(DEBUG) << LOG_DESC("noteForwardRemainTxs after the node rotating out")
                               << LOG_KV("idx", m_idx) << LOG_KV("chosedOutIdx", chosedOutIdx)
                               << LOG_KV("chosedInNodeIdx", chosedInNodeIdx)
                               << LOG_KV("rotatingRound", m_rotatingRound);
        m_blockSync->noteForwardRemainTxs(chosedInNodeId);
    }

    m_leaderFailed = false;
    RPBFTENGINE_LOG(INFO) << LOG_DESC("chooseConsensusNodes") << LOG_KV("blockNumber", blockNumber)
                          << LOG_KV("chosedOutIdx", chosedOutIdx)
                          << LOG_KV("chosedInIdx", chosedInNodeIdx)
                          << LOG_KV("rotatingRound", m_rotatingRound)
                          << LOG_KV("sealersNum", m_sealersNum)
                          << LOG_KV("chosedOutNode", chosedOutNodeId.abridged())
                          << LOG_KV("chosedInNode", chosedInNodeId.abridged())
                          << LOG_KV("idx", m_idx) << LOG_KV("nodeId", m_keyPair.pub().abridged());
}

void RotatingPBFTEngine::updateConsensusInfoForTreeRouter()
{
    ReadGuard l(x_chosedConsensusNodes);
    h512s chosedConsensusNodes(m_chosedConsensusNodes.size());
    std::copy(
        m_chosedConsensusNodes.begin(), m_chosedConsensusNodes.end(), chosedConsensusNodes.end());
    std::sort(chosedConsensusNodes.begin(), chosedConsensusNodes.end());
    m_blockSync->updateConsensusNodeInfo(chosedConsensusNodes);
    RPBFTENGINE_LOG(DEBUG) << LOG_DESC("updateConsensusInfoForTreeRouter")
                           << LOG_KV("chosedConsensusNodes", chosedConsensusNodes.size());
}

ssize_t RotatingPBFTEngine::filterBroadcastTargets(dev::network::NodeID const& _nodeId)
{
    // the node should be a sealer
    if (-1 == getIndexBySealer(_nodeId))
    {
        return -1;
    }
    // the node should be located in the chosed consensus node list
    if (!m_chosedConsensusNodes.count(_nodeId))
    {
        return -1;
    }
    return 0;
}

// reset m_locatedInConsensusNodes
void RotatingPBFTEngine::resetLocatedInConsensusNodes()
{
    ReadGuard l(x_chosedConsensusNodes);
    if (m_chosedConsensusNodes.count(m_keyPair.pub()))
    {
        m_locatedInConsensusNodes = true;
        return;
    }
    m_locatedInConsensusNodes = false;
}