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
 * @file: PBFTConsensus.cpp
 * @author: yujiechen
 * @date: 2018-09-28
 */
#include "PBFTConsensus.h"
#include <libdevcore/CommonJS.h>
#include <libethcore/CommonJS.h>
using namespace dev::eth;
using namespace dev::db;
using namespace dev::blockverifier;
using namespace dev::blockchain;
namespace dev
{
namespace consensus
{
const std::string PBFTConsensus::c_backupKeyCommitted = "committed";
const std::string PBFTConsensus::c_backupMsgDirName = "pbftMsgBackup";
void PBFTConsensus::initPBFTEnv(KeyPair const& _key_pair, unsigned view_timeout)
{
    m_keyPair = _key_pair;
    resetConfig();
    m_consensusBlockNumber = 0;
    m_toView = u256(0);
    m_leaderFailed = false;
    initBackupDB();
    m_broadCastCache = std::make_shared<PBFTBroadcastCache>();
    m_reqCache = std::make_shared<PBFTReqCache>();
    m_timeManager.initTimerManager(view_timeout);
    LOG(INFO) << "PBFT initEnv success";
}

void PBFTConsensus::resetConfig()
{
    m_idx = u256(-1);
    for (size_t i = 0; i < m_minerList.size(); i++)
    {
        if (m_minerList[i] == m_keyPair.pub())
        {
            m_accountType = NodeAccountType::MinerAccount;
            m_idx = u256(i);
            break;
        }
    }
    auto node_num = m_minerList.size();
    m_nodeNum = u256(node_num);
    m_f = (m_nodeNum - u256(1)) / u256(3);
    m_cfgErr = (m_idx < u256(0));
    /// clear caches
    m_reqCache->clearAll();
    LOG(INFO) << "resetConfig: m_node_idx=" << m_idx.convert_to<ssize_t>()
              << ", m_nodeNum =" << m_nodeNum;
}

/// init pbftMsgBackup
void PBFTConsensus::initBackupDB()
{
    std::string path = getGroupBackupMsgPath();
    if (boost::filesystem::space(path).available < 1024)
    {
        LOG(ERROR) << "Not enough available space found on hard drive. Please free some up and "
                      "then re-run. Bailing.";
        BOOST_THROW_EXCEPTION(NotEnoughAvailableSpace());
    }
    /// try-catch has already been considered by libdevcore/LevelDB.*
    m_backupDB = std::make_shared<LevelDB>(path);
    // reload msg from db
    PBFTMsg pbft_msg = m_reqCache->committedPrepareCache();
    reloadMsg(c_backupKeyCommitted, &pbft_msg);
}

/// reload msg from db
void PBFTConsensus::reloadMsg(std::string const& key, PBFTMsg* msg)
{
    if (!m_backupDB || !msg)
        return;
    try
    {
        std::string data = m_backupDB->lookup(key);
        if (data.empty())
        {
            LOG(ERROR) << "reloadMsg failed";
            return;
        }
        msg->decode(data, 0);
        LOG(INFO) << "reloadMsg, data len=" << data.size() << ", height=" << msg->height
                  << ",hash=" << msg->block_hash.abridged() << ",idx=" << msg->idx;
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Read PBFT message from db failed, error information:" << e.what();
        return;
    }
}

void PBFTConsensus::backupMsg(std::string const& _key, PBFTMsg const& _msg)
{
    if (!m_backupDB)
        return;
    bytes message_data;
    _msg.encode(message_data);
    try
    {
        m_backupDB->insert(_key, ref(message_data).toString());
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "backup message " << ref(message_data).toString()
                   << " failed, error information:" << e.what();
    }
}

/// generate block after exectute all transactions
bool PBFTConsensus::execBlock()
{
    try
    {
        /// execute block
        executeBlock();
        m_timeManager.m_lastExecBlockFiniTime = utcTime();
        return true;
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "ExecuteTransaction Exception, error message:" << e.what();
        m_syncTxPool = true;
        return false;
    }
}

inline void PBFTConsensus::setBlock()
{
    ResetSealingBlock();
    setSealingRoot(m_sealing.block.getTransactionRoot(), h256(Invalid256), h256(Invalid256));
}

/// change view for empty block
inline void PBFTConsensus::changeViewForEmptyBlock()
{
    LOG(INFO) << "changeViewForEmptyBlock m_toView=" << m_toView << ", node=" << m_idx;
    m_timeManager.changeView();
    m_emptyBlockFlag = true;
    m_leaderFailed = true;
    m_signalled.notify_all();
}

void PBFTConsensus::broadcastSignedReq()
{
    PrepareReq req(m_sealing.block, m_keyPair, m_view, m_idx);
    m_reqCache->addPrepareReq(req);
    broadcastSignReq(req);
    checkAndCommit();
}

void PBFTConsensus::handleBlock()
{
    setBlock();
    LOG(INFO) << "+++++++++++++++++++++++++++ Generating seal on" << m_sealing.block.header().hash()
              << "#" << m_sealing.block.header().number()
              << "tx:" << m_sealing.block.getTransactionSize() << "time:" << utcTime();
    Timer t;
    broadcastPrepareReq(m_sealing.block);
    LOG(DEBUG) << "broadcast generated block, blk=" << m_sealing.block.blockHeader().number()
               << ", timecost=" << 1000 * t.elapsed();
    /// handle empty block
    if (shouldChangeViewForEmptyBlock())
    {
        changeViewForEmptyBlock();
        return;
    }
    /// execute the transaction
    auto start_exec_time = utcTime();
    bool ret = execBlock();
    if (ret == false)
    {
        m_sealing.block.resetCurrentBlock();
        return;
    }
    /// check seal
    if (!m_sealing.block.isSealed())
    {
        LOG(ERROR) << "m_sealing is not sealed, maybe resetted by view change";
        return;
    }
    /// generate sign req
    LOG(INFO) << "************************** Generating sign on" << m_sealing.block.header().hash()
              << "#" << m_sealing.block.header().number()
              << "tx:" << m_sealing.block.getTransactionSize() << "time:" << utcTime();
    broadcastSignedReq();
    m_timeManager.updateTimeAfterHandleBlock(m_sealing.block.getTransactionSize(), start_exec_time);
}

bool PBFTConsensus::shouldSeal()
{
    if (m_cfgErr || m_accountType != NodeAccountType::MinerAccount)
        return false;
    /// check leader
    std::pair<bool, u256> ret = getLeader();
    if (!ret.first)
        return false;
    /// fast view change
    if (ret.second != m_idx)
    {
        h512 node_id = getMinerByIndex(ret.second.convert_to<size_t>());
        if (node_id != h512() && m_service->isConnected(node_id))
        {
            m_timeManager.m_lastConsensusTime = 0;
            m_timeManager.m_lastSignTime = 0;
            m_signalled.notify_all();
        }
        return false;
    }
    if (m_reqCache->committedPrepareCache().height == m_consensusBlockNumber)
    {
        if (m_reqCache->rawPrepareCache().height != m_consensusBlockNumber)
        {
            rehandleCommitedPrepareCache(m_reqCache->committedPrepareCache());
        }
        return false;
    }
    return true;
}

void PBFTConsensus::rehandleCommitedPrepareCache(PrepareReq const& req)
{
    LOG(INFO) << "shouldSeal: found an committed but not saved block, post out again. hash="
              << req.block_hash.abridged();
    m_broadCastCache->clearAll();
    PrepareReq prepare_req(req, m_keyPair, m_view, m_idx);

    LOG(INFO) << "BLOCK_TIMESTAMP_STAT:[" << toString(prepare_req.block_hash) << "]["
              << prepare_req.height << "][" << utcTime() << "]["
              << "broadcastPrepareReq"
              << "]";
    bytes prepare_data;
    prepare_req.encode(prepare_data);
    /// broadcast prepare message
    broadcastMsg(PrepareReqPacket, prepare_req.block_hash.hex(), ref(prepare_data));
    handlePrepareMsg(prepare_req, true);
}

uint64_t PBFTConsensus::calculateMaxPackTxNum()
{
    return m_timeManager.calculateMaxPackTxNum(m_maxBlockTransactions, m_view);
}

/// broadcast related
void PBFTConsensus::broadcastPrepareReq(Block& block)
{
    PrepareReq prepare_req(block, m_keyPair, m_view, m_idx);
    bytes prepare_req_data;
    prepare_req.encode(prepare_req_data);
    broadcastMsg(PrepareReqPacket, prepare_req.block_hash.hex(), ref(prepare_req_data));
    m_reqCache->addRawPrepare(prepare_req);
}

void PBFTConsensus::broadcastSignReq(PrepareReq const& req)
{
    SignReq sign_req(req, m_keyPair, m_idx);
    bytes sign_req_data;
    sign_req.encode(sign_req_data);
    broadcastMsg(SignReqPacket, sign_req.sig.hex(), ref(sign_req_data));
    m_reqCache->addSignReq(sign_req);
}

inline bool PBFTConsensus::isMiner(h512& nodeID, const u256& idx) const
{
    nodeID = getMinerByIndex(idx.convert_to<size_t>());
    if (nodeID == h512())
    {
        LOG(ERROR) << "Can't find node, idx=" << idx;
        return false;
    }
    return true;
}

inline bool PBFTConsensus::checkSign(PBFTMsg const& req) const
{
    h512 node_id;
    if (isMiner(node_id, req.idx))
    {
        Public pub_id = jsToPublic(toJS(node_id.hex()));
        return dev::verify(pub_id, req.sig, req.block_hash) &&
               dev::verify(pub_id, req.sig2, req.fieldsWithoutBlock());
    }
    return false;
}

void PBFTConsensus::broadcastCommitReq(PrepareReq const& req)
{
    CommitReq commit_req(req, m_keyPair, m_idx);
    bytes commit_req_data;
    commit_req.encode(commit_req_data);
    broadcastMsg(CommitReqPacket, commit_req.sig.hex(), ref(commit_req_data));
    m_reqCache->addCommitReq(commit_req);
}

void PBFTConsensus::broadcastViewChangeReq()
{
    LOG(INFO) << "Ready to broadcastViewChangeReq, blk = " << m_highestBlock.number()
              << " , view = " << m_view << ", to_view:" << m_toView;

    ViewChangeReq req(m_keyPair, m_highestBlock.number(), m_toView, m_idx, m_highestBlock.hash());

    bytes view_change_data;
    req.encode(view_change_data);
    broadcastMsg(ViewChangeReqPacket, req.sig.hex() + toJS(req.view), ref(view_change_data));
}

void PBFTConsensus::broadcastMsg(unsigned const& packetType, std::string const& key,
    bytesConstRef data, std::unordered_set<h512> const& filter)
{
    auto sessions = m_service->sessionInfos();
    for (auto session : sessions)
    {
        /// peer is a miner ?
        if (getIndexByMiner(session.nodeID) < 0)
            return;
        /// peer is in the _filter list
        if (filter.count(session.nodeID))
        {
            broadcastMark(session.nodeID, packetType, key);
            return;
        }
        if (broadcastFilter(session.nodeID, packetType, key))
        {
            /// send messages
            m_service->asyncSendMessageByNodeID(
                session.nodeID, transDataToMessage(data, packetType), nullptr);
            broadcastMark(session.nodeID, packetType, key);
        }
    }
}

h512 PBFTConsensus::getMinerByIndex(size_t index) const
{
    if (index < m_minerList.size())
        return m_minerList[index];
    return h512();
}

ssize_t PBFTConsensus::getIndexByMiner(dev::h512 node_id) const
{
    ssize_t index = -1;
    for (size_t i = 0; i < m_minerList.size(); ++i)
    {
        if (m_minerList[i] == node_id)
        {
            index = i;
            break;
        }
    }
    return index;
}

inline bool PBFTConsensus::isValidPrepare(
    PrepareReq const& req, bool allowSelf, ostringstream& oss) const
{
    if (m_reqCache->isExistPrepare(req))
    {
        LOG(ERROR) << oss.str() << " , Discard an illegal prepare, duplicated";
        return false;
    }
    if (!allowSelf && req.idx == m_idx)
    {
        LOG(ERROR) << oss.str() << ", Discard an illegal prepare, your own req";
        return false;
    }
    if (hasConsensused(req))
    {
        LOG(ERROR) << oss.str() << ", Discard an illegal prepare, maybe consensused";
        return false;
    }

    if (isFutureBlock(req))
    {
        LOG(INFO) << oss.str() << ",Recv a future block, wait to be handled later";
        m_reqCache->addFuturePrepareCache(req);
        return false;
    }
    if (!isValidLeader(req))
    {
        LOG(ERROR) << oss.str() << "Recv an illegal prepare, err leader";
        return false;
    }
    if (!isHashSavedAfterCommit(req))
    {
        LOG(ERROR) << oss.str() << ", Discard an illegal prepare req, commited but not saved hash="
                   << m_reqCache->committedPrepareCache().block_hash.abridged();
        return false;
    }
    if (!checkSign(req))
    {
        LOG(ERROR) << oss.str() << ",CheckSign failed";
        return false;
    }
    return true;
}

/// check miner list
inline void PBFTConsensus::checkMinerList(Block const& block)
{
    if (m_minerList != block.blockHeader().sealerList())
    {
#if DEBUG
        std::string miners;
        for (auto miner : m_minerList)
            miners += miner + " ";
        LOG(DEBUG) << "Miner list = " << miners;
#endif
        LOG(ERROR) << "Wrong Miner List, right_miner_size = " << m_minerList.size()
                   << " miner size of block_hash = " << block.blockHeader().hash() << " is "
                   << block.blockHeader().sealerList().size();
        BOOST_THROW_EXCEPTION(
            BlockMinerListWrong() << errinfo_comment("Wrong Miner List of Block"));
    }
}

Sealing PBFTConsensus::execBlock(PrepareReq& req, ostringstream& oss)
{
    Block working_block(req.block);
    LOG(TRACE) << "start exec tx, blk=" << req.height << ", hash = " << req.block_hash
               << ", idx = " << req.idx << ", time =" << utcTime();
    checkBlockValid(working_block);
    Sealing sealing;
    sealing.p_execContext = executeBlock(working_block);
    sealing.block = working_block;
    m_timeManager.m_lastExecFinishTime = utcTime();
    return sealing;
}

/// check whether the block is empty
inline bool PBFTConsensus::needOmit(Sealing const& sealing)
{
    if (sealing.block.getTransactionSize() == 0 && m_omitEmptyBlock)
    {
        LOG(TRACE) << "omit empty block: " << sealing.block.blockHeader().hash();
        return true;
    }
    return false;
}

void PBFTConsensus::onRecvPBFTMessage(
    P2PException exception, std::shared_ptr<Session> session, Message::Ptr message)
{
    PBFTMsgPacket pbft_msg;
    bool valid = decodeToRequests(pbft_msg, message, session);
    if (!valid)
        return;
    if (pbft_msg.packet_id <= ViewChangeReqPacket)
    {
        m_msgQueue.push(pbft_msg);
    }
    else
    {
        LOG(ERROR) << "Recv an illegal msg, id = " << pbft_msg.packet_id;
    }
}

PrepareReq PBFTConsensus::handlePrepareMsg(PBFTMsgPacket const& pbftMsg)
{
    PrepareReq prepare_req;
    bool valid = decodeToRequests(prepare_req, ref(pbftMsg.data));
    if (!valid)
        return prepare_req;
    handlePrepareMsg(prepare_req);
    return prepare_req;
}

void PBFTConsensus::handlePrepareMsg(PrepareReq& prepare_req, bool self)
{
    Timer t;
    ostringstream oss;
    oss << "handlePrepareMsg: idx=" << prepare_req.idx << ",view=" << prepare_req.view
        << ",blk=" << prepare_req.height << ",hash=" << prepare_req.block_hash.abridged();

    if (!isValidPrepare(prepare_req, self, oss))
        return;
    /// add block into prepare request
    m_reqCache->addRawPrepare(prepare_req);
    Sealing workingSealing;
    try
    {
        workingSealing = execBlock(prepare_req, oss);
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << oss.str() << "Check Block or Run Block failed, error info:" << e.what();
        return;
    }
    /// whether to omit empty block
    if (needOmit(workingSealing))
        return;

    h256 origin_blockHash = prepare_req.block_hash;
    prepare_req.updatePrepareReq(workingSealing, m_keyPair);
    m_reqCache->addPrepareReq(prepare_req);

    /// broadcast the re-generated signReq
    broadcastSignReq(prepare_req);
    prepare_req.block_hash = origin_blockHash;

    LOG(INFO) << oss.str() << ",real_block_hash=" << workingSealing.block.header().hash().abridged()
              << " success";
    checkAndCommit();
    LOG(DEBUG) << "handlePrepareMsg, timecost=" << 1000 * t.elapsed();
}

void PBFTConsensus::checkAndCommit()
{
    u256 sign_size = m_reqCache->getSigCacheSize(m_reqCache->prepareCache().block_hash);
    if (sign_size == minValidNodes())
    {
        LOG(INFO) << "######### Reach enough sign for block="
                  << m_reqCache->prepareCache().block_hash << ", sign_size = " << sign_size
                  << " , min_need_sign = " << minValidNodes();
        if (m_reqCache->prepareCache().view != m_view)
        {
            LOG(WARNING) << "view has changed, discard this block, preq.view="
                         << m_reqCache->prepareCache().view << ", m_view=" << m_view;
            return;
        }
        /// update and backup the commit cache
        m_reqCache->updateCommittedPrepare();
        backupMsg(c_backupKeyCommitted, m_reqCache->committedPrepareCache());
        broadcastCommitReq(m_reqCache->prepareCache());
        m_timeManager.m_lastSignTime = utcTime();
        checkAndSave();
    }
}

void PBFTConsensus::checkAndSave()
{
    u256 sign_size = m_reqCache->getSigCacheSize(m_reqCache->prepareCache().block_hash);
    u256 commit_size = m_reqCache->getCommitCacheSize(m_reqCache->prepareCache().block_hash);
    if (sign_size >= minValidNodes() && commit_size >= minValidNodes())
    {
        LOG(INFO) << "######### Reach enough commit for block=" << m_reqCache->prepareCache().height
                  << ",hash=" << m_reqCache->prepareCache().block_hash.abridged()
                  << ",have_sign=" << sign_size << ",have_commit=" << commit_size
                  << ",minValidNodes=" << minValidNodes();
        if (m_reqCache->prepareCache().view != m_view)
        {
            LOG(WARNING) << "view has changed, discard this block, preq.view="
                         << m_reqCache->prepareCache().view << ", m_view=" << m_view;
            return;
        }
        /// add sign-list into the block header
        if (m_reqCache->prepareCache().height > m_highestBlock.number())
        {
            Block block = m_reqCache->generateAndSetSigList(minValidNodes());
            LOG(INFO) << "BLOCK_TIMESTAMP_STAT:[" << toString(m_reqCache->prepareCache().block_hash)
                      << "][" << m_reqCache->prepareCache().height << "][" << utcTime() << "]["
                      << "onSealGenerated"
                      << "]"
                      << ",idx=" << m_reqCache->prepareCache().idx;
            /// callback block chain to commit block
            m_blockChain->commitBlock(
                block, std::shared_ptr<ExecutiveContext>(m_reqCache->prepareCache().p_execContext));
            /// report block
            reportBlock(block.blockHeader());
        }
        else
        {
            LOG(INFO) << "Discard this block, blk_no=" << m_reqCache->prepareCache().height
                      << ",highest_block=" << m_highestBlock.number();
        }
    }
}

void PBFTConsensus::reportBlock(BlockHeader const& blockHeader)
{
    /// update the highest block
    m_highestBlock = blockHeader;
    if (m_highestBlock.number() >= m_consensusBlockNumber)
    {
        m_view = m_toView = u256(0);
        m_leaderFailed = false;
        m_timeManager.m_lastConsensusTime = utcTime();
        m_consensusBlockNumber = m_highestBlock.number() + 1;
        /// delete invalid view change requests from the cache
        m_reqCache->delInvalidViewChange(m_highestBlock);
    }
    resetConfig();
    m_reqCache->delCache(m_highestBlock.hash());
    LOG(INFO) << "^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ Report: blk=" << m_highestBlock.number()
              << ",hash=" << blockHeader.hash().abridged() << ",idx=" << m_highestBlock.sealer()
              << ", Next: blk=" << m_consensusBlockNumber;
}

/// recv sign message from p2p
SignReq PBFTConsensus::handleSignMsg(PBFTMsgPacket const& pbftMsg)
{
    Timer t;
    SignReq sign_req;
    bool valid = decodeToRequests(sign_req, ref(pbftMsg.data));
    if (!valid)
        return sign_req;
    ostringstream oss;
    oss << "handleSignMsg: idx=" << sign_req.idx << ",view=" << sign_req.view
        << ",blk=" << sign_req.height << ",hash=" << sign_req.block_hash.abridged()
        << ", from=" << pbftMsg.node_id;

    valid = isValidSignReq(sign_req, oss);
    if (!valid)
        return sign_req;
    m_reqCache->addSignReq(sign_req);
    checkAndCommit();
    LOG(DEBUG) << "handleSignMsg, timecost=" << 1000 * t.elapsed();
    return sign_req;
}

inline bool PBFTConsensus::isValidSignReq(SignReq const& req, ostringstream& oss) const
{
    if (m_reqCache->isExistSign(req))
    {
        LOG(ERROR) << oss.str() << "Discard a duplicated sign";
        return false;
    }
    CheckResult result = checkReq(req, oss);
    if (result == CheckResult::FUTURE)
    {
        m_reqCache->addSignReq(req);
        return false;
    }
    if (result == CheckResult::INVALID)
        return false;
    return true;
}

CommitReq PBFTConsensus::handleCommitMsg(PBFTMsgPacket const& pbftMsg)
{
    Timer t;
    CommitReq commit_req;
    bool valid = decodeToRequests(commit_req, ref(pbftMsg.data));
    if (!valid)
        return commit_req;
    ostringstream oss;
    oss << "handleCommitMsg: idx=" << commit_req.idx << ",view=" << commit_req.view
        << ",blk=" << commit_req.height << ",hash=" << commit_req.block_hash.abridged()
        << ", from=" << pbftMsg.node_id;

    valid = isValidCommitReq(commit_req, oss);
    if (!valid)
        return commit_req;
    m_reqCache->addCommitReq(commit_req);
    checkAndSave();
    LOG(DEBUG) << "handleCommitMsg, timecost=" << 1000 * t.elapsed();
    return commit_req;
}

inline bool PBFTConsensus::isValidCommitReq(CommitReq const& req, ostringstream& oss) const
{
    if (m_reqCache->isExistCommit(req))
    {
        LOG(ERROR) << oss.str() << ", Discard duplicated commit request";
        return false;
    }
    CheckResult result = checkReq(req, oss);
    if (result == CheckResult::FUTURE)
    {
        m_reqCache->addCommitReq(req);
        return false;
    }
    if (result == CheckResult::INVALID)
        return false;
    return true;
}

ViewChangeReq PBFTConsensus::handleViewChangeMsg(PBFTMsgPacket const& pbftMsg)
{
    ViewChangeReq viewChange_req;
    bool valid = decodeToRequests(viewChange_req, ref(pbftMsg.data));
    if (!valid)
        return viewChange_req;
    ostringstream oss;
    oss << "handleViewChangeMsg: idx=" << viewChange_req.idx << ",view=" << viewChange_req.view
        << ",blk=" << viewChange_req.height << ",hash=" << viewChange_req.block_hash.abridged()
        << ",from=" << pbftMsg.node_id;
    valid = isValidViewChangeReq(viewChange_req, oss);
    if (!valid)
        return viewChange_req;
    catchupView(viewChange_req, oss);
    m_reqCache->addViewChangeReq(viewChange_req);
    if (viewChange_req.view == m_toView)
        checkAndChangeView();
    else
    {
        u256 min_view = u256(0);
        bool should_trigger = m_reqCache->canTriggerViewChange(
            min_view, m_f, m_toView, m_highestBlock, m_consensusBlockNumber, viewChange_req);
        if (should_trigger)
        {
            LOG(INFO) << "Fast start viewchange, m_to_view = " << m_toView
                      << ",req.view = " << viewChange_req.view << ",min_view = " << min_view;
            m_timeManager.changeView();
            m_toView = min_view - u256(1);
            m_signalled.notify_all();
        }
    }
    return viewChange_req;
}

inline bool PBFTConsensus::isValidViewChangeReq(ViewChangeReq const& req, ostringstream& oss) const
{
    if (m_reqCache->isExistViewChange(req))
    {
        LOG(ERROR) << oss.str() << ", Discard duplicated view change request";
        return false;
    }
    /// check view and block height
    if (req.height < m_highestBlock.number() || req.view <= m_view)
    {
        LOG(ERROR) << oss.str() << ", Discard illegal viewchange, req.height=" << req.height
                   << ", m_highestBlock.height=" << m_highestBlock.number()
                   << ", req.view=" << req.view << ", m_view=" << m_view;
        return false;
    }
    /// check block hash
    if (req.height == m_highestBlock.number() && req.block_hash != m_highestBlock.hash())
    {
        LOG(ERROR) << oss.str()
                   << ", Discard an illegal viewchange for block hash diff, req.block_hash="
                   << req.block_hash << ", m_highestBlock.hash()=" << m_highestBlock.hash();
        return false;
    }
    if (!checkSign(req))
    {
        LOG(ERROR) << oss.str() << ", CheckSign failed";
        return false;
    }
    return true;
}

inline void PBFTConsensus::catchupView(ViewChangeReq const& req, ostringstream& oss)
{
    if (req.view + u256(1) < m_toView)
    {
        LOG(INFO) << oss.str()
                  << " broadcast view change requst for view-behind, req.view=" << req.view
                  << "m_toView = " << m_toView;
        broadcastViewChangeReq();
    }
}

void PBFTConsensus::checkAndChangeView()
{
    u256 count = m_reqCache->getViewChangeSize(m_toView);
    if (count >= minValidNodes() - u256(1))
    {
        LOG(INFO) << "######### Reach consensus, to_view=" << m_toView;
        m_leaderFailed = false;
        m_view = m_toView;
        m_reqCache->triggerViewChange(m_view);
        m_broadCastCache->clearAll();
    }
}

void PBFTConsensus::collectGarbage()
{
    if (!m_highestBlock)
        return;
    Timer t;
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    if (now - m_timeManager.m_lastGarbageCollection >
        std::chrono::seconds(TimeManager::CollectInterval))
    {
        m_reqCache->collectGarbage(m_highestBlock);
        m_timeManager.m_lastGarbageCollection = now;
        LOG(TRACE) << "collectGarbage timecost(ms)=" << 1000 * t.elapsed();
    }
}

void PBFTConsensus::checkTimeout()
{
    if (m_timeManager.isTimeout())
    {
        Timer t;
        m_toView += u256(1);
        m_leaderFailed = true;
        m_timeManager.updateChangeCycle();
        m_timeManager.m_lastConsensusTime = utcTime();
        m_reqCache->removeInvalidViewChange(m_toView, m_highestBlock);
        broadcastViewChangeReq();
        checkAndChangeView();
        LOG(DEBUG) << "checkTimeout timecost=" << t.elapsed() << ", m_view=" << m_view
                   << ",m_toView=" << m_toView;
    }
}

void PBFTConsensus::handleMsg(PBFTMsgPacket const& pbftMsg)
{
    PBFTMsg pbft_msg;
    std::string key;
    switch (pbftMsg.packet_id)
    {
    case PrepareReqPacket:
    {
        PrepareReq prepare_req = handlePrepareMsg(pbftMsg);
        key = prepare_req.block_hash.hex();
        pbft_msg = prepare_req;
        break;
    }
    case SignReqPacket:
    {
        SignReq req = handleSignMsg(pbftMsg);
        key = req.sig.hex();
        pbft_msg = req;
        break;
    }
    case CommitReqPacket:
    {
        CommitReq req = handleCommitMsg(pbftMsg);
        key = req.sig.hex();
        pbft_msg = req;
        break;
    }
    case ViewChangeReqPacket:
    {
        ViewChangeReq req = handleViewChangeMsg(pbftMsg);
        key = req.sig.hex() + toJS(req.view);
        pbft_msg = req;
        break;
    }
    default:
    {
        LOG(ERROR) << "Recv error msg, id=" << pbftMsg.node_idx;
        return;
    }
    }
    auto now_time = u256(utcTime());
    bool time_flag = (pbft_msg.timestamp >= now_time) ||
                     (now_time - pbft_msg.timestamp < u256(m_timeManager.m_viewTimeout));
    bool height_flag = (pbft_msg.height > m_highestBlock.number()) ||
                       (m_highestBlock.number() - pbft_msg.height < 10);
    if (key.size() > 0 && time_flag && height_flag)
    {
        std::unordered_set<h512> filter;
        filter.insert(pbftMsg.node_id);
        /// get the origin gen node id of the request
        h512 gen_node_id = getMinerByIndex(pbft_msg.idx.convert_to<size_t>());
        if (gen_node_id != h512())
            filter.insert(gen_node_id);
        broadcastMsg(pbftMsg.packet_id, key, ref(pbftMsg.data), filter);
    }
}

void PBFTConsensus::workLoop()
{
    while (isWorking())
    {
        try
        {
            std::pair<bool, PBFTMsgPacket> ret = m_msgQueue.tryPop(c_PopWaitSeconds);
            if (ret.first)
                handleMsg(ret.second);
            else
            {
                std::unique_lock<std::mutex> l(x_signalled);
                m_signalled.wait_for(l, chrono::milliseconds(5));
            }
            checkTimeout();
            handleFutureBlock();
            collectGarbage();
        }
        catch (std::exception& _e)
        {
            LOG(ERROR) << _e.what();
        }
    }
}

void PBFTConsensus::handleFutureBlock()
{
    PrepareReq future_req = m_reqCache->futurePrepareCache();
    if (future_req.height == m_consensusBlockNumber && future_req.view == m_view)
    {
        LOG(INFO) << "handleFurtureBlock, blk = " << m_reqCache->futurePrepareCache().height;
        handlePrepareMsg(future_req);
        m_reqCache->resetFuturePrepare();
    }
}

}  // namespace consensus
}  // namespace dev
