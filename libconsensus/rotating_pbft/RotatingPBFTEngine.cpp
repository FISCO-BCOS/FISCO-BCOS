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
    PBFTEngine::resetConfig();
    // update the epochBlockNum
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
        m_blockChain->getSystemConfigByKey(dev::precompiled::SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM);
    int64_t Epoch = boost::lexical_cast<int64_t>(EpochStr);

    if (Epoch == m_epochSize)
    {
        return false;
    }
    RPBFTENGINE_LOG(DEBUG) << LOG_DESC("updateEpochSize") << LOG_KV("originEpoch", m_epochSize)
                           << LOG_KV("expectedEpoch", Epoch);
    auto orgEpoch = m_epochSize.load();
    setEpochSealerNum(Epoch);
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
    auto ret =
        m_blockChain->getSystemConfigInfoByKey(dev::precompiled::SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM);
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
    if (chosedConsensusNodes != *m_chosedConsensusNodes)
    {
        UpgradeGuard ul(lock);
        *m_chosedConsensusNodes = chosedConsensusNodes;
        m_chosedConsNodeChanged = true;
    }
    RPBFTENGINE_LOG(DEBUG)
        << LOG_DESC("resetChosedConsensusNodes for sealerList changed or epoch size changed")
        << LOG_KV("epochUpdated", epochUpdated) << LOG_KV("updatedSealersNum", m_sealersNum)
        << LOG_KV("updatedStartNodeIdx", m_startNodeIdx)
        << LOG_KV("chosedConsensusNodesNum", m_chosedConsensusNodes->size())
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
                               << LOG_KV("chosedConsensusNodesNum", m_chosedConsensusNodes->size())
                               << LOG_KV("sealersNum", m_sealersNum) << LOG_KV("idx", m_idx)
                               << LOG_KV("nodeId", m_keyPair.pub().abridged());
    }
    {
        WriteGuard l(x_chosedConsensusNodes);
        m_chosedConsensusNodes->erase(chosedOutNodeId);
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
        m_chosedConsensusNodes->insert(chosedInNodeId);
        chosedConsensusNodesSize = m_chosedConsensusNodes->size();
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
    if (m_chosedConsensusNodes->size() > 0)
    {
        WriteGuard l(x_chosedSealerList);
        m_chosedSealerList->resize(m_chosedConsensusNodes->size());

        std::copy(m_chosedConsensusNodes->begin(), m_chosedConsensusNodes->end(),
            m_chosedSealerList->begin());
        std::sort(m_chosedSealerList->begin(), m_chosedSealerList->end());
        // update consensus node info
        if (m_blockSync->syncTreeRouterEnabled())
        {
            dev::h512s nodeList = m_blockChain->sealerList() + m_blockChain->observerList();
            std::sort(nodeList.begin(), nodeList.end());
            m_blockSync->updateConsensusNodeInfo(*m_chosedSealerList, nodeList);
        }
        if (m_treeRouter)
        {
            m_treeRouter->updateConsensusNodeInfo(*m_chosedSealerList);
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
    if (!m_chosedConsensusNodes->count(_nodeId))
    {
        return -1;
    }
    return 0;
}

// reset m_locatedInConsensusNodes
void RotatingPBFTEngine::resetLocatedInConsensusNodes()
{
    ReadGuard l(x_chosedConsensusNodes);
    if (m_chosedConsensusNodes->count(m_keyPair.pub()))
    {
        m_locatedInConsensusNodes = true;
        return;
    }
    m_locatedInConsensusNodes = false;
}

dev::network::NodeID RotatingPBFTEngine::getSealerByIndex(size_t const& _index) const
{
    auto nodeId = PBFTEngine::getSealerByIndex(_index);
    if (nodeId != dev::network::NodeID())
    {
        ReadGuard l(x_chosedConsensusNodes);
        if (m_chosedConsensusNodes->count(nodeId))
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

// the leader forward prepareMsg to other nodes
void RotatingPBFTEngine::sendPrepareMsgFromLeader(
    PrepareReq::Ptr _prepareReq, bytesConstRef _data, dev::PACKET_TYPE const& _p2pPacketType)
{
    // the tree-topology has been disabled
    if (!m_treeRouter)
    {
        return PBFTEngine::sendPrepareMsgFromLeader(_prepareReq, _data, _p2pPacketType);
    }
    m_rpbftReqCache->insertRequestedRawPrepare(_prepareReq->block_hash);

    // send prepareReq by tree
    std::shared_ptr<dev::h512s> selectedNodes;
    {
        ReadGuard l(x_chosedConsensusNodes);
        NodeID leaderNodeID = getNodeIDByIndex(m_idx);
        if (leaderNodeID == dev::h512())
        {
            return;
        }
        selectedNodes = m_treeRouter->selectNodesByNodeID(m_chosedConsensusNodes, leaderNodeID);
    }
    auto key = _prepareReq->uniqueKey();
    for (auto const& targetNode : *selectedNodes)
    {
        // the leader doesn't connect with the targetNode
        if (!m_service->getP2PSessionByNodeId(targetNode))
        {
            continue;
        }
        if (broadcastFilter(targetNode, PrepareReqPacket, key))
        {
            continue;
        }
        auto p2pMessage = transDataToMessage(_data, PrepareReqPacket, 0);
        p2pMessage->setPacketType(_p2pPacketType);
        m_service->asyncSendMessageByNodeID(targetNode, p2pMessage, nullptr);
        broadcastMark(targetNode, PrepareReqPacket, key);
        RPBFTENGINE_LOG(DEBUG)
            << LOG_DESC("sendPrepareMsgFromLeader: The leader forward prepare message")
            << LOG_KV("prepHash", _prepareReq->block_hash.abridged())
            << LOG_KV("height", _prepareReq->height) << LOG_KV("prepView", _prepareReq->view)
            << LOG_KV("curView", m_view) << LOG_KV("consensusNumber", m_consensusBlockNumber)
            << LOG_KV("targetNode", targetNode.abridged())
            << LOG_KV("packetSize", p2pMessage->length())
            << LOG_KV("nodeId", m_keyPair.pub().abridged()) << LOG_KV("idx", nodeIdx());
    }
}

// forward the received prepare message by tree-topology
void RotatingPBFTEngine::forwardReceivedPrepareMsgByTree(
    std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _prepareMsg, PBFTMsgPacket::Ptr _pbftMsg)
{
    if (!m_treeRouter)
    {
        return;
    }
    m_threadPool->enqueue([this, _session, _prepareMsg, _pbftMsg]() {
        // get leader idx
        std::shared_ptr<PBFTMsg> decodedPrepareMsg = std::make_shared<PBFTMsg>();
        decodedPrepareMsg->decode(ref(_pbftMsg->data));
        auto leaderIdx = decodedPrepareMsg->idx;
        m_rpbftReqCache->insertRequestedRawPrepare(decodedPrepareMsg->block_hash);

        // select the nodes that should forward the prepareMsg
        std::shared_ptr<dev::h512s> selectedNodes;
        {
            ReadGuard l(x_chosedConsensusNodes);
            auto leaderNodeId = getNodeIDByIndex(leaderIdx);
            if (leaderNodeId == dev::h512())
            {
                return;
            }
            selectedNodes = m_treeRouter->selectNodesByNodeID(m_chosedConsensusNodes, leaderNodeId);
            RPBFTENGINE_LOG(DEBUG)
                << LOG_DESC("Rcv prepare message") << LOG_KV("peer", _session->nodeID().abridged())
                << LOG_KV("packetSize", _prepareMsg->length()) << LOG_KV("_leaderIdx", leaderIdx)
                << LOG_KV("selectedNodesSize", selectedNodes->size())
                << LOG_KV("prepHeight", decodedPrepareMsg->height)
                << LOG_KV("prepHash", decodedPrepareMsg->block_hash.abridged())
                << LOG_KV("prepView", decodedPrepareMsg->view) << LOG_KV("curView", m_view)
                << LOG_KV("consNumber", m_consensusBlockNumber)
                << LOG_KV("nodeId", m_keyPair.pub().abridged()) << LOG_KV("idx", nodeIdx());
        }
        // forward the message
        auto key = decodedPrepareMsg->uniqueKey();
        for (auto const& targetNode : *selectedNodes)
        {
            // the node doesn't connect with the targetNode
            if (!m_service->getP2PSessionByNodeId(targetNode))
            {
                continue;
            }
            if (broadcastFilter(targetNode, PrepareReqPacket, key) ||
                targetNode == _session->nodeID())
            {
                continue;
            }
            m_service->asyncSendMessageByNodeID(targetNode, _prepareMsg, nullptr);
            broadcastMark(targetNode, PrepareReqPacket, key);
            broadcastMark(_session->nodeID(), PrepareReqPacket, key);

            RPBFTENGINE_LOG(DEBUG)
                << LOG_DESC("forward Prepare message")
                << LOG_KV("targetNode", targetNode.abridged())
                << LOG_KV("nodeId", m_keyPair.pub().abridged()) << LOG_KV("idx", nodeIdx());
        }
    });
}

void RotatingPBFTEngine::onRecvPBFTMessage(
    NetworkException _exception, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
{
    if (!m_treeRouter)
    {
        return PBFTEngine::pushValidPBFTMsgIntoQueue(_exception, _session, _message, nullptr);
    }
    return PBFTEngine::pushValidPBFTMsgIntoQueue(
        _exception, _session, _message, [&](PBFTMsgPacket::Ptr _pbftMsg) {
            if (_pbftMsg->packet_id == PrepareReqPacket)
            {
                forwardReceivedPrepareMsgByTree(_session, _message, _pbftMsg);
            }
        });
}

bool RotatingPBFTEngine::handlePartiallyPrepare(
    std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message)
{
    if (!m_treeRouter)
    {
        return PBFTEngine::handleReceivedPartiallyPrepare(_session, _message, nullptr);
    }
    return PBFTEngine::handleReceivedPartiallyPrepare(
        _session, _message, [&](PBFTMsgPacket::Ptr _pbftMsg) {
            if (_pbftMsg->packet_id == PrepareReqPacket)
            {
                forwardReceivedPrepareMsgByTree(_session, _message, _pbftMsg);
            }
        });
}

// select node to request missed transactions when enable bip 152
dev::h512 RotatingPBFTEngine::selectNodeToRequestMissedTxs(PrepareReq::Ptr _prepareReq)
{
    if (!m_treeRouter)
    {
        return PBFTEngine::selectNodeToRequestMissedTxs(_prepareReq);
    }
    auto parentNodeList = getParentNode(_prepareReq);
    std::stringstream oss;
    oss << LOG_KV("reqHeight", _prepareReq->height)
        << LOG_KV("reqHash", _prepareReq->block_hash.abridged())
        << LOG_KV("reqIdx", _prepareReq->idx) << LOG_KV("idx", nodeIdx());
    // has no parent, request missed transactions to the leader directly
    if (!parentNodeList || parentNodeList->size() == 0)
    {
        RPBFTENGINE_LOG(DEBUG) << LOG_DESC(
                                      "selectNodeToRequestMissedTxs: request to leader for missed "
                                      "txs because of getParentNode failed")
                               << oss.str();
        return PBFTEngine::selectNodeToRequestMissedTxs(_prepareReq);
    }
    auto expectedNodeId = (*parentNodeList)[0];
    auto selectedNode = m_rpbftReqCache->selectNodeToRequestTxs(expectedNodeId, _prepareReq);
    RPBFTENGINE_LOG(DEBUG) << LOG_DESC("selectNodeToRequestMissedTxs")
                           << LOG_KV("parentNodeID", expectedNodeId.abridged())
                           << LOG_KV("selectedNode", selectedNode.abridged()) << oss.str();
    if (selectedNode == dev::h512())
    {
        RPBFTENGINE_LOG(DEBUG) << LOG_DESC(
            "selectNodeToRequestMissedTxs: request to leader for missed txs because of no required "
            "node found");
        return PBFTEngine::selectNodeToRequestMissedTxs(_prepareReq);
    }
    return selectedNode;
}

void RotatingPBFTEngine::createPBFTReqCache()
{
    m_rpbftReqCache = std::make_shared<RPBFTReqCache>();
    m_rpbftReqCache->setMaxRequestMissedTxsWaitTime(m_maxRequestMissedTxsWaitTime);
    // only broadcast rawPrepareStatus randomly when broadcast prepare by tree
    if (m_treeRouter)
    {
        m_rpbftReqCache->setRandomSendRawPrepareStatusCallback(
            boost::bind(&RotatingPBFTEngine::sendRawPrepareStatusRandomly, this, _1));
    }
    m_reqCache = m_rpbftReqCache;
    if (m_enablePrepareWithTxsHash)
    {
        m_partiallyPrepareCache = m_rpbftReqCache;
    }
}

// send the rawPrepareReq status to other nodes after addRawPrepare
void RotatingPBFTEngine::sendRawPrepareStatusRandomly(PBFTMsg::Ptr _rawPrepareReq)
{
    std::shared_ptr<bytes> rawPrepareReqData = std::make_shared<bytes>();
    _rawPrepareReq->encodeStatus(*rawPrepareReqData);

    // only chose 25% peers to broadcast RPBFTRawPrepareStatusPacket
    std::shared_ptr<P2PSessionInfos> sessions = std::make_shared<P2PSessionInfos>();
    *sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    size_t selectedSize = std::min(sessions->size(),
        (m_prepareStatusBroadcastPercent * m_chosedConsensusNodes->size() + 99) / 100);
    if (selectedSize < sessions->size())
    {
        std::srand(utcTime());
        for (size_t i = 0; i < selectedSize; i++)
        {
            size_t randomValue = (std::rand() + selectedSize * nodeIdx()) % sessions->size();
            std::swap((*sessions)[i], (*sessions)[randomValue]);
        }
    }
    auto p2pMessage = toP2PMessage(rawPrepareReqData, P2PRawPrepareStatusPacket);
    for (size_t i = 0; i < selectedSize; i++)
    {
        auto const& targetNode = (*sessions)[i].nodeID();
        m_service->asyncSendMessageByNodeID(targetNode, p2pMessage, nullptr);
        RPBFTENGINE_LOG(DEBUG) << LOG_DESC("sendRawPrepareStatusRandomly")
                               << LOG_KV("targetNode", targetNode.abridged());
    }
    RPBFTENGINE_LOG(DEBUG) << LOG_DESC("sendRawPrepareStatusRandomly")
                           << LOG_KV("peers", selectedSize)
                           << LOG_KV("rawPrepHeight", _rawPrepareReq->height)
                           << LOG_KV("rawPrepHash", _rawPrepareReq->block_hash.abridged())
                           << LOG_KV("rawPrepView", _rawPrepareReq->view)
                           << LOG_KV("rawPrepIdx", _rawPrepareReq->idx)
                           << LOG_KV("packetSize", p2pMessage->length())
                           << LOG_KV("idx", nodeIdx());
}

PBFTMsg::Ptr RotatingPBFTEngine::decodeP2PMsgIntoPBFTMsg(
    std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message)
{
    ssize_t consIndex = 0;
    bool valid = isValidReq(_message, _session, consIndex);
    if (!valid)
    {
        return nullptr;
    }
    PBFTMsg::Ptr pbftMsg = std::make_shared<PBFTMsg>();
    pbftMsg->decodeStatus(ref(*(_message->buffer())));
    return pbftMsg;
}

std::shared_ptr<dev::h512s> RotatingPBFTEngine::getParentNode(PBFTMsg::Ptr _pbftMsg)
{
    NodeID leaderNodeID = getNodeIDByIndex(_pbftMsg->idx);
    if (leaderNodeID == dev::h512())
    {
        return nullptr;
    }
    return m_treeRouter->selectParentByNodeID(m_chosedConsensusNodes, leaderNodeID);
}

// receive the rawPrepareReqStatus packet and update the cachedRawPrepareStatus
void RotatingPBFTEngine::onReceiveRawPrepareStatus(
    std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
{
    try
    {
        auto pbftMsg = decodeP2PMsgIntoPBFTMsg(_session, _message);
        if (!pbftMsg)
        {
            return;
        }
        if (!m_rpbftReqCache->checkReceivedRawPrepareStatus(
                pbftMsg, m_view, m_blockChain->number()))
        {
            return;
        }
        RPBFTENGINE_LOG(DEBUG) << LOG_DESC("checkReceivedRawPrepareStatus: valid rawPrepareStatus")
                               << LOG_KV("peer", _session->nodeID().abridged())
                               << LOG_KV("reqHeight", pbftMsg->height)
                               << LOG_KV("reqView", pbftMsg->view)
                               << LOG_KV("reqHash", pbftMsg->block_hash.abridged())
                               << LOG_KV("reqIdx", pbftMsg->idx);

        m_rpbftReqCache->updateRawPrepareStatusCache(_session->nodeID(), pbftMsg);
        // request rawPreparePacket to the selectedNode
        auto parentNodeList = getParentNode(pbftMsg);
        // the root node of the tree-topology
        if (!parentNodeList || parentNodeList->size() == 0)
        {
            return;
        }
        // the node disconnected with the parent node
        if (!connectWithParent(parentNodeList))
        {
            if (!m_rpbftReqCache->checkAndRequestRawPrepare(pbftMsg))
            {
                return;
            }
            requestRawPreparePacket(_session->nodeID(), pbftMsg);
        }
        else
        {
            // wait for at most m_maxRequestPrepareWaitTime before request the rawPrepare
            m_waitToRequestRawPrepare->enqueue([this, pbftMsg, _session, parentNodeList] {
                this->waitAndRequestRawPrepare(pbftMsg, _session->nodeID(), parentNodeList);
            });
        }
    }
    catch (std::exception const& _e)
    {
        RPBFTENGINE_LOG(WARNING) << LOG_DESC("onReceiveRawPrepareStatus exceptioned")
                                 << LOG_KV("peer", _session->nodeID().abridged())
                                 << LOG_KV("errorInfo", boost::diagnostic_information(_e));
    }
}

bool RotatingPBFTEngine::connectWithParent(std::shared_ptr<const dev::h512s> _parentNodeList)
{
    for (auto const& node : *_parentNodeList)
    {
        if (m_service->getP2PSessionByNodeId(node))
        {
            return true;
        }
    }
    return false;
}

void RotatingPBFTEngine::waitAndRequestRawPrepare(PBFTMsg::Ptr _rawPrepareStatus,
    dev::h512 const& _targetNode, std::shared_ptr<const dev::h512s> _parentNodeList)
{
    try
    {
        if (shouldRequestRawPrepare(_rawPrepareStatus, _targetNode, _parentNodeList))
        {
            // the targetNode disconnect with this node
            if (!m_service->getP2PSessionByNodeId(_targetNode))
            {
                return;
            }
            // in case of request rawPrepare to multiple nodes
            if (!m_rpbftReqCache->checkAndRequestRawPrepare(_rawPrepareStatus))
            {
                return;
            }
            requestRawPreparePacket(_targetNode, _rawPrepareStatus);
            RPBFTENGINE_LOG(DEBUG)
                << LOG_BADGE("waitAndRequestRawPrepare")
                << LOG_DESC(
                       "no prepareReq received from the connectedParent, request the rawPrepare "
                       "now")
                << LOG_KV("targetNode", _targetNode.abridged())
                << LOG_KV("hash", _rawPrepareStatus->block_hash.abridged())
                << LOG_KV("height", _rawPrepareStatus->height)
                << LOG_KV("view", _rawPrepareStatus->view) << LOG_KV("idx", _rawPrepareStatus->idx);
        }
    }
    catch (std::exception const& _e)
    {
        RPBFTENGINE_LOG(WARNING) << LOG_DESC("waitAndRequestRawPrepare exceptioned")
                                 << LOG_KV("targetNode", _targetNode.abridged())
                                 << LOG_KV("hash", _rawPrepareStatus->block_hash.abridged())
                                 << LOG_KV("errorInfo", boost::diagnostic_information(_e));
    }
}

bool RotatingPBFTEngine::shouldRequestRawPrepare(PBFTMsg::Ptr _rawPrepareStatus,
    dev::h512 const& _targetNode, std::shared_ptr<const dev::h512s> _parentNodeList)
{
    auto startTime = utcSteadyTime();
    // wait for at most (m_maxRequestPrepareWaitTime)ms before request the rawPrepare
    while (utcSteadyTime() - startTime < m_maxRequestPrepareWaitTime)
    {
        // the status is expired
        if (!m_rpbftReqCache->checkReceivedRawPrepareStatus(
                _rawPrepareStatus, m_view, m_blockChain->number()))
        {
            return false;
        }
        // disconnect with the parent sundenly
        if (!connectWithParent(_parentNodeList))
        {
            return false;
        }
        // the targetNode disconnect with this node
        if (!m_service->getP2PSessionByNodeId(_targetNode))
        {
            return false;
        }
        boost::unique_lock<boost::mutex> l(x_rawPrepareSignalled);
        m_rawPrepareSignalled.wait_for(l, boost::chrono::milliseconds(5));
    }
    return true;
}

void RotatingPBFTEngine::addRawPrepare(PrepareReq::Ptr _prepareReq)
{
    PBFTEngine::addRawPrepare(_prepareReq);
    m_rawPrepareSignalled.notify_all();
}

// request rawPreparePacket from other node when the local rawPrepareReq is empty
void RotatingPBFTEngine::requestRawPreparePacket(
    dev::h512 const& _targetNode, PBFTMsg::Ptr _requestedRawPrepareStatus)
{
    // encode and send the _requestedRawPrepareStatus to targetNode
    std::shared_ptr<bytes> rawPrepareStatusData = std::make_shared<bytes>();
    _requestedRawPrepareStatus->encodeStatus(*rawPrepareStatusData);
    auto p2pMessage = toP2PMessage(rawPrepareStatusData, RequestRawPreparePacket);
    m_service->asyncSendMessageByNodeID(_targetNode, p2pMessage, nullptr);
    RPBFTENGINE_LOG(DEBUG) << LOG_DESC("requestRawPreparePacket for disconnect with parentNodeList")
                           << LOG_KV("targetNode", _targetNode.abridged())
                           << LOG_KV("hash", _requestedRawPrepareStatus->block_hash.abridged())
                           << LOG_KV("height", _requestedRawPrepareStatus->height)
                           << LOG_KV("view", _requestedRawPrepareStatus->view)
                           << LOG_KV("consNumber", m_consensusBlockNumber)
                           << LOG_KV("curView", m_view)
                           << LOG_KV("packetSize", p2pMessage->length()) << LOG_KV("idx", m_idx);
}

// receive rawPrepare request
void RotatingPBFTEngine::onReceiveRawPrepareRequest(
    std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
{
    try
    {
        PBFTMsg::Ptr pbftMsg = decodeP2PMsgIntoPBFTMsg(_session, _message);
        if (!pbftMsg)
        {
            return;
        }
        auto key = pbftMsg->block_hash.hex();
        if (broadcastFilter(_session->nodeID(), PrepareReqPacket, key))
        {
            RPBFTENGINE_LOG(DEBUG)
                << LOG_DESC("return for the has already received the rawPrepareRequest")
                << LOG_KV("peer", _session->nodeID().abridged())
                << LOG_KV("hash", pbftMsg->block_hash.abridged());
            return;
        }
        // the node doesn't connect with the targetNode
        if (!m_service->getP2PSessionByNodeId(_session->nodeID()))
        {
            return;
        }
        std::shared_ptr<bytes> encodedRawPrepare = std::make_shared<bytes>();
        m_rpbftReqCache->responseRawPrepare(encodedRawPrepare, pbftMsg);
        // response the rawPrepare
        auto p2pMessage = transDataToMessage(ref(*encodedRawPrepare), PrepareReqPacket, 0);
        p2pMessage->setPacketType(RawPrepareResponse);
        m_service->asyncSendMessageByNodeID(_session->nodeID(), p2pMessage, nullptr);
        broadcastMark(_session->nodeID(), PrepareReqPacket, key);
        RPBFTENGINE_LOG(DEBUG) << LOG_DESC("onReceiveRawPrepareRequest and responseRawPrepare")
                               << LOG_KV("peer", _session->nodeID().abridged())
                               << LOG_KV("hash", pbftMsg->block_hash.abridged())
                               << LOG_KV("view", pbftMsg->view) << LOG_KV("height", pbftMsg->height)
                               << LOG_KV("prepIdx", pbftMsg->idx)
                               << LOG_KV("packetSize", p2pMessage->length())
                               << LOG_KV("peer", _session->nodeID().abridged())
                               << LOG_KV("idx", nodeIdx());
    }
    catch (std::exception const& _e)
    {
        RPBFTENGINE_LOG(WARNING) << LOG_DESC("onReceiveRawPrepareRequest exceptioned")
                                 << LOG_KV("peer", _session->nodeID().abridged())
                                 << LOG_KV("errorInfo", boost::diagnostic_information(_e));
    }
}


void RotatingPBFTEngine::onReceiveRawPrepareResponse(
    std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message)
{
    PBFTMsgPacket::Ptr pbftMsgPacket = m_pbftMsgFactory->createPBFTMsgPacket();
    // invalid pbftMsgPacket
    if (!decodePBFTMsgPacket(pbftMsgPacket, _message, _session))
    {
        return;
    }
    PrepareReq::Ptr prepareReq = std::make_shared<PrepareReq>();
    prepareReq->decode(ref(pbftMsgPacket->data));
    RPBFTENGINE_LOG(DEBUG) << LOG_DESC("onReceiveRawPrepareResponse")
                           << LOG_KV("peer", _session->nodeID().abridged())
                           << LOG_KV("respHash", prepareReq->block_hash.abridged())
                           << LOG_KV("respHeight", prepareReq->height)
                           << LOG_KV("respView", prepareReq->view)
                           << LOG_KV("respIdx", prepareReq->idx)
                           << LOG_KV("consNumber", m_consensusBlockNumber)
                           << LOG_KV("curView", m_view) << LOG_KV("idx", m_idx);

    Guard l(m_mutex);
    handlePrepareMsg(prepareReq, pbftMsgPacket->endpoint);
}

void RotatingPBFTEngine::handleP2PMessage(dev::p2p::NetworkException _exception,
    std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message)
{
    try
    {
        // disable broadcast prepare by tree
        if (!m_treeRouter)
        {
            return PBFTEngine::handleP2PMessage(_exception, _session, _message);
        }
        // enable broadcast prepare by tree
        switch (_message->packetType())
        {
        // status of RawPrepareReq
        case P2PRawPrepareStatusPacket:
            m_rawPrepareStatusReceiver->enqueue([this, _session, _message]() {
                this->onReceiveRawPrepareStatus(_session, _message);
            });
            break;
        // receive raw prepare request from other node
        case RequestRawPreparePacket:
            m_requestRawPrepareWorker->enqueue([this, _session, _message]() {
                this->onReceiveRawPrepareRequest(_session, _message);
            });
            break;
        // receive raw prepare response from the other node
        case RawPrepareResponse:
            m_rawPrepareResponse->enqueue([this, _exception, _session, _message]() {
                try
                {
                    this->onReceiveRawPrepareResponse(_session, _message);
                }
                catch (std::exception const& _e)
                {
                    RPBFTENGINE_LOG(WARNING)
                        << LOG_DESC("handle RawPrepareResponse exceptioned")
                        << LOG_KV("peer", _session->nodeID().abridged())
                        << LOG_KV("errorInfo", boost::diagnostic_information(_e));
                }
            });

            break;
        default:
            PBFTEngine::handleP2PMessage(_exception, _session, _message);
            break;
        }
    }
    catch (std::exception const& _e)
    {
        RPBFTENGINE_LOG(WARNING) << LOG_DESC("handleP2PMessage: invalid message")
                                 << LOG_KV("peer", _session->nodeID().abridged())
                                 << LOG_KV("errorInfo", boost::diagnostic_information(_e));
    }
}
