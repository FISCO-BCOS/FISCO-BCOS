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
 * @file: PBFTEngine.h
 * @author: yujiechen
 * @date: 2018-09-28
 */
#pragma once
#include "Common.h"
#include "PBFTMsgCache.h"
#include "PBFTReqCache.h"
#include "TimeManager.h"
#include <libconsensus/ConsensusEngineBase.h>
#include <libdevcore/FileSystem.h>
#include <libdevcore/LevelDB.h>
#include <libdevcore/concurrent_queue.h>
#include <libsync/SyncStatus.h>
#include <sstream>

#include <libp2p/P2PMessage.h>
#include <libp2p/P2PSession.h>
#include <libp2p/Service.h>

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
using PBFTMsgQueue = dev::concurrent_queue<PBFTMsgPacket>;
class PBFTEngine : public ConsensusEngineBase
{
public:
    virtual ~PBFTEngine() { stop(); }
    PBFTEngine(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        dev::PROTOCOL_ID const& _protocolId, std::string const& _baseDir, KeyPair const& _keyPair,
        h512s const& _sealerList = h512s())
      : ConsensusEngineBase(_service, _txPool, _blockChain, _blockSync, _blockVerifier, _protocolId,
            _keyPair, _sealerList),
        m_baseDir(_baseDir)
    {
        PBFTENGINE_LOG(INFO) << LOG_DESC("Register handler for PBFTEngine");
        m_service->registerHandlerByProtoclID(
            m_protocolId, boost::bind(&PBFTEngine::onRecvPBFTMessage, this, _1, _2, _3));
        m_broadCastCache = std::make_shared<PBFTBroadcastCache>();
        m_reqCache = std::make_shared<PBFTReqCache>(m_protocolId);

        /// register checkSealerList to blockSync for check SealerList
        m_blockSync->registerConsensusVerifyHandler(boost::bind(&PBFTEngine::checkBlock, this, _1));
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

    virtual bool reachBlockIntervalTime()
    {
        /// the block is sealed by the next leader, and can execute after the last block has been
        /// consensused
        if (m_notifyNextLeaderSeal)
        {
            /// represent that the latest block has been consensused
            if (getNextLeader() == nodeIdx())
            {
                return false;
            }
            return true;
        }
        /// the block is sealed by the current leader
        return (utcTime() - m_timeManager.m_lastConsensusTime) >= m_timeManager.m_intervalBlockTime;
    }

    /// in case of the next leader packeted the number of maxTransNum transactions before the last
    /// block is consensused
    /// when sealing for the next leader,  return true only if the last block has been consensused
    /// even if the maxTransNum condition has been meeted
    bool canHandleBlockForNextLeader()
    {
        if (m_notifyNextLeaderSeal && getNextLeader() == nodeIdx())
        {
            return false;
        }
        return true;
    }
    void rehandleCommitedPrepareCache(PrepareReq const& req);
    bool shouldSeal();
    /// broadcast prepare message
    bool generatePrepare(dev::eth::Block const& block);
    /// update the context of PBFT after commit a block into the block-chain
    void reportBlock(dev::eth::Block const& block) override;
    void onViewChange(std::function<void()> const& _f) { m_onViewChange = _f; }
    void onNotifyNextLeaderReset(std::function<void(dev::h256Hash const& filter)> const& _f)
    {
        m_onNotifyNextLeaderReset = _f;
    }

    bool inline shouldReset(dev::eth::Block const& block)
    {
        return block.getTransactionSize() == 0 && m_omitEmptyBlock;
    }
    void setStorage(dev::storage::Storage::Ptr storage) { m_storage = storage; }
    const std::string consensusStatus() override;
    void setOmitEmptyBlock(bool setter) { m_omitEmptyBlock = setter; }

    void setMaxTTL(uint8_t const& ttl) { maxTTL = ttl; }

    inline IDXTYPE getNextLeader() const { return (m_highestBlock.number() + 1) % m_nodeNum; }

    inline std::pair<bool, IDXTYPE> getLeader() const
    {
        if (m_cfgErr || m_leaderFailed || m_highestBlock.sealer() == Invalid256)
        {
            return std::make_pair(false, MAXIDX);
        }
        return std::make_pair(true, (m_view + m_highestBlock.number()) % m_nodeNum);
    }

protected:
    void reportBlockWithoutLock(dev::eth::Block const& block);
    void workLoop() override;
    void handleFutureBlock();
    void collectGarbage();
    void checkTimeout();
    bool getNodeIDByIndex(dev::network::NodeID& nodeId, const IDXTYPE& idx) const;
    inline void checkBlockValid(dev::eth::Block const& block) override
    {
        ConsensusEngineBase::checkBlockValid(block);
        checkSealerList(block);
    }
    bool needOmit(Sealing const& sealing);

    void getAllNodesViewStatus(json_spirit::Array& status);

    /// broadcast specified message to all-peers with cache-filter and specified filter
    bool broadcastMsg(unsigned const& packetType, std::string const& key, bytesConstRef data,
        std::unordered_set<dev::network::NodeID> const& filter =
            std::unordered_set<dev::network::NodeID>(),
        unsigned const& ttl = 0);

    void sendViewChangeMsg(dev::network::NodeID const& nodeId);
    bool sendMsg(dev::network::NodeID const& nodeId, unsigned const& packetType,
        std::string const& key, bytesConstRef data, unsigned const& ttl = 1);
    /// 1. generate and broadcast signReq according to given prepareReq
    /// 2. add the generated signReq into the cache
    bool broadcastSignReq(PrepareReq const& req);

    /// broadcast commit message
    bool broadcastCommitReq(PrepareReq const& req);
    /// broadcast view change message
    bool shouldBroadcastViewChange();
    bool broadcastViewChangeReq();
    /// handler called when receiving data from the network
    void onRecvPBFTMessage(dev::p2p::NetworkException exception,
        std::shared_ptr<dev::p2p::P2PSession> session, dev::p2p::P2PMessage::Ptr message);
    bool handlePrepareMsg(PrepareReq const& prepare_req, std::string const& endpoint = "self");
    /// handler prepare messages
    bool handlePrepareMsg(PrepareReq& prepareReq, PBFTMsgPacket const& pbftMsg);
    /// 1. decode the network-received PBFTMsgPacket to signReq
    /// 2. check the validation of the signReq
    /// add the signReq to the cache and
    /// heck the size of the collected signReq is over 2/3 or not
    bool handleSignMsg(SignReq& signReq, PBFTMsgPacket const& pbftMsg);
    bool handleCommitMsg(CommitReq& commitReq, PBFTMsgPacket const& pbftMsg);
    bool handleViewChangeMsg(ViewChangeReq& viewChangeReq, PBFTMsgPacket const& pbftMsg);
    void handleMsg(PBFTMsgPacket const& pbftMsg);
    void catchupView(ViewChangeReq const& req, std::ostringstream& oss);
    void checkAndCommit();

    /// if collect >= 2/3 SignReq and CommitReq, then callback this function to commit block
    void checkAndSave();
    void checkAndChangeView();

protected:
    void initPBFTEnv(unsigned _view_timeout);
    /// recalculate m_nodeNum && m_f && m_cfgErr(must called after setSigList)
    void resetConfig() override;
    virtual void initBackupDB();
    void reloadMsg(std::string const& _key, PBFTMsg* _msg);
    void backupMsg(std::string const& _key, PBFTMsg const& _msg);
    inline std::string getBackupMsgPath() { return m_baseDir + "/" + c_backupMsgDirName; }

    bool checkSign(PBFTMsg const& req) const;
    inline bool broadcastFilter(
        dev::network::NodeID const& nodeId, unsigned const& packetType, std::string const& key)
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
        dev::network::NodeID const& nodeId, unsigned const& packetType, std::string const& key)
    {
        /// in case of useless insert
        if (m_broadCastCache->keyExists(nodeId, packetType, key))
            return;
        m_broadCastCache->insertKey(nodeId, packetType, key);
    }
    inline void clearMask() { m_broadCastCache->clearAll(); }
    /// get the index of specified sealer according to its node id
    /// @param nodeId: the node id of the sealer
    /// @return : 1. >0: the index of the sealer
    ///           2. equal to -1: the node is not a sealer(not exists in sealer list)
    inline ssize_t getIndexBySealer(dev::network::NodeID const& nodeId)
    {
        ReadGuard l(m_sealerListMutex);
        ssize_t index = -1;
        for (size_t i = 0; i < m_sealerList.size(); ++i)
        {
            if (m_sealerList[i] == nodeId)
            {
                index = i;
                break;
            }
        }
        return index;
    }
    /// get the node id of specified sealer according to its index
    /// @param index: the index of the node
    /// @return h512(): the node is not in the sealer list
    /// @return node id: the node id of the node
    inline dev::network::NodeID getSealerByIndex(size_t const& index) const
    {
        ReadGuard l(m_sealerListMutex);
        if (index < m_sealerList.size())
            return m_sealerList[index];
        return dev::network::NodeID();
    }

    /// trans data into message
    inline dev::p2p::P2PMessage::Ptr transDataToMessage(bytesConstRef data,
        PACKET_TYPE const& packetType, PROTOCOL_ID const& protocolId, unsigned const& ttl)
    {
        dev::p2p::P2PMessage::Ptr message = std::make_shared<dev::p2p::P2PMessage>();
        // std::shared_ptr<dev::bytes> p_data = std::make_shared<dev::bytes>();
        bytes ret_data;
        PBFTMsgPacket packet;
        packet.data = data.toBytes();
        packet.packet_id = packetType;
        if (ttl == 0)
            packet.ttl = maxTTL;
        else
            packet.ttl = ttl;
        packet.encode(ret_data);
        std::shared_ptr<dev::bytes> p_data = std::make_shared<dev::bytes>(std::move(ret_data));
        message->setBuffer(p_data);
        message->setProtocolID(protocolId);
        return message;
    }

    inline dev::p2p::P2PMessage::Ptr transDataToMessage(
        bytesConstRef data, PACKET_TYPE const& packetType, unsigned const& ttl)
    {
        return transDataToMessage(data, packetType, m_protocolId, ttl);
    }

    /**
     * @brief : the message received from the network is valid or not?
     *      invalid cases: 1. received data is empty
     *                     2. the message is not sended by sealers
     *                     3. the message is not receivied by sealers
     *                     4. the message is sended by the node-self
     * @param message : message constructed from data received from the network
     * @param session : the session related to the network data(can get informations about the
     * sender)
     * @return true : the network-received message is valid
     * @return false: the network-received message is invalid
     */
    bool isValidReq(dev::p2p::P2PMessage::Ptr message,
        std::shared_ptr<dev::p2p::P2PSession> session, ssize_t& peerIndex) override
    {
        /// check message size
        if (message->buffer()->size() <= 0)
            return false;
        /// check whether in the sealer list
        peerIndex = getIndexBySealer(session->nodeID());
        if (peerIndex < 0)
        {
            PBFTENGINE_LOG(TRACE) << LOG_DESC(
                "isValidReq: Recv PBFT msg from unkown peer:" + session->nodeID().abridged());
            return false;
        }
        /// check whether this node is in the sealer list
        dev::network::NodeID node_id;
        bool is_sealer = getNodeIDByIndex(node_id, nodeIdx());
        if (!is_sealer || session->nodeID() == node_id)
            return false;
        return true;
    }

    /// check the specified prepareReq is valid or not
    CheckResult isValidPrepare(PrepareReq const& req, std::ostringstream& oss) const;

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
            PBFTENGINE_LOG(TRACE) << LOG_DESC("checkReq: sign or commit Not exist in prepare cache")
                                  << LOG_KV("prepHash",
                                         m_reqCache->prepareCache().block_hash.abridged())
                                  << LOG_KV("hash", req.block_hash.abridged())
                                  << LOG_KV("INFO", oss.str());
            /// is future ?
            bool is_future = isFutureBlock(req);
            if (is_future && checkSign(req))
            {
                PBFTENGINE_LOG(INFO)
                    << LOG_DESC("checkReq: Recv future request:")
                    << LOG_KV("prepHash", m_reqCache->prepareCache().block_hash.abridged())
                    << LOG_KV("INFO", oss.str());
                return CheckResult::FUTURE;
            }
            return CheckResult::INVALID;
        }
        /// check the sealer of this request
        if (req.idx == nodeIdx())
        {
            PBFTENGINE_LOG(TRACE) << LOG_DESC("checkReq: Recv own req")
                                  << LOG_KV("INFO", oss.str());
            return CheckResult::INVALID;
        }
        /// check view
        if (m_reqCache->prepareCache().view != req.view)
        {
            PBFTENGINE_LOG(TRACE) << LOG_DESC("checkReq: Recv req with unconsistent view")
                                  << LOG_KV("prepView", m_reqCache->prepareCache().view)
                                  << LOG_KV("view", req.view) << LOG_KV("INFO", oss.str());
            return CheckResult::INVALID;
        }
        if (!checkSign(req))
        {
            PBFTENGINE_LOG(TRACE) << LOG_DESC("checkReq:  invalid sign")
                                  << LOG_KV("INFO", oss.str());
            return CheckResult::INVALID;
        }
        return CheckResult::VALID;
    }

    CheckResult isValidSignReq(SignReq const& req, std::ostringstream& oss) const;
    CheckResult isValidCommitReq(CommitReq const& req, std::ostringstream& oss) const;
    bool isValidViewChangeReq(
        ViewChangeReq const& req, IDXTYPE const& source, std::ostringstream& oss);

    template <class T>
    inline bool hasConsensused(T const& req) const
    {
        if (req.height < m_consensusBlockNumber ||
            (req.height == m_consensusBlockNumber && req.view < m_view))
        {
            return true;
        }
        return false;
    }

    /**
     * @brief : decide the sign or commit request is the future request or not
     *          1. the block number is no smalller than the current consensused block number
     *          2. or the view is no smaller than the current consensused block number
     */
    template <typename T>
    inline bool isFutureBlock(T const& req) const
    {
        if (req.height >= m_consensusBlockNumber || req.view > m_view)
        {
            return true;
        }
        return false;
    }

    template <typename T>
    inline bool isFuturePrepare(T const& req) const
    {
        if (req.height > m_consensusBlockNumber ||
            (req.height == m_consensusBlockNumber && req.view > m_view))
        {
            return true;
        }
        return false;
    }

    inline bool isHashSavedAfterCommit(PrepareReq const& req) const
    {
        if (req.height == m_reqCache->committedPrepareCache().height &&
            req.block_hash != m_reqCache->committedPrepareCache().block_hash)
        {
            PBFTENGINE_LOG(DEBUG)
                << LOG_DESC("isHashSavedAfterCommit: hasn't been cached after commit")
                << LOG_KV("height", req.height)
                << LOG_KV("cacheHeight", m_reqCache->committedPrepareCache().height)
                << LOG_KV("hash", req.block_hash.abridged())
                << LOG_KV("cacheHash", m_reqCache->committedPrepareCache().block_hash.abridged());
            return false;
        }
        return true;
    }

    inline bool isValidLeader(PrepareReq const& req) const
    {
        auto leader = getLeader();
        /// get leader failed or this prepareReq is not broadcasted from leader
        if (!leader.first || req.idx != leader.second)
        {
            if (!m_emptyBlockViewChange)
            {
                PBFTENGINE_LOG(WARNING)
                    << LOG_DESC("InvalidPrepare: Get leader failed") << LOG_KV("cfgErr", m_cfgErr)
                    << LOG_KV("reqIdx", req.idx) << LOG_KV("sealerIdx", leader.second)
                    << LOG_KV("leaderFailed", m_leaderFailed) << LOG_KV("view", m_view)
                    << LOG_KV("highSealer", m_highestBlock.sealer())
                    << LOG_KV("highNum", m_highestBlock.number()) << LOG_KV("nodeIdx", nodeIdx());
            }
            return false;
        }

        return true;
    }

    void checkSealerList(dev::eth::Block const& block);
    /// check block
    bool checkBlock(dev::eth::Block const& block);
    void execBlock(Sealing& sealing, PrepareReq const& req, std::ostringstream& oss);
    void changeViewForEmptyBlock()
    {
        m_timeManager.changeView();
        m_timeManager.m_changeCycle = 0;
        m_emptyBlockViewChange = true;
        m_signalled.notify_all();
    }
    void notifySealing(dev::eth::Block const& block);
    virtual bool isDiskSpaceEnough(std::string const& path)
    {
        return boost::filesystem::space(path).available > 1024;
    }

    void updateViewMap(IDXTYPE const& idx, VIEWTYPE const& view)
    {
        WriteGuard l(x_viewMap);
        m_viewMap[idx] = view;
    }

protected:
    VIEWTYPE m_view = 0;
    VIEWTYPE m_toView = 0;
    std::string m_baseDir;
    bool m_leaderFailed = false;
    bool m_notifyNextLeaderSeal = false;

    dev::storage::Storage::Ptr m_storage;

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

    std::condition_variable m_signalled;
    Mutex x_signalled;

    std::function<void()> m_onViewChange;
    std::function<void(dev::h256Hash const& filter)> m_onNotifyNextLeaderReset;

    bool m_emptyBlockViewChange = false;

    uint8_t maxTTL = MAXTTL;

    /// map between nodeIdx to view
    mutable SharedMutex x_viewMap;
    std::map<IDXTYPE, VIEWTYPE> m_viewMap;
};
}  // namespace consensus
}  // namespace dev
