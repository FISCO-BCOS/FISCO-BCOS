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
 * @brief : implementation of PBFT consensus
 * @file: PBFTConsensus.h
 * @author: yujiechen
 * @date: 2018-09-28
 */
#pragma once
#include "PBFTMsgCache.h"
#include "PBFTReqCache.h"
#include <libconsensus/Consensus.h>
#include <libdevcore/FileSystem.h>
#include <libdevcore/LevelDB.h>
#include <libdevcore/concurrent_queue.h>
namespace dev
{
namespace consensus
{
enum CheckResult
{
    VALID = 0,
    INVALID = 1,
    FUTURE = 2
};

struct TimeManager
{
    /// last execution finish time
    uint64_t m_lastExecFinishTime;
    uint64_t m_lastExecBlockFiniTime;
    unsigned m_viewTimeout;
    unsigned m_changeCycle = 0;
    uint64_t m_lastSignTime = 0;
    uint64_t m_lastConsensusTime;
    unsigned m_intervalBlockTime = 1000;
    /// time point of last signature collection
    std::chrono::system_clock::time_point m_lastGarbageCollection;
    static const unsigned kMaxChangeCycle = 20;
    static const unsigned CollectInterval = 60;
    float m_execTimePerTx;
    uint64_t m_leftTime;

    void initTimerManager(unsigned view_timeout)
    {
        m_lastExecFinishTime = m_lastExecBlockFiniTime = m_lastConsensusTime = utcTime();
        m_viewTimeout = view_timeout;
        m_changeCycle = 0;
        m_lastGarbageCollection = std::chrono::system_clock::now();
    }

    void changeView()
    {
        m_lastConsensusTime = 0;
        m_lastSignTime = 0;
        m_changeCycle = 0;
    }

    inline void updateChangeCycle()
    {
        m_changeCycle = std::min(m_changeCycle + 1, (unsigned)kMaxChangeCycle);
    }

    bool isTimeout()
    {
        auto now = utcTime();
        auto last = std::max(m_lastConsensusTime, m_lastSignTime);
        auto interval = (uint64_t)(m_viewTimeout * std::pow(1.5, m_changeCycle));
        return (now - last >= interval);
    }

    void updateTimeAfterHandleBlock(size_t const& txNum, uint64_t const& startExecTime)
    {
        m_lastExecFinishTime = utcTime();
        if (txNum != 0)
        {
            float execTime_per_tx = (float)(m_lastExecFinishTime - startExecTime);
            m_execTimePerTx = (m_execTimePerTx + execTime_per_tx) / 2;
            if (m_execTimePerTx >= m_intervalBlockTime)
                m_execTimePerTx = m_intervalBlockTime;
        }
    }

    uint64_t calculateMaxPackTxNum(uint64_t defaultMaxTxNum, u256 const& view)
    {
        auto last_exec_finish_time = std::min(m_lastExecFinishTime, m_lastExecBlockFiniTime);
        unsigned passed_time = 0;
        if (view != u256(0))
            passed_time = utcTime() - m_lastConsensusTime;
        else
        {
            if (m_lastConsensusTime - last_exec_finish_time >= m_intervalBlockTime)
                passed_time = utcTime() - m_lastConsensusTime;
            else
                passed_time = utcTime() - last_exec_finish_time;
        }
        auto left_time = 0;
        if (passed_time < m_intervalBlockTime)
            left_time = m_intervalBlockTime - passed_time;
        uint64_t max_tx_num = defaultMaxTxNum;
        if (m_execTimePerTx != 0)
        {
            max_tx_num = left_time / m_execTimePerTx;
            if (left_time > 0 && left_time < m_execTimePerTx)
                max_tx_num = 1;
            if (max_tx_num > defaultMaxTxNum)
                max_tx_num = defaultMaxTxNum;
        }
        return max_tx_num;
    }
};

using PBFTMsgQueue = dev::concurrent_queue<PBFTMsgPacket>;
class PBFTConsensus : public Consensus
{
public:
    PBFTConsensus(std::shared_ptr<dev::p2p::Service> _service,
        std::shared_ptr<dev::txpool::TxPool> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        KeyPair const& _key_pair, int16_t const& _protocolId, h512s const& _minerList = h512s())
      : Consensus(
            _service, _txPool, _blockChain, _blockSync, m_blockVerifier, _protocolId, _minerList)
    {
        m_service->registerHandlerByProtoclID(
            m_protocolId, boost::bind(&PBFTConsensus::onRecvPBFTMessage, this, _1, _2, _3));
        initPBFTEnv(_key_pair, 3 * getIntervalBlockTime());
    }

    void setIntervalBlockTime(unsigned const& _intervalBlockTime)
    {
        m_timeManager.m_intervalBlockTime = _intervalBlockTime;
    }
    unsigned const& getIntervalBlockTime() const { return m_timeManager.m_intervalBlockTime; }

protected:
    void workLoop() override;
    void handleBlock() override;
    void handleFutureBlock();
    void collectGarbage();
    void checkTimeout();
    bool shouldSeal() override;
    void rehandleCommitedPrepareCache(PrepareReq const& req);
    bool isMiner(h512& nodeId, const u256& idx) const;
    void checkBlockValid(Block const& block)
    {
        Consensus::checkBlockValid(block);
        checkMinerList(block);
    }
    bool needOmit(Sealing const& sealing);
    uint64_t calculateMaxPackTxNum() override;
    /// broadcast prepare message
    void broadcastPrepareReq(Block& block);
    /// broadcast sign message
    void broadcastSignReq(PrepareReq const& req);
    void broadcastSignedReq();
    /// broadcast commit message
    void broadcastCommitReq(PrepareReq const& req);
    /// broadcast view change message
    bool shouldBroadcastViewChange();
    void broadcastViewChangeReq();

    void onRecvPBFTMessage(
        P2PException exception, std::shared_ptr<Session> session, Message::Ptr message);

    /// handler prepare messages
    PrepareReq handlePrepareMsg(PBFTMsgPacket const& pbftMsg);
    void handlePrepareMsg(PrepareReq& prepare_req, bool self = false);
    SignReq handleSignMsg(PBFTMsgPacket const& pbftMsg);
    CommitReq handleCommitMsg(PBFTMsgPacket const& pbftMsg);
    ViewChangeReq handleViewChangeMsg(PBFTMsgPacket const& pbftMsg);
    void handleMsg(PBFTMsgPacket const& pbftMsg);
    void catchupView(ViewChangeReq const& req, ostringstream& oss);
    void checkAndCommit();
    void checkAndSave();
    void checkAndChangeView();

    bool shouldChangeViewForEmptyBlock()
    {
        bool ret = (m_sealing.block.getTransactionSize() == 0 && m_omitEmptyBlock);
        if (ret)
            m_sealing.block.resetCurrentBlock();
        return ret;
    }

    void reportBlock(BlockHeader const& blockHeader) override;

private:
    void initPBFTEnv(KeyPair const& _key_pair, unsigned _view_timeout);
    void resetConfig();
    void initBackupDB();
    void reloadMsg(std::string const& _key, PBFTMsg* _msg);
    void backupMsg(std::string const& _key, PBFTMsg const& _msg);
    std::string getGroupBackupMsgPath()
    {
        return getLedgerDir(toString(m_groupId)).string() + "/" + c_backupMsgDirName;
    }

    bool checkSign(PBFTMsg const& req) const;

    void broadcastMsg(unsigned const& packetType, std::string const& key, bytesConstRef data,
        std::unordered_set<h512> const& filter = std::unordered_set<h512>());
    bool broadcastFilter(h512 const& nodeId, unsigned const& packetType, std::string const& key)
    {
        return m_broadCastCache->keyExists(nodeId, packetType, key);
    }
    void broadcastMark(h512 const& nodeId, unsigned const& packetType, std::string const& key)
    {
        m_broadCastCache->insertKey(nodeId, packetType, key);
    }
    void clearMask() { m_broadCastCache->clearAll(); }
    ssize_t getIndexByMiner(dev::h512 node_id) const;
    h512 getMinerByIndex(size_t index) const;

    /// trans data into message
    Message::Ptr transDataToMessage(
        bytesConstRef data, uint16_t const& packetType, uint16_t const& protocolId)
    {
        Message::Ptr message;
        std::shared_ptr<dev::bytes> p_data = std::make_shared<dev::bytes>(data.toBytes());
        message->setBuffer(p_data);
        message->setPacketType(packetType);
        message->setProtocolID(protocolId);
        return message;
    }

    Message::Ptr transDataToMessage(bytesConstRef data, uint16_t const& packetType)
    {
        return transDataToMessage(data, packetType, m_protocolId);
    }

    bool isValidReq(Message::Ptr message, std::shared_ptr<Session> session)
    {
        h512 node_id;
        bool is_miner = isMiner(node_id, m_idx);
        if (message->buffer()->size() <= 0 || session->id() == node_id || !is_miner)
            return false;
        return true;
    }

    /// translate network-recevied packets to requests
    template <class T>
    bool decodeToRequests(T& req, Message::Ptr message, std::shared_ptr<Session> session)
    {
        bool valid = isValidReq(message, session);
        if (valid)
        {
            valid = decodeToRequests(req, ref(*(message->buffer())));
            /// get index
            ssize_t index = getIndexByMiner(session->id());
            if (index < 0)
            {
                LOG(ERROR) << "Recv an pbft msg from unknown peer id=" << session->id();
                return false;
            }
            req.setOtherField(u256(index), session->id());
        }
        return valid;
    }

    template <class T>
    bool decodeToRequests(T& req, bytesConstRef data)
    {
        try
        {
            req.decode(data);
            return true;
        }
        catch (std::exception& e)
        {
            LOG(ERROR) << "invalid network-recevied packet";
            return false;
        }
    }

    bool isValidPrepare(PrepareReq const& req, bool self, ostringstream& oss) const;
    template <class T>
    CheckResult checkReq(T const& req, ostringstream& oss) const
    {
        if (m_reqCache->prepareCache().block_hash != req.block_hash)
        {
            LOG(WARNING) << oss.str() << " Recv a req which not in prepareCache, block_hash = "
                         << m_reqCache->prepareCache().block_hash.abridged();
            /// is future ?
            bool is_future = isFutureBlock(req);
            if (is_future && checkSign(req))
            {
                LOG(INFO) << "Recv a future request, hash="
                          << m_reqCache->prepareCache().block_hash.abridged();
                return CheckResult::FUTURE;
            }
            return CheckResult::INVALID;
        }
        /// check view
        if (m_reqCache->prepareCache().view != req.view)
        {
            LOG(ERROR) << oss.str()
                       << ", Discard a req of which view is not equal to prepare.view = "
                       << m_reqCache->prepareCache().view;
            return CheckResult::INVALID;
        }
        if (!checkSign(req))
        {
            LOG(ERROR) << oss.str() << ", CheckSign failed";
            return CheckResult::INVALID;
        }
        return CheckResult::VALID;
    }

    bool isValidSignReq(SignReq const& req, ostringstream& oss) const;
    bool isValidCommitReq(CommitReq const& req, ostringstream& oss) const;
    bool isValidViewChangeReq(ViewChangeReq const& req, ostringstream& oss) const;

    template <class T>
    bool hasConsensused(T const& req) const
    {
        return req.height < m_consensusBlockNumber || req.view < m_view;
    }

    template <typename T>
    bool isFutureBlock(T const& req) const
    {
        return req.height > m_consensusBlockNumber || req.view > m_view;
    }

    bool isHashSavedAfterCommit(PrepareReq const& req) const
    {
        if (req.height == m_reqCache->committedPrepareCache().height &&
            req.block_hash != m_reqCache->committedPrepareCache().block_hash)
            return false;
        return true;
    }

    bool isValidLeader(PrepareReq const& req) const
    {
        auto leader = getLeader();
        if (!leader.first || req.idx != leader.second)
            return false;
        return true;
    }

    std::pair<bool, u256> getLeader() const
    {
        if (m_cfgErr || m_leaderFailed || m_highestBlock.number() == 0)
        {
            return std::make_pair(false, Invalid256);
        }
        return std::make_pair(true, (m_view + u256(m_highestBlock.number())) % u256(m_nodeNum));
    }
    void checkMinerList(Block const& block);
    bool execBlock();
    Sealing execBlock(PrepareReq& req, ostringstream& oss);
    u256 minValidNodes() const { return m_nodeNum - m_f; }
    void setBlock();
    void changeViewForEmptyBlock();

private:
    u256 m_view = u256(0);
    u256 m_toView = u256(0);
    KeyPair m_keyPair;
    /// total number of nodes
    u256 m_nodeNum = u256(0);
    /// at-least number of valid nodes
    u256 m_f = u256(0);

    bool m_cfgErr = false;
    bool m_leaderFailed = false;
    // the block which is waiting consensus
    int64_t m_consensusBlockNumber;

    /// the latest block header
    BlockHeader m_highestBlock;
    bool m_emptyBlockFlag;
    /// whether to omit empty block
    bool m_omitEmptyBlock;
    // backup msg
    std::shared_ptr<dev::db::LevelDB> m_backupDB = nullptr;

    /// get group id
    uint8_t m_groupId;
    /// static vars
    const static std::string c_backupKeyCommitted;
    const static std::string c_backupMsgDirName;
    const static unsigned c_PopWaitSeconds = 5;

    std::shared_ptr<PBFTBroadcastCache> m_broadCastCache;
    std::shared_ptr<PBFTReqCache> m_reqCache;
    TimeManager m_timeManager;
    PBFTMsgQueue m_msgQueue;
};
}  // namespace consensus
}  // namespace dev
