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
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief : implementation of Raft consensus engine
 * @file: RaftEngine.cpp
 * @author: catli
 * @date: 2018-12-05
 */
#include "RaftEngine.h"
#include <libblockchain/BlockChainInterface.h>
#include <libconfig/SystemConfigMgr.h>
#include <libconsensus/Common.h>
#include <libdevcore/Guards.h>
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <chrono>
#include <random>
#include <thread>
#include <unordered_map>

using namespace dev;
using namespace dev::consensus;
using namespace dev::p2p;
using namespace dev::eth;
using namespace dev::blockchain;

void RaftEngine::resetElectTimeout()
{
    m_electTimeout =
        m_minElectTimeout + std::rand() % 100 * (m_maxElectTimeout - m_minElectTimeout) / 100;
    m_lastElectTime = std::chrono::system_clock::now();
    RAFTENGINE_LOG(INFO) << "[#resetElectTimeout] Reset elect timeout and last elect time"
                         << " [electTimeout]: " << m_electTimeout;
}

void RaftEngine::initRaftEnv()
{
    RecursiveGuard recursiveGuard(m_mutex);

    resetConfig();

    m_minElectTimeoutInit = m_minElectTimeout;
    m_maxElectTimeoutInit = m_maxElectTimeout;
    m_minElectTimeoutBoundary = m_minElectTimeout;
    m_maxElectTimeoutBoundary = m_maxElectTimeout + (m_maxElectTimeout - m_minElectTimeout) / 2;
    m_lastElectTime = std::chrono::system_clock::now();

    m_lastHeartbeatTime = m_lastElectTime;
    m_heartbeatTimeout = m_minElectTimeout;
    m_heartbeatInterval = m_heartbeatTimeout / RaftEngine::s_heartBeatIntervalRatio;

    m_increaseTime = (m_maxElectTimeout - m_minElectTimeout) / 4;

    resetElectTimeout();
    std::srand(static_cast<unsigned>(utcTime()));

    RAFTENGINE_LOG(INFO) << "[#initRaftEnv] Raft init env success";
}

void RaftEngine::resetConfig()
{
    updateMinerList();

    auto nodeNum = m_minerList.size();
    if (nodeNum == 0)
    {
        RAFTENGINE_LOG(WARNING) << "[#resetConfig] Reset config error: no miner. Stop sealing";
        m_cfgErr = true;
        return;
    }

    auto iter = std::find(m_minerList.begin(), m_minerList.end(), m_keyPair.pub());
    if (iter == m_minerList.end())
    {
        RAFTENGINE_LOG(WARNING)
            << "[#resetConfig] Reset config error: can't find myself in miner list. Stop sealing";
        m_cfgErr = true;
        return;
    }

    m_accountType = NodeAccountType::MinerAccount;

    auto nodeIdx = iter - m_minerList.begin();
    if (nodeNum != m_nodeNum || nodeIdx != m_nodeIdx)
    {
        m_nodeNum = nodeNum;
        m_nodeIdx = nodeIdx;
        m_f = (m_nodeNum - 1) / 2;
        RAFTENGINE_LOG(DEBUG) << "[#resetConfig] [m_nodeNum]: " << m_nodeNum
                              << ", [m_nodeIdx]: " << m_nodeIdx << ", [m_f]: " << m_f;

        switchToFollower(raft::InvalidIndex);
        resetElectTimeout();
    }

    m_cfgErr = false;
}

void RaftEngine::updateMinerList()
{
    if (m_storage == nullptr)
        return;
    if (m_highestBlockHeader.number() == m_lastObtainMinerNum)
        return;
    try
    {
        UpgradableGuard l(m_minerListMutex);
        auto minerList = m_minerList;
        int64_t curBlockNum = m_highestBlockHeader.number();
        /// get node from storage DB
        auto nodes =
            m_storage->select(m_highestBlockHeader.hash(), curBlockNum, "_sys_miners_", "node");
        /// obtain miner list
        if (!nodes)
            return;
        for (size_t i = 0; i < nodes->size(); i++)
        {
            auto node = nodes->get(i);
            if (!node)
                return;
            if ((node->getField("type") == "miner") &&
                (boost::lexical_cast<int>(node->getField("enable_num")) <= curBlockNum))
            {
                h512 nodeId = h512(node->getField("node_id"));
                if (find(minerList.begin(), minerList.end(), nodeId) == minerList.end())
                {
                    minerList.push_back(nodeId);
                    RAFTENGINE_LOG(INFO)
                        << "[#updateMinerList] Add miner node [nodeId/idx]: " << toHex(nodeId)
                        << "/" << i << std::endl;
                }
            }
        }
        /// remove observe nodes
        for (size_t i = 0; i < nodes->size(); i++)
        {
            auto node = nodes->get(i);
            if (!node)
                return;
            if ((node->getField("type") == "observer") &&
                (boost::lexical_cast<int>(node->getField("enable_num")) <= curBlockNum))
            {
                h512 nodeId = h512(node->getField("node_id"));
                auto it = find(minerList.begin(), minerList.end(), nodeId);
                if (it != minerList.end())
                {
                    minerList.erase(it);
                    RAFTENGINE_LOG(INFO)
                        << "[#updateMinerList] Remove observe node [nodeId/idx]:" << toHex(nodeId)
                        << "/" << i;
                }
            }
        }
        UpgradeGuard ul(l);
        m_minerList = minerList;
        /// to make sure the index of all miners are consistent
        std::sort(m_minerList.begin(), m_minerList.end());
        m_lastObtainMinerNum = m_highestBlock.number();
    }
    catch (std::exception& e)
    {
        RAFTENGINE_LOG(ERROR) << "[#updateMinerList] Update minerList failed [EINFO]: "
                              << boost::diagnostic_information(e);
    }
}

void RaftEngine::start()
{
    initRaftEnv();
    ConsensusEngineBase::start();
    RAFTENGINE_LOG(INFO) << "[#start] Start Raft engine..." << std::endl;
    RAFTENGINE_LOG(INFO) << "[#start] [ConsensusStatus]: " << consensusStatus();
}

void RaftEngine::reportBlock(dev::eth::Block const& _block)
{
    RecursiveGuard recursiveGuard(m_mutex);

    if (m_blockChain->number() == 0 || m_highestBlock.number() < _block.blockHeader().number())
    {
        m_lastBlockTime = utcTime();
        m_highestBlockHeader = _block.blockHeader();
        if (m_highestBlock.number() >= m_consensusBlockNumber)
        {
            m_consensusBlockNumber = m_highestBlock.number() + 1;
        }
        resetConfig();
        RAFTENGINE_LOG(INFO) << "[#reportBlock] ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^Report:"
                             << " [number]: " << m_highestBlock.number()
                             << ", [idx]: " << m_highestBlock.sealer()
                             << ", [hash]: " << m_highestBlock.hash().abridged()
                             << ", [next]: " << m_consensusBlockNumber
                             << ", [txNum]: " << _block.getTransactionSize()
                             << ", [blockTime]: " << m_lastBlockTime;
    }
}

bool RaftEngine::isValidReq(P2PMessage::Ptr _message, P2PSession::Ptr _session, ssize_t& _peerIndex)
{
    /// check whether message is empty
    if (_message->buffer()->size() <= 0)
        return false;
    /// check whether in the miner list
    _peerIndex = getIndexByMiner(_session->nodeID());
    if (_peerIndex < 0)
    {
        RAFTENGINE_LOG(WARNING) << "[#isValidReq] Recv Raft msg from unknown peer"
                                << " [peerNodeId]: " << _session->nodeID();
        return false;
    }
    /// check whether this node is in the miner list
    h512 nodeId;
    bool isMiner = getNodeIdByIndex(nodeId, m_nodeIdx);
    if (!isMiner || _session->nodeID() == nodeId)
    {
        RAFTENGINE_LOG(WARNING) << "[#isValidReq] I'm not a miner";
        return false;
    }
    return true;
}

ssize_t RaftEngine::getIndexByMiner(dev::h512 const& _nodeId)
{
    ReadGuard l(m_minerListMutex);
    ssize_t index = -1;
    for (size_t i = 0; i < m_minerList.size(); ++i)
    {
        if (m_minerList[i] == _nodeId)
        {
            index = i;
            break;
        }
    }
    return index;
}

bool RaftEngine::getNodeIdByIndex(h512& _nodeId, const u256& _nodeIdx) const
{
    _nodeId = getMinerByIndex(_nodeIdx.convert_to<size_t>());
    if (_nodeId == h512())
    {
        RAFTENGINE_LOG(ERROR) << "[#getNodeIdByIndex] Not a miner [idx]:" << _nodeIdx << std::endl;
        return false;
    }
    return true;
}

void RaftEngine::onRecvRaftMessage(dev::p2p::NetworkException _exception,
    dev::p2p::P2PSession::Ptr _session, dev::p2p::P2PMessage::Ptr _message)
{
    RaftMsgPacket raftMsg;

    bool valid = decodeToRequests(raftMsg, _message, _session);
    if (!valid)
    {
        RAFTENGINE_LOG(WARNING) << "[#onRecvRaftMessage] Invalid message";
        return;
    }

    if (raftMsg.packetType < RaftPacketType::RaftPacketCount)
    {
        RAFTENGINE_LOG(DEBUG) << "[#onRecvRaftMessage] Push message to message queue";
        m_msgQueue.push(raftMsg);
    }
    else
    {
        RAFTENGINE_LOG(WARNING) << "[#onRecvRaftMessage] Illegal message [idx/fromIp]:  "
                                << raftMsg.packetType << "/" << raftMsg.endpoint << std::endl;
    }
}

void RaftEngine::workLoop()
{
    while (isWorking())
    {
        if (m_cfgErr || m_accountType != NodeAccountType::MinerAccount)
        {
            RAFTENGINE_LOG(DEBUG) << "[#workLoop] Config error or I'm not a miner";
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        switch (getState())
        {
        case RaftRole::EN_STATE_LEADER:
        {
            RAFTENGINE_LOG(DEBUG) << "[#workLoop] Run as leader";
            runAsLeader();
            break;
        }
        case RaftRole::EN_STATE_FOLLOWER:
        {
            RAFTENGINE_LOG(DEBUG) << "[#workLoop] Run as follower";
            runAsFollower();
            break;
        }
        case RaftRole::EN_STATE_CANDIDATE:
        {
            RAFTENGINE_LOG(DEBUG) << "[#workLoop] Run as candidate";
            runAsCandidate();
            break;
        }
        default:
        {
            RAFTENGINE_LOG(WARNING) << "[#workLoop] Unknown state [errorState]: " << m_state;
            break;
        }
        }
    }
}

void RaftEngine::runAsLeader()
{
    RAFTENGINE_LOG(DEBUG) << "[#runAsLeader]";

    m_firstVote = raft::InvalidIndex;
    m_lastLeaderTerm = m_term;
    m_lastHeartbeatReset = m_lastHeartbeatTime = std::chrono::system_clock::now();
    std::unordered_map<h512, unsigned> memberHeartbeatLog;

    while (true)
    {
        if (m_state != RaftRole::EN_STATE_LEADER)
        {
            break;
        }

        // heartbeat timeout, change role to candidate
        if (m_nodeNum > 1 && checkHeartBeatTimeout())
        {
            RAFTENGINE_LOG(INFO) << "[#runAsLeader] Heartbeat Timeout [currentNodeNum]: "
                                 << m_nodeNum;
            for (auto& i : memberHeartbeatLog)
            {
                RAFTENGINE_LOG(INFO)
                    << "[#runAsLeader] ======= [node]: " << i.first.hex().substr(0, 5)
                    << " , [hbLog]:" << i.second;
            }
            switchToCandidate();
            return;
        }

        broadcastHeartbeat();

        std::pair<bool, RaftMsgPacket> ret = m_msgQueue.tryPop(c_PopWaitSeconds);

        if (!ret.first)
        {
            continue;
        }
        else
        {
            switch (ret.second.packetType)
            {
            case RaftPacketType::RaftVoteReqPacket:
            {
                RAFTENGINE_LOG(DEBUG) << "[#runAsLeader] Receieve vote req packet";

                RaftVoteReq req;
                req.populate(RLP(ref(ret.second.data).cropped(1)));
                if (handleVoteRequest(ret.second.nodeIdx, ret.second.nodeId, req))
                {
                    switchToFollower(raft::InvalidIndex);
                    return;
                }
                break;
            }
            case RaftPacketType::RaftVoteRespPacket:
            {
                RAFTENGINE_LOG(DEBUG) << "[#runAsLeader] Receieve vote resp packet";

                // do nothing
                break;
            }
            case RaftPacketType::RaftHeartBeatPacket:
            {
                RAFTENGINE_LOG(DEBUG) << "[#runAsLeader] Receieve heartbeat packet";

                RaftHeartBeat hb;
                hb.populate(RLP(ref(ret.second.data).cropped(1)));
                if (handleHeartBeat(ret.second.nodeIdx, ret.second.nodeId, hb))
                {
                    switchToFollower(hb.leader);
                    return;
                }
                break;
            }
            case RaftPacketType::RaftHeartBeatRespPacket:
            {
                RAFTENGINE_LOG(DEBUG) << "[#runAsLeader] Receieve heartbeat resp packet";

                RaftHeartBeatResp resp;
                RAFTENGINE_LOG(INFO)
                    << "[#runAsLeader] Receive heartbeat ack [from]: " << ret.second.nodeIdx
                    << ", [node]: " << ret.second.nodeId.hex().substr(0, 5);
                resp.populate(RLP(ref(ret.second.data).cropped(1)));
                {
                    RecursiveGuard recursiveGuard(m_mutex);
                    m_memberBlock[ret.second.nodeId] = BlockRef(resp.height, resp.blockHash);
                }

                // receive large term
                if (resp.term > m_term)
                    break;

                auto it = memberHeartbeatLog.find(ret.second.nodeId);
                if (it == memberHeartbeatLog.end())
                {
                    memberHeartbeatLog.insert(std::make_pair(ret.second.nodeId, 1));
                }
                else
                {
                    it->second++;
                }
                int count = count_if(memberHeartbeatLog.begin(), memberHeartbeatLog.end(),
                    [](std::pair<const h512, unsigned>& item) {
                        if (item.second > 0)
                            return true;
                        else
                            return false;
                    });
                // add myself
                if (count + 1 >= m_nodeNum - m_f)
                {
                    // collect heartbeat exceed half
                    // reset heartbeat timeout
                    m_lastHeartbeatReset = std::chrono::system_clock::now();
                    for_each(memberHeartbeatLog.begin(), memberHeartbeatLog.end(),
                        [](std::pair<const h512, unsigned>& item) {
                            if (item.second > 0)
                                --item.second;
                        });
                    RAFTENGINE_LOG(INFO) << "[#runAsLeader] Reset heartbeat timeout";
                }
                break;
            }
            default:
            {
                break;
            }
            }
        }
    }
}

void RaftEngine::runAsCandidate()
{
    RAFTENGINE_LOG(DEBUG) << "[#runAsCandidate]";

    if (m_state != EN_STATE_CANDIDATE)
    {
        RAFTENGINE_LOG(DEBUG) << "[#runAsCandidate] [state]:" << m_state
                              << " != EN_STATE_CANDIDATE";
        return;
    }

    broadcastVoteReq();

    VoteState voteState;
    voteState.vote += 1;  // vote self
    setVote(m_nodeIdx);
    m_firstVote = m_nodeIdx;

    if (wonElection(voteState.vote))
    {
        RAFTENGINE_LOG(DEBUG) << "[#runAsCandidate] Won election, switch to leader now...";
        switchToLeader();
        return;
    }

    while (true)
    {
        if (checkElectTimeout())
        {
            RAFTENGINE_LOG(INFO) << "[#runAsCandidate] VoteState: [vote]: " << voteState.vote
                                 << ", [unVote]: " << voteState.unVote
                                 << ", [lastTermErr]: " << voteState.lastTermErr
                                 << ", [firstVote]: " << voteState.firstVote
                                 << ", [discardedVote]: " << voteState.discardedVote;
            if (isMajorityVote(voteState.totalVoteCount()))
            {
                RAFTENGINE_LOG(INFO) << "[#runAsCandidate] Candidate campaign leader time out";
                switchToCandidate();
            }
            else
            {
                // not receive enough vote
                RAFTENGINE_LOG(INFO)
                    << "[#runAsCandidate] Not enough vote received,  recover term from " << m_term
                    << " to " << m_term - 1;
                increaseElectTime();
                m_term--;  // recover to previous term
                RAFTENGINE_LOG(INFO) << "[#runAsCandidate] Switch to Follower";
                switchToFollower(raft::InvalidIndex);
            }
            return;
        }
        std::pair<bool, RaftMsgPacket> ret = m_msgQueue.tryPop(5);
        if (!ret.first)
        {
            continue;
        }
        else
        {
            switch (ret.second.packetType)
            {
            case RaftPacketType::RaftVoteReqPacket:
            {
                RAFTENGINE_LOG(DEBUG) << "[#runAsCandidate] Receieve vote req packet";

                RaftVoteReq req;
                req.populate(RLP(ref(ret.second.data).cropped(1)));
                if (handleVoteRequest(ret.second.nodeIdx, ret.second.nodeId, req))
                {
                    switchToFollower(raft::InvalidIndex);
                    return;
                }
                break;
            }
            case RaftVoteRespPacket:
            {
                RAFTENGINE_LOG(DEBUG) << "[#runAsCandidate] Receieve vote resp packet";

                RaftVoteResp resp;
                resp.populate(RLP(ref(ret.second.data).cropped(1)));
                RAFTENGINE_LOG(INFO)
                    << "[#runAsCandidate] Receieve response [term]: " << resp.term
                    << ", [voteFlag]: " << resp.voteFlag << ", [from]: " << ret.second.nodeIdx
                    << ", [node]: " << ret.second.nodeId.hex().substr(0, 5);
                HandleVoteResult handleRet =
                    handleVoteResponse(ret.second.nodeIdx, ret.second.nodeId, resp, voteState);
                if (handleRet == TO_LEADER)
                {
                    switchToLeader();
                    return;
                }
                else if (handleRet == TO_FOLLOWER)
                {
                    switchToFollower(raft::InvalidIndex);
                    return;
                }
                break;
            }
            case RaftHeartBeatPacket:
            {
                RAFTENGINE_LOG(DEBUG) << "[#runAsCandidate] Receieve heartbeat packet";

                RaftHeartBeat hb;
                hb.populate(RLP(ref(ret.second.data).cropped(1)));
                if (handleHeartBeat(ret.second.nodeIdx, ret.second.nodeId, hb))
                {
                    switchToFollower(hb.leader);
                    return;
                }
                break;
            }
            default:
            {
                break;
            }
            }
        }
    }
}

void RaftEngine::runAsFollower()
{
    RAFTENGINE_LOG(DEBUG) << "[#runAsFollwer] [currentLeader]: " << m_leader;

    while (true)
    {
        if (m_state != EN_STATE_FOLLOWER)
        {
            RAFTENGINE_LOG(DEBUG) << "[#runAsFollwer] [state]:" << m_state
                                  << " != EN_STATE_FOLLOWER";
            break;
        }

        if (checkElectTimeout())
        {
            RAFTENGINE_LOG(DEBUG) << "[#runAsFollwer] Elect timeout, switch to Candidate";
            switchToCandidate();
            return;
        }

        std::pair<bool, RaftMsgPacket> ret = m_msgQueue.tryPop(5);
        if (!ret.first)
        {
            continue;
        }
        else
        {
            switch (ret.second.packetType)
            {
            case RaftVoteReqPacket:
            {
                RAFTENGINE_LOG(DEBUG) << "[#runAsFollower] Receieve vote req packet";

                RaftVoteReq req;
                req.populate(RLP(ref(ret.second.data).cropped(1)));
                if (handleVoteRequest(ret.second.nodeIdx, ret.second.nodeId, req))
                {
                    return;
                }
                break;
            }
            case RaftVoteRespPacket:
            {
                RAFTENGINE_LOG(DEBUG) << "[#runAsFollower] Receieve vote resp packet";

                // do nothing
                break;
            }
            case RaftHeartBeatPacket:
            {
                RAFTENGINE_LOG(DEBUG) << "[#runAsFollower] Receieve heartbeat packet";

                RaftHeartBeat hb;
                hb.populate(RLP(ref(ret.second.data).cropped(1)));
                if (m_leader == Invalid256)
                {
                    setLeader(hb.leader);
                }
                if (handleHeartBeat(ret.second.nodeIdx, ret.second.nodeId, hb))
                {
                    setLeader(hb.leader);
                }
                break;
            }
            default:
            {
                break;
            }
            }
        }
    }
}

bool RaftEngine::checkHeartBeatTimeout()
{
    std::chrono::system_clock::time_point nowTime = std::chrono::system_clock::now();
    auto interval = nowTime - m_lastHeartbeatReset;

    RAFTENGINE_LOG(DEBUG) << "[#checkHeartBeatTimeout] [interval]: " << interval.count()
                          << ", [heartbeatTimeout]: " << m_heartbeatTimeout;

    return interval >= std::chrono::milliseconds(m_heartbeatTimeout);
}

P2PMessage::Ptr RaftEngine::generateHeartbeat()
{
    RaftHeartBeat req;
    req.idx = m_nodeIdx;
    req.term = m_term;
    req.height = m_highestBlockHeader.number();
    req.blockHash = m_highestBlockHeader.hash();
    req.leader = m_nodeIdx;

    RLPStream ts;
    req.streamRLPFields(ts);
    auto heartbeatMsg =
        transDataToMessage(ref(ts.out()), RaftPacketType::RaftHeartBeatPacket, m_protocolId);

    RAFTENGINE_LOG(INFO) << "[#generateHeartbeat] Heartbeat message generated, [term]: " << req.term
                         << ", [leader]: " << req.leader;
    return heartbeatMsg;
}

void RaftEngine::broadcastHeartbeat()
{
    std::chrono::system_clock::time_point nowTime = std::chrono::system_clock::now();
    if (nowTime - m_lastHeartbeatTime >= std::chrono::milliseconds(m_heartbeatInterval))
    {
        m_lastHeartbeatTime = nowTime;
        auto heartbeatMsg = generateHeartbeat();
        RAFTENGINE_LOG(INFO) << "[#broadcastHeartbeat] Ready to broadcast heartbeat";
        broadcastMsg(heartbeatMsg);
        clearFirstVoteCache();
    }
    else
    {
        RAFTENGINE_LOG(INFO)
            << "[#broadcastHeartbeat] Failed to broadcast heartbeat: broadcast too fast";
    }
}

P2PMessage::Ptr RaftEngine::generateVoteReq()
{
    RaftVoteReq req;
    req.idx = m_nodeIdx;
    req.term = m_term;
    req.height = m_highestBlockHeader.number();
    req.blockHash = m_highestBlockHeader.hash();
    req.candidate = m_nodeIdx;
    req.lastLeaderTerm = m_lastLeaderTerm;

    RLPStream ts;
    req.streamRLPFields(ts);
    auto voteReqMsg =
        transDataToMessage(ref(ts.out()), RaftPacketType::RaftVoteReqPacket, m_protocolId);

    RAFTENGINE_LOG(INFO) << "[#generateVoteReq] VoteReq message generated, [term]: " << req.term
                         << ", [lastLeaderTerm]: " << req.lastLeaderTerm
                         << ", [vote]: " << req.candidate;
    return voteReqMsg;
}

void RaftEngine::broadcastVoteReq()
{
    auto voteReqMsg = generateVoteReq();

    if (voteReqMsg)
    {
        RAFTENGINE_LOG(INFO) << "[#broadcastVoteReq] Ready to broadcast VoteReq";
        broadcastMsg(voteReqMsg);
    }
    else
    {
        RAFTENGINE_LOG(WARNING) << "[#broadcastVoteReq] Failed to broadcast VoteReq";
    }
}

P2PMessage::Ptr RaftEngine::transDataToMessage(
    bytesConstRef _data, RaftPacketType const& _packetType, PROTOCOL_ID const& _protocolId)
{
    dev::p2p::P2PMessage::Ptr message = std::make_shared<dev::p2p::P2PMessage>();
    std::shared_ptr<dev::bytes> dataPtr = std::make_shared<dev::bytes>();
    RaftMsgPacket packet;

    RLPStream listRLP;
    listRLP.appendList(1).append(_data);
    bytes packetData;
    listRLP.swapOut(packetData);
    packet.data = packetData;
    packet.packetType = _packetType;

    RAFTENGINE_LOG(DEBUG) << "[#transDataToMessage] [data]: " << toHex(packet.data)
                          << ", [packetType]: " << packet.packetType;

    packet.encode(*dataPtr);
    message->setBuffer(dataPtr);
    message->setProtocolID(_protocolId);
    return message;
}

void RaftEngine::broadcastMsg(P2PMessage::Ptr _data)
{
    auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    for (auto session : sessions)
    {
        if (getIndexByMiner(session.nodeID) < 0)
        {
            continue;
        }

        m_service->asyncSendMessageByNodeID(session.nodeID, _data, nullptr);
        RAFTENGINE_LOG(INFO) << "[#broadcastMsg] Sent Raft msg to " << session.nodeID;
    }
}

void RaftEngine::clearFirstVoteCache()
{
    if (m_firstVote != Invalid256)
    {
        ++m_heartbeatCount;
        if (m_heartbeatCount >= 2 * s_heartBeatIntervalRatio)
        {
            // clear m_firstVote
            m_heartbeatCount = 0;
            m_firstVote = raft::InvalidIndex;
            RAFTENGINE_LOG(INFO) << "[#clearFirstVoteCache] Broadcast or receive enough hb package "
                                    "and clear m_firstVote cache";
        }
    }
}

bool RaftEngine::handleVoteRequest(u256 const& _from, h512 const& _node, RaftVoteReq const& _req)
{
    RAFTENGINE_LOG(INFO) << "[#handleVoteRequest] [from]: " << _from
                         << ", [node]: " << _node.hex().substr(0, 5) << ", [term]: " << _req.term
                         << ", [candidate]: " << _req.candidate;

    RaftVoteResp resp;
    resp.idx = m_nodeIdx;
    resp.term = m_term;
    resp.height = m_highestBlockHeader.number();
    resp.blockHash = m_highestBlockHeader.hash();

    resp.voteFlag = VOTE_RESP_REJECT;
    resp.lastLeaderTerm = m_lastLeaderTerm;

    if (_req.term <= m_term)
    {
        if (m_state == EN_STATE_LEADER)
        {
            // include _req.term < m_term and _req.term == m_term
            resp.voteFlag = VOTE_RESP_LEADER_REJECT;
            RAFTENGINE_LOG(INFO) << "[#handleVoteRequest] Discard vreq for I'm the bigger leader"
                                 << " [term]: " << m_term << ", [receiveTerm]: " << _req.term;
        }
        else
        {
            if (_req.term == m_term)
            {
                // _req.term == m_term for follower and candidate
                resp.voteFlag = VOTE_RESP_DISCARD;
                RAFTENGINE_LOG(INFO)
                    << "[#handleVoteRequest] Discard vreq for I'm already in this term [term]: "
                    << m_term << ", [m_vote]: " << m_vote;
            }
            else
            {
                // _req.term < m_term for follower and candidate
                resp.voteFlag = VOTE_RESP_REJECT;
                RAFTENGINE_LOG(INFO)
                    << "[#handleVoteRequest] Discard vreq for smaller term, [term]: " << m_term;
            }
            sendResponse(_from, _node, RaftVoteRespPacket, resp);
            return false;
        }
    }

    // handle lastLeaderTerm error
    if (_req.lastLeaderTerm < m_lastLeaderTerm)
    {
        resp.voteFlag = VOTE_RESP_LASTTERM_ERROR;
        RAFTENGINE_LOG(INFO)
            << "[#handleVoteRequest] Discard vreq for smaller last leader term, [lastLeaderTerm]: "
            << m_lastLeaderTerm << ", [receiveLastLeaderTerm]: " << _req.lastLeaderTerm;
        sendResponse(_from, _node, RaftVoteRespPacket, resp);
        return false;
    }

    // first vote, not change term
    if (m_firstVote == raft::InvalidIndex)
    {
        m_firstVote = _req.candidate;
        resp.voteFlag = VOTE_RESP_FIRST_VOTE;
        RAFTENGINE_LOG(INFO)
            << "[#handleVoteRequest] Discard vreq for I'm the first time to vote, [term]: "
            << m_term << ", [voteReqTerm]: " << _req.term << ", [firstVote]: " << m_firstVote;
        sendResponse(_from, _node, RaftVoteRespPacket, resp);
        return false;
    }

    m_term = _req.term;
    m_leader = raft::InvalidIndex;
    m_vote = raft::InvalidIndex;

    m_firstVote = _req.candidate;
    setVote(_req.candidate);

    resp.term = m_term;
    resp.voteFlag = VOTE_RESP_GRANTED;
    sendResponse(_from, _node, RaftVoteRespPacket, resp);

    resetElectTimeout();

    return true;
}

bool RaftEngine::checkElectTimeout()
{
    std::chrono::system_clock::time_point nowTime = std::chrono::system_clock::now();
    return nowTime - m_lastElectTime >= std::chrono::milliseconds(m_electTimeout);
}

bool RaftEngine::handleHeartBeat(u256 const& _from, h512 const& _node, RaftHeartBeat const& _hb)
{
    RAFTENGINE_LOG(INFO) << "[#handleHeartBeat] [from]: " << _from
                         << ", [node]: " << _node.hex().substr(0, 5) << ", [term]: " << _hb.term
                         << ", [leader]: " << _hb.leader;

    RaftHeartBeatResp resp;
    resp.idx = m_nodeIdx;
    resp.term = m_term;
    resp.height = m_highestBlockHeader.number();
    resp.blockHash = m_highestBlockHeader.hash();
    sendResponse(_from, _node, RaftPacketType::RaftHeartBeatRespPacket, resp);

    if (_hb.term < m_term && _hb.term <= m_lastLeaderTerm)
    {
        RAFTENGINE_LOG(INFO) << "[#handleHeartBeat] Discard hb for smaller term"
                             << "[term]: " << m_term << ", [receiveTerm]: " << _hb.term
                             << ", [lastLeaderTerm]: " << m_lastLeaderTerm
                             << ", [receivelastLeaderTerm]: " << _hb.term;
        return false;
    }

    bool step_down = false;

    // _hb.term >= m_term || _hb.lastLeaderTerm > m_lastLeaderTerm
    // receive larger lastLeaderTerm, recover my term to hb term, set self to next step (follower)
    if (_hb.term > m_lastLeaderTerm)
    {
        RAFTENGINE_LOG(INFO)
            << "[#handleHeartBeat] Prepare to switch follower due to last leader term error "
            << "[lastLeaderTerm]: " << m_lastLeaderTerm << ", [hbLastLeader]: " << _hb.term;
        m_term = _hb.term;
        m_vote = raft::InvalidIndex;
        step_down = true;
    }

    if (_hb.term > m_term)
    {
        RAFTENGINE_LOG(INFO)
            << "[#handleHeartBeat] Prepare to switch follower due to receive higher term"
            << " [term]: " << m_term << ", [hbTerm]: " << _hb.term;
        m_term = _hb.term;
        m_vote = raft::InvalidIndex;
        step_down = true;
    }

    if (m_state == EN_STATE_CANDIDATE && _hb.term >= m_term)
    {
        RAFTENGINE_LOG(INFO) << "[#handleHeartBeat] Prepare to switch follower due to receive "
                                "higher or equal term in candidate"
                             << " [term]: " << m_term << ", [hbTerm]: " << _hb.term;
        m_term = _hb.term;
        m_vote = raft::InvalidIndex;
        step_down = true;
    }

    clearFirstVoteCache();
    // see the leader last time
    m_lastLeaderTerm = _hb.term;

    resetElectTimeout();

    return step_down;
}

void RaftEngine::recoverElectTime()
{
    m_maxElectTimeout = m_maxElectTimeoutInit;
    m_minElectTimeout = m_minElectTimeoutInit;
    RAFTENGINE_LOG(INFO) << "[#recoverElectTime] reset elect time to init"
                         << " [minElectTimeout]: " << m_minElectTimeout
                         << ", [maxElectTimeout]: " << m_maxElectTimeout;
}

void RaftEngine::switchToLeader()
{
    RecursiveGuard recursiveGuard(m_mutex);
    m_leader = m_nodeIdx;
    m_state = EN_STATE_LEADER;
    recoverElectTime();
    RAFTENGINE_LOG(INFO) << "[#switchToLeader] [currentTerm]: " << m_term;
}

void RaftEngine::switchToFollower(raft::NodeIndex const& _leader)
{
    RecursiveGuard recursiveGuard(m_mutex);
    m_leader = _leader;
    m_state = EN_STATE_FOLLOWER;
    m_heartbeatCount = 0;
    resetElectTimeout();
    RAFTENGINE_LOG(INFO) << "[#switchToFollower] [currentTerm]: " << m_term;
}

void RaftEngine::switchToCandidate()
{
    RecursiveGuard recursiveGuard(m_mutex);
    m_term++;
    m_leader = raft::InvalidIndex;
    m_state = RaftRole::EN_STATE_CANDIDATE;
    resetElectTimeout();
    RAFTENGINE_LOG(INFO) << "[#switchToCandidate] [newTerm]: " << m_term;
}

bool RaftEngine::sendResponse(
    u256 const& _to, h512 const& _node, RaftPacketType _packetType, RaftMsg const& _msg)
{
    RAFTENGINE_LOG(INFO) << "[#sendResponse] Ready to send sesponse"
                         << " [to]: " << _to << ", [term]: " << _msg.term;
    RLPStream ts;
    _msg.streamRLPFields(ts);

    auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    for (auto session : sessions)
    {
        if (session.nodeID != _node || getIndexByMiner(session.nodeID) < 0)
        {
            continue;
        }

        m_service->asyncSendMessageByNodeID(
            session.nodeID, transDataToMessage(ref(ts.out()), _packetType, m_protocolId), nullptr);
        RAFTENGINE_LOG(INFO) << "Sent raftmsg"
                             << " [to]: " << session.nodeID << ", [packetType]: " << _packetType;
        return true;
    }

    return false;
}

HandleVoteResult RaftEngine::handleVoteResponse(
    u256 const& _from, h512 const& _node, RaftVoteResp const& _req, VoteState& _vote_state)
{
    unsigned voteFlag = (unsigned)_req.voteFlag;
    switch (voteFlag)
    {
    case VOTE_RESP_REJECT:
    {
        _vote_state.unVote++;
        if (isMajorityVote(_vote_state.unVote))
        {
            // increase elect time
            increaseElectTime();
            return TO_FOLLOWER;
        }
        break;
    }
    case VOTE_RESP_LEADER_REJECT:
    {  // switch to leader directly
        m_term = _req.term;
        m_lastLeaderTerm = _req.lastLeaderTerm;
        // increase elect time
        increaseElectTime();
        return TO_FOLLOWER;
    }
    case VOTE_RESP_LASTTERM_ERROR:
    {
        _vote_state.lastTermErr++;
        if (isMajorityVote(_vote_state.lastTermErr))
        {
            // increase elect time
            increaseElectTime();
            return TO_FOLLOWER;
        }
        break;
    }
    case VOTE_RESP_FIRST_VOTE:
    {
        _vote_state.firstVote++;
        if (isMajorityVote(_vote_state.firstVote))
        {
            RAFTENGINE_LOG(INFO)
                << "[#handleVoteResponse] Receive majority first vote, recover from " << m_term
                << "to term " << m_term - 1;
            m_term--;
            RAFTENGINE_LOG(INFO) << "[#handleVoteResponse] Switch to Follower";
            return TO_FOLLOWER;
        }
        break;
    }
    case VOTE_RESP_DISCARD:
    {
        _vote_state.discardedVote++;
        // do nothing
        break;
    }
    case VOTE_RESP_GRANTED:
    {
        _vote_state.vote++;
        if (isMajorityVote(_vote_state.vote))
            return TO_LEADER;
        break;
    }
    default:
    {
        RAFTENGINE_LOG(INFO) << "[#handleVoteResponse] Error voteFlag"
                             << " [voteFlag]: " << voteFlag << ", [from]: " << _from
                             << ", [node]:" << _node.hex().substr(0, 5);
    }
    }
    return NONE;
}

void RaftEngine::increaseElectTime()
{
    if (m_maxElectTimeout + m_increaseTime > m_maxElectTimeoutBoundary)
    {
        m_maxElectTimeout = m_maxElectTimeoutBoundary;
    }
    else
    {
        m_maxElectTimeout += m_increaseTime;
        m_minElectTimeout += m_increaseTime;
    }
    RAFTENGINE_LOG(INFO) << "[#increaseElectTime] Increase elect time"
                         << " [minElectTimeout]: " << m_minElectTimeout << ", [maxElectTimeout]"
                         << m_maxElectTimeout;
}

bool RaftEngine::shouldSeal()
{
    RecursiveGuard recursiveGuard(m_mutex);
    if (m_cfgErr || m_accountType != NodeAccountType::MinerAccount || m_state != EN_STATE_LEADER)
    {
        return false;
    }

    u256 count = 1;
    u256 currentHeight = m_highestBlockHeader.number();
    for (auto iter = m_memberBlock.begin(); iter != m_memberBlock.end(); ++iter)
    {
        if (iter->second.height > currentHeight)
        {
            RAFTENGINE_LOG(INFO) << "[#shouldSeal] Wait to download block, shouldSeal return false";
            return false;
        }
        if (iter->second.height == currentHeight)
        {
            ++count;
        }
    }

    if (count < m_nodeNum - m_f)
    {
        RAFTENGINE_LOG(INFO) << "[#shouldSeal] Wait somebody to sync block, shouldSeal return false"
                             << " [count]: " << count << ", [nodeNum]: " << m_nodeNum
                             << ", [m_f]=" << m_f << ", [memberBlockSize]=" << m_memberBlock.size();
        return false;
    }

    RAFTENGINE_LOG(INFO) << "[#shouldSeal] Return true";
    return true;
}

bool RaftEngine::commit(Block const& _block)
{
    if (getState() == RaftRole::EN_STATE_LEADER)
    {
        Sealing workingSealing;
        try
        {
            execBlock(workingSealing, _block);
        }
        catch (std::exception& e)
        {
            RAFTENGINE_LOG(WARNING)
                << "[#commit] Block execute failed: [EINFO]:  " << boost::diagnostic_information(e);
            return false;
        }

        checkAndSave(workingSealing);
        return true;
    }
    RAFTENGINE_LOG(INFO) << "[#commit] I'm not the leader";
    return false;
}

void RaftEngine::execBlock(Sealing& _sealing, Block const& _block)
{
    Block working_block(_block);
    RAFTENGINE_LOG(INFO) << "[#execBlock] [number/hash]:  " << working_block.header().number()
                         << "/" << working_block.header().hash().abridged();

    checkBlockValid(working_block);
    m_blockSync->noteSealingBlockNumber(working_block.header().number());
    _sealing.p_execContext = executeBlock(working_block);
    _sealing.block = working_block;
}

void RaftEngine::checkBlockValid(dev::eth::Block const& _block)
{
    ConsensusEngineBase::checkBlockValid(_block);
    checkMinerList(_block);
}

void RaftEngine::checkMinerList(Block const& _block)
{
    ReadGuard l(m_minerListMutex);
    if (m_minerList != _block.blockHeader().sealerList())
    {
        std::string miners;
        for (auto miner : m_minerList)
            miners += toHex(miner) + " ";
        RAFTENGINE_LOG(ERROR) << "[#checkMinerList] [miners]: " << miners
                              << "Wrong miners : [Cminers/CblockMiner/hash] : "
                              << m_minerList.size() << "/"
                              << _block.blockHeader().sealerList().size() << "/"
                              << _block.blockHeader().hash();
        BOOST_THROW_EXCEPTION(
            BlockMinerListWrong() << errinfo_comment("Wrong Miner List of Block"));
    }
}

void RaftEngine::checkAndSave(Sealing& _sealing)
{
    /// callback block chain to commit block
    CommitResult ret = m_blockChain->commitBlock(_sealing.block, _sealing.p_execContext);
    RAFTENGINE_LOG(DEBUG) << "[#checkAndSave] Commit block succ";
    /// drop handled transactions
    if (ret == CommitResult::OK)
    {
        dropHandledTransactions(_sealing.block);
        RAFTENGINE_LOG(DEBUG) << "[#checkAndSave] Commit block succ";
    }
    else
    {
        RAFTENGINE_LOG(ERROR) << "[#checkAndSave] Commit block failed"
                              << " [highNum/SNum/Shash]:  " << m_highestBlockHeader.number() << "/"
                              << _sealing.block.blockHeader().number() << "/"
                              << _sealing.block.blockHeader().hash().abridged() << std::endl;
        /// note blocksync to sync
        m_blockSync->noteSealingBlockNumber(m_blockChain->number());
        m_txPool->handleBadBlock(_sealing.block);
    }

    resetConfig();
}

bool RaftEngine::reachBlockIntervalTime()
{
    auto nowTime = utcTime();
    auto parentTime = m_lastBlockTime;

    return nowTime - parentTime >= dev::config::SystemConfigMgr::c_intervalBlockTime;
}