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
#include <libprecompiled/SystemConfigPrecompiled.h>

using namespace dev::eth;
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
    size_t index = (m_view + m_highestBlock.number()) % m_epochSize;
    return (IDXTYPE)((m_startNodeIdx.load() + index) % m_sealersNum);
}

bool RotatingPBFTEngine::locatedInChosedConsensensusNodes() const
{
    return m_locatedInConsensusNodes;
}

void RotatingPBFTEngine::resetConfig()
{
    ConsensusEngineBase::resetConfig();
    // update the rotatingInterval
    m_rotatingIntervalUpdated = updateRotatingInterval();
    if (m_rotatingIntervalUpdated)
    {
        m_rotatingRound =
            (m_blockChain->number() - m_rotatingIntervalEnableNumber) / m_rotatingInterval;
    }

    // if the whole consensus node list has been changed, reset the chosen consensus node list
    resetChosedConsensusNodes();
    // update fault tolerance
    m_f = std::min((m_epochSize - 1) / 3, (m_sealersNum - 1) / 3);
    // when reach the m_rotatingInterval, update the chosen consensus node list
    chooseConsensusNodes();
    // update consensusInfo when send block status by tree-topology
    updateConsensusInfo();

    resetLocatedInConsensusNodes();
}

bool RotatingPBFTEngine::updateEpochSize()
{
    // get the system-configured epoch size
    auto EpochStr =
        m_blockChain->getSystemConfigByKey(dev::precompiled::SYSTEM_KEY_RPBFT_EPOCH_SIZE);
    int64_t Epoch = boost::lexical_cast<int64_t>(EpochStr);

    if (Epoch == m_epochSize)
    {
        return false;
    }
    RPBFTENGINE_LOG(DEBUG) << LOG_DESC("updateEpochSize") << LOG_KV("originEpoch", m_epochSize)
                           << LOG_KV("expectedEpoch", Epoch);
    auto orgEpoch = m_epochSize.load();
    setEpochSize(Epoch);
    // the E has been changed
    if (orgEpoch != m_epochSize)
    {
        return true;
    }
    return false;
}

bool RotatingPBFTEngine::updateRotatingInterval()
{
    // get the system-configured rotating-interval
    auto ret = m_blockChain->getSystemConfigInfoByKey(
        dev::precompiled::SYSTEM_KEY_RPBFT_ROTATING_INTERVAL);
    int64_t rotatingInterval = boost::lexical_cast<int64_t>(ret.first);
    BlockNumber enableNumber = ret.second;
    if (rotatingInterval == m_rotatingInterval && enableNumber == m_rotatingIntervalEnableNumber)
    {
        return false;
    }
    RPBFTENGINE_LOG(DEBUG) << LOG_DESC("updateRotatingInterval")
                           << LOG_KV("originRotatingInterval", m_rotatingInterval)
                           << LOG_KV("updatedRotatingInterval", rotatingInterval)
                           << LOG_KV("enableNumber", enableNumber)
                           << LOG_KV(
                                  "rotatingIntervalEnableNumber", m_rotatingIntervalEnableNumber);
    m_rotatingInterval = rotatingInterval;
    m_rotatingIntervalEnableNumber = enableNumber;
    return true;
}

void RotatingPBFTEngine::resetChosedConsensusNodes()
{
    if (m_sealerListUpdated)
    {
        ReadGuard l(m_sealerListMutex);
        m_sealersNum = m_sealerList.size();
    }
    bool epochUpdated = updateEpochSize();
    if (!epochUpdated && !m_sealerListUpdated && !m_rotatingIntervalUpdated)
    {
        return;
    }
    assert(m_sealersNum != 0);
    // the rotatingInterval or epochSize hasn't been set
    if (m_rotatingInterval == -1 || m_epochSize == -1)
    {
        return;
    }
    // first set up
    auto blockNumber = m_blockChain->number();
    if (m_startNodeIdx == -1)
    {
        m_rotatingRound = (blockNumber - m_rotatingIntervalEnableNumber) / m_rotatingInterval;
    }
    m_startNodeIdx = m_rotatingRound % m_sealersNum;
    int64_t idx = m_startNodeIdx;
    std::set<dev::h512> chosedConsensusNodes;

    ReadGuard l(m_sealerListMutex);
    for (auto i = 0; i < m_epochSize; i++)
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
        m_chosedConsNodeChanged = true;
    }
    RPBFTENGINE_LOG(DEBUG)
        << LOG_DESC("resetChosedConsensusNodes for sealerList changed or epoch size changed")
        << LOG_KV("epochUpdated", epochUpdated) << LOG_KV("updatedSealersNum", m_sealersNum)
        << LOG_KV("updatedStartNodeIdx", m_startNodeIdx)
        << LOG_KV("chosedConsensusNodesNum", m_chosedConsensusNodes.size())
        << LOG_KV("blockNumber", blockNumber) << LOG_KV("rotatingRound", m_rotatingRound)
        << LOG_KV("idx", m_idx) << LOG_KV("nodeId", m_keyPair.pub().abridged());
}

void RotatingPBFTEngine::chooseConsensusNodes()
{
    // the rotatingInterval or E hasn't been set
    if (m_rotatingInterval == -1 || m_epochSize == -1)
    {
        return;
    }
    auto blockNumber = m_blockChain->number();
    // don't need to switch the consensus node for not reach the rotatingInterval
    int64_t consensusRound =
        blockNumber - m_rotatingIntervalEnableNumber - m_rotatingRound * m_rotatingInterval;
    if (consensusRound < m_rotatingInterval)
    {
        return;
    }
    // remove one consensus Node
    auto chosedOutIdx = m_rotatingRound % m_sealersNum;
    NodeID chosedOutNodeId = getNodeIDByIndex(chosedOutIdx);
    if (chosedOutNodeId == dev::h512())
    {
        ReadGuard l(x_chosedConsensusNodes);
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
    {
        WriteGuard l(x_chosedConsensusNodes);
        m_chosedConsensusNodes.erase(chosedOutNodeId);
    }
    m_chosedConsNodeChanged = true;
    // update the startIndex
    m_rotatingRound += 1;
    m_startNodeIdx = m_rotatingRound % m_sealersNum;
    auto epochSize = std::min(m_epochSize.load(), m_sealersNum.load());
    IDXTYPE chosedInNodeIdx = (m_startNodeIdx.load() + epochSize - 1) % m_sealersNum;
    NodeID chosedInNodeId = getNodeIDByIndex(chosedInNodeIdx);
    if (chosedOutNodeId == dev::h512())
    {
        ReadGuard l(x_chosedConsensusNodes);
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
    size_t chosedConsensusNodesSize = 0;
    {
        WriteGuard l(x_chosedConsensusNodes);
        m_chosedConsensusNodes.insert(chosedInNodeId);
        chosedConsensusNodesSize = m_chosedConsensusNodes.size();
    }

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
                          << LOG_KV("chosedConsensusNodesNum", chosedConsensusNodesSize)
                          << LOG_KV("chosedOutIdx", chosedOutIdx)
                          << LOG_KV("chosedInIdx", chosedInNodeIdx)
                          << LOG_KV("rotatingRound", m_rotatingRound)
                          << LOG_KV("sealersNum", m_sealersNum)
                          << LOG_KV("chosedOutNode", chosedOutNodeId.abridged())
                          << LOG_KV("chosedInNode", chosedInNodeId.abridged())
                          << LOG_KV("idx", m_idx) << LOG_KV("nodeId", m_keyPair.pub().abridged());
}

void RotatingPBFTEngine::updateConsensusInfo()
{
    // return directly if the chosedConsensusNode hasn't been changed
    if (!m_chosedConsNodeChanged)
    {
        return;
    }
    ReadGuard l(x_chosedConsensusNodes);
    if (m_chosedConsensusNodes.size() > 0)
    {
        WriteGuard l(x_chosedSealerList);
        m_chosedSealerList->resize(m_chosedConsensusNodes.size());

        std::copy(m_chosedConsensusNodes.begin(), m_chosedConsensusNodes.end(),
            m_chosedSealerList->begin());
        std::sort(m_chosedSealerList->begin(), m_chosedSealerList->end());
        // update consensus node info
        if (m_blockSync->syncTreeRouterEnabled())
        {
            m_blockSync->updateConsensusNodeInfo(*m_chosedSealerList);
        }
        RPBFTENGINE_LOG(DEBUG) << LOG_DESC("updateConsensusInfo")
                               << LOG_KV("chosedSealerList", m_chosedSealerList->size());
    }
    m_chosedConsNodeChanged = false;
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


// get the forwardNodes
void RotatingPBFTEngine::getForwardNodes(
    dev::h512s& _forwardNodes, dev::p2p::P2PSessionInfos const& _sessions)
{
    std::set<h512> consensusNodes;
    {
        ReadGuard l(x_chosedConsensusNodes);
        consensusNodes = m_chosedConsensusNodes;
    }
    // select the disconnected consensus nodes
    for (auto const& session : _sessions)
    {
        if (consensusNodes.count(session.nodeID()))
        {
            consensusNodes.erase(session.nodeID());
        }
    }
    consensusNodes.erase(m_keyPair.pub());
    if (consensusNodes.size() > 0)
    {
        _forwardNodes.resize(consensusNodes.size());
        std::copy(consensusNodes.begin(), consensusNodes.end(), _forwardNodes.begin());
        RPBFTENGINE_LOG(DEBUG)
            << LOG_DESC("forwardPBFTMsgByForwardNodes: get disconnected consensus nodes")
            << LOG_KV("forwardNodesSize", _forwardNodes.size())
            << LOG_KV("sessionSize", _sessions.size()) << LOG_KV("minValidNodes", minValidNodes())
            << LOG_KV("idx", nodeIdx());
    }
}

// forward PBFTMsg according to the forwardNodes
void RotatingPBFTEngine::forwardMsg(
    std::string const& _key, PBFTMsgPacket::Ptr _pbftMsgPacket, PBFTMsg const&)
{
    RPBFTMsgPacket::Ptr rpbftMsgPacket = std::dynamic_pointer_cast<RPBFTMsgPacket>(_pbftMsgPacket);
    // get the forwardNodes from the _pbftMsgPacket
    std::set<dev::h512> forwardedNodes(
        rpbftMsgPacket->forwardNodes.begin(), rpbftMsgPacket->forwardNodes.end());

    auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    // find the remaining forwardNodes
    auto remainingForwardNodes = forwardedNodes;
    // send message to the forwardNodes
    for (auto const& session : sessions)
    {
        if (remainingForwardNodes.count(session.nodeID()))
        {
            remainingForwardNodes.erase(session.nodeID());
        }
    }
    // erase the node-self from the remaining forwardNodes
    if (remainingForwardNodes.count(m_keyPair.pub()))
    {
        remainingForwardNodes.erase(m_keyPair.pub());
    }

    h512s remainingForwardNodeList(remainingForwardNodes.size());
    std::copy(remainingForwardNodes.begin(), remainingForwardNodes.end(),
        remainingForwardNodeList.begin());
    // forward the message to corresponding nodes
    for (auto const& nodeID : forwardedNodes)
    {
        sendMsg(nodeID, _pbftMsgPacket->packet_id, _key, ref(_pbftMsgPacket->data), 1,
            remainingForwardNodeList);
    }
}

void RotatingPBFTEngine::broadcastMsg(dev::h512s const& _targetNodes, bytesConstRef _data,
    unsigned const& _packetType, unsigned const& _ttl)
{
    auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    // get the forwardNodes
    dev::h512s forwardNodes;
    getForwardNodes(forwardNodes, sessions);
    return m_service->asyncMulticastMessageByNodeIDList(
        _targetNodes, transDataToMessage(_data, _packetType, _ttl, forwardNodes));
}

PBFTMsgPacket::Ptr RotatingPBFTEngine::createPBFTMsgPacket(bytesConstRef _data,
    PACKET_TYPE const& _packetType, unsigned const& _ttl, dev::h512s const& _forwardNodes)
{
    auto pbftMsgPacket = PBFTEngine::createPBFTMsgPacket(_data, _packetType, _ttl, _forwardNodes);
    if (_forwardNodes.size() > 0)
    {
        RPBFTMsgPacket::Ptr rpbftMsgPacket =
            std::dynamic_pointer_cast<RPBFTMsgPacket>(pbftMsgPacket);
        rpbftMsgPacket->setForwardNodes(_forwardNodes);
    }
    return pbftMsgPacket;
}

dev::network::NodeID RotatingPBFTEngine::getSealerByIndex(size_t const& _index) const
{
    auto nodeId = PBFTEngine::getSealerByIndex(_index);
    if (nodeId != dev::network::NodeID())
    {
        ReadGuard l(x_chosedConsensusNodes);
        if (m_chosedConsensusNodes.count(nodeId))
        {
            return nodeId;
        }
    }
    return dev::network::NodeID();
}

dev::network::NodeID RotatingPBFTEngine::getNodeIDByIndex(size_t const& _index) const
{
    return PBFTEngine::getSealerByIndex(_index);
}