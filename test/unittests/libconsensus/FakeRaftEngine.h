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
 * @brief:
 * @file: FakeRaftEngine.cpp
 * @author: catli
 * @date: 2019-01-16
 */
#include <libconsensus/raft/RaftEngine.h>
#include <chrono>
#include <thread>

namespace bcos
{
namespace test
{
class FakeRaftEngine : public bcos::consensus::RaftEngine
{
public:
    FakeRaftEngine(std::shared_ptr<bcos::p2p::P2PInterface> _service,
        std::shared_ptr<bcos::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<bcos::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<bcos::sync::SyncInterface> _blockSync,
        std::shared_ptr<bcos::blockverifier::BlockVerifierInterface> _blockVerifier,
        KeyPair const& _keyPair, unsigned _minElectTime, unsigned _maxElectTime,
        bcos::PROTOCOL_ID const& _protocolId, bcos::h512s const& _sealerList = bcos::h512s())
      : RaftEngine(_service, _txPool, _blockChain, _blockSync, _blockVerifier, _keyPair,
            _minElectTime, _maxElectTime, _protocolId, _sealerList)
    {
        m_blockFactory = std::make_shared<bcos::protocol::BlockFactory>();
    }

    /*
    void updateSealerList()
    {
        if (m_highestBlock.number() == m_lastObtainSealerNum)
            return;
        m_lastObtainSealerNum = m_highestBlock.number();
    }
    */
    void updateConsensusNodeList() override {}

    bcos::p2p::P2PMessage::Ptr generateVoteReq()
    {
        return bcos::consensus::RaftEngine::generateVoteReq();
    }

    bcos::p2p::P2PMessage::Ptr generateHeartbeat()
    {
        return bcos::consensus::RaftEngine::generateHeartbeat();
    }

    bcos::consensus::IDXTYPE getIdx() { return m_idx; }

    bcos::consensus::RaftMsgQueue& msgQueue() { return m_msgQueue; }

    void onRecvRaftMessage(bcos::p2p::NetworkException _exception,
        bcos::p2p::P2PSession::Ptr _session, bcos::p2p::P2PMessage::Ptr _message)
    {
        return bcos::consensus::RaftEngine::onRecvRaftMessage(_exception, _session, _message);
    }

    ssize_t getIndexBySealer(bcos::h512 const& _nodeId)
    {
        return bcos::consensus::RaftEngine::getIndexBySealer(_nodeId);
    }

    bool getNodeIdByIndex(h512& _nodeId, const u256& _nodeIdx) const
    {
        return bcos::consensus::RaftEngine::getNodeIdByIndex(_nodeId, _nodeIdx);
    }

    void setState(bcos::consensus::RaftRole _role) { m_state = _role; }
    void setTerm(size_t _term) { m_term = _term; }
    void setVote(bcos::consensus::raft::NodeIndex _vote)
    {
        bcos::consensus::RaftEngine::setVote(_vote);
    }
    void setFirstVote(bcos::consensus::raft::NodeIndex _firstVote) { m_firstVote = _firstVote; }
    void setLastLeaderTerm(size_t _term) { m_lastLeaderTerm = _term; }
    void setUncommitedBlock(bcos::protocol::Block const& _block) { m_uncommittedBlock = _block; }
    void setUncommitedNumber(int64_t _number) { m_uncommittedBlockNumber = _number; }
    void setConsensusBlockNumber(int64_t _number) { m_consensusBlockNumber = _number; }
    void setLeader(bcos::consensus::raft::NodeIndex _leader)
    {
        bcos::consensus::RaftEngine::setLeader(_leader);
    }


    bcos::protocol::Block& getUncommitedBlock() { return m_uncommittedBlock; }

    std::shared_ptr<bcos::blockchain::BlockChainInterface> getBlockChain() { return m_blockChain; }

    bcos::protocol::BlockHeader getHighestBlock()
    {
        return bcos::consensus::RaftEngine::getHighestBlock();
    }

    bcos::consensus::RaftRole getState() const { return bcos::consensus::RaftEngine::getState(); }

    bool runAsLeaderImp(std::unordered_map<bcos::h512, unsigned>& memberHeartbeatLog)
    {
        return bcos::consensus::RaftEngine::runAsLeaderImp(memberHeartbeatLog);
    }

    bool runAsFollowerImp() { return bcos::consensus::RaftEngine::runAsFollowerImp(); }

    bool runAsCandidateImp(bcos::consensus::VoteState _voteState)
    {
        return bcos::consensus::RaftEngine::runAsCandidateImp(_voteState);
    }

    bool checkHeartbeatTimeout() override
    {
        bcos::consensus::RaftEngine::checkHeartbeatTimeout();
        return heartbeatTimeout;
    }

    bool checkElectTimeout() override
    {
        bcos::consensus::RaftEngine::checkElectTimeout();
        return electTimeout;
    }

    bool handleHeartbeat(
        bcos::u256 const& _from, bcos::h512 const& _node, bcos::consensus::RaftHeartBeat const& _hb)
    {
        return bcos::consensus::RaftEngine::handleHeartbeat(_from, _node, _hb);
    }

    bool handleVoteRequest(
        bcos::u256 const& _from, bcos::h512 const& _node, bcos::consensus::RaftVoteReq const& _req)
    {
        return bcos::consensus::RaftEngine::handleVoteRequest(_from, _node, _req);
    }

    bcos::consensus::HandleVoteResult handleVoteResponse(bcos::u256 const& _from,
        bcos::h512 const& _node, bcos::consensus::RaftVoteResp const& _resp,
        bcos::consensus::VoteState& vote)
    {
        return bcos::consensus::RaftEngine::handleVoteResponse(_from, _node, _resp, vote);
    }

    void reportBlock(bcos::protocol::Block const& _block) override
    {
        bcos::consensus::RaftEngine::reportBlock(_block);
    }

    void taskProcessLoop() override {}

    void fakeLeader()
    {
        while (true)
        {
            std::unique_lock<std::mutex> ul(m_commitMutex);
            if (m_waitingForCommitting)
            {
                RAFTENGINE_LOG(DEBUG) << LOG_DESC(
                    "[#tryCommitUncommitedBlock]Some thread waiting on "
                    "commitCV, commit by other thread");

                m_commitReady = true;
                ul.unlock();
                m_commitCV.notify_all();
                break;
            }
            else
            {
                ul.unlock();
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    void tryCommitUncommitedBlock(bcos::consensus::RaftHeartBeatResp& _resp)
    {
        bcos::consensus::RaftEngine::tryCommitUncommitedBlock(_resp);
    }

    bool heartbeatTimeout = false;
    bool electTimeout = false;
};
}  // namespace test
}  // namespace bcos
