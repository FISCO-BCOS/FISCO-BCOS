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
#include "Common.h"
#include "PBFTMsgCache.h"
#include "PBFTReqCache.h"
#include <libconsensus/Consensus.h>
#include <libdevcore/FileSystem.h>
#include <libdevcore/LevelDB.h>
#include <libdevcore/concurrent_queue.h>
#include <libp2p/Session.h>
#include <libp2p/SessionFace.h>
#include <sstream>
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

    inline void initTimerManager(unsigned view_timeout)
    {
        m_lastExecFinishTime = m_lastExecBlockFiniTime = m_lastConsensusTime = utcTime();
        m_viewTimeout = view_timeout;
        m_changeCycle = 0;
        m_lastGarbageCollection = std::chrono::system_clock::now();
    }

    inline void changeView()
    {
        m_lastConsensusTime = 0;
        m_lastSignTime = 0;
        m_changeCycle = 0;
    }

    inline void updateChangeCycle()
    {
        m_changeCycle = std::min(m_changeCycle + 1, (unsigned)kMaxChangeCycle);
    }

    inline bool isTimeout()
    {
        auto now = utcTime();
        auto last = std::max(m_lastConsensusTime, m_lastSignTime);
        auto interval = (uint64_t)(m_viewTimeout * std::pow(1.5, m_changeCycle));
        return (now - last >= interval);
    }

    inline void updateTimeAfterHandleBlock(size_t const& txNum, uint64_t const& startExecTime)
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

    inline uint64_t calculateMaxPackTxNum(uint64_t defaultMaxTxNum, u256 const& view)
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
    PBFTConsensus(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        int16_t const& _protocolId, std::string const& _baseDir, KeyPair const& _key_pair,
        h512s const& _minerList = h512s())
      : Consensus(
            _service, _txPool, _blockChain, _blockSync, _blockVerifier, _protocolId, _minerList),
        m_keyPair(_key_pair),
        m_baseDir(_baseDir)
    {
        m_service->registerHandlerByProtoclID(
            m_protocolId, boost::bind(&PBFTConsensus::onRecvPBFTMessage, this, _1, _2, _3));
        m_broadCastCache = std::make_shared<PBFTBroadcastCache>();
        m_reqCache = std::make_shared<PBFTReqCache>();
    }

    void setBaseDir(std::string const& _path) { m_baseDir = _path; }

    std::string const& getBaseDir() { return m_baseDir; }

    inline void setIntervalBlockTime(unsigned const& _intervalBlockTime)
    {
        m_timeManager.m_intervalBlockTime = _intervalBlockTime;
    }

    inline unsigned const& getIntervalBlockTime() const
    {
        return m_timeManager.m_intervalBlockTime;
    }
    void start() override;

protected:
    void workLoop() override;
    void handleBlock() override;
    void handleFutureBlock();
    void collectGarbage();
    void checkTimeout();
    bool shouldSeal() override;
    void rehandleCommitedPrepareCache(PrepareReq const& req);
    bool getNodeIDByIndex(h512& nodeId, const u256& idx) const;
    inline void checkBlockValid(dev::eth::Block const& block)
    {
        Consensus::checkBlockValid(block);
        checkMinerList(block);
    }
    bool needOmit(Sealing const& sealing);
    uint64_t calculateMaxPackTxNum() override;
    /// broadcast prepare message
    void broadcastPrepareReq(dev::eth::Block& block);

    /// 1. generate and broadcast signReq according to given prepareReq
    /// 2. add the generated signReq into the cache
    void broadcastSignReq(PrepareReq const& req);
    void broadcastSignedReq();

    /// broadcast commit message
    void broadcastCommitReq(PrepareReq const& req);
    /// broadcast view change message
    bool shouldBroadcastViewChange();
    void broadcastViewChangeReq();
    /// handler called when receiving data from the network
    void onRecvPBFTMessage(dev::p2p::P2PException exception,
        std::shared_ptr<dev::p2p::Session> session, dev::p2p::Message::Ptr message);

    /// handler prepare messages
    void handlePrepareMsg(PrepareReq& prepareReq, PBFTMsgPacket const& pbftMsg);
    void handlePrepareMsg(PrepareReq const& prepare_req, bool self = false);
    void handleSignMsg(SignReq& signReq, PBFTMsgPacket const& pbftMsg);
    void handleCommitMsg(CommitReq& commitReq, PBFTMsgPacket const& pbftMsg);
    void handleViewChangeMsg(ViewChangeReq& viewChangeReq, PBFTMsgPacket const& pbftMsg);
    void handleMsg(PBFTMsgPacket const& pbftMsg);
    void catchupView(ViewChangeReq const& req, std::ostringstream& oss);
    void checkAndCommit();

    /// if collect >= 2/3 SignReq and CommitReq, then callback this function to commit block
    void checkAndSave();
    void checkAndChangeView();

    inline bool shouldChangeViewForEmptyBlock()
    {
        bool ret = (m_sealing.block.getTransactionSize() == 0 && m_omitEmptyBlock);
        if (ret)
            resetSealingBlock();
        return ret;
    }

    /// update the context of PBFT after commit a block into the block-chain
    void reportBlock(dev::eth::BlockHeader const& blockHeader) override;

protected:
    void initPBFTEnv(unsigned _view_timeout);
    /// recalculate m_nodeNum && m_f && m_cfgErr(must called after setSigList)
    void resetConfig() override;
    virtual void initBackupDB();
    void reloadMsg(std::string const& _key, PBFTMsg* _msg);
    void backupMsg(std::string const& _key, PBFTMsg const& _msg);
    inline std::string getBackupMsgPath() { return m_baseDir + "/" + c_backupMsgDirName; }

    bool checkSign(PBFTMsg const& req) const;
    /// broadcast specified message to all-peers with cache-filter and specified filter
    void broadcastMsg(unsigned const& packetType, std::string const& key, bytesConstRef data,
        std::unordered_set<h512> const& filter = std::unordered_set<h512>());
    inline bool broadcastFilter(
        h512 const& nodeId, unsigned const& packetType, std::string const& key)
    {
        return m_broadCastCache->keyExists(nodeId, packetType, key);
    }

    /**
     * @brief: insert specified key into the cache of broadcast
     *         used to filter the broadcasted message(in case of too-many repeated broadcast
     * messages)
     * @param nodeId: the node id of the message broadcasted to
     * @param packetType: the packet type of the broadcast-message
     * @param key: the key of the broadcast-message, is the signature of the broadcast-message in
     * common
     */
    inline void broadcastMark(
        h512 const& nodeId, unsigned const& packetType, std::string const& key)
    {
        m_broadCastCache->insertKey(nodeId, packetType, key);
    }
    inline void clearMask() { m_broadCastCache->clearAll(); }
    /// get the index of specified miner according to its node id
    /// @param nodeId: the node id of the miner
    /// @return : 1. >0: the index of the miner
    ///           2. equal to -1: the node is not a miner(not exists in miner list)
    inline ssize_t getIndexByMiner(dev::h512 const& nodeId) const
    {
        ssize_t index = -1;
        for (size_t i = 0; i < m_minerList.size(); ++i)
        {
            if (m_minerList[i] == nodeId)
            {
                index = i;
                break;
            }
        }
        return index;
    }
    /// get the node id of specified miner according to its index
    /// @param index: the index of the node
    /// @return h512(): the node is not in the miner list
    /// @return node id: the node id of the node
    inline h512 getMinerByIndex(size_t const& index) const
    {
        if (index < m_minerList.size())
            return m_minerList[index];
        return h512();
    }

    /// trans data into message
    inline dev::p2p::Message::Ptr transDataToMessage(
        bytesConstRef data, uint16_t const& packetType, uint16_t const& protocolId)
    {
        dev::p2p::Message::Ptr message = std::make_shared<dev::p2p::Message>();
        std::shared_ptr<dev::bytes> p_data = std::make_shared<dev::bytes>();
        PBFTMsgPacket packet;
        packet.data = data.toBytes();
        packet.packet_id = packetType;

        packet.encode(*p_data);
        message->setBuffer(p_data);
        message->setProtocolID(protocolId);
        return message;
    }

    inline dev::p2p::Message::Ptr transDataToMessage(bytesConstRef data, uint16_t const& packetType)
    {
        return transDataToMessage(data, packetType, m_protocolId);
    }

    /**
     * @brief : the message received from the network is valid or not?
     *      invalid cases: 1. received data is empty
     *                     2. the message is not sended by miners
     *                     3. the message is not receivied by miners
     *                     4. the message is sended by the node-self
     * @param message : message constructed from data received from the network
     * @param session : the session related to the network data(can get informations about the
     * sender)
     * @return true : the network-received message is valid
     * @return false: the network-received message is invalid
     */
    inline bool isValidReq(dev::p2p::Message::Ptr message,
        std::shared_ptr<dev::p2p::Session> session, ssize_t& peerIndex)
    {
        /// check message size
        if (message->buffer()->size() <= 0)
            return false;
        /// check whether in the miner list
        peerIndex = getIndexByMiner(session->id());
        if (peerIndex < 0)
        {
            LOG(ERROR) << "Recv an pbft msg from unknown peer id=" << session->id();
            return false;
        }
        /// check whether this node is in the miner list
        h512 node_id;
        bool is_miner = getNodeIDByIndex(node_id, m_idx);
        if (!is_miner || session->id() == node_id)
            return false;
        return true;
    }

    /**
     * @brief: decode the network-received message to corresponding message(PBFTMsgPacket)
     *
     * @tparam T: the type of corresponding message
     * @param req : constructed object from network-received message
     * @param message : network-received(received from service of p2p module)
     * @param session : session related with the received-message(can obtain information of
     * sender-peer)
     * @return true : decode succeed
     * @return false : decode failed
     */
    template <class T>
    inline bool decodeToRequests(
        T& req, dev::p2p::Message::Ptr message, std::shared_ptr<dev::p2p::Session> session)
    {
        ssize_t peer_index = 0;
        bool valid = isValidReq(message, session, peer_index);
        if (valid)
        {
            valid = decodeToRequests(req, ref(*(message->buffer())));
            if (valid)
                req.setOtherField(u256(peer_index), session->id());
        }
        return valid;
    }

    /**
     * @brief : decode the given data to object
     * @tparam T : type of the object obtained from the given data
     * @param req : the object obtained from the given data
     * @param data : data need to be decoded into object
     * @return true : decode succeed
     * @return false : decode failed
     */
    template <class T>
    inline bool decodeToRequests(T& req, bytesConstRef data)
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
    /// check the specified prepareReq is valid or not
    bool isValidPrepare(PrepareReq const& req, bool self, std::ostringstream& oss) const;

    /**
     * @brief: common check process when handle SignReq and CommitReq
     *         1. the request should be existed in prepare cache,
     *            if the request is the future request, should add it to the prepare cache
     *         2. the sealer of the request shouldn't be the node-self
     *         3. the view of the request must be equal to the view of the prepare cache
     *         4. the signature of the request must be valid
     * @tparam T: the type of the request
     * @param req: the request should be checked
     * @param oss: information to debug
     * @return CheckResult:
     *  1. CheckResult::FUTURE: the request is the future req;
     *  2. CheckResult::INVALID: the request is invalid
     *  3. CheckResult::VALID: the request is valid
     */
    template <class T>
    inline CheckResult checkReq(T const& req, std::ostringstream& oss) const
    {
        if (m_reqCache->prepareCache().block_hash != req.block_hash)
        {
            LOG(WARNING) << oss.str()
                         << " Recv a req which not in prepareCache,prepareCache block_hash = "
                         << m_reqCache->prepareCache().block_hash.abridged()
                         << "req block_hash = " << req.block_hash;
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
        /// check the sealer of this request
        if (req.idx == m_idx)
        {
            LOG(WARNING) << oss.str() << " Discard an illegal request, your own req";
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

    bool isValidSignReq(SignReq const& req, std::ostringstream& oss) const;
    bool isValidCommitReq(CommitReq const& req, std::ostringstream& oss) const;
    bool isValidViewChangeReq(ViewChangeReq const& req, std::ostringstream& oss) const;

    template <class T>
    inline bool hasConsensused(T const& req) const
    {
        return req.height < m_consensusBlockNumber || req.view < m_view;
    }

    template <typename T>
    inline bool isFutureBlock(T const& req) const
    {
        if (req.height > m_consensusBlockNumber)
            return true;
        if (req.height == m_consensusBlockNumber && req.view > m_view)
            return true;
        return false;
    }

    inline bool isHashSavedAfterCommit(PrepareReq const& req) const
    {
        if (req.height == m_reqCache->committedPrepareCache().height &&
            req.block_hash != m_reqCache->committedPrepareCache().block_hash)
            return false;
        return true;
    }

    inline bool isValidLeader(PrepareReq const& req) const
    {
        auto leader = getLeader();
        /// get leader failed or this prepareReq is not broadcasted from leader
        if (!leader.first || req.idx != leader.second)
            return false;
        return true;
    }

    inline std::pair<bool, u256> getLeader() const
    {
        if (m_cfgErr || m_leaderFailed || m_highestBlock.sealer() == Invalid256)
        {
            return std::make_pair(false, Invalid256);
        }
        return std::make_pair(true, (m_view + u256(m_highestBlock.number())) % u256(m_nodeNum));
    }
    void checkMinerList(dev::eth::Block const& block);
    bool execBlock();
    void execBlock(Sealing& sealing, PrepareReq const& req, std::ostringstream& oss);
    void setBlock();
    void changeViewForEmptyBlock();
    virtual bool isDiskSpaceEnough(std::string const& path)
    {
        return boost::filesystem::space(path).available > 1024;
    }

protected:
    u256 m_view = u256(0);
    u256 m_toView = u256(0);
    KeyPair m_keyPair;
    std::string m_baseDir;
    bool m_cfgErr = false;
    bool m_leaderFailed = false;
    // the block which is waiting consensus
    int64_t m_consensusBlockNumber;

    /// the latest block header
    dev::eth::BlockHeader m_highestBlock;
    bool m_emptyBlockFlag;
    /// whether to omit empty block
    bool m_omitEmptyBlock;
    // backup msg
    std::shared_ptr<dev::db::LevelDB> m_backupDB = nullptr;

    /// static vars
    static const std::string c_backupKeyCommitted;
    static const std::string c_backupMsgDirName;
    static const unsigned c_PopWaitSeconds = 5;

    std::shared_ptr<PBFTBroadcastCache> m_broadCastCache;
    std::shared_ptr<PBFTReqCache> m_reqCache;
    TimeManager m_timeManager;
    PBFTMsgQueue m_msgQueue;
    mutable Mutex m_mutex;
};
}  // namespace consensus
}  // namespace dev
