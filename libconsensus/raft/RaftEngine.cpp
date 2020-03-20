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
#include <libconfig/GlobalConfigure.h>
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
    m_lastElectTime = std::chrono::steady_clock::now();
    RAFTENGINE_LOG(TRACE) << LOG_DESC("[#resetElectTimeout]Reset elect timeout and last elect time")
                          << LOG_KV("electTimeout", m_electTimeout);
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
        m_lastElectTime = std::chrono::steady_clock::now();
        m_lastHeartbeatTime = m_lastElectTime;
        m_heartbeatTimeout = m_minElectTimeout;
        m_heartbeatInterval = m_heartbeatTimeout / RaftEngine::s_heartBeatIntervalRatio;
        m_increaseTime = (m_maxElectTimeout - m_minElectTimeout) / 4;
    }

    resetElectTimeout();
    std::srand(static_cast<unsigned>(utcTime()));

    RAFTENGINE_LOG(INFO) << LOG_DESC("[#initRaftEnv]Raft init env success");
}

void RaftEngine::resetConfig()
{
    updateMaxBlockTransactions();
    updateConsensusNodeList();

    auto shouldSwitchToFollower = false;
    {
        Guard guard(m_mutex);
        auto nodeNum = m_sealerList.size();
        if (nodeNum == 0)
        {
            RAFTENGINE_LOG(WARNING)
                << LOG_DESC("[#resetConfig]Reset config error: no sealer, stop sealing");
            m_cfgErr = true;
            return;
        }

        auto iter = std::find(m_sealerList.begin(), m_sealerList.end(), m_keyPair.pub());
        if (iter == m_sealerList.end())
        {
            RAFTENGINE_LOG(TRACE) << LOG_DESC(
                "[#resetConfig]Reset config error: can't find myself in "
                "sealer list, stop sealing");
            m_cfgErr = true;
            m_accountType = NodeAccountType::ObserverAccount;
            return;
        }

        m_accountType = NodeAccountType::SealerAccount;
        auto nodeIdx = iter - m_sealerList.begin();
        if (nodeNum != m_nodeNum || nodeIdx != m_idx)
        {
            m_nodeNum = nodeNum;
            m_idx = nodeIdx;
            m_f = (m_nodeNum - 1) / 2;
            shouldSwitchToFollower = true;
            RAFTENGINE_LOG(INFO) << LOG_DESC("[#resetConfig]Reset config")
                                 << LOG_KV("nodeNum", m_nodeNum) << LOG_KV("idx", m_idx)
                                 << LOG_KV("f", m_f);
        }

        m_cfgErr = false;
    }

    if (shouldSwitchToFollower)
    {
        switchToFollower(InvalidIndex);
        resetElectTimeout();
    }
}

void RaftEngine::start()
{
    initRaftEnv();
    ConsensusEngineBase::start();
    RAFTENGINE_LOG(INFO) << LOG_DESC("[#start]Raft engine started")
                         << LOG_KV("consensusStatus", consensusStatus());
}

void RaftEngine::stop()
{
    // remove the registered handler when stop the pbftEngine
    if (m_service)
    {
        m_service->removeHandlerByProtocolID(m_protocolId);
    }
    ConsensusEngineBase::stop();
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
            m_lastBlockTime = utcSteadyTime();
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
        RAFTENGINE_LOG(INFO) << LOG_DESC("[#reportBlock]^^^^^^^^Report Block")
                             << LOG_KV("num", m_highestBlock.number())
                             << LOG_KV("sealer", m_highestBlock.sealer())
                             << LOG_KV("hash", m_highestBlock.hash().abridged())
                             << LOG_KV("next", m_consensusBlockNumber)
                             << LOG_KV("txNum", _block.getTransactionSize())
                             << LOG_KV("blockTime", m_lastBlockTime);
    }
}

bool RaftEngine::isValidReq(P2PMessage::Ptr _message, P2PSession::Ptr _session, ssize_t& _peerIndex)
{
    /// check whether message is empty
    if (_message->buffer()->size() <= 0)
        return false;
    /// check whether in the sealer list
    _peerIndex = getIndexBySealer(_session->nodeID());
    if (_peerIndex < 0)
    {
        RAFTENGINE_LOG(WARNING) << LOG_DESC("[#isValidReq]Recv Raft msg from unknown peer")
                                << LOG_KV("peerNodeId", _session->nodeID());
        return false;
    }
    /// check whether this node is in the sealer list
    h512 nodeId;
    bool isSealer = getNodeIdByIndex(nodeId, nodeIdx());
    if (!isSealer || _session->nodeID() == nodeId)
    {
        RAFTENGINE_LOG(WARNING) << LOG_DESC("[#isValidReq]I'm not a sealer");
        return false;
    }
    return true;
}

ssize_t RaftEngine::getIndexBySealer(dev::h512 const& _nodeId)
{
    ReadGuard guard(m_sealerListMutex);
    ssize_t index = -1;
    for (size_t i = 0; i < m_sealerList.size(); ++i)
    {
        if (m_sealerList[i] == _nodeId)
        {
            index = i;
            break;
        }
    }
    return index;
}

bool RaftEngine::getNodeIdByIndex(h512& _nodeId, const u256& _nodeIdx) const
{
    _nodeId = getSealerByIndex(_nodeIdx.convert_to<size_t>());
    if (_nodeId == h512())
    {
        RAFTENGINE_LOG(WARNING) << LOG_DESC("[#getNodeIdByIndex]Invalid node idx")
                                << LOG_KV("nodeIdx", _nodeIdx);
        return false;
    }
    return true;
}

void RaftEngine::onRecvRaftMessage(dev::p2p::NetworkException, dev::p2p::P2PSession::Ptr _session,
    dev::p2p::P2PMessage::Ptr _message)
{
    RaftMsgPacket raftMsg;

    bool valid = decodeToRequests(raftMsg, _message, _session);
    if (!valid)
    {
        RAFTENGINE_LOG(WARNING) << LOG_DESC("[#onRecvRaftMessage]Invalid message");
        return;
    }

    if (raftMsg.packetType < RaftPacketType::RaftPacketCount)
    {
        m_msgQueue.push(raftMsg);
    }
    else
    {
        RAFTENGINE_LOG(WARNING) << LOG_DESC("[#onRecvRaftMessage]Illegal message type")
                                << LOG_KV("msgType", raftMsg.packetType)
                                << LOG_KV("fromIP", raftMsg.endpoint);
    }
}

void RaftEngine::workLoop()
{
    while (isWorking())
    {
        auto isSyncing = m_blockSync->isSyncing();
        if (isSyncing)
        {
            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#workLoop]work loop suspend due to syncing");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        resetConfig();

        if (m_cfgErr || m_accountType != NodeAccountType::SealerAccount)
        {
            RAFTENGINE_LOG(DEBUG) << LOG_DESC(
                                         "[#workLoop]work loop suspend due to disturbing config")
                                  << LOG_KV("cfgError", m_cfgErr)
                                  << LOG_KV("accountType", m_accountType);
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
            RAFTENGINE_LOG(WARNING)
                << LOG_DESC("[#workLoop]Unknown work state") << LOG_KV("state", m_state);
            break;
        }
        }
    }
}

void RaftEngine::tryCommitUncommitedBlock(RaftHeartBeatResp& _resp)
{
    std::unique_lock<std::mutex> ul(m_commitMutex);
    if (bool(m_uncommittedBlock))
    {
        auto uncommitedBlockHash = m_uncommittedBlock.header().hash();
        if (m_uncommittedBlockNumber == m_consensusBlockNumber)
        {
            if (_resp.uncommitedBlockHash != h256() &&
                _resp.uncommitedBlockHash == uncommitedBlockHash)
            {
                // Collect ack from follower
                // ensure that the block has been transfered to most of followers
                m_commitFingerPrint[uncommitedBlockHash].insert(_resp.idx);
                if (m_commitFingerPrint[uncommitedBlockHash].size() >=
                    static_cast<uint64_t>(m_nodeNum - m_f))
                {
                    if (m_waitingForCommitting)
                    {
                        RAFTENGINE_LOG(TRACE) << LOG_DESC(
                            "[#tryCommitUncommitedBlock]Some thread waiting on "
                            "commitCV, commit by other thread");

                        m_commitReady = true;
                        ul.unlock();
                        m_commitCV.notify_all();
                    }
                    else
                    {
                        RAFTENGINE_LOG(TRACE) << LOG_DESC(
                            "[#tryCommitUncommitedBlock]No thread waiting on "
                            "commitCV, commit by meself");
                        ul.unlock();
                        checkAndExecute(m_uncommittedBlock);
                        reportBlock(m_uncommittedBlock);
                    }
                }
            }
            else
            {
                if (_resp.uncommitedBlockHash == h256())
                {
                    // I'm the only one in sealer list, commit block without any ack
                    if (m_waitingForCommitting)
                    {
                        RAFTENGINE_LOG(TRACE) << LOG_DESC(
                            "[#tryCommitUncommitedBlock]Some thread waiting on "
                            "commitCV, commit by other thread");

                        m_commitReady = true;
                        ul.unlock();
                        m_commitCV.notify_all();
                    }
                    else
                    {
                        RAFTENGINE_LOG(TRACE) << LOG_DESC(
                            "[#tryCommitUncommitedBlock]No thread waiting on "
                            "commitCV, commit by meself");

                        ul.unlock();
                        checkAndExecute(m_uncommittedBlock);
                        reportBlock(m_uncommittedBlock);
                    }
                }
                else
                {
                    // Stale or illegal ack message receieved
                    RAFTENGINE_LOG(TRACE)
                        << LOG_DESC("[#tryCommitUncommitedBlock]Uneuqal fingerprint")
                        << LOG_KV("ackFingerprint", _resp.uncommitedBlockHash)
                        << LOG_KV("myFingerprint", uncommitedBlockHash);
                }
            }
        }
        else
        {
            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#tryCommitUncommitedBlock]Give up uncommited block")
                                  << LOG_KV("uncommittedBlockNumber", m_uncommittedBlockNumber)
                                  << LOG_KV("myHeight", m_highestBlock.number());

            m_uncommittedBlock = Block();
            m_uncommittedBlockNumber = 0;
        }
    }
    else
    {
        RAFTENGINE_LOG(TRACE) << LOG_DESC("[#tryCommitUncommitedBlock]No uncommited block");
        ul.unlock();
    }
}

bool RaftEngine::runAsLeaderImp(std::unordered_map<h512, unsigned>& memberHeartbeatLog)
{
    if (m_state != RaftRole::EN_STATE_LEADER || m_accountType != NodeAccountType::SealerAccount)
    {
        return false;
    }

    // heartbeat timeout, change role to candidate
    if (m_nodeNum > 1 && checkHeartbeatTimeout())
    {
        RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#runAsLeaderImp]Heartbeat Timeout");
        for (auto& i : memberHeartbeatLog)
        {
            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#runAsLeaderImp]Heartbeat Log")
                                  << LOG_KV("node", i.first.hex().substr(0, 5))
                                  << LOG_KV("hbLog", i.second);
        }
        switchToCandidate();
        return false;
    }

    if (m_nodeNum > 1)
    {
        broadcastHeartbeat();

        std::pair<bool, RaftMsgPacket> ret = m_msgQueue.tryPop(c_PopWaitSeconds);

        if (!ret.first)
        {
            return true;
        }

        switch (ret.second.packetType)
        {
        case RaftPacketType::RaftVoteReqPacket:
        {
            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#runAsLeaderImp]Recv vote req packet");

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
            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#runAsLeaderImp]Recv vote resp packet");

            /// do nothing
            return true;
        }
        case RaftPacketType::RaftHeartBeatPacket:
        {
            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#runAsLeaderImp]Recv heartbeat packet");

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

            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#runAsLeaderImp]Recv heartbeat ack")
                                  << LOG_KV("from", ret.second.nodeId)
                                  << LOG_KV("peerHeight", resp.height)
                                  << LOG_KV("peerBlockHash", toString(resp.blockHash));
            /// receive strange term
            if (resp.term != m_term)
            {
                RAFTENGINE_LOG(TRACE) << LOG_DESC("[#runAsLeaderImp]Heartbeat ack term is strange")
                                      << LOG_KV("ackTerm", resp.term) << LOG_KV("myTerm", m_term);
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
                    RAFTENGINE_LOG(TRACE)
                        << LOG_DESC("[#runAsLeaderImp]Collect heartbeat resp exceed half");

                    m_lastHeartbeatReset = std::chrono::steady_clock::now();
                    for_each(memberHeartbeatLog.begin(), memberHeartbeatLog.end(),
                        [](std::pair<const h512, unsigned>& item) {
                            if (item.second > 0)
                                --item.second;
                        });

                    RAFTENGINE_LOG(TRACE) << LOG_DESC("[#runAsLeaderImp]Heartbeat timeout reset");
                }
            }

            tryCommitUncommitedBlock(resp);
            return true;
        }
        default:
        {
            return true;
        }
        }
    }
    else
    {
        RaftHeartBeatResp resp;
        tryCommitUncommitedBlock(resp);
        return true;
    }
}

void RaftEngine::runAsLeader()
{
    m_firstVote = InvalidIndex;
    m_lastLeaderTerm = m_term;
    m_lastHeartbeatReset = m_lastHeartbeatTime = std::chrono::steady_clock::now();
    std::unordered_map<h512, unsigned> memberHeartbeatLog;

    while (isWorking())
    {
        auto isSyncing = m_blockSync->isSyncing();
        if (isSyncing)
        {
            break;
        }

        if (!runAsLeaderImp(memberHeartbeatLog))
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool RaftEngine::runAsCandidateImp(VoteState& _voteState)
{
    if (m_state != RaftRole::EN_STATE_CANDIDATE || m_accountType != NodeAccountType::SealerAccount)
    {
        return false;
    }

    if (checkElectTimeout())
    {
        RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#runAsCandidateImp]VoteState")
                              << LOG_KV("vote", _voteState.vote)
                              << LOG_KV("unVote", _voteState.unVote)
                              << LOG_KV("lastTermErr", _voteState.lastTermErr)
                              << LOG_KV("firstVote", _voteState.firstVote)
                              << LOG_KV("discardedVote", _voteState.discardedVote);

        if (isMajorityVote(_voteState.totalVoteCount()))
        {
            RAFTENGINE_LOG(TRACE) << LOG_DESC(
                "[#runAsCandidateImp]Candidate campaign leader time out");
            switchToCandidate();
        }
        else
        {
            /// not receive enough vote
            RAFTENGINE_LOG(DEBUG)
                << LOG_DESC("[#runAsCandidateImp]Not enough vote received, recover term")
                << LOG_KV("currentTerm", m_term) << LOG_KV("toTerm", m_term - 1);
            increaseElectTime();
            /// recover to previous term
            m_term--;
            RAFTENGINE_LOG(TRACE) << "[#runAsCandidateImp]Switch to Follower";
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
            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#runAsCandidateImp]Recv vote req packet");

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

            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#runAsCandidateImp]Recv vote response packet")
                                  << LOG_KV("respTerm", resp.term)
                                  << LOG_KV("voteFlag", resp.voteFlag)
                                  << LOG_KV("from", ret.second.nodeIdx)
                                  << LOG_KV("node", ret.second.nodeId.hex().substr(0, 5));

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
            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#runAsCandidateImp]Recv heartbeat packet");

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
    if (m_state != RaftRole::EN_STATE_CANDIDATE || m_accountType != NodeAccountType::SealerAccount)
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
        RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#runAsCandidate]Won election, switch to leader now");
        switchToLeader();
        return;
    }

    while (isWorking())
    {
        auto isSyncing = m_blockSync->isSyncing();
        if (isSyncing)
        {
            break;
        }

        if (!runAsCandidateImp(voteState))
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool RaftEngine::runAsFollowerImp()
{
    if (m_state != RaftRole::EN_STATE_FOLLOWER || m_accountType != NodeAccountType::SealerAccount)
    {
        return false;
    }

    RAFTENGINE_LOG(TRACE) << LOG_DESC("[#runAsFollowerImp]") << LOG_KV("currentLeader", m_leader);

    if (checkElectTimeout())
    {
        RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#runAsFollowerImp]Elect timeout, switch to Candidate");
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
            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#runAsFollowerImp]Recv vote req packet");

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
            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#runAsFollowerImp]Recv vote resp packet");

            // do nothing
            return true;
        }
        case RaftHeartBeatPacket:
        {
            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#runAsFollowerImp]Recv heartbeat packet");

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
    while (isWorking())
    {
        auto isSyncing = m_blockSync->isSyncing();
        if (isSyncing)
        {
            break;
        }

        if (!runAsFollowerImp())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool RaftEngine::checkHeartbeatTimeout()
{
    steady_clock::time_point nowTime = steady_clock::now();
    auto interval = duration_cast<milliseconds>(nowTime - m_lastHeartbeatReset).count();

    RAFTENGINE_LOG(TRACE) << LOG_DESC("[#checkHeartbeatTimeout]") << LOG_KV("interval", interval)
                          << LOG_KV("heartbeatTimeout", m_heartbeatTimeout);

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
            m_uncommittedBlock.encode(hb.uncommitedBlock);
            hb.uncommitedBlockNumber = m_consensusBlockNumber;
            m_commitFingerPrint[m_uncommittedBlock.header().hash()].insert(m_idx);

            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#generateHeartbeat]Has uncommited block")
                                  << LOG_KV("nextBlockNumber", hb.uncommitedBlockNumber);
        }
        else
        {
            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#generateHeartbeat]No uncommited block");

            hb.uncommitedBlock = bytes();
            hb.uncommitedBlockNumber = 0;
        }
    }

    RLPStream ts;
    hb.streamRLPFields(ts);
    auto heartbeatMsg =
        transDataToMessage(ref(ts.out()), RaftPacketType::RaftHeartBeatPacket, m_protocolId);

    RAFTENGINE_LOG(TRACE) << LOG_DESC("[#generateHeartbeat]Heartbeat message generated")
                          << LOG_KV("term", hb.term) << LOG_KV("leader", hb.leader);
    return heartbeatMsg;
}

void RaftEngine::broadcastHeartbeat()
{
    std::chrono::steady_clock::time_point nowTime = std::chrono::steady_clock::now();
    auto interval =
        std::chrono::duration_cast<std::chrono::milliseconds>(nowTime - m_lastHeartbeatTime)
            .count();
    if (interval >= m_heartbeatInterval)
    {
        m_lastHeartbeatTime = nowTime;
        auto heartbeatMsg = generateHeartbeat();
        broadcastMsg(heartbeatMsg);
        clearFirstVoteCache();
        RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#broadcastHeartbeat]Heartbeat broadcasted");
    }
    else
    {
        RAFTENGINE_LOG(TRACE) << LOG_DESC("[#broadcastHeartbeat]Too fast to broadcast heartbeat");
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

    RAFTENGINE_LOG(TRACE) << LOG_DESC("[#generateVoteReq]VoteReq message generated")
                          << LOG_KV("term", req.term)
                          << LOG_KV("lastLeaderTerm", req.lastLeaderTerm)
                          << LOG_KV("vote", req.candidate)
                          << LOG_KV("lastBlockNumber", req.lastBlockNumber);

    return voteReqMsg;
}

void RaftEngine::broadcastVoteReq()
{
    auto voteReqMsg = generateVoteReq();

    if (voteReqMsg)
    {
        broadcastMsg(voteReqMsg);
        RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#broadcastVoteReq]VoteReq broadcasted");
    }
    else
    {
        RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#broadcastVoteReq]Failed to broadcast VoteReq");
    }
}

P2PMessage::Ptr RaftEngine::transDataToMessage(
    bytesConstRef _data, RaftPacketType const& _packetType, PROTOCOL_ID const& _protocolId)
{
    dev::p2p::P2PMessage::Ptr message = std::dynamic_pointer_cast<dev::p2p::P2PMessage>(
        m_service->p2pMessageFactory()->buildMessage());
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
    m_connectedNode = sessions.size();
    for (auto session : sessions)
    {
        if (getIndexBySealer(session.nodeID()) < 0)
        {
            continue;
        }

        m_service->asyncSendMessageByNodeID(session.nodeID(), _data, nullptr);
        RAFTENGINE_LOG(TRACE) << LOG_DESC("[#broadcastMsg]Raft msg sent")
                              << LOG_KV("peer", session.nodeID());
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
            RAFTENGINE_LOG(DEBUG) << LOG_DESC(
                "[#clearFirstVoteCache]Broadcast or receive enough hb "
                "package, clear first vote cache");
        }
    }
}

bool RaftEngine::handleVoteRequest(u256 const& _from, h512 const& _node, RaftVoteReq const& _req)
{
    RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#handleVoteRequest]") << LOG_KV("from", _from)
                          << LOG_KV("node", _node.hex().substr(0, 5)) << LOG_KV("term", _req.term)
                          << LOG_KV("candidate", _req.candidate);

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
            RAFTENGINE_LOG(DEBUG)
                << LOG_DESC("[#handleVoteRequest]Discard vreq for I'm the bigger leader")
                << LOG_KV("myTerm", m_term);
        }
        else
        {
            if (_req.term == m_term)
            {
                // _req.term == m_term for follower and candidate
                resp.voteFlag = VOTE_RESP_DISCARD;
                RAFTENGINE_LOG(DEBUG)
                    << LOG_DESC("[#handleVoteRequest]Discard vreq for I'm already in this term")
                    << LOG_KV("myTerm", m_term);
            }
            else
            {
                // _req.term < m_term for follower and candidate
                resp.voteFlag = VOTE_RESP_REJECT;
                RAFTENGINE_LOG(DEBUG)
                    << LOG_DESC("[#handleVoteRequest]Discard vreq for smaller term")
                    << LOG_KV("myTerm", m_term);
            }
            sendResponse(_from, _node, RaftVoteRespPacket, resp);
            return false;
        }
    }

    // handle lastLeaderTerm error
    if (_req.lastLeaderTerm < m_lastLeaderTerm)
    {
        RAFTENGINE_LOG(DEBUG)
            << LOG_DESC("[#handleVoteRequest]Discard vreq for smaller last leader term")
            << LOG_KV("myLastLeaderTerm", m_lastLeaderTerm)
            << LOG_KV("reqLastLeaderTerm", _req.lastLeaderTerm);

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
        RAFTENGINE_LOG(DEBUG)
            << LOG_DESC("[#handleVoteRequest]Discard vreq for peer's data is older than me")
            << LOG_KV("myBlockNumber", currentBlockNumber)
            << LOG_KV("reqBlockNumber", _req.lastBlockNumber);

        resp.voteFlag = VOTE_RESP_OUTDATED;
        sendResponse(_from, _node, RaftVoteRespPacket, resp);
        return false;
    }

    // first vote, not change term
    if (m_firstVote == InvalidIndex)
    {
        RAFTENGINE_LOG(DEBUG) << LOG_DESC(
            "[#handleVoteRequest]Discard vreq for I'm the first time to vote");

        m_firstVote = _req.candidate;
        resp.voteFlag = VOTE_RESP_FIRST_VOTE;
        sendResponse(_from, _node, RaftVoteRespPacket, resp);
        return false;
    }

    RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#handleVoteRequest]Grant vreq");

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
    std::chrono::steady_clock::time_point nowTime = std::chrono::steady_clock::now();
    return nowTime - m_lastElectTime >= std::chrono::milliseconds(m_electTimeout);
}

bool RaftEngine::handleHeartbeat(u256 const& _from, h512 const& _node, RaftHeartBeat const& _hb)
{
    RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#handleHeartbeat]") << LOG_KV("fromIdx", _from)
                          << LOG_KV("fromId", _node.hex().substr(0, 5))
                          << LOG_KV("hbTerm", _hb.term) << LOG_KV("hbLeader", _hb.leader);

    if (_hb.term < m_term && _hb.term <= m_lastLeaderTerm)
    {
        RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#handleHeartbeat]Discard hb for smaller term")
                              << LOG_KV("myTerm", m_term) << LOG_KV("hbTerm", _hb.term)
                              << LOG_KV("myLastLeaderTerm", m_lastLeaderTerm);
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
            RAFTENGINE_LOG(WARNING)
                << LOG_DESC("[#handleHeartbeat]Leader's height is not equal to mine")
                << LOG_KV("leaderNextHeight", _hb.uncommitedBlockNumber)
                << LOG_KV("myHeight", m_highestBlock.number());

            return false;
        }
    }
    sendResponse(_from, _node, RaftPacketType::RaftHeartBeatRespPacket, resp);

    bool stepDown = false;
    /// _hb.term >= m_term || _hb.lastLeaderTerm > m_lastLeaderTerm
    /// receive larger lastLeaderTerm, recover my term to hb term, set self to next step (follower)
    if (_hb.term > m_lastLeaderTerm)
    {
        RAFTENGINE_LOG(DEBUG)
            << LOG_DESC(
                   "[#handleHeartbeat]Prepare to switch to follower due to last leader term error")
            << LOG_KV("lastLeaderTerm", m_lastLeaderTerm) << LOG_KV("hbLastLeader", _hb.term);

        m_term = _hb.term;
        m_vote = InvalidIndex;
        stepDown = true;
    }

    if (_hb.term > m_term)
    {
        RAFTENGINE_LOG(DEBUG)
            << LOG_DESC(
                   "[#handleHeartbeat]Prepare to switch to follower due to receive higher term")
            << LOG_KV("term", m_term) << LOG_KV("hbTerm", _hb.term);

        m_term = _hb.term;
        m_vote = InvalidIndex;
        stepDown = true;
    }

    if (m_state == EN_STATE_CANDIDATE && _hb.term >= m_term)
    {
        RAFTENGINE_LOG(DEBUG)
            << LOG_DESC(
                   "[#handleHeartbeat]Prepare to switch to follower due to receive "
                   "higher or equal term in candidate state")
            << LOG_KV("myTerm", m_term) << LOG_KV("hbTerm", _hb.term);

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
    RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#recoverElectTime]Reset elect time to init")
                          << LOG_KV("minElectTimeout", m_minElectTimeout)
                          << LOG_KV("maxElectTimeout", m_maxElectTimeout);
}

void RaftEngine::switchToLeader()
{
    {
        Guard guard(m_mutex);
        m_leader = m_idx;
        m_state = EN_STATE_LEADER;
    }

    recoverElectTime();
    RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#switchToLeader]") << LOG_KV("currentTerm", m_term);
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
        RAFTENGINE_LOG(DEBUG) << LOG_DESC(
            "[#switchToFollower]Some thread still waiting on "
            "commitCV, need to wake up and cleanup uncommited block buffer");

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
    RAFTENGINE_LOG(INFO) << LOG_DESC("[#switchToFollower]") << LOG_KV("currentTerm", m_term);
}

void RaftEngine::switchToCandidate()
{
    resetConfig();
    {
        Guard guard(m_mutex);
        m_term++;
        m_leader = InvalidIndex;
        m_state = RaftRole::EN_STATE_CANDIDATE;
    }
    resetElectTimeout();
    RAFTENGINE_LOG(INFO) << LOG_DESC("[#switchToCandidate]") << LOG_KV("currentTerm", m_term);
}

bool RaftEngine::sendResponse(
    u256 const& _to, h512 const& _node, RaftPacketType _packetType, RaftMsg const& _msg)
{
    RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#sendResponse]Ready to send response") << LOG_KV("to", _to)
                          << LOG_KV("term", _msg.term) << LOG_KV("packetType", _packetType);

    RLPStream ts;
    _msg.streamRLPFields(ts);

    auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    for (auto session : sessions)
    {
        if (session.nodeID() != _node || getIndexBySealer(session.nodeID()) < 0)
        {
            continue;
        }

        m_service->asyncSendMessageByNodeID(session.nodeID(),
            transDataToMessage(ref(ts.out()), _packetType, m_protocolId), nullptr);
        RAFTENGINE_LOG(TRACE) << LOG_DESC("[#sendResponse]Response sent");
        return true;
    }
    return false;
}

HandleVoteResult RaftEngine::handleVoteResponse(
    u256 const& _from, h512 const& _node, RaftVoteResp const& _resp, VoteState& _vote_state)
{
    if (_resp.term < m_term - 1)
    {
        RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#handleVoteResponse]Peer's term is smaller than mine")
                              << LOG_KV("respTerm", _resp.term) << LOG_KV("myTerm", m_term);
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
                << LOG_DESC("[#handleVoteResponse]Receive majority first vote, recover term")
                << LOG_KV("currentTerm", m_term) << LOG_KV("toTerm", m_term - 1);
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
        RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#handleVoteResponse]Error voteFlag")
                              << LOG_KV("voteFlag", _resp.voteFlag) << LOG_KV("from", _from)
                              << LOG_KV("node", _node.hex().substr(0, 5));
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
    RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#increaseElectTime]Increase elect time")
                          << LOG_KV("minElectTimeout", m_minElectTimeout)
                          << LOG_KV("maxElectTimeout", m_maxElectTimeout);
}

bool RaftEngine::shouldSeal()
{
    {
        Guard guard(m_mutex);
        if (m_state != EN_STATE_LEADER)
        {
            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#shouldSeal]I'm not the leader");
            return false;
        }

        if (m_cfgErr || m_accountType != NodeAccountType::SealerAccount)
        {
            RAFTENGINE_LOG(TRACE) << LOG_DESC("[#shouldSeal]My state is not well")
                                  << LOG_KV("cfgError", m_cfgErr)
                                  << LOG_KV("accountType", m_accountType);
            return false;
        }

        u256 count = 1;
        u256 currentHeight = m_highestBlock.number();
        h256 currentBlockHash = m_highestBlock.hash();
        for (auto iter = m_memberBlock.begin(); iter != m_memberBlock.end(); ++iter)
        {
            if (iter->second.height > currentHeight)
            {
                RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#shouldSeal]Wait to download block");
                return false;
            }

            if (iter->second.height == currentHeight && iter->second.block_hash == currentBlockHash)
            {
                ++count;
            }
        }

        if (count < m_nodeNum - m_f)
        {
            RAFTENGINE_LOG(INFO) << LOG_DESC("[#shouldSeal]Wait somebody to sync block")
                                 << LOG_KV("count", count) << LOG_KV("nodeNum", m_nodeNum)
                                 << LOG_KV("f", m_f)
                                 << LOG_KV("memberBlockSize", m_memberBlock.size());

            return false;
        }
    }

    {
        Guard guard(m_commitMutex);
        if (bool(m_uncommittedBlock))
        {
            RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#shouldSeal]Wait to commit uncommitted block")
                                  << LOG_KV("uncommittedBlockHeight",
                                         m_uncommittedBlock.header().number())
                                  << LOG_KV("uncommittedBlockHash",
                                         m_uncommittedBlock.header().hash());
            return false;
        }
    }

    RAFTENGINE_LOG(TRACE) << LOG_DESC("[#shouldSeal]Seal granted");
    return true;
}

bool RaftEngine::commit(Block const& _block)
{
    std::unique_lock<std::mutex> ul(m_commitMutex);
    m_uncommittedBlock = _block;
    m_uncommittedBlockNumber = m_consensusBlockNumber;
    m_waitingForCommitting = true;
    m_commitReady = false;
    RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#commit]Wait to commit block")
                          << LOG_KV("nextHeight", m_uncommittedBlockNumber);
    m_commitCV.wait(ul, [this]() { return m_commitReady; });

    m_commitReady = false;
    m_waitingForCommitting = false;

    if (!bool(m_uncommittedBlock))
    {
        ul.unlock();
        return false;
    }

    ul.unlock();

    if (getState() != RaftRole::EN_STATE_LEADER)
    {
        RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#commit]I'm not the leader anymore, stop committing");
        return false;
    }

    RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#commit]Start to commit block");
    return checkAndExecute(_block);
}

bool RaftEngine::checkAndExecute(Block const& _block)
{
    Sealing workingSealing(m_blockFactory);
    try
    {
        execBlock(workingSealing, _block);
    }
    catch (std::exception& e)
    {
        RAFTENGINE_LOG(WARNING) << LOG_DESC("[#checkAndExecute]Block execute failed")
                                << LOG_KV("EINFO", boost::diagnostic_information(e));
        return false;
    }

    checkAndSave(workingSealing);
    return true;
}

void RaftEngine::execBlock(Sealing& _sealing, Block const& _block)
{
    auto working_block = std::make_shared<Block>(_block);
    RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#execBlock]")
                          << LOG_KV("number", working_block->header().number())
                          << LOG_KV("hash", working_block->header().hash().abridged());

    checkBlockValid(*working_block);
    m_blockSync->noteSealingBlockNumber(working_block->header().number());
    _sealing.p_execContext = executeBlock(*working_block);
    _sealing.block = working_block;
}

void RaftEngine::checkBlockValid(dev::eth::Block const& _block)
{
    ConsensusEngineBase::checkBlockValid(_block);
    checkSealerList(_block);
}

void RaftEngine::checkSealerList(Block const& _block)
{
    ReadGuard guard(m_sealerListMutex);
    if (m_sealerList != _block.blockHeader().sealerList())
    {
        std::string sealers;
        for (auto sealer : m_sealerList)
            sealers += toHex(sealer) + "/";

        std::string blockSealers;
        for (auto sealer : _block.blockHeader().sealerList())
        {
            blockSealers += toHex(sealer) + "/";
        }

        RAFTENGINE_LOG(ERROR) << LOG_DESC("[#checkSealerList]Wrong sealers")
                              << LOG_KV("sealers", sealers) << LOG_KV("blockSealers", blockSealers);
        BOOST_THROW_EXCEPTION(
            BlockSealerListWrong() << errinfo_comment("Wrong sealer list of block"));
    }
}

void RaftEngine::checkAndSave(Sealing& _sealing)
{
    // callback block chain to commit block
    std::unique_lock<std::mutex> ul(m_commitMutex);
    CommitResult ret = m_blockChain->commitBlock(_sealing.block, _sealing.p_execContext);
    if (ret == CommitResult::OK)
    {
        m_uncommittedBlock = Block();
        m_uncommittedBlockNumber = 0;
        ul.unlock();
        RAFTENGINE_LOG(DEBUG) << LOG_DESC("[#checkAndSave]Commit block succ");
        // drop handled transactions
        dropHandledTransactions(_sealing.block);
    }
    else
    {
        ul.unlock();
        RAFTENGINE_LOG(ERROR) << LOG_DESC("[#checkAndSave]Commit block failed")
                              << LOG_KV("highestNum", m_highestBlock.number())
                              << LOG_KV("sealingNum", _sealing.block->blockHeader().number())
                              << LOG_KV("sealingHash",
                                     _sealing.block->blockHeader().hash().abridged());
        /// note blocksync to sync
        // m_blockSync->noteSealingBlockNumber(m_blockChain->number());
        m_txPool->handleBadBlock(*(_sealing.block));
    }
}

bool RaftEngine::reachBlockIntervalTime()
{
    auto nowTime = utcSteadyTime();
    auto parentTime = m_lastBlockTime;

    return nowTime - parentTime >= g_BCOSConfig.c_intervalBlockTime;
}

const std::string RaftEngine::consensusStatus()
{
    Json::Value status;
    Json::Value statusObj;
    getBasicConsensusStatus(statusObj);
    // get current leader ID
    h512 leaderId;
    auto isSucc = getNodeIdByIndex(leaderId, m_leader);
    if (isSucc)
    {
        statusObj["leaderId"] = toString(leaderId);
        statusObj["leaderIdx"] = m_leader;
    }
    else
    {
        statusObj["leaderId"] = "get leader ID failed";
        statusObj["leaderIdx"] = "NULL";
    }
    status.append(statusObj);
    Json::FastWriter fastWriter;
    std::string status_str = fastWriter.write(status);
    return status_str;
}
