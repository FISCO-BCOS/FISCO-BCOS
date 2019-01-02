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
#include <cassert>
#include <chrono>
#include <cstdint>
#include <random>
#include <thread>
#include <unordered_map>

using namespace dev;
using namespace dev::consensus;
using namespace dev::p2p;
using namespace dev::eth;
using namespace dev::blockchain;
using namespace std;
using namespace std::chrono;

typename raft::NodeIndex RaftEngine::InvalidIndex = raft::NodeIndex(-1);

void RaftEngine::resetElectTimeout()
{
    Guard guard(m_mutex);

    m_electTimeout =
        m_minElectTimeout + std::rand() % 100 * (m_maxElectTimeout - m_minElectTimeout) / 100;
    m_lastElectTime = std::chrono::system_clock::now();
    RAFTENGINE_LOG(DEBUG) << "[#resetElectTimeout]Reset elect timeout and last elect time"
                          << "[electTimeout=" << m_electTimeout << "]";
}

void RaftEngine::initRaftEnv()
{
    resetConfig();

    {
        Guard guard(m_mutex);
        m_minElectTimeoutInit = m_minElectTimeout;
        m_maxElectTimeoutInit = m_maxElectTimeout;
        m_minElectTimeoutBoundary = m_minElectTimeout;
        m_maxElectTimeoutBoundary = m_maxElectTimeout + (m_maxElectTimeout - m_minElectTimeout) / 2;
        m_lastElectTime = std::chrono::system_clock::now();
        m_lastHeartbeatTime = m_lastElectTime;
        m_heartbeatTimeout = m_minElectTimeout;
        m_heartbeatInterval = m_heartbeatTimeout / RaftEngine::s_heartBeatIntervalRatio;
        m_increaseTime = (m_maxElectTimeout - m_minElectTimeout) / 4;
    }

    resetElectTimeout();
    std::srand(static_cast<unsigned>(utcTime()));

    RAFTENGINE_LOG(INFO) << "[#initRaftEnv]Raft init env success";
}

void RaftEngine::resetConfig()
{
    updateMinerList();

    auto shouldSwitchToFollower = false;
    {
        Guard guard(m_mutex);
        auto nodeNum = m_minerList.size();
        if (nodeNum == 0)
        {
            RAFTENGINE_LOG(WARNING) << "[#resetConfig]Reset config error: no miner, stop sealing";
            m_cfgErr = true;
            return;
        }

        auto iter = std::find(m_minerList.begin(), m_minerList.end(), m_keyPair.pub());
        if (iter == m_minerList.end())
        {
            RAFTENGINE_LOG(WARNING) << "[#resetConfig]Reset config error: can't find myself in "
                                       "miner list, stop sealing";
            m_cfgErr = true;
            return;
        }

        m_accountType = NodeAccountType::MinerAccount;
        auto nodeIdx = iter - m_minerList.begin();
        if (nodeNum != m_nodeNum || nodeIdx != m_idx)
        {
            m_nodeNum = nodeNum;
            m_idx = nodeIdx;
            m_idx = m_idx;
            m_f = (m_nodeNum - 1) / 2;
            shouldSwitchToFollower = true;
            RAFTENGINE_LOG(DEBUG) << "[#resetConfig]Reset config"
                                  << "[m_nodeNum=" << m_nodeNum << ",m_idx=" << m_idx
                                  << ",m_f=" << m_f << "]";
        }

        m_cfgErr = false;
    }

    if (shouldSwitchToFollower)
    {
        switchToFollower(InvalidIndex);
        resetElectTimeout();
    }
}

void RaftEngine::updateMinerList()
{
    if (m_storage == nullptr)
        return;
    if (m_highestBlock.number() == m_lastObtainMinerNum)
        return;
    try
    {
        UpgradableGuard guard(m_minerListMutex);
        auto minerList = m_minerList;
        int64_t curBlockNum = m_highestBlock.number();
        /// get node from storage DB
        auto nodes = m_storage->select(m_highestBlock.hash(), curBlockNum, "_sys_miners_", "node");
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
                    RAFTENGINE_LOG(DEBUG) << "[#updateMinerList]Add miner node"
                                          << "[nodeId=" << toHex(nodeId) << ",idx=" << i << "]";
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
                    RAFTENGINE_LOG(DEBUG) << "[#updateMinerList]Remove observe node"
                                          << "[nodeId=" << toHex(nodeId) << ",idx=" << i << "]";
                }
            }
        }
        UpgradeGuard ul(guard);
        m_minerList = minerList;
        /// to make sure the index of all miners are consistent
        std::sort(m_minerList.begin(), m_minerList.end());
        m_lastObtainMinerNum = m_highestBlock.number();
    }
    catch (std::exception& e)
    {
        RAFTENGINE_LOG(ERROR) << "[#updateMinerList]Update minerList failed"
                              << "[EINFO=" << boost::diagnostic_information(e) << "]";
    }
}

void RaftEngine::start()
{
    initRaftEnv();
    ConsensusEngineBase::start();
    RAFTENGINE_LOG(INFO) << "[#start]Raft engine started"
                         << "[consensusStatus=" << consensusStatus() << "]";
}

void RaftEngine::reportBlock(dev::eth::Block const& _block)
{
    auto shouldReport = false;
    {
        Guard guard(m_mutex);
        shouldReport = (m_blockChain->number() == 0 ||
                        m_highestBlock.number() < _block.blockHeader().number());
        if (shouldReport)
        {
            m_lastBlockTime = utcTime();
            m_highestBlock = m_blockChain->getBlockByNumber(m_blockChain->number())->header();
        }
    }

    if (shouldReport)
    {
        {
            Guard guard(m_commitMutex);

            auto iter = m_commitFingerPrint.find(m_uncommittedBlock.header().hash());
            if (iter != m_commitFingerPrint.end())
            {
                m_commitFingerPrint.erase(iter);
            }

            m_uncommittedBlock = Block();
            m_uncommittedBlockNumber = 0;
            if (m_highestBlock.number() >= m_consensusBlockNumber)
            {
                m_consensusBlockNumber = m_highestBlock.number() + 1;
            }
        }

        resetConfig();
        RAFTENGINE_LOG(INFO) << "[#reportBlock]Block reported"
                             << "[number=" << m_highestBlock.number()
                             << ",sealer=" << m_highestBlock.sealer()
                             << ",hash=" << m_highestBlock.hash().abridged()
                             << ",next=" << m_consensusBlockNumber
                             << ",txNum=" << _block.getTransactionSize()
                             << ",blockTime=" << m_lastBlockTime << "]";
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
        RAFTENGINE_LOG(WARNING) << "[#isValidReq]Recv Raft msg from unknown peer"
                                << "[peerNodeId=" << _session->nodeID() << "]";
        return false;
    }
    /// check whether this node is in the miner list
    h512 nodeId;
    bool isMiner = getNodeIdByIndex(nodeId, m_idx);
    if (!isMiner || _session->nodeID() == nodeId)
    {
        RAFTENGINE_LOG(WARNING) << "[#isValidReq]I'm not a miner";
        return false;
    }
    return true;
}

ssize_t RaftEngine::getIndexByMiner(dev::h512 const& _nodeId)
{
    ReadGuard guard(m_minerListMutex);
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
        RAFTENGINE_LOG(WARNING) << "[#getNodeIdByIndex]Invalid node idx"
                                << "[nodeIdx=" << _nodeIdx << "]";
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
        RAFTENGINE_LOG(WARNING) << "[#onRecvRaftMessage]Invalid message";
        return;
    }

    if (raftMsg.packetType < RaftPacketType::RaftPacketCount)
    {
        m_msgQueue.push(raftMsg);
    }
    else
    {
        RAFTENGINE_LOG(WARNING) << "[#onRecvRaftMessage]Illegal message type"
                                << "[msgType=" << raftMsg.packetType
                                << ",fromIP=" << raftMsg.endpoint << "]";
    }
}

void RaftEngine::workLoop()
{
    while (isWorking())
    {
        if (m_cfgErr || m_accountType != NodeAccountType::MinerAccount)
        {
            RAFTENGINE_LOG(DEBUG) << "[#workLoop]Config error or I'm not a miner";
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        switch (getState())
        {
        case RaftRole::EN_STATE_LEADER:
        {
            runAsLeader();
            break;
        }
        case RaftRole::EN_STATE_FOLLOWER:
        {
            runAsFollower();
            break;
        }
        case RaftRole::EN_STATE_CANDIDATE:
        {
            runAsCandidate();
            break;
        }
        default:
        {
            RAFTENGINE_LOG(WARNING) << "[#workLoop]Unknown work state"
                                    << "[state=" << m_state << "]";
            break;
        }
        }
    }
}

bool RaftEngine::runAsLeaderImp(std::unordered_map<h512, unsigned>& memberHeartbeatLog)
{
    if (m_state != RaftRole::EN_STATE_LEADER)
    {
        return false;
    }

    // heartbeat timeout, change role to candidate
    if (m_nodeNum > 1 && checkHeartbeatTimeout())
    {
        RAFTENGINE_LOG(DEBUG) << "[#runAsLeaderImp]Heartbeat Timeout";
        for (auto& i : memberHeartbeatLog)
        {
            RAFTENGINE_LOG(DEBUG) << "[#runAsLeader]Heartbeat Log"
                                  << "[node=" << i.first.hex().substr(0, 5)
                                  << " ,hbLog=" << i.second << "]";
        }
        switchToCandidate();
        return false;
    }

    broadcastHeartbeat();

    std::pair<bool, RaftMsgPacket> ret = m_msgQueue.tryPop(c_PopWaitSeconds);

    if (!ret.first)
    {
        return true;
    }
    else
    {
        switch (ret.second.packetType)
        {
        case RaftPacketType::RaftVoteReqPacket:
        {
            RAFTENGINE_LOG(DEBUG) << "[#runAsLeaderImp]Recv vote req packet";

            RaftVoteReq req;
            req.populate(RLP(ref(ret.second.data))[0]);
            if (handleVoteRequest(ret.second.nodeIdx, ret.second.nodeId, req))
            {
                switchToFollower(InvalidIndex);
                return false;
            }
            return true;
        }
        case RaftPacketType::RaftVoteRespPacket:
        {
            RAFTENGINE_LOG(DEBUG) << "[#runAsLeaderImp]Recv vote resp packet";

            /// do nothing
            return true;
        }
        case RaftPacketType::RaftHeartBeatPacket:
        {
            RAFTENGINE_LOG(DEBUG) << "[#runAsLeaderImp]Recv heartbeat packet";

            RaftHeartBeat hb;
            hb.populate(RLP(ref(ret.second.data))[0]);
            if (handleHeartbeat(ret.second.nodeIdx, ret.second.nodeId, hb))
            {
                switchToFollower(hb.leader);
                return false;
            }
            return true;
        }
        case RaftPacketType::RaftHeartBeatRespPacket:
        {
            RaftHeartBeatResp resp;
            resp.populate(RLP(ref(ret.second.data))[0]);

            RAFTENGINE_LOG(DEBUG) << "[#runAsLeaderImp]Recv heartbeat ack"
                                  << "[from=" << ret.second.nodeId << ",peerHeight=" << resp.height
                                  << ",peerBlockHash=" << toString(resp.blockHash) << "]";
            /// receive strange term
            if (resp.term != m_term)
            {
                RAFTENGINE_LOG(DEBUG) << "[#runAsLeaderImp]Heartbeat ack term is strange"
                                      << "[ackTerm=" << resp.term << ",myTerm=" << m_term;
                return true;
            }

            {
                Guard guard(m_mutex);

                m_memberBlock[ret.second.nodeId] = BlockRef(resp.height, resp.blockHash);

                auto it = memberHeartbeatLog.find(ret.second.nodeId);
                if (it == memberHeartbeatLog.end())
                {
                    memberHeartbeatLog.insert(std::make_pair(ret.second.nodeId, 1));
                }
                else
                {
                    it->second++;
                }
                auto count = count_if(memberHeartbeatLog.begin(), memberHeartbeatLog.end(),
                    [](std::pair<const h512, unsigned>& item) {
                        if (item.second > 0)
                            return true;
                        else
                            return false;
                    });

                // add myself
                auto exceedHalf = (count + 1 >= m_nodeNum - m_f);
                if (exceedHalf)
                {
                    RAFTENGINE_LOG(DEBUG) << "[#runAsLeaderImp]Collect heartbeat resp exceed half";

                    m_lastHeartbeatReset = std::chrono::system_clock::now();
                    for_each(memberHeartbeatLog.begin(), memberHeartbeatLog.end(),
                        [](std::pair<const h512, unsigned>& item) {
                            if (item.second > 0)
                                --item.second;
                        });

                    RAFTENGINE_LOG(TRACE) << "[#runAsLeaderImp]Heartbeat timeout reset";
                }
            }

            {
                std::unique_lock<std::mutex> ul(m_commitMutex);
                if (bool(m_uncommittedBlock))
                {
                    if (m_uncommittedBlockNumber == m_consensusBlockNumber)
                    {
                        h256 uncommitedBlockHash = m_uncommittedBlock.header().hash();
                        if (resp.uncommitedBlockHash == uncommitedBlockHash)
                        {
                            m_commitFingerPrint[uncommitedBlockHash].insert(ret.second.nodeId);
                            if (m_commitFingerPrint[uncommitedBlockHash].size() + 1 >=
                                static_cast<uint64_t>(m_nodeNum - m_f))
                            {
                                if (m_waitingForCommitting)
                                {
                                    RAFTENGINE_LOG(DEBUG)
                                        << "[#runAsLeaderImp]Some thread waiting on "
                                           "commitCV, commit by other thread";

                                    m_commitReady = true;
                                    ul.unlock();
                                    m_commitCV.notify_all();
                                }
                                else
                                {
                                    RAFTENGINE_LOG(DEBUG)
                                        << "[#runAsLeaderImp]No thread waiting on "
                                           "commitCV, commit by meself";

                                    checkAndExecute(m_uncommittedBlock);
                                    ul.unlock();
                                    reportBlock(m_uncommittedBlock);
                                }
                            }
                        }
                        else
                        {
                            RAFTENGINE_LOG(DEBUG) << "[#runAsLeaderImp]Uneuqal fingerprint"
                                                  << "[ackFingerprint=" << resp.uncommitedBlockHash
                                                  << ",myFingerprint=" << uncommitedBlockHash;
                        }
                    }
                    else
                    {
                        RAFTENGINE_LOG(DEBUG)
                            << "[#runAsLeaderImp]Give up uncommited block"
                            << "[uncommittedBlockNumber=" << m_uncommittedBlockNumber
                            << ",myHeight=" << m_highestBlock.number() << "]";

                        m_uncommittedBlock = Block();
                        m_uncommittedBlockNumber = 0;
                    }
                }
                else
                {
                    RAFTENGINE_LOG(DEBUG) << "[#runAsLeaderImp]No uncommited block";
                    ul.unlock();
                }
            }
            return true;
        }
        default:
        {
            return true;
        }
        }
    }
}

void RaftEngine::runAsLeader()
{
    m_firstVote = InvalidIndex;
    m_lastLeaderTerm = m_term;
    m_lastHeartbeatReset = m_lastHeartbeatTime = std::chrono::system_clock::now();
    std::unordered_map<h512, unsigned> memberHeartbeatLog;

    while (runAsLeaderImp(memberHeartbeatLog))
    {
    }
}

bool RaftEngine::runAsCandidateImp(VoteState& _voteState)
{
    if (checkElectTimeout())
    {
        RAFTENGINE_LOG(DEBUG) << "[#runAsCandidateImp]VoteState"
                              << "[vote=" << _voteState.vote << ",unVote=" << _voteState.unVote
                              << ",lastTermErr=" << _voteState.lastTermErr
                              << ",firstVote=" << _voteState.firstVote
                              << ",discardedVote=" << _voteState.discardedVote << "]";
        if (isMajorityVote(_voteState.totalVoteCount()))
        {
            RAFTENGINE_LOG(DEBUG) << "[#runAsCandidateImp]Candidate campaign leader time out";
            switchToCandidate();
        }
        else
        {
            // not receive enough vote
            RAFTENGINE_LOG(DEBUG) << "[#runAsCandidateImp]Not enough vote received, recover term"
                                  << "[currentTerm=" << m_term << ",toTerm=" << m_term - 1 << "]";
            increaseElectTime();
            m_term--;  // recover to previous term
            RAFTENGINE_LOG(INFO) << "[#runAsCandidateImp]Switch to Follower";
            switchToFollower(InvalidIndex);
        }
        return false;
    }

    std::pair<bool, RaftMsgPacket> ret = m_msgQueue.tryPop(5);
    if (!ret.first)
    {
        return true;
    }
    else
    {
        switch (ret.second.packetType)
        {
        case RaftPacketType::RaftVoteReqPacket:
        {
            RAFTENGINE_LOG(DEBUG) << "[#runAsCandidateImp]Recv vote req packet";

            RaftVoteReq req;
            req.populate(RLP(ref(ret.second.data))[0]);
            if (handleVoteRequest(ret.second.nodeIdx, ret.second.nodeId, req))
            {
                switchToFollower(InvalidIndex);
                return false;
            }
            return true;
        }
        case RaftVoteRespPacket:
        {
            RaftVoteResp resp;
            resp.populate(RLP(ref(ret.second.data))[0]);

            RAFTENGINE_LOG(DEBUG) << "[#runAsCandidateImp]Recv vote response packet"
                                  << "[respTerm=" << resp.term << ",voteFlag=" << resp.voteFlag
                                  << ",from=" << ret.second.nodeIdx
                                  << ",node=" << ret.second.nodeId.hex().substr(0, 5) << "]";
            HandleVoteResult handleRet =
                handleVoteResponse(ret.second.nodeIdx, ret.second.nodeId, resp, _voteState);
            if (handleRet == TO_LEADER)
            {
                switchToLeader();
                return false;
            }
            else if (handleRet == TO_FOLLOWER)
            {
                switchToFollower(InvalidIndex);
                return false;
            }
            return true;
        }
        case RaftHeartBeatPacket:
        {
            RAFTENGINE_LOG(DEBUG) << "[#runAsCandidateImp]Recv heartbeat packet";

            RaftHeartBeat hb;
            hb.populate(RLP(ref(ret.second.data))[0]);
            if (handleHeartbeat(ret.second.nodeIdx, ret.second.nodeId, hb))
            {
                switchToFollower(hb.leader);
                return false;
            }
            return true;
        }
        default:
        {
            return true;
        }
        }
    }
}

void RaftEngine::runAsCandidate()
{
    if (m_state != EN_STATE_CANDIDATE)
    {
        return;
    }

    broadcastVoteReq();

    VoteState voteState;

    /// vote self
    voteState.vote += 1;
    setVote(m_idx);
    m_firstVote = m_idx;

    if (wonElection(voteState.vote))
    {
        RAFTENGINE_LOG(DEBUG) << "[#runAsCandidate]Won election, switch to leader now";
        switchToLeader();
        return;
    }

    while (runAsCandidateImp(voteState))
    {
    }
}

bool RaftEngine::runAsFollowerImp()
{
    if (m_state != EN_STATE_FOLLOWER)
    {
        return false;
    }

    RAFTENGINE_LOG(DEBUG) << "[#runAsFollower] [currentLeader]: " << m_leader;

    if (checkElectTimeout())
    {
        RAFTENGINE_LOG(DEBUG) << "[#runAsFollowerImp]Elect timeout, switch to Candidate";
        switchToCandidate();
        return false;
    }

    std::pair<bool, RaftMsgPacket> ret = m_msgQueue.tryPop(5);
    if (!ret.first)
    {
        return true;
    }
    else
    {
        switch (ret.second.packetType)
        {
        case RaftVoteReqPacket:
        {
            RAFTENGINE_LOG(DEBUG) << "[#runAsFollowerImp]Recv vote req packet";

            RaftVoteReq req;
            req.populate(RLP(ref(ret.second.data))[0]);
            if (handleVoteRequest(ret.second.nodeIdx, ret.second.nodeId, req))
            {
                return false;
            }
            return true;
        }
        case RaftVoteRespPacket:
        {
            RAFTENGINE_LOG(DEBUG) << "[#runAsFollowerImp]Recv vote resp packet";

            // do nothing
            return true;
        }
        case RaftHeartBeatPacket:
        {
            RAFTENGINE_LOG(DEBUG) << "[#runAsFollowerImp]Recv heartbeat packet";

            RaftHeartBeat hb;
            hb.populate(RLP(ref(ret.second.data))[0]);
            if (m_leader == Invalid256)
            {
                setLeader(hb.leader);
            }
            if (handleHeartbeat(ret.second.nodeIdx, ret.second.nodeId, hb))
            {
                setLeader(hb.leader);
            }
            return true;
        }
        default:
        {
            return true;
        }
        }
    }
}

void RaftEngine::runAsFollower()
{
    while (runAsFollowerImp())
    {
    }
}

bool RaftEngine::checkHeartbeatTimeout()
{
    system_clock::time_point nowTime = system_clock::now();
    auto interval = duration_cast<milliseconds>(nowTime - m_lastHeartbeatReset).count();

    RAFTENGINE_LOG(DEBUG) << "[#checkHeartbeatTimeout]"
                          << "[interval=" << interval << ",heartbeatTimeout=" << m_heartbeatTimeout
                          << "]";

    return interval >= m_heartbeatTimeout;
}

P2PMessage::Ptr RaftEngine::generateHeartbeat()
{
    RaftHeartBeat hb;
    hb.idx = m_idx;
    hb.term = m_term;
    hb.height = m_highestBlock.number();
    hb.blockHash = m_highestBlock.hash();
    hb.leader = m_idx;
    {
        Guard guard(m_commitMutex);
        if (bool(m_uncommittedBlock))
        {
            RAFTENGINE_LOG(DEBUG) << "[#generateHeartbeat]Has uncommited block";

            m_uncommittedBlock.encode(hb.uncommitedBlock);
            hb.uncommitedBlockNumber = m_consensusBlockNumber;

            RAFTENGINE_LOG(DEBUG) << "[#generateHeartbeat]"
                                  << "[nextBlockNumber=" << hb.uncommitedBlockNumber << "]";
        }
        else
        {
            RAFTENGINE_LOG(DEBUG) << "[#generateHeartbeat]No uncommited block";

            hb.uncommitedBlock = bytes();
            hb.uncommitedBlockNumber = 0;
        }
    }

    RLPStream ts;
    hb.streamRLPFields(ts);
    auto heartbeatMsg =
        transDataToMessage(ref(ts.out()), RaftPacketType::RaftHeartBeatPacket, m_protocolId);

    RAFTENGINE_LOG(DEBUG) << "[#generateHeartbeat]Heartbeat message generated"
                          << "[term=" << hb.term << ",leader=" << hb.leader << "]";
    return heartbeatMsg;
}

void RaftEngine::broadcastHeartbeat()
{
    std::chrono::system_clock::time_point nowTime = std::chrono::system_clock::now();
    auto interval =
        std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - m_lastHeartbeatTime)
            .count();
    if (interval >= m_heartbeatInterval)
    {
        m_lastHeartbeatTime = nowTime;
        auto heartbeatMsg = generateHeartbeat();
        broadcastMsg(heartbeatMsg);
        clearFirstVoteCache();
        RAFTENGINE_LOG(DEBUG) << "[#broadcastHeartbeat]Heartbeat broadcasted";
    }
    else
    {
        RAFTENGINE_LOG(DEBUG) << "[#broadcastHeartbeat]Too fast to broadcast heartbeat";
    }
}

P2PMessage::Ptr RaftEngine::generateVoteReq()
{
    RaftVoteReq req;
    req.idx = m_idx;
    req.term = m_term;
    req.height = m_highestBlock.number();
    req.blockHash = m_highestBlock.hash();
    req.candidate = m_idx;
    req.lastLeaderTerm = m_lastLeaderTerm;
    auto currentBlockNumber = m_blockChain->number();
    {
        Guard guard(m_commitMutex);
        if (bool(m_uncommittedBlock))
        {
            req.lastBlockNumber = currentBlockNumber + 1;
        }
        else
        {
            req.lastBlockNumber = currentBlockNumber;
        }
    }

    RLPStream ts;
    req.streamRLPFields(ts);
    auto voteReqMsg =
        transDataToMessage(ref(ts.out()), RaftPacketType::RaftVoteReqPacket, m_protocolId);

    RAFTENGINE_LOG(DEBUG) << "[#generateVoteReq]VoteReq message generated"
                          << "[term=" << req.term << ",lastLeaderTerm=" << req.lastLeaderTerm
                          << ",vote=" << req.candidate << ",lastBlockNumber=" << req.lastBlockNumber
                          << "]";
    return voteReqMsg;
}

void RaftEngine::broadcastVoteReq()
{
    auto voteReqMsg = generateVoteReq();

    if (voteReqMsg)
    {
        broadcastMsg(voteReqMsg);
        RAFTENGINE_LOG(DEBUG) << "[#broadcastVoteReq]VoteReq broadcasted";
    }
    else
    {
        RAFTENGINE_LOG(WARNING) << "[#broadcastVoteReq]Failed to broadcast VoteReq";
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
        RAFTENGINE_LOG(DEBUG) << "[#broadcastMsg]Raft msg sent"
                              << "[peer=" << session.nodeID << "]";
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
            m_firstVote = InvalidIndex;
            RAFTENGINE_LOG(DEBUG) << "[#clearFirstVoteCache]Broadcast or receive enough hb "
                                     "package, clear first vote cache";
        }
    }
}

bool RaftEngine::handleVoteRequest(u256 const& _from, h512 const& _node, RaftVoteReq const& _req)
{
    RAFTENGINE_LOG(DEBUG) << "[#handleVoteRequest]"
                          << "[from=" << _from << ",node=" << _node.hex().substr(0, 5)
                          << ",term=" << _req.term << ",candidate=" << _req.candidate << "]";

    RaftVoteResp resp;
    resp.idx = m_idx;
    resp.term = m_term;
    resp.height = m_highestBlock.number();
    resp.blockHash = m_highestBlock.hash();

    resp.voteFlag = VOTE_RESP_REJECT;
    resp.lastLeaderTerm = m_lastLeaderTerm;

    if (_req.term <= m_term)
    {
        if (m_state == EN_STATE_LEADER)
        {
            // include _req.term < m_term and _req.term == m_term
            resp.voteFlag = VOTE_RESP_LEADER_REJECT;
            RAFTENGINE_LOG(DEBUG) << "[#handleVoteRequest]Discard vreq for I'm the bigger leader"
                                  << "[myTerm=" << m_term << "]";
        }
        else
        {
            if (_req.term == m_term)
            {
                // _req.term == m_term for follower and candidate
                resp.voteFlag = VOTE_RESP_DISCARD;
                RAFTENGINE_LOG(DEBUG)
                    << "[#handleVoteRequest]Discard vreq for I'm already in this term"
                    << "[myTerm=" << m_term << "]";
            }
            else
            {
                // _req.term < m_term for follower and candidate
                resp.voteFlag = VOTE_RESP_REJECT;
                RAFTENGINE_LOG(DEBUG) << "[#handleVoteRequest]Discard vreq for smaller term"
                                      << "[myTerm=" << m_term << "]";
            }
            sendResponse(_from, _node, RaftVoteRespPacket, resp);
            return false;
        }
    }

    // handle lastLeaderTerm error
    if (_req.lastLeaderTerm < m_lastLeaderTerm)
    {
        RAFTENGINE_LOG(DEBUG) << "[#handleVoteRequest]Discard vreq for smaller last leader term"
                              << "[myLastLeaderTerm=" << m_lastLeaderTerm
                              << ",reqLastLeaderTerm=" << _req.lastLeaderTerm << "]";

        resp.voteFlag = VOTE_RESP_LASTTERM_ERROR;
        sendResponse(_from, _node, RaftVoteRespPacket, resp);
        return false;
    }

    auto currentBlockNumber = m_blockChain->number();
    {
        Guard guard(m_commitMutex);
        if (bool(m_uncommittedBlock))
        {
            currentBlockNumber++;
        }
    }

    if (_req.lastBlockNumber < currentBlockNumber)
    {
        RAFTENGINE_LOG(DEBUG) << "[#handleVoteRequest]Discard vreq for peer's data is older than me"
                              << "[myBlockNumber=" << currentBlockNumber
                              << ",reqBlockNumber=" << _req.lastBlockNumber << "]";

        resp.voteFlag = VOTE_RESP_OUTDATED;
        sendResponse(_from, _node, RaftVoteRespPacket, resp);
        return false;
    }

    // first vote, not change term
    if (m_firstVote == InvalidIndex)
    {
        RAFTENGINE_LOG(DEBUG) << "[#handleVoteRequest]Discard vreq for I'm the first time to vote";

        m_firstVote = _req.candidate;
        resp.voteFlag = VOTE_RESP_FIRST_VOTE;
        sendResponse(_from, _node, RaftVoteRespPacket, resp);
        return false;
    }

    RAFTENGINE_LOG(DEBUG) << "[#handleVoteRequest]Grant vreq";

    m_term = _req.term;
    m_leader = InvalidIndex;
    m_vote = InvalidIndex;

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

bool RaftEngine::handleHeartbeat(u256 const& _from, h512 const& _node, RaftHeartBeat const& _hb)
{
    RAFTENGINE_LOG(DEBUG) << "[#handleHeartbeat]"
                          << "[fromIdx=" << _from << ",fromId=" << _node.hex().substr(0, 5)
                          << ",hbTerm=" << _hb.term << ",hbLeader=" << _hb.leader << "]";

    if (_hb.term < m_term && _hb.term <= m_lastLeaderTerm)
    {
        RAFTENGINE_LOG(DEBUG) << "[#handleHeartbeat]Discard hb for smaller term"
                              << "[myTerm=" << m_term << ",hbTerm=" << _hb.term
                              << ",myLastLeaderTerm" << m_lastLeaderTerm << "]";
        return false;
    }

    RaftHeartBeatResp resp;
    resp.idx = m_idx;
    resp.term = m_term;
    resp.height = m_highestBlock.number();
    resp.blockHash = m_highestBlock.hash();
    resp.uncommitedBlockHash = h256(0);

    if (_hb.hasData())
    {
        if (_hb.uncommitedBlockNumber - 1 == m_highestBlock.number())
        {
            Guard guard(m_commitMutex);
            m_uncommittedBlock = Block(_hb.uncommitedBlock);
            m_uncommittedBlockNumber = _hb.uncommitedBlockNumber;
            resp.uncommitedBlockHash = m_uncommittedBlock.header().hash();
        }
        else
        {
            RAFTENGINE_LOG(WARNING) << "[#handleHeartbeat]Leader's height is not equal to mine"
                                    << "[leaderNextHeight=" << _hb.uncommitedBlockNumber
                                    << ",myHeight=" << m_highestBlock.number() << "]";

            return false;
        }
    }
    sendResponse(_from, _node, RaftPacketType::RaftHeartBeatRespPacket, resp);

    bool stepDown = false;
    // _hb.term >= m_term || _hb.lastLeaderTerm > m_lastLeaderTerm
    // receive larger lastLeaderTerm, recover my term to hb term, set self to next step (follower)
    if (_hb.term > m_lastLeaderTerm)
    {
        RAFTENGINE_LOG(DEBUG)
            << "[#handleHeartbeat]Prepare to switch to follower due to last leader term error"
            << "[lastLeaderTerm=" << m_lastLeaderTerm << ",hbLastLeader=" << _hb.term << "]";

        m_term = _hb.term;
        m_vote = InvalidIndex;
        stepDown = true;
    }

    if (_hb.term > m_term)
    {
        RAFTENGINE_LOG(DEBUG)
            << "[#handleHeartbeat]Prepare to switch to follower due to receive higher term"
            << "[term= " << m_term << ",hbTerm=" << _hb.term << "]";

        m_term = _hb.term;
        m_vote = InvalidIndex;
        stepDown = true;
    }

    if (m_state == EN_STATE_CANDIDATE && _hb.term >= m_term)
    {
        RAFTENGINE_LOG(DEBUG) << "[#handleHeartbeat]Prepare to switch to follower due to receive "
                                 "higher or equal term in candidate state"
                              << "[myTerm=" << m_term << ",hbTerm=" << _hb.term << "]";

        m_term = _hb.term;
        m_vote = InvalidIndex;
        stepDown = true;
    }

    clearFirstVoteCache();
    // see the leader last time
    m_lastLeaderTerm = _hb.term;

    resetElectTimeout();

    return stepDown;
}

void RaftEngine::recoverElectTime()
{
    m_maxElectTimeout = m_maxElectTimeoutInit;
    m_minElectTimeout = m_minElectTimeoutInit;
    RAFTENGINE_LOG(DEBUG) << "[#recoverElectTime]Reset elect time to init"
                          << "[minElectTimeout=" << m_minElectTimeout
                          << ",maxElectTimeout=" << m_maxElectTimeout << "]";
}

void RaftEngine::switchToLeader()
{
    {
        Guard guard(m_mutex);
        m_leader = m_idx;
        m_state = EN_STATE_LEADER;
    }

    recoverElectTime();
    RAFTENGINE_LOG(DEBUG) << "[#switchToLeader]"
                          << "[currentTerm=" << m_term << "]";
}

void RaftEngine::switchToFollower(raft::NodeIndex const& _leader)
{
    {
        Guard guard(m_mutex);
        m_leader = _leader;
        m_state = EN_STATE_FOLLOWER;
        m_heartbeatCount = 0;
    }

    std::unique_lock<std::mutex> ul(m_commitMutex);
    if (m_waitingForCommitting)
    {
        RAFTENGINE_LOG(DEBUG) << "[#switchToFollower]Some thread still waiting on "
                                 "commitCV, need to wake up and cleanup uncommited block buffer";

        m_uncommittedBlock = Block();
        m_uncommittedBlockNumber = 0;
        m_commitReady = true;
        ul.unlock();
        m_commitCV.notify_all();
    }
    else
    {
        ul.unlock();
    }

    resetElectTimeout();
    RAFTENGINE_LOG(INFO) << "[#switchToFollower]"
                         << "[currentTerm=" << m_term << "]";
}

void RaftEngine::switchToCandidate()
{
    {
        Guard guard(m_mutex);
        m_term++;
        m_leader = InvalidIndex;
        m_state = RaftRole::EN_STATE_CANDIDATE;
    }
    resetElectTimeout();
    RAFTENGINE_LOG(INFO) << "[#switchToCandidate]"
                         << "[currentTerm=" << m_term << "]";
}

bool RaftEngine::sendResponse(
    u256 const& _to, h512 const& _node, RaftPacketType _packetType, RaftMsg const& _msg)
{
    RAFTENGINE_LOG(DEBUG) << "[#sendResponse]Ready to send sesponse"
                          << "[to=" << _to << ",term=" << _msg.term << ",packetType=" << _packetType
                          << "]";

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
        RAFTENGINE_LOG(DEBUG) << "[#sendResponse]Response sent";
        return true;
    }
    return false;
}

HandleVoteResult RaftEngine::handleVoteResponse(
    u256 const& _from, h512 const& _node, RaftVoteResp const& _resp, VoteState& _vote_state)
{
    if (_resp.term < m_term - 1)
    {
        RAFTENGINE_LOG(DEBUG) << "[#handleVoteResponse]Peer's term is smaller than mine"
                              << "[respTerm=" << _resp.term << ",myTerm=" << m_term << "]";
        return HandleVoteResult::NONE;
    }

    switch (_resp.voteFlag)
    {
    case VoteRespFlag::VOTE_RESP_REJECT:
    {
        _vote_state.unVote++;
        if (isMajorityVote(_vote_state.unVote))
        {
            /// increase elect time
            increaseElectTime();
            return TO_FOLLOWER;
        }
        break;
    }
    case VoteRespFlag::VOTE_RESP_LEADER_REJECT:
    {
        /// switch to leader directly
        m_term = _resp.term;
        m_lastLeaderTerm = _resp.lastLeaderTerm;
        /// increase elect time
        increaseElectTime();
        return TO_FOLLOWER;
    }
    case VoteRespFlag::VOTE_RESP_LASTTERM_ERROR:
    {
        _vote_state.lastTermErr++;
        if (isMajorityVote(_vote_state.lastTermErr))
        {
            /// increase elect time
            increaseElectTime();
            return TO_FOLLOWER;
        }
        break;
    }
    case VoteRespFlag::VOTE_RESP_FIRST_VOTE:
    {
        _vote_state.firstVote++;
        if (isMajorityVote(_vote_state.firstVote))
        {
            RAFTENGINE_LOG(DEBUG)
                << "[#handleVoteResponse]Receive majority first vote, recover term"
                << "[currentTerm=" << m_term << ",toTerm=" << m_term - 1 << "]";
            m_term--;
            return TO_FOLLOWER;
        }
        break;
    }
    case VoteRespFlag::VOTE_RESP_DISCARD:
    {
        _vote_state.discardedVote++;
        // do nothing
        break;
    }
    case VoteRespFlag::VOTE_RESP_OUTDATED:
    {
        _vote_state.outdated++;
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
        RAFTENGINE_LOG(DEBUG) << "[#handleVoteResponse]Error voteFlag"
                              << "[voteFlag=" << _resp.voteFlag << ",from=" << _from
                              << ",node=" << _node.hex().substr(0, 5) << "]";
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
    RAFTENGINE_LOG(DEBUG) << "[#increaseElectTime]Increase elect time"
                          << "[minElectTimeout=" << m_minElectTimeout
                          << ",maxElectTimeout=" << m_maxElectTimeout << "]";
}

bool RaftEngine::shouldSeal()
{
    {
        Guard guard(m_mutex);
        if (m_state != EN_STATE_LEADER)
        {
            RAFTENGINE_LOG(DEBUG) << "[#shouldSeal]I'm not the leader";
            return false;
        }

        if (m_cfgErr || m_accountType != NodeAccountType::MinerAccount)
        {
            RAFTENGINE_LOG(DEBUG) << "[#shouldSeal]My state is not well"
                                  << "[cfgError=" << m_cfgErr << ",accountType=" << m_accountType
                                  << "]";
            return false;
        }

        u256 count = 1;
        u256 currentHeight = m_highestBlock.number();
        h256 currentBlockHash = m_highestBlock.hash();
        for (auto iter = m_memberBlock.begin(); iter != m_memberBlock.end(); ++iter)
        {
            if (iter->second.height > currentHeight)
            {
                RAFTENGINE_LOG(DEBUG) << "[#shouldSeal]Wait to download block";
                return false;
            }

            if (iter->second.height == currentHeight && iter->second.block_hash == currentBlockHash)
            {
                ++count;
            }
        }

        if (count < m_nodeNum - m_f)
        {
            RAFTENGINE_LOG(INFO) << "[#shouldSeal]Wait somebody to sync block"
                                 << "[count=" << count << ",m_nodeNum=" << m_nodeNum
                                 << ",m_f=" << m_f << ",memberBlockSize=" << m_memberBlock.size()
                                 << "]";

            return false;
        }
    }

    {
        Guard guard(m_commitMutex);
        if (bool(m_uncommittedBlock))
        {
            RAFTENGINE_LOG(DEBUG) << "[#shouldSeal]Wait to commit uncommitted block"
                                  << "[uncommittedBlockHeight="
                                  << m_uncommittedBlock.header().number()
                                  << ",uncommittedBlockHash=" << m_uncommittedBlock.header().hash()
                                  << "]";
            return false;
        }
    }

    RAFTENGINE_LOG(DEBUG) << "[#shouldSeal]Seal granted";
    return true;
}

bool RaftEngine::commit(Block const& _block)
{
    {
        Guard guard(m_commitMutex);
        m_uncommittedBlock = _block;
        m_uncommittedBlockNumber = m_consensusBlockNumber;
        RAFTENGINE_LOG(DEBUG) << "[#commit]Prepare to commit block"
                              << "[nextHeight=" << m_uncommittedBlockNumber << "]";
    }

    std::unique_lock<std::mutex> ul(m_commitMutex);
    m_waitingForCommitting = true;
    m_commitReady = false;
    RAFTENGINE_LOG(DEBUG) << "[#commit]Wait to commit block";
    m_commitCV.wait(ul, [this]() { return m_commitReady; });

    m_commitReady = false;
    m_waitingForCommitting = false;

    if (!bool(m_uncommittedBlock))
    {
        assert(m_uncommittedBlockNumber == 0);
        ul.unlock();
        return false;
    }

    m_uncommittedBlock = Block();
    m_uncommittedBlockNumber = 0;
    ul.unlock();

    if (getState() != RaftRole::EN_STATE_LEADER)
    {
        RAFTENGINE_LOG(DEBUG) << "[#commit]I'm not the leader anymore, stop committing";
        return false;
    }

    RAFTENGINE_LOG(DEBUG) << "[#commit]Start to commit block";
    return checkAndExecute(_block);
}

bool RaftEngine::checkAndExecute(Block const& _block)
{
    Sealing workingSealing;
    try
    {
        execBlock(workingSealing, _block);
    }
    catch (std::exception& e)
    {
        RAFTENGINE_LOG(WARNING) << "[#checkAndExecute]Block execute failed"
                                << "[EINFO=" << boost::diagnostic_information(e) << "]";
        return false;
    }

    checkAndSave(workingSealing);
    return true;
}

void RaftEngine::execBlock(Sealing& _sealing, Block const& _block)
{
    Block working_block(_block);
    RAFTENGINE_LOG(DEBUG) << "[#execBlock]"
                          << "[number=" << working_block.header().number()
                          << ",hash=" << working_block.header().hash() << "]";

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
    ReadGuard guard(m_minerListMutex);
    if (m_minerList != _block.blockHeader().sealerList())
    {
        std::string miners;
        for (auto miner : m_minerList)
            miners += toHex(miner) + "/";

        std::string blockMiners;
        for (auto miner : _block.blockHeader().sealerList())
        {
            blockMiners += toHex(miner) + "/";
        }

        RAFTENGINE_LOG(ERROR) << "[#checkMinerList]Wrong miners"
                              << "[miners=" << miners << ",blockMiners" << blockMiners << "]";
        BOOST_THROW_EXCEPTION(
            BlockMinerListWrong() << errinfo_comment("Wrong miner list of block"));
    }
}

void RaftEngine::checkAndSave(Sealing& _sealing)
{
    // callback block chain to commit block
    CommitResult ret = m_blockChain->commitBlock(_sealing.block, _sealing.p_execContext);
    if (ret == CommitResult::OK)
    {
        RAFTENGINE_LOG(DEBUG) << "[#checkAndSave]Commit block succ";
        // drop handled transactions
        dropHandledTransactions(_sealing.block);
    }
    else
    {
        RAFTENGINE_LOG(ERROR) << "[#checkAndSave]Commit block failed"
                              << "[highestNum=" << m_highestBlock.number()
                              << ",sealingNum=" << _sealing.block.blockHeader().number()
                              << ",SealingHash=" << _sealing.block.blockHeader().hash().abridged()
                              << "]";
        /// note blocksync to sync
        // m_blockSync->noteSealingBlockNumber(m_blockChain->number());
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