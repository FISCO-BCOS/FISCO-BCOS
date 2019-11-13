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
#include <libethcore/PartiallyBlock.h>
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
    NodeID chosedOutNodeId = getNodeIDViaIndex(chosedOutIdx);
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
    NodeID chosedInNodeId = getNodeIDViaIndex(chosedInNodeIdx);
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
    forwardMsg(_key, _pbftMsgPacket, ref(_pbftMsgPacket->data));
}

void RotatingPBFTEngine::forwardMsg(
    std::string const& _key, PBFTMsgPacket::Ptr _pbftMsgPacket, bytesConstRef _data)
{
    RPBFTMsgPacket::Ptr rpbftMsgPacket = std::dynamic_pointer_cast<RPBFTMsgPacket>(_pbftMsgPacket);
    if (rpbftMsgPacket->forwardNodes.size() == 0)
    {
        return;
    }

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
        sendMsg(nodeID, _pbftMsgPacket->packet_id, _key, _data, 1, remainingForwardNodeList);
    }
}

void RotatingPBFTEngine::broadcastMsgToTargetNodes(dev::h512s const& _targetNodes,
    bytesConstRef _data, unsigned const& _packetType, unsigned const& _ttl,
    PACKET_TYPE const& _p2pPacketType)
{
    auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    // get the forwardNodes
    dev::h512s forwardNodes;
    getForwardNodes(forwardNodes, sessions);
    auto p2pMsg = transDataToMessage(_data, _packetType, _ttl, forwardNodes);
    p2pMsg->setPacketType(_p2pPacketType);
    m_service->asyncMulticastMessageByNodeIDList(_targetNodes, p2pMsg);
    if (m_statisticHandler)
    {
        m_statisticHandler->updateConsOutPacketsInfo(
            _packetType, _targetNodes.size(), p2pMsg->length());
    }
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

dev::network::NodeID RotatingPBFTEngine::getNodeIDViaIndex(size_t const& _index) const
{
    return PBFTEngine::getSealerByIndex(_index);
}

void RotatingPBFTEngine::registerP2PHandler()
{
    m_service->registerHandlerByProtoclID(
        m_protocolId, boost::bind(&RotatingPBFTEngine::onRecvPBFTMessage, this, _1, _2, _3));
}

void RotatingPBFTEngine::onRecvPBFTMessage(dev::p2p::NetworkException _exception,
    std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message)
{
    if (nodeIdx() == MAXIDX)
    {
        RPBFTENGINE_LOG(TRACE) << LOG_DESC(
            "onRecvPBFTMessage: I'm an observer or not in the group, drop the PBFT message packets "
            "directly");
        return;
    }
    handleP2PMessage(_exception, _session, _message);
}

void RotatingPBFTEngine::handleP2PMessage(dev::p2p::NetworkException _exception,
    std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message)
{
    if (!m_enablePrepareWithTxsHash)
    {
        PBFTEngine::onRecvPBFTMessage(_exception, _session, _message);
        return;
    }
    try
    {
        // update the network-in statistic information
        if (m_statisticHandler && (_message->packetType() != 0))
        {
            m_statisticHandler->updateConsInPacketsInfo(_message->packetType(), _message->length());
        }
        switch (_message->packetType())
        {
        case PartiallyPreparePacket:
            m_prepareWorker->enqueue(
                [this, _session, _message]() { handlePartiallyPrepare(_session, _message); });
            break;
        // receive getMissedPacket request, response missed transactions
        case GetMissedTxsPacket:
            m_messageHandler->enqueue(
                [this, _session, _message]() { onReceiveGetMissedTxsRequest(_session, _message); });
            break;
        // receive missed transactions, fill block
        case MissedTxsPacket:
            m_messageHandler->enqueue(
                [this, _session, _message]() { onReceiveMissedTxsResponse(_session, _message); });
            break;
        default:
            PBFTEngine::onRecvPBFTMessage(_exception, _session, _message);
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

// handle Partially prepare
bool RotatingPBFTEngine::handlePartiallyPrepare(
    std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message)
{
    // decode the _message into prepareReq
    PBFTMsgPacket::Ptr pbftMsg = m_pbftMsgFactory->createPBFTMsgPacket();
    if (!decodePBFTMsgPacket(pbftMsg, _message, _session))
    {
        return false;
    }
    PrepareReq::Ptr prepareReq = std::make_shared<PrepareReq>();
    if (!decodeToRequests(*prepareReq, ref(pbftMsg->data)))
    {
        return false;
    }
    Guard l(m_mutex);
    bool succ = handlePartiallyPrepare(prepareReq);
    // maybe return succ for addFuturePrepare
    if (!prepareReq->pBlock)
    {
        return false;
    }
    if (needForwardMsg(succ, prepareReq->uniqueKey(), pbftMsg, *prepareReq))
    {
        clearInvalidCachedForwardMsg();
        m_cachedForwardMsg->insert(
            std::make_pair(prepareReq->block_hash, std::make_pair(prepareReq->height, pbftMsg)));
    }
    // all hit ?
    if (prepareReq->pBlock->txsAllHit())
    {
        forwardPrepareMsg(pbftMsg, prepareReq);
    }
    return succ;
}

void RotatingPBFTEngine::clearInvalidCachedForwardMsg()
{
    for (auto it = m_cachedForwardMsg->begin(); it != m_cachedForwardMsg->end();)
    {
        if (it->second.first < m_highestBlock.number() &&
            m_highestBlock.number() - it->second.first >= 10)
        {
            it = m_cachedForwardMsg->erase(it);
        }
        else
        {
            it++;
        }
    }
}

void RotatingPBFTEngine::forwardPrepareMsg(
    PBFTMsgPacket::Ptr _pbftMsgPacket, PrepareReq::Ptr _prepareReq)
{
    // forward the message
    std::shared_ptr<dev::bytes> encodedBytes = std::make_shared<dev::bytes>();
    _prepareReq->encode(*encodedBytes);
    forwardMsg(_prepareReq->uniqueKey(), _pbftMsgPacket, ref(*encodedBytes));
}

bool RotatingPBFTEngine::handlePartiallyPrepare(PrepareReq::Ptr _prepareReq)
{
    std::ostringstream oss;
    oss << LOG_DESC("handlePartiallyPrepare") << LOG_KV("reqIdx", _prepareReq->idx)
        << LOG_KV("view", _prepareReq->view) << LOG_KV("reqNum", _prepareReq->height)
        << LOG_KV("curNum", m_highestBlock.number()) << LOG_KV("consNum", m_consensusBlockNumber)
        << LOG_KV("hash", _prepareReq->block_hash.abridged()) << LOG_KV("nodeIdx", nodeIdx())
        << LOG_KV("myNode", m_keyPair.pub().abridged())
        << LOG_KV("curChangeCycle", m_timeManager.m_changeCycle);
    RPBFTENGINE_LOG(DEBUG) << oss.str();
    // check the PartiallyPrepare
    auto ret = isValidPrepare(*_prepareReq, oss);
    if (ret == CheckResult::INVALID)
    {
        return false;
    }
    /// update the view for given idx
    updateViewMap(_prepareReq->idx, _prepareReq->view);

    if (ret == CheckResult::FUTURE)
    {
        m_rpbftReqCache->addPartiallyFuturePrepare(_prepareReq);
        return true;
    }
    _prepareReq->pBlock = m_blockFactory->createBlock();
    if (!m_rpbftReqCache->addPartiallyRawPrepare(_prepareReq))
    {
        return false;
    }
    assert(_prepareReq->pBlock);
    bool allHit = m_txPool->initPartiallyBlock(_prepareReq->pBlock);
    // hit all transactions
    if (allHit)
    {
        RPBFTENGINE_LOG(DEBUG) << LOG_DESC(
            "hit all the transactions, handle the rawPrepare directly");
        m_rpbftReqCache->transPartiallyPrepareIntoRawPrepare();
        // begin to handlePrepare
        return execPrepareAndGenerateSignMsg(*_prepareReq, oss);
    }
    // can't find the node that generate the prepareReq
    h512 targetNode;
    if (!getNodeIDByIndex(targetNode, _prepareReq->idx))
    {
        return false;
    }

    // miss some transactions, request the missed transaction
    PartiallyBlock::Ptr partiallyBlock =
        std::dynamic_pointer_cast<PartiallyBlock>(_prepareReq->pBlock);
    assert(partiallyBlock);
    std::shared_ptr<bytes> encodedMissTxsInfo = std::make_shared<bytes>();
    partiallyBlock->encodeMissedInfo(encodedMissTxsInfo);

    auto p2pMsg = toP2PMessage(encodedMissTxsInfo, GetMissedTxsPacket);
    p2pMsg->setPacketType(GetMissedTxsPacket);

    m_service->asyncSendMessageByNodeID(targetNode, p2pMsg, nullptr);
    if (m_statisticHandler)
    {
        m_statisticHandler->updateConsOutPacketsInfo(p2pMsg->packetType(), 1, p2pMsg->length());
    }

    RPBFTENGINE_LOG(DEBUG) << LOG_DESC("send GetMissedTxsPacket to the leader")
                           << LOG_KV("targetIdx", _prepareReq->idx)
                           << LOG_KV("number", _prepareReq->height)
                           << LOG_KV("hash", _prepareReq->block_hash.abridged())
                           << LOG_KV("size", p2pMsg->length());
    return true;
}

// m_enablePrepareWithTxsHash is true: only send transaction hash to other sealers
// m_enablePrepareWithTxsHash is false: send all the transactions to other sealers
PrepareReq::Ptr RotatingPBFTEngine::constructPrepareReq(dev::eth::Block::Ptr _block)
{
    dev::eth::Block::Ptr engineBlock = m_blockFactory->createBlock();
    *engineBlock = std::move(*_block);
    PrepareReq::Ptr prepareReq = std::make_shared<PrepareReq>(
        engineBlock, m_keyPair, m_view, nodeIdx(), m_enablePrepareWithTxsHash);
    // broadcast prepare request
    m_threadPool->enqueue([this, prepareReq]() {
        std::shared_ptr<bytes> prepare_data = std::make_shared<bytes>();
        prepareReq->encode(*prepare_data);
        // the non-empty block only broadcast hash when enable-prepare-with-txs-hash
        if (m_enablePrepareWithTxsHash && prepareReq->pBlock->transactions()->size() > 0)
        {
            broadcastMsg(PartiallyPreparePacket, prepareReq->uniqueKey(), ref(*prepare_data),
                PartiallyPreparePacket);
        }
        else
        {
            broadcastMsg(PrepareReqPacket, prepareReq->uniqueKey(), ref(*prepare_data));
        }
    });
    return prepareReq;
}

dev::p2p::P2PMessage::Ptr RotatingPBFTEngine::toP2PMessage(
    std::shared_ptr<bytes> _data, PACKET_TYPE const& _packetType)
{
    dev::p2p::P2PMessage::Ptr message = std::dynamic_pointer_cast<dev::p2p::P2PMessage>(
        m_service->p2pMessageFactory()->buildMessage());
    message->setBuffer(_data);
    message->setPacketType(_packetType);
    message->setProtocolID(m_protocolId);
    return message;
}

void RotatingPBFTEngine::onReceiveGetMissedTxsRequest(
    std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message)
{
    try
    {
        RPBFTENGINE_LOG(DEBUG) << LOG_DESC("onReceiveGetMissedTxsRequest")
                               << LOG_KV("size", _message->length())
                               << LOG_KV("peer", _session->nodeID().abridged());
        std::shared_ptr<bytes> _encodedBytes = std::make_shared<bytes>();
        if (!m_rpbftReqCache->fetchMissedTxs(_encodedBytes, ref(*(_message->buffer()))))
        {
            return;
        }
        // response the transaction to the request node
        auto p2pMsg = toP2PMessage(_encodedBytes, MissedTxsPacket);
        p2pMsg->setPacketType(MissedTxsPacket);

        m_service->asyncSendMessageByNodeID(_session->nodeID(), p2pMsg, nullptr);
        if (m_statisticHandler)
        {
            m_statisticHandler->updateConsOutPacketsInfo(p2pMsg->packetType(), 1, p2pMsg->length());
        }
    }
    catch (std::exception const& _e)
    {
        RPBFTENGINE_LOG(WARNING) << LOG_DESC("onReceiveGetMissedTxsRequest exceptioned")
                                 << LOG_KV("peer", _session->nodeID().abridged())
                                 << LOG_KV("errorInfo", boost::diagnostic_information(_e));
    }
}

void RotatingPBFTEngine::onReceiveMissedTxsResponse(
    std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message)
{
    try
    {
        Guard l(m_mutex);
        RPBFTENGINE_LOG(DEBUG) << LOG_DESC("onReceiveMissedTxsResponse and fillBlock")
                               << LOG_KV("size", _message->length())
                               << LOG_KV("peer", _session->nodeID().abridged());
        if (!m_rpbftReqCache->fillBlock(ref(*(_message->buffer()))))
        {
            return;
        }
        // handlePrepare
        auto prepareReq = m_rpbftReqCache->partiallyRawPrepare();
        bool ret = handlePrepareMsg(*prepareReq);
        // forward the completed prepare message
        if (ret && m_cachedForwardMsg->count(prepareReq->block_hash))
        {
            auto pbftMsg = (*m_cachedForwardMsg)[prepareReq->block_hash].second;
            // forward the message
            forwardPrepareMsg(pbftMsg, prepareReq);
        }
        m_cachedForwardMsg->erase(prepareReq->block_hash);
    }
    catch (std::exception const& _e)
    {
        RPBFTENGINE_LOG(WARNING) << LOG_DESC("onReceiveMissedTxsResponse exceptioned")
                                 << LOG_KV("peer", _session->nodeID().abridged())
                                 << LOG_KV("errorInfo", boost::diagnostic_information(_e));
    }
}

bool RotatingPBFTEngine::handleFutureBlock()
{
    // hit the futurePrepareCache
    bool ret = PBFTEngine::handleFutureBlock();
    if (!m_enablePrepareWithTxsHash || ret)
    {
        return ret;
    }
    {
        Guard l(m_mutex);
        // miss the future prepare cache, find from the partially future prepare
        auto partiallyFuturePrepare =
            m_rpbftReqCache->getPartiallyFuturePrepare(m_consensusBlockNumber);
        if (partiallyFuturePrepare && partiallyFuturePrepare->view == m_view)
        {
            PBFTENGINE_LOG(INFO) << LOG_DESC("handleFutureBlock: partiallyFuturePrepare")
                                 << LOG_KV("reqNum", partiallyFuturePrepare->height)
                                 << LOG_KV("curNum", m_highestBlock.number())
                                 << LOG_KV("view", m_view)
                                 << LOG_KV("conNum", m_consensusBlockNumber)
                                 << LOG_KV("hash", partiallyFuturePrepare->block_hash.abridged())
                                 << LOG_KV("nodeIdx", nodeIdx())
                                 << LOG_KV("myNode", m_keyPair.pub().abridged());
            handlePartiallyPrepare(partiallyFuturePrepare);
            m_rpbftReqCache->eraseHandledPartiallyFutureReq(partiallyFuturePrepare->height);
            return true;
        }
    }
    return false;
}