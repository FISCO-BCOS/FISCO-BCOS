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
 * @brief : header file of Raft consensus engine
 * @file: RaftEngine.h
 * @author: catli
 * @date: 2018-12-05
 */
#pragma once

#include "Common.h"
#include <libblockchain/BlockChainInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libconsensus/ConsensusEngineBase.h>
#include <libethcore/Block.h>
#include <libnetwork/Common.h>
#include <libp2p/P2PInterface.h>
#include <libp2p/P2PMessageFactory.h>
#include <libp2p/P2PSession.h>
#include <libstorage/Storage.h>
#include <libsync/SyncInterface.h>
#include <libtxpool/TxPoolInterface.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace bcos
{
namespace consensus
{
DERIVE_BCOS_EXCEPTION(RaftEngineInitFailed);
DERIVE_BCOS_EXCEPTION(RaftEngineUnexpectError);

class RaftEngine : public ConsensusEngineBase
{
public:
    RaftEngine(std::shared_ptr<bcos::p2p::P2PInterface> _service,
        std::shared_ptr<bcos::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<bcos::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<bcos::sync::SyncInterface> _blockSync,
        std::shared_ptr<bcos::blockverifier::BlockVerifierInterface> _blockVerifier,
        KeyPair const& _keyPair, unsigned _minElectTime, unsigned _maxElectTime,
        bcos::PROTOCOL_ID const& _protocolId, bcos::h512s const& _sealerList = bcos::h512s())
      : ConsensusEngineBase(_service, _txPool, _blockChain, _blockSync, _blockVerifier, _protocolId,
            _keyPair, _sealerList),
        m_minElectTimeout(_minElectTime),
        m_maxElectTimeout(_maxElectTime),
        m_uncommittedBlock(bcos::eth::Block()),
        m_uncommittedBlockNumber(0),
        m_waitingForCommitting(false)
    {
        m_service->registerHandlerByProtoclID(
            m_protocolId, boost::bind(&RaftEngine::onRecvRaftMessage, this, _1, _2, _3));
        m_blockSync->registerConsensusVerifyHandler([](bcos::eth::Block const&) { return true; });
        /// set thread name for raftEngine
        std::string threadName = "Raft-" + std::to_string(m_groupId);
        setName(threadName);
    }

    raft::NodeIndex getNodeIdx() const
    {
        Guard Guard(m_mutex);
        return m_idx;
    }

    size_t getTerm() const
    {
        Guard Guard(m_mutex);
        return m_term;
    }

    bcos::eth::BlockHeader getHighestBlock() const
    {
        Guard Guard(m_mutex);
        return m_highestBlock;
    }

    size_t getLastLeaderTerm() const
    {
        Guard Guard(m_mutex);
        return m_lastLeaderTerm;
    }

    void start() override;
    void stop() override;
    void reportBlock(bcos::eth::Block const& _block) override;
    bool shouldSeal();
    bool commit(bcos::eth::Block const& _block);
    bool reachBlockIntervalTime();
    void resetLastBlockTime() { m_lastBlockTime = bcos::utcSteadyTime(); }
    const std::string consensusStatus() override;

protected:
    void initRaftEnv();
    void onRecvRaftMessage(bcos::p2p::NetworkException _exception,
        bcos::p2p::P2PSession::Ptr _session, bcos::p2p::P2PMessage::Ptr _message);
    bool isValidReq(bcos::p2p::P2PMessage::Ptr _message, bcos::p2p::P2PSession::Ptr _session,
        ssize_t& _peerIndex) override;

    void resetElectTimeout();

    void switchToCandidate();
    void switchToFollower(raft::NodeIndex const& _leader);
    void switchToLeader();

    bool wonElection(u256 const& _votes) { return _votes >= m_nodeNum - m_f; }
    bool isMajorityVote(u256 const& _votes) { return _votes >= m_nodeNum - m_f; }

    bcos::p2p::P2PMessage::Ptr generateVoteReq();
    bcos::p2p::P2PMessage::Ptr generateHeartbeat();

    void broadcastVoteReq();
    void broadcastHeartbeat();
    void broadcastMsg(bcos::p2p::P2PMessage::Ptr _data);

    // handle response
    bool handleVoteRequest(u256 const& _from, h512 const& _node, RaftVoteReq const& _req);
    HandleVoteResult handleVoteResponse(
        u256 const& _from, h512 const& _node, RaftVoteResp const& _resp, VoteState& vote);
    bool handleHeartbeat(u256 const& _from, h512 const& _node, RaftHeartBeat const& _hb);
    bool sendResponse(
        u256 const& _to, h512 const& _node, RaftPacketType _packetType, RaftMsg const& _resp);

    void workLoop() override;
    void resetConfig() override;

    void runAsLeader();
    bool runAsLeaderImp(std::unordered_map<bcos::h512, unsigned>& _memberHeartbeatLog);
    void runAsFollower();
    bool runAsFollowerImp();
    void runAsCandidate();
    bool runAsCandidateImp(bcos::consensus::VoteState& _voteState);

    void tryCommitUncommitedBlock(bcos::consensus::RaftHeartBeatResp& _resp);
    virtual bool checkHeartbeatTimeout();
    virtual bool checkElectTimeout();
    ssize_t getIndexBySealer(bcos::h512 const& _nodeId);
    bool getNodeIdByIndex(h512& _nodeId, const u256& _nodeIdx) const;
    bcos::p2p::P2PMessage::Ptr transDataToMessage(
        bytesConstRef data, RaftPacketType const& packetType, PROTOCOL_ID const& protocolId);

    RaftRole getState() const
    {
        Guard l(m_mutex);
        return m_state;
    }

    void setLeader(raft::NodeIndex const& _leader)
    {
        Guard l(m_mutex);
        m_leader = _leader;
    }

    void setVote(raft::NodeIndex const& _candidate)
    {
        Guard l(m_mutex);
        m_vote = _candidate;
    }

    void increaseElectTime();
    void recoverElectTime();
    void clearFirstVoteCache();

    void execBlock(Sealing& _sealing, bcos::eth::Block const& _block);
    void checkBlockValid(bcos::eth::Block const& _block) override;
    void checkSealerList(bcos::eth::Block const& _block);
    bool checkAndExecute(bcos::eth::Block const& _block);
    void checkAndSave(Sealing& _sealing);

    mutable Mutex m_mutex;

    unsigned m_electTimeout;
    unsigned m_minElectTimeout;
    unsigned m_maxElectTimeout;
    unsigned m_minElectTimeoutInit;
    unsigned m_maxElectTimeoutInit;
    unsigned m_minElectTimeoutBoundary;
    unsigned m_maxElectTimeoutBoundary;
    std::chrono::steady_clock::time_point m_lastElectTime;

    static unsigned const s_heartBeatIntervalRatio = 4;
    unsigned m_heartbeatTimeout;
    unsigned m_heartbeatInterval;
    unsigned m_heartbeatCount = 0;
    std::chrono::steady_clock::time_point m_lastHeartbeatTime;
    std::chrono::steady_clock::time_point m_lastHeartbeatReset;

    unsigned m_increaseTime;

    // message queue, defined in Common.h
    RaftMsgQueue m_msgQueue;
    // role of node
    RaftRole m_state = EN_STATE_FOLLOWER;

    raft::NodeIndex m_firstVote = InvalidIndex;
    size_t m_term = 0;
    size_t m_lastLeaderTerm = 0;

    raft::NodeIndex m_leader;
    raft::NodeIndex m_vote;

    struct BlockRef
    {
        u256 height;
        h256 block_hash;
        BlockRef() : height(0), block_hash(0) {}
        BlockRef(u256 _height, h256 _hash) : height(_height), block_hash(_hash) {}
    };
    std::unordered_map<h512, BlockRef> m_memberBlock;  // <node_id, BlockRef>
    static const unsigned c_PopWaitSeconds = 5;

    // the block number that update the sealer list
    int64_t m_lastObtainSealerNum = 0;
    uint64_t m_lastBlockTime;

    bcos::eth::Block m_uncommittedBlock;
    int64_t m_uncommittedBlockNumber;

    std::mutex m_commitMutex;
    std::condition_variable m_commitCV;
    bool m_commitReady;
    bool m_waitingForCommitting;
    std::unordered_map<h256, std::unordered_set<bcos::consensus::IDXTYPE>> m_commitFingerPrint;

private:
    static typename raft::NodeIndex InvalidIndex;
};
}  // namespace consensus
}  // namespace bcos
