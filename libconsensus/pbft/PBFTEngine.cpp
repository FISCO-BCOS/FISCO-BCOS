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
 * @file: PBFTEngine.cpp
 * @author: yujiechen
 * @date: 2018-09-28
 */
#include "PBFTEngine.h"
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/CommonJS.h>
#include <libethcore/CommonJS.h>
#include <libsecurity/EncryptedLevelDB.h>
#include <libtxpool/TxPool.h>
using namespace dev::eth;
using namespace dev::db;
using namespace dev::blockverifier;
using namespace dev::blockchain;
using namespace dev::p2p;
using namespace dev::storage;

namespace dev
{
namespace consensus
{
const std::string PBFTEngine::c_backupKeyCommitted = "committed";
const std::string PBFTEngine::c_backupMsgDirName = "pbftMsgBackup";

void PBFTEngine::start()
{
    initPBFTEnv(3 * getEmptyBlockGenTime());
    ConsensusEngineBase::start();
    PBFTENGINE_LOG(INFO) << "[Start PBFTEngine...]";
}

void PBFTEngine::initPBFTEnv(unsigned view_timeout)
{
    m_consensusBlockNumber = 0;
    m_view = m_toView = 0;
    m_leaderFailed = false;
    auto block = m_blockChain->getBlockByNumber(m_blockChain->number());
    if (!block)
    {
        PBFTENGINE_LOG(FATAL) << "can't find latest block";
    }
    reportBlock(*block);
    initBackupDB();
    m_timeManager.initTimerManager(view_timeout);
    PBFTENGINE_LOG(INFO) << "[PBFT init env successfully]";
}

bool PBFTEngine::shouldSeal()
{
    if (m_cfgErr || m_accountType != NodeAccountType::SealerAccount)
    {
        return false;
    }
    /// check leader
    std::pair<bool, IDXTYPE> ret = getLeader();
    if (!ret.first)
    {
        return false;
    }
    if (ret.second != nodeIdx())
    {
        /// if current node is the next leader
        /// and it has been notified to seal new block, return true
        if (m_notifyNextLeaderSeal && getNextLeader() == nodeIdx())
        {
            return true;
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

/**
 * @brief: rehandle the unsubmitted committedPrepare
 * @param req: the unsubmitted committed prepareReq
 */
void PBFTEngine::rehandleCommitedPrepareCache(PrepareReq const& req)
{
    Guard l(m_mutex);
    PBFTENGINE_LOG(INFO) << LOG_DESC("rehandleCommittedPrepare") << LOG_KV("nodeIdx", nodeIdx())
                         << LOG_KV("nodeId", m_keyPair.pub().abridged())
                         << LOG_KV("hash", req.block_hash.abridged()) << LOG_KV("H", req.height);
    m_broadCastCache->clearAll();
    PrepareReq prepare_req(req, m_keyPair, m_view, nodeIdx());

    std::shared_ptr<bytes> prepare_data = std::make_shared<bytes>();
    prepare_req.encode(*prepare_data);
    /// broadcast prepare message
    broadcastMsg(PrepareReqPacket, prepare_req.uniqueKey(), ref(*prepare_data));
    handlePrepareMsg(prepare_req);
    /// note blockSync to the latest number, in case of the block number of other nodes is larger
    /// than this node
    m_blockSync->noteSealingBlockNumber(m_blockChain->number());
}

/// recalculate m_nodeNum && m_f && m_cfgErr(must called after setSigList)
void PBFTEngine::resetConfig()
{
    updateMaxBlockTransactions();
    auto node_idx = MAXIDX;
    m_accountType = NodeAccountType::ObserverAccount;
    size_t nodeNum = 0;
    updateConsensusNodeList();
    {
        ReadGuard l(m_sealerListMutex);
        for (size_t i = 0; i < m_sealerList.size(); i++)
        {
            if (m_sealerList[i] == m_keyPair.pub())
            {
                m_accountType = NodeAccountType::SealerAccount;
                node_idx = i;
                break;
            }
        }
        nodeNum = m_sealerList.size();
    }
    if (nodeNum < 1)
    {
        PBFTENGINE_LOG(ERROR) << LOG_DESC(
            "Must set at least one pbft sealer, current number of sealers is zero");
        raise(SIGTERM);
        BOOST_THROW_EXCEPTION(
            EmptySealers() << errinfo_comment("Must set at least one pbft sealer!"));
    }
    // update m_nodeNum
    if (m_nodeNum != nodeNum)
    {
        m_nodeNum = nodeNum;
    }
    m_f = (m_nodeNum - 1) / 3;
    m_cfgErr = (node_idx == MAXIDX);
    m_idx = node_idx;
}

/// init pbftMsgBackup
void PBFTEngine::initBackupDB()
{
    /// try-catch has already been considered by libdevcore/LevelDB.*
    std::string path = getBackupMsgPath();
    boost::filesystem::path path_handler = boost::filesystem::path(path);
    if (!boost::filesystem::exists(path_handler))
    {
        boost::filesystem::create_directories(path_handler);
    }

    db::BasicLevelDB* basicDB = NULL;
    leveldb::Status status;

    if (g_BCOSConfig.diskEncryption.enable && g_BCOSConfig.version() <= RC3_VERSION)
    {
        status =
            EncryptedLevelDB::Open(LevelDB::defaultDBOptions(), path_handler.string(), &basicDB,
                g_BCOSConfig.diskEncryption.cipherDataKey, g_BCOSConfig.diskEncryption.dataKey);
    }
    else
    {
        status = BasicLevelDB::Open(LevelDB::defaultDBOptions(), path_handler.string(), &basicDB);
    }

    LevelDB::checkStatus(status, path_handler);

    m_backupDB = std::make_shared<LevelDB>(basicDB);

    if (!isDiskSpaceEnough(path))
    {
        PBFTENGINE_LOG(ERROR) << LOG_DESC(
            "initBackupDB: Disk space is insufficient, less than 100MB. Release disk space and try "
            "again");
        raise(SIGTERM);
        BOOST_THROW_EXCEPTION(NotEnoughAvailableSpace());
    }
    // reload msg from db to commited-prepare-cache
    reloadMsg(c_backupKeyCommitted, m_reqCache->mutableCommittedPrepareCache());
}

/**
 * @brief: reload PBFTMsg from DB to msg according to specified key
 * @param key: key used to index the PBFTMsg
 * @param msg: save the PBFTMsg readed from the DB
 */
void PBFTEngine::reloadMsg(std::string const& key, PBFTMsg* msg)
{
    if (!m_backupDB || !msg)
    {
        return;
    }
    try
    {
        bytes data = fromHex(m_backupDB->lookup(key));
        if (data.empty())
        {
            PBFTENGINE_LOG(DEBUG) << LOG_DESC("reloadMsg: Empty message stored")
                                  << LOG_KV("nodeIdx", nodeIdx())
                                  << LOG_KV("nodeId", m_keyPair.pub().abridged());
            return;
        }
        msg->decode(ref(data), 0);
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("reloadMsg") << LOG_KV("fromIdx", msg->idx)
                              << LOG_KV("nodeId", m_keyPair.pub().abridged())
                              << LOG_KV("H", msg->height)
                              << LOG_KV("hash", msg->block_hash.abridged())
                              << LOG_KV("nodeIdx", nodeIdx())
                              << LOG_KV("myNode", m_keyPair.pub().abridged());
    }
    catch (std::exception& e)
    {
        PBFTENGINE_LOG(WARNING) << LOG_DESC("reloadMsg from db failed")
                                << LOG_KV("EINFO", boost::diagnostic_information(e));
        return;
    }
}

/**
 * @brief: backup specified PBFTMsg with specified key into the DB
 * @param _key: key of the PBFTMsg
 * @param _msg : data to backup in the DB
 */
void PBFTEngine::backupMsg(std::string const& _key, PBFTMsg const& _msg)
{
    if (!m_backupDB)
    {
        return;
    }
    bytes message_data;
    _msg.encode(message_data);
    try
    {
        m_backupDB->insert(_key, toHex(message_data));
    }
    catch (DatabaseError const& e)
    {
        PBFTENGINE_LOG(ERROR) << LOG_BADGE("DatabaseError")
                              << LOG_DESC("store backupMsg to db failed")
                              << LOG_KV("EINFO", boost::diagnostic_information(e));
        raise(SIGTERM);
        BOOST_THROW_EXCEPTION(std::invalid_argument(" store backupMsg to leveldb failed."));
    }
    catch (std::exception const& e)
    {
        PBFTENGINE_LOG(ERROR) << LOG_DESC("store backupMsg to db failed")
                              << LOG_KV("EINFO", boost::diagnostic_information(e));
        raise(SIGTERM);
        BOOST_THROW_EXCEPTION(std::invalid_argument(" store backupMsg to leveldb failed."));
    }
}

/// sealing the generated block into prepareReq and push its to msgQueue
bool PBFTEngine::generatePrepare(Block const& block)
{
    Guard l(m_mutex);
    m_notifyNextLeaderSeal = false;
    PrepareReq prepare_req(block, m_keyPair, m_view, nodeIdx());

    // broadcast prepare request
    m_threadPool->enqueue([=]() {
        std::shared_ptr<bytes> prepare_data = std::make_shared<bytes>();
        prepare_req.encode(*prepare_data);
        broadcastMsg(PrepareReqPacket, prepare_req.uniqueKey(), ref(*prepare_data));
    });

    if (prepare_req.pBlock->getTransactionSize() == 0 && m_omitEmptyBlock)
    {
        m_leaderFailed = true;
        changeViewForFastViewChange();
        m_timeManager.m_changeCycle = 0;
        return true;
    }
    handlePrepareMsg(prepare_req);

    /// reset the block according to broadcast result
    PBFTENGINE_LOG(INFO) << LOG_DESC("generateLocalPrepare")
                         << LOG_KV("hash", prepare_req.block_hash.abridged())
                         << LOG_KV("H", prepare_req.height) << LOG_KV("nodeIdx", nodeIdx())
                         << LOG_KV("myNode", m_keyPair.pub().abridged());
    m_signalled.notify_all();
    return true;
}

/**
 * @brief : 1. generate and broadcast signReq according to given prepareReq,
 *          2. add the generated signReq into the cache
 * @param req: specified PrepareReq used to generate signReq
 */
bool PBFTEngine::broadcastSignReq(PrepareReq const& req)
{
    SignReq sign_req(req, m_keyPair, nodeIdx());
    bytes sign_req_data;
    sign_req.encode(sign_req_data);
    bool succ = broadcastMsg(SignReqPacket, sign_req.uniqueKey(), ref(sign_req_data));
    m_reqCache->addSignReq(sign_req);
    return succ;
}

bool PBFTEngine::getNodeIDByIndex(h512& nodeID, const IDXTYPE& idx) const
{
    nodeID = getSealerByIndex(idx);
    if (nodeID == h512())
    {
        PBFTENGINE_LOG(ERROR) << LOG_DESC("getNodeIDByIndex: not sealer") << LOG_KV("Idx", idx)
                              << LOG_KV("myNode", m_keyPair.pub().abridged());
        return false;
    }
    return true;
}

bool PBFTEngine::checkSign(PBFTMsg const& req) const
{
    h512 node_id;
    if (getNodeIDByIndex(node_id, req.idx))
    {
        Public pub_id = jsToPublic(toJS(node_id.hex()));
        return dev::verify(pub_id, req.sig, req.block_hash) &&
               dev::verify(pub_id, req.sig2, req.fieldsWithoutBlock());
    }
    return false;
}

/**
 * @brief: 1. generate commitReq according to prepare req
 *         2. broadcast the commitReq
 * @param req: the prepareReq that used to generate commitReq
 */
bool PBFTEngine::broadcastCommitReq(PrepareReq const& req)
{
    CommitReq commit_req(req, m_keyPair, nodeIdx());
    bytes commit_req_data;
    commit_req.encode(commit_req_data);
    bool succ = broadcastMsg(CommitReqPacket, commit_req.uniqueKey(), ref(commit_req_data));
    if (succ)
        m_reqCache->addCommitReq(commit_req);
    return succ;
}


/// send view change message to the given node
void PBFTEngine::sendViewChangeMsg(dev::network::NodeID const& nodeId)
{
    ViewChangeReq req(
        m_keyPair, m_highestBlock.number(), m_toView, nodeIdx(), m_highestBlock.hash());
    PBFTENGINE_LOG(INFO) << LOG_DESC("sendViewChangeMsg: send viewchange to started node")
                         << LOG_KV("v", m_view) << LOG_KV("toV", m_toView)
                         << LOG_KV("curNum", m_highestBlock.number())
                         << LOG_KV("peerNode", nodeId.abridged())
                         << LOG_KV("hash", req.block_hash.abridged())
                         << LOG_KV("nodeIdx", nodeIdx())
                         << LOG_KV("myNode", m_keyPair.pub().abridged());

    bytes view_change_data;
    req.encode(view_change_data);
    sendMsg(nodeId, ViewChangeReqPacket, req.uniqueKey(), ref(view_change_data));
}

bool PBFTEngine::broadcastViewChangeReq()
{
    ViewChangeReq req(
        m_keyPair, m_highestBlock.number(), m_toView, nodeIdx(), m_highestBlock.hash());
    PBFTENGINE_LOG(DEBUG) << LOG_DESC("broadcastViewChangeReq ") << LOG_KV("v", m_view)
                          << LOG_KV("toV", m_toView) << LOG_KV("curNum", m_highestBlock.number())
                          << LOG_KV("hash", req.block_hash.abridged())
                          << LOG_KV("nodeIdx", nodeIdx())
                          << LOG_KV("myNode", m_keyPair.pub().abridged());
    /// view change not caused by fast view change
    if (!m_fastViewChange)
    {
        PBFTENGINE_LOG(WARNING) << LOG_DESC("ViewChangeWarning: not caused by omit empty block ")
                                << LOG_KV("v", m_view) << LOG_KV("toV", m_toView)
                                << LOG_KV("curNum", m_highestBlock.number())
                                << LOG_KV("hash", req.block_hash.abridged())
                                << LOG_KV("nodeIdx", nodeIdx())
                                << LOG_KV("myNode", m_keyPair.pub().abridged());
    }

    bytes view_change_data;
    req.encode(view_change_data);
    return broadcastMsg(ViewChangeReqPacket, req.uniqueKey(), ref(view_change_data));
}

/// set default ttl to 1 to in case of forward-broadcast
bool PBFTEngine::sendMsg(dev::network::NodeID const& nodeId, unsigned const& packetType,
    std::string const& key, bytesConstRef data, unsigned const& ttl)
{
    /// is sealer?
    if (getIndexBySealer(nodeId) < 0)
    {
        return true;
    }
    /// packet has been broadcasted?
    if (broadcastFilter(nodeId, packetType, key))
    {
        return true;
    }
    auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    if (sessions.size() == 0)
    {
        return false;
    }
    for (auto session : sessions)
    {
        if (session.nodeID() == nodeId)
        {
            m_service->asyncSendMessageByNodeID(
                session.nodeID(), transDataToMessage(data, packetType, ttl), nullptr);
            PBFTENGINE_LOG(DEBUG) << LOG_DESC("sendMsg") << LOG_KV("packetType", packetType)
                                  << LOG_KV("dstNodeId", nodeId.abridged())
                                  << LOG_KV("remote_endpoint", session.nodeIPEndpoint.name())
                                  << LOG_KV("nodeIdx", nodeIdx())
                                  << LOG_KV("myNode", m_keyPair.pub().abridged());
            broadcastMark(session.nodeID(), packetType, key);
            return true;
        }
    }
    return false;
}

/**
 * @brief: broadcast specified message to all-peers with cache-filter and specified filter
 *         broadcast solutions:
 *         1. peer is not the sealer: stop broadcasting
 *         2. peer is in the filter list: mark the message as broadcasted, and stop broadcasting
 *         3. the packet has been broadcasted: stop broadcast
 * @param packetType: the packet type of the broadcast-message
 * @param key: the key of the broadcast-message(is the signature of the message in common)
 * @param data: the encoded data of to be broadcasted(RLP encoder now)
 * @param filter: the list that shouldn't be broadcasted to
 */
bool PBFTEngine::broadcastMsg(unsigned const& packetType, std::string const& key,
    bytesConstRef data, std::unordered_set<h512> const& filter, unsigned const& ttl)
{
    auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    m_connectedNode = sessions.size();
    NodeIDs nodeIdList;
    for (auto session : sessions)
    {
        /// get node index of the sealer from m_sealerList failed ?
        if (getIndexBySealer(session.nodeID()) < 0)
            continue;
        /// peer is in the _filter list ?
        if (filter.count(session.nodeID()))
        {
            broadcastMark(session.nodeID(), packetType, key);
            continue;
        }
        /// packet has been broadcasted?
        if (broadcastFilter(session.nodeID(), packetType, key))
            continue;
        PBFTENGINE_LOG(TRACE) << LOG_DESC("broadcastMsg") << LOG_KV("packetType", packetType)
                              << LOG_KV("dstNodeId", session.nodeID().abridged())
                              << LOG_KV("dstIp", session.nodeIPEndpoint.name())
                              << LOG_KV("ttl", (ttl == 0 ? maxTTL : ttl))
                              << LOG_KV("nodeIdx", nodeIdx())
                              << LOG_KV("toNode", session.nodeID().abridged());
        nodeIdList.push_back(session.nodeID());
        broadcastMark(session.nodeID(), packetType, key);
    }
    /// send messages according to node id
    m_service->asyncMulticastMessageByNodeIDList(
        nodeIdList, transDataToMessage(data, packetType, ttl));
    return true;
}

/**
 * @brief: check the specified prepareReq is valid or not
 *       1. should not be existed in the prepareCache
 *       2. if allowSelf is false, shouldn't be generated from the node-self
 *       3. hash of committed prepare should be equal to the block hash of prepareReq if their
 * height is equal
 *       4. sign of PrepareReq should be valid(public key to verify sign is obtained according to
 * req.idx)
 * @param req: the prepareReq need to be checked
 * @param allowSelf: whether can solve prepareReq generated by self-node
 * @param oss
 * @return true: the specified prepareReq is valid
 * @return false: the specified prepareReq is invalid
 */
CheckResult PBFTEngine::isValidPrepare(PrepareReq const& req, std::ostringstream& oss) const
{
    if (m_reqCache->isExistPrepare(req))
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InvalidPrepare: Duplicated Prep")
                              << LOG_KV("EINFO", oss.str());
        return CheckResult::INVALID;
    }
    if (isSyncingHigherBlock(req))
    {
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("InvalidPrepare: Is Syncing higher number")
                              << LOG_KV("EINFO", oss.str());
        return CheckResult::INVALID;
    }
    if (hasConsensused(req))
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InvalidPrepare: Consensused Prep")
                              << LOG_KV("EINFO", oss.str());
        return CheckResult::INVALID;
    }

    if (isFuturePrepare(req))
    {
        PBFTENGINE_LOG(INFO) << LOG_DESC("FutureBlock") << LOG_KV("EINFO", oss.str());
        m_reqCache->addFuturePrepareCache(req);
        return CheckResult::FUTURE;
    }
    if (!isValidLeader(req))
    {
        return CheckResult::INVALID;
    }
    if (!isHashSavedAfterCommit(req))
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InvalidPrepare: not saved after commit")
                              << LOG_KV("EINFO", oss.str());
        return CheckResult::INVALID;
    }
    if (!checkSign(req))
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InvalidPrepare: invalid signature")
                              << LOG_KV("EINFO", oss.str());
        return CheckResult::INVALID;
    }
    return CheckResult::VALID;
}

/// check sealer list
void PBFTEngine::checkSealerList(Block const& block)
{
    ReadGuard l(m_sealerListMutex);
    if (m_sealerList != block.blockHeader().sealerList())
    {
        PBFTENGINE_LOG(ERROR) << LOG_DESC("checkSealerList: wrong sealers")
                              << LOG_KV("Nsealer", m_sealerList.size())
                              << LOG_KV("NBlockSealer", block.blockHeader().sealerList().size())
                              << LOG_KV("hash", block.blockHeader().hash().abridged())
                              << LOG_KV("nodeIdx", nodeIdx())
                              << LOG_KV("myNode", m_keyPair.pub().abridged());
        BOOST_THROW_EXCEPTION(
            BlockSealerListWrong() << errinfo_comment("Wrong Sealer List of Block"));
    }
}

/// check Block sign
bool PBFTEngine::checkBlock(Block const& block)
{
    if (block.blockHeader().number() <= m_blockChain->number())
    {
        return false;
    }
    {
        Guard l(m_mutex);
        resetConfig();
    }
    auto sealers = sealerList();
    /// ignore the genesis block
    if (block.blockHeader().number() == 0)
    {
        return true;
    }
    {
        if (sealers != block.blockHeader().sealerList())
        {
            PBFTENGINE_LOG(ERROR) << LOG_DESC("checkBlock: wrong sealers")
                                  << LOG_KV("Nsealer", sealers.size())
                                  << LOG_KV("NBlockSealer", block.blockHeader().sealerList().size())
                                  << LOG_KV("hash", block.blockHeader().hash().abridged())
                                  << LOG_KV("nodeIdx", nodeIdx())
                                  << LOG_KV("myNode", m_keyPair.pub().abridged());
            return false;
        }
    }

    /// check sealer(sealer must be a sealer)
    if (getSealerByIndex(block.blockHeader().sealer().convert_to<size_t>()) == NodeID())
    {
        PBFTENGINE_LOG(ERROR) << LOG_DESC("checkBlock: invalid sealer ")
                              << LOG_KV("sealer", block.blockHeader().sealer());
        return false;
    }
    /// check sign num
    auto sig_list = block.sigList();
    if (sig_list->size() < minValidNodes())
    {
        PBFTENGINE_LOG(ERROR) << LOG_DESC("checkBlock: insufficient signatures")
                              << LOG_KV("signNum", sig_list->size())
                              << LOG_KV("minValidSign", minValidNodes());
        return false;
    }
    /// check sign
    for (auto& sign : *sig_list)
    {
        if (sign.first >= sealers.size())
        {
            PBFTENGINE_LOG(ERROR) << LOG_DESC("checkBlock: overflowed signer")
                                  << LOG_KV("signer", sign.first)
                                  << LOG_KV("Nsealer", sealers.size());
            return false;
        }
        if (!dev::verify(
                sealers[sign.first.convert_to<size_t>()], sign.second, block.blockHeader().hash()))
        {
            PBFTENGINE_LOG(ERROR) << LOG_DESC("checkBlock: invalid sign")
                                  << LOG_KV("signer", sign.first)
                                  << LOG_KV(
                                         "pub", sealers[sign.first.convert_to<size_t>()].abridged())
                                  << LOG_KV("hash", block.blockHeader().hash().abridged());
            return false;
        }
    }  /// end of check sign

    /// Check whether the number of transactions in block exceeds the limit
    if (block.transactions()->size() > maxBlockTransactions())
    {
        return false;
    }
    return true;
}

/**
 * @brief: notify the seal module to seal block if the current node is the next leader
 * @param block: block obtained from the prepare packet, used to filter transactions
 */
void PBFTEngine::notifySealing(dev::eth::Block const& block)
{
    if (!m_onNotifyNextLeaderReset)
    {
        return;
    }
    /// only if the current node is the next leader and not the current leader
    /// notify the seal module to seal new block
    if (getLeader().first == true && getLeader().second != nodeIdx() &&
        nodeIdx() == getNextLeader())
    {
        /// obtain transaction filters
        h256Hash filter;
        for (auto& trans : *(block.transactions()))
        {
            filter.insert(trans->sha3());
        }
        PBFTENGINE_LOG(INFO) << "I am the next leader = " << getNextLeader()
                             << ", filter trans size = " << filter.size()
                             << ", total trans = " << m_txPool->status().current;
        m_notifyNextLeaderSeal = true;
        /// function registered in PBFTSealer to reset the block for the next leader by
        /// resetting the block number to current block number + 2
        m_onNotifyNextLeaderReset(filter);
    }
}

void PBFTEngine::execBlock(Sealing& sealing, PrepareReq const& req, std::ostringstream&)
{
    /// no need to decode the local generated prepare packet
    auto start_time = utcTime();
    auto record_time = utcTime();
    if (req.pBlock)
    {
        //*(sealing.block) = *(req.pBlock);
        +sealing.block = req.pBlock;
    }
    /// decode the network received prepare packet
    else
    {
        // without receipt, with transaction hash(parallel calc txs' hash)
        sealing.block->decode(ref(req.block), CheckTransaction::None, false, true);
    }
    auto decode_time_cost = utcTime() - record_time;
    record_time = utcTime();

    m_sealingNumber = sealing.block->getTransactionSize();

    /// return directly if it's an empty block
    if (sealing.block->getTransactionSize() == 0 && m_omitEmptyBlock)
    {
        sealing.p_execContext = nullptr;
        return;
    }

    checkBlockValid(*(sealing.block));
    auto check_time_cost = utcTime() - record_time;
    record_time = utcTime();

    /// notify the next leader seal a new block
    /// this if condition to in case of dead-lock when generate local prepare and notifySealing
    if (req.idx != nodeIdx())
    {
        notifySealing(*(sealing.block));
    }
    auto notify_time_cost = utcTime() - record_time;
    record_time = utcTime();


    m_blockSync->noteSealingBlockNumber(sealing.block->header().number());
    auto noteSealing_time_cost = utcTime() - record_time;
    record_time = utcTime();

    /// ignore the signature verification of the transactions have already been verified in
    /// transation pool
    /// the transactions that has not been verified by the txpool should be verified
    m_txPool->verifyAndSetSenderForBlock(*sealing.block);
    auto verifyAndSetSender_time_cost = utcTime() - record_time;
    record_time = utcTime();


    sealing.p_execContext = executeBlock(*sealing.block);
    auto exec_time_cost = utcTime() - record_time;
    PBFTENGINE_LOG(INFO)
        << LOG_DESC("execBlock") << LOG_KV("blkNum", sealing.block->header().number())
        << LOG_KV("reqIdx", req.idx) << LOG_KV("hash", sealing.block->header().hash().abridged())
        << LOG_KV("nodeIdx", nodeIdx()) << LOG_KV("myNode", m_keyPair.pub().abridged())
        << LOG_KV("decodeCost", decode_time_cost) << LOG_KV("checkCost", check_time_cost)
        << LOG_KV("notifyCost", notify_time_cost)
        << LOG_KV("noteSealingCost", noteSealing_time_cost)
        << LOG_KV("currentCycle", m_timeManager.m_changeCycle)
        << LOG_KV("verifyAndSetSenderCost", verifyAndSetSender_time_cost)
        << LOG_KV("execCost", exec_time_cost)
        << LOG_KV("execPerTx", (float)exec_time_cost / (float)sealing.block->getTransactionSize())
        << LOG_KV("totalCost", utcTime() - start_time);
}

/// check whether the block is empty
bool PBFTEngine::needOmit(Sealing const& sealing)
{
    if (sealing.block->getTransactionSize() == 0 && m_omitEmptyBlock)
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("needOmit")
                              << LOG_KV("blkNum", sealing.block->blockHeader().number())
                              << LOG_KV("hash", sealing.block->blockHeader().hash().abridged())
                              << LOG_KV("nodeIdx", nodeIdx())
                              << LOG_KV("myNode", m_keyPair.pub().abridged());
        return true;
    }
    return false;
}

/**
 * @brief: this function is called when receive-given-protocol related message from the network
 *        1. check the validation of the network-received data(include the account type of the
 * sender and receiver)
 *        2. decode the data into PBFTMsgPacket
 *        3. push the message into message queue to handler later by workLoop
 * @param exception: exceptions related to the received-message
 * @param session: the session related to the network data(can get informations about the sender)
 * @param message: message constructed from data received from the network
 */
void PBFTEngine::onRecvPBFTMessage(
    NetworkException, std::shared_ptr<P2PSession> session, P2PMessage::Ptr message)
{
    if (nodeIdx() == MAXIDX)
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC(
            "onRecvPBFTMessage: I'm an observer or not in the group, drop the PBFT message packets "
            "directly");
        return;
    }
    PBFTMsgPacket pbft_msg;
    bool valid = decodeToRequests(pbft_msg, message, session);
    if (!valid)
    {
        return;
    }
    if (pbft_msg.packet_id <= ViewChangeReqPacket)
    {
        m_msgQueue.push(pbft_msg);
        /// notify to handleMsg after push new PBFTMsgPacket into m_msgQueue
        m_signalled.notify_all();
    }
    else
    {
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("onRecvPBFTMessage: illegal msg ")
                              << LOG_KV("fromId", pbft_msg.packet_id)
                              << LOG_KV("fromIp", pbft_msg.endpoint) << LOG_KV("nodeIdx", nodeIdx())
                              << LOG_KV("myNode", m_keyPair.pub().abridged());
    }
}

bool PBFTEngine::handlePrepareMsg(PrepareReq& prepare_req, PBFTMsgPacket const& pbftMsg)
{
    bool valid = decodeToRequests(prepare_req, ref(pbftMsg.data));
    if (!valid)
    {
        return false;
    }
    return handlePrepareMsg(prepare_req, pbftMsg.endpoint);
}

/**
 * @brief: handle the prepare request:
 *       1. check whether the prepareReq is valid or not
 *       2. if the prepareReq is valid:
 *       (1) add the prepareReq to raw-prepare-cache
 *       (2) execute the block
 *       (3) sign the prepareReq and broadcast the signed prepareReq
 *       (4) callback checkAndCommit function to determin can submit the block or not
 * @param prepare_req: the prepare request need to be handled
 * @param self: if generated-prepare-request need to handled, then set self to be true;
 *              else this function will filter the self-generated prepareReq
 */
bool PBFTEngine::handlePrepareMsg(PrepareReq const& prepareReq, std::string const& endpoint)
{
    Timer t;
    std::ostringstream oss;
    oss << LOG_DESC("handlePrepareMsg") << LOG_KV("reqIdx", prepareReq.idx)
        << LOG_KV("view", prepareReq.view) << LOG_KV("reqNum", prepareReq.height)
        << LOG_KV("curNum", m_highestBlock.number()) << LOG_KV("consNum", m_consensusBlockNumber)
        << LOG_KV("fromIp", endpoint) << LOG_KV("hash", prepareReq.block_hash.abridged())
        << LOG_KV("nodeIdx", nodeIdx()) << LOG_KV("myNode", m_keyPair.pub().abridged())
        << LOG_KV("curChangeCycle", m_timeManager.m_changeCycle);
    /// check the prepare request is valid or not
    auto valid_ret = isValidPrepare(prepareReq, oss);
    if (valid_ret == CheckResult::INVALID)
    {
        return false;
    }
    /// update the view for given idx
    updateViewMap(prepareReq.idx, prepareReq.view);

    if (valid_ret == CheckResult::FUTURE)
    {
        return true;
    }
    /// add raw prepare request
    m_reqCache->addRawPrepare(prepareReq);

    Sealing workingSealing;
    try
    {
        execBlock(workingSealing, prepareReq, oss);
        // old block (has already executed correctly by block sync)
        if (workingSealing.p_execContext == nullptr &&
            workingSealing.block->getTransactionSize() > 0)
        {
            return false;
        }
    }
    catch (std::exception& e)
    {
        PBFTENGINE_LOG(WARNING) << LOG_DESC("Block execute failed") << LOG_KV("INFO", oss.str())
                                << LOG_KV("EINFO", boost::diagnostic_information(e));
        return true;
    }
    /// whether to omit empty block
    if (needOmit(workingSealing))
    {
        changeViewForFastViewChange();
        m_timeManager.m_changeCycle = 0;
        return true;
    }

    /// generate prepare request with signature of this node to broadcast
    /// (can't change prepareReq since it may be broadcasted-forwarded to other nodes)
    auto startT = utcTime();
    PrepareReq sign_prepare(prepareReq, workingSealing, m_keyPair);
    m_execContextForAsyncReset.push_back(m_reqCache->prepareCache().p_execContext);
    m_reqCache->addPrepareReq(sign_prepare);
    PBFTENGINE_LOG(DEBUG) << LOG_DESC("handlePrepareMsg: add prepare cache and broadcastSignReq")
                          << LOG_KV("reqNum", sign_prepare.height)
                          << LOG_KV("hash", sign_prepare.block_hash.abridged())
                          << LOG_KV("nodeIdx", nodeIdx())
                          << LOG_KV("addPrepareTime", utcTime() - startT)
                          << LOG_KV("myNode", m_keyPair.pub().abridged());

    /// broadcast the re-generated signReq(add the signReq to cache)
    broadcastSignReq(sign_prepare);

    checkAndCommit();
    PBFTENGINE_LOG(INFO) << LOG_DESC("handlePrepareMsg Succ")
                         << LOG_KV("Timecost", 1000 * t.elapsed()) << LOG_KV("INFO", oss.str());
    return true;
}


void PBFTEngine::checkAndCommit()
{
    size_t sign_size = m_reqCache->getSigCacheSize(m_reqCache->prepareCache().block_hash);
    /// must be equal to minValidNodes:in case of callback checkAndCommit repeatly in a round of
    /// PBFT consensus
    if (sign_size == minValidNodes())
    {
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("checkAndCommit, SignReq enough")
                              << LOG_KV("number", m_reqCache->prepareCache().height)
                              << LOG_KV("sigSize", sign_size)
                              << LOG_KV("hash", m_reqCache->prepareCache().block_hash.abridged())
                              << LOG_KV("nodeIdx", nodeIdx())
                              << LOG_KV("myNode", m_keyPair.pub().abridged());
        if (m_reqCache->prepareCache().view != m_view)
        {
            PBFTENGINE_LOG(DEBUG) << LOG_DESC("checkAndCommit: InvalidView")
                                  << LOG_KV("prepView", m_reqCache->prepareCache().view)
                                  << LOG_KV("view", m_view)
                                  << LOG_KV(
                                         "hash", m_reqCache->prepareCache().block_hash.abridged())
                                  << LOG_KV("prepH", m_reqCache->prepareCache().height);
            return;
        }
        m_reqCache->updateCommittedPrepare();
        /// update and backup the commit cache
        PBFTENGINE_LOG(INFO) << LOG_DESC("checkAndCommit: backup/updateCommittedPrepare")
                             << LOG_KV("reqNum", m_reqCache->committedPrepareCache().height)
                             << LOG_KV("hash",
                                    m_reqCache->committedPrepareCache().block_hash.abridged())
                             << LOG_KV("nodeIdx", nodeIdx())
                             << LOG_KV("myNode", m_keyPair.pub().abridged());
        m_threadPool->enqueue(
            [=]() { backupMsg(c_backupKeyCommitted, m_reqCache->committedPrepareCache()); });

        PBFTENGINE_LOG(DEBUG) << LOG_DESC("checkAndCommit: broadcastCommitReq")
                              << LOG_KV("prepareHeight", m_reqCache->prepareCache().height)
                              << LOG_KV("hash", m_reqCache->prepareCache().block_hash.abridged())
                              << LOG_KV("nodeIdx", nodeIdx())
                              << LOG_KV("myNode", m_keyPair.pub().abridged());

        if (!broadcastCommitReq(m_reqCache->prepareCache()))
        {
            PBFTENGINE_LOG(WARNING) << LOG_DESC("checkAndCommit: broadcastCommitReq failed");
        }

        m_timeManager.m_lastSignTime = utcTime();
        checkAndSave();
    }
}

/// if collect >= 2/3 SignReq and CommitReq, then callback this function to commit block
/// check whether view and height is valid, if valid, then commit the block and clear the context
void PBFTEngine::checkAndSave()
{
    auto start_commit_time = utcTime();
    auto record_time = utcTime();
    size_t sign_size = m_reqCache->getSigCacheSize(m_reqCache->prepareCache().block_hash);
    size_t commit_size = m_reqCache->getCommitCacheSize(m_reqCache->prepareCache().block_hash);
    if (sign_size >= minValidNodes() && commit_size >= minValidNodes())
    {
        PBFTENGINE_LOG(INFO) << LOG_DESC("checkAndSave: CommitReq enough")
                             << LOG_KV("prepareHeight", m_reqCache->prepareCache().height)
                             << LOG_KV("commitSize", commit_size)
                             << LOG_KV("hash", m_reqCache->prepareCache().block_hash.abridged())
                             << LOG_KV("nodeIdx", nodeIdx())
                             << LOG_KV("myNode", m_keyPair.pub().abridged());
        if (m_reqCache->prepareCache().view != m_view)
        {
            PBFTENGINE_LOG(DEBUG) << LOG_DESC("checkAndSave: InvalidView")
                                  << LOG_KV("prepView", m_reqCache->prepareCache().view)
                                  << LOG_KV("view", m_view)
                                  << LOG_KV("prepHeight", m_reqCache->prepareCache().height)
                                  << LOG_KV(
                                         "hash", m_reqCache->prepareCache().block_hash.abridged())
                                  << LOG_KV("nodeIdx", nodeIdx())
                                  << LOG_KV("myNode", m_keyPair.pub().abridged());
            return;
        }
        /// add sign-list into the block header
        if (m_reqCache->prepareCache().height > m_highestBlock.number())
        {
            /// Block block(m_reqCache->prepareCache().block);
            std::shared_ptr<dev::eth::Block> p_block = m_reqCache->prepareCache().pBlock;
            m_reqCache->generateAndSetSigList(*p_block, minValidNodes());
            auto genSig_time_cost = utcTime() - record_time;
            record_time = utcTime();
            /// callback block chain to commit block
            CommitResult ret = m_blockChain->commitBlock(p_block,
                std::shared_ptr<ExecutiveContext>(m_reqCache->prepareCache().p_execContext));
            auto commitBlock_time_cost = utcTime() - record_time;
            record_time = utcTime();

            /// drop handled transactions
            if (ret == CommitResult::OK)
            {
                dropHandledTransactions(p_block);
                auto dropTxs_time_cost = utcTime() - record_time;
                record_time = utcTime();
                m_blockSync->noteSealingBlockNumber(m_reqCache->prepareCache().height);
                auto noteSealing_time_cost = utcTime() - record_time;
                PBFTENGINE_LOG(INFO)
                    << LOG_DESC("CommitBlock Succ")
                    << LOG_KV("prepareHeight", m_reqCache->prepareCache().height)
                    << LOG_KV("reqIdx", m_reqCache->prepareCache().idx)
                    << LOG_KV("hash", m_reqCache->prepareCache().block_hash.abridged())
                    << LOG_KV("nodeIdx", nodeIdx()) << LOG_KV("myNode", m_keyPair.pub().abridged())
                    << LOG_KV("genSigTimeCost", genSig_time_cost)
                    << LOG_KV("commitBlockTimeCost", commitBlock_time_cost)
                    << LOG_KV("dropTxsTimeCost", dropTxs_time_cost)
                    << LOG_KV("noteSealingTimeCost", noteSealing_time_cost)
                    << LOG_KV("totalTimeCost", utcTime() - start_commit_time);
                m_reqCache->delCache(m_reqCache->prepareCache().block_hash);
                m_reqCache->removeInvalidFutureCache(m_highestBlock);
            }
            else
            {
                PBFTENGINE_LOG(WARNING)
                    << LOG_DESC("CommitBlock Failed")
                    << LOG_KV("reqNum", p_block->blockHeader().number())
                    << LOG_KV("curNum", m_highestBlock.number())
                    << LOG_KV("reqIdx", m_reqCache->prepareCache().idx)
                    << LOG_KV("hash", p_block->blockHeader().hash().abridged())
                    << LOG_KV("nodeIdx", nodeIdx()) << LOG_KV("myNode", m_keyPair.pub().abridged());
                /// note blocksync to sync
                m_blockSync->noteSealingBlockNumber(m_blockChain->number());
                m_txPool->handleBadBlock(*p_block);
            }
        }
        else
        {
            PBFTENGINE_LOG(WARNING)
                << LOG_DESC("checkAndSave: Consensus Failed, Block already exists")
                << LOG_KV("reqNum", m_reqCache->prepareCache().height)
                << LOG_KV("curNum", m_highestBlock.number())
                << LOG_KV("blkHash", m_reqCache->prepareCache().block_hash.abridged())
                << LOG_KV("highHash", m_highestBlock.hash().abridged())
                << LOG_KV("nodeIdx", nodeIdx()) << LOG_KV("myNode", m_keyPair.pub().abridged());
        }
    }
}

void PBFTEngine::reportBlock(Block const& block)
{
    Guard l(m_mutex);
    reportBlockWithoutLock(block);
}
/// update the context of PBFT after commit a block into the block-chain
/// 1. update the highest to new-committed blockHeader
/// 2. update m_view/m_toView/m_leaderFailed/m_lastConsensusTime/m_consensusBlockNumber
/// 3. delete invalid view-change requests according to new highestBlock
/// 4. recalculate the m_nodeNum/m_f according to newer SealerList
/// 5. clear all caches related to prepareReq and signReq
void PBFTEngine::reportBlockWithoutLock(Block const& block)
{
    if (m_blockChain->number() == 0 || m_highestBlock.number() < block.blockHeader().number())
    {
        /// remove invalid future block
        m_reqCache->removeInvalidFutureCache(m_highestBlock);
        /// update the highest block
        m_highestBlock = block.blockHeader();
        if (m_highestBlock.number() >= m_consensusBlockNumber)
        {
            m_view = m_toView = 0;
            m_leaderFailed = false;
            m_timeManager.m_lastConsensusTime = utcTime();
            m_timeManager.m_changeCycle = 0;
            m_consensusBlockNumber = m_highestBlock.number() + 1;
            /// delete invalid view change requests from the cache
            m_reqCache->delInvalidViewChange(m_highestBlock);
        }
        resetConfig();
        if (m_onCommitBlock)
        {
            m_onCommitBlock(block.blockHeader().number(), block.getTransactionSize(),
                m_timeManager.m_changeCycle);
        }
        m_reqCache->delCache(m_highestBlock.hash());
        PBFTENGINE_LOG(INFO) << LOG_DESC("^^^^^^^^Report") << LOG_KV("num", m_highestBlock.number())
                             << LOG_KV("sealerIdx", m_highestBlock.sealer())
                             << LOG_KV("hash", m_highestBlock.hash().abridged())
                             << LOG_KV("next", m_consensusBlockNumber)
                             << LOG_KV("tx", block.getTransactionSize())
                             << LOG_KV("nodeIdx", nodeIdx());
    }
}

/**
 * @brief: 1. decode the network-received PBFTMsgPacket to signReq
 *         2. check the validation of the signReq
 *         3. submit the block into blockchain if the size of collected signReq and
 *            commitReq is over 2/3
 * @param sign_req: return value, the decoded signReq
 * @param pbftMsg: the network-received PBFTMsgPacket
 */
bool PBFTEngine::handleSignMsg(SignReq& sign_req, PBFTMsgPacket const& pbftMsg)
{
    Timer t;
    bool valid = decodeToRequests(sign_req, ref(pbftMsg.data));
    if (!valid)
    {
        return false;
    }
    std::ostringstream oss;
    oss << LOG_DESC("handleSignMsg") << LOG_KV("num", sign_req.height)
        << LOG_KV("curNum", m_highestBlock.number()) << LOG_KV("GenIdx", sign_req.idx)
        << LOG_KV("Sview", sign_req.view) << LOG_KV("view", m_view)
        << LOG_KV("fromIdx", pbftMsg.node_idx) << LOG_KV("fromNode", pbftMsg.node_id.abridged())
        << LOG_KV("fromIp", pbftMsg.endpoint) << LOG_KV("hash", sign_req.block_hash.abridged())
        << LOG_KV("nodeIdx", nodeIdx()) << LOG_KV("myNode", m_keyPair.pub().abridged());
    auto check_ret = isValidSignReq(sign_req, oss);
    if (check_ret == CheckResult::INVALID)
    {
        return false;
    }
    updateViewMap(sign_req.idx, sign_req.view);

    if (check_ret == CheckResult::FUTURE)
    {
        return true;
    }
    m_reqCache->addSignReq(sign_req);

    checkAndCommit();
    PBFTENGINE_LOG(INFO) << LOG_DESC("handleSignMsg Succ") << LOG_KV("Timecost", 1000 * t.elapsed())
                         << LOG_KV("INFO", oss.str());
    return true;
}

/**
 * @brief: check the given signReq is valid or not
 *         1. the signReq shouldn't be existed in the cache
 *         2. callback checkReq to check the validation of given request
 * @param req: the given request to be checked
 * @param oss: log to debug
 * @return true: check succeed
 * @return false: check failed
 */
CheckResult PBFTEngine::isValidSignReq(SignReq const& req, std::ostringstream& oss) const
{
    if (m_reqCache->isExistSign(req))
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InValidSignReq: Duplicated sign")
                              << LOG_KV("INFO", oss.str());
        return CheckResult::INVALID;
    }
    if (hasConsensused(req))
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("Sign requests have been consensused")
                              << LOG_KV("INFO", oss.str());
        return CheckResult::INVALID;
    }
    CheckResult result = checkReq(req, oss);
    /// to ensure that the collected signature size is equal to minValidNodes
    /// so that checkAndCommit can be called, and the committed request backup can be stored
    if ((result == CheckResult::FUTURE) &&
        m_reqCache->getSigCacheSize(req.block_hash) < (size_t)(minValidNodes() - 1))
    {
        m_reqCache->addSignReq(req);
        PBFTENGINE_LOG(INFO) << LOG_DESC("FutureBlock") << LOG_KV("INFO", oss.str());
    }
    return result;
}

/**
 * @brief : 1. decode the network-received message into commitReq
 *          2. check the validation of the commitReq
 *          3. add the valid commitReq into the cache
 *          4. submit to blockchain if the size of collected commitReq is over 2/3
 * @param commit_req: return value, the decoded commitReq
 * @param pbftMsg: the network-received PBFTMsgPacket
 */
bool PBFTEngine::handleCommitMsg(CommitReq& commit_req, PBFTMsgPacket const& pbftMsg)
{
    Timer t;
    bool valid = decodeToRequests(commit_req, ref(pbftMsg.data));
    if (!valid)
    {
        return false;
    }
    std::ostringstream oss;
    oss << LOG_DESC("handleCommitMsg") << LOG_KV("reqNum", commit_req.height)
        << LOG_KV("curNum", m_highestBlock.number()) << LOG_KV("GenIdx", commit_req.idx)
        << LOG_KV("Cview", commit_req.view) << LOG_KV("view", m_view)
        << LOG_KV("fromIdx", pbftMsg.node_idx) << LOG_KV("fromNode", pbftMsg.node_id.abridged())
        << LOG_KV("fromIp", pbftMsg.endpoint) << LOG_KV("hash", commit_req.block_hash.abridged())
        << LOG_KV("nodeIdx", nodeIdx()) << LOG_KV("myNode", m_keyPair.pub().abridged());
    auto valid_ret = isValidCommitReq(commit_req, oss);
    if (valid_ret == CheckResult::INVALID)
    {
        return false;
    }
    /// update the view for given idx
    updateViewMap(commit_req.idx, commit_req.view);

    if (valid_ret == CheckResult::FUTURE)
    {
        return true;
    }
    m_reqCache->addCommitReq(commit_req);
    checkAndSave();
    PBFTENGINE_LOG(INFO) << LOG_DESC("handleCommitMsg Succ") << LOG_KV("INFO", oss.str())
                         << LOG_KV("Timecost", 1000 * t.elapsed());
    return true;
}

/**
 * @brief: check the given commitReq is valid or not
 * @param req: the given commitReq need to be checked
 * @param oss: info to debug
 * @return true: the given commitReq is valid
 * @return false: the given commitReq is invalid
 */
CheckResult PBFTEngine::isValidCommitReq(CommitReq const& req, std::ostringstream& oss) const
{
    if (m_reqCache->isExistCommit(req))
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InvalidCommitReq: Duplicated")
                              << LOG_KV("INFO", oss.str());
        return CheckResult::INVALID;
    }
    if (hasConsensused(req))
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InvalidCommitReq: has consensued")
                              << LOG_KV("INFO", oss.str());
        return CheckResult::INVALID;
    }
    CheckResult result = checkReq(req, oss);
    if (result == CheckResult::FUTURE)
    {
        m_reqCache->addCommitReq(req);
    }
    return result;
}

bool PBFTEngine::handleViewChangeMsg(ViewChangeReq& viewChange_req, PBFTMsgPacket const& pbftMsg)
{
    bool valid = decodeToRequests(viewChange_req, ref(pbftMsg.data));
    if (!valid)
    {
        return false;
    }
    std::ostringstream oss;
    oss << LOG_KV("reqNum", viewChange_req.height) << LOG_KV("curNum", m_highestBlock.number())
        << LOG_KV("GenIdx", viewChange_req.idx) << LOG_KV("Cview", viewChange_req.view)
        << LOG_KV("view", m_view) << LOG_KV("fromIdx", pbftMsg.node_idx)
        << LOG_KV("fromNode", pbftMsg.node_id.abridged()) << LOG_KV("fromIp", pbftMsg.endpoint)
        << LOG_KV("hash", viewChange_req.block_hash.abridged()) << LOG_KV("nodeIdx", nodeIdx())
        << LOG_KV("myNode", m_keyPair.pub().abridged());
    valid = isValidViewChangeReq(viewChange_req, pbftMsg.node_idx, oss);
    if (!valid)
    {
        return false;
    }

    m_reqCache->addViewChangeReq(viewChange_req);
    if (viewChange_req.view == m_toView)
    {
        checkAndChangeView();
    }
    else
    {
        VIEWTYPE min_view = 0;
        bool should_trigger = m_reqCache->canTriggerViewChange(
            min_view, m_f, m_toView, m_highestBlock, m_consensusBlockNumber);
        if (should_trigger)
        {
            m_toView = min_view - 1;
            PBFTENGINE_LOG(INFO) << LOG_DESC("Trigger fast-viewchange") << LOG_KV("view", m_view)
                                 << LOG_KV("toView", m_toView) << LOG_KV("minView", min_view)
                                 << LOG_KV("INFO", oss.str());
            changeViewForFastViewChange();
        }
    }
    PBFTENGINE_LOG(DEBUG) << LOG_DESC("handleViewChangeMsg Succ ") << oss.str();
    return true;
}

bool PBFTEngine::isValidViewChangeReq(
    ViewChangeReq const& req, IDXTYPE const& source, std::ostringstream& oss)
{
    if (m_reqCache->isExistViewChange(req))
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InvalidViewChangeReq: Duplicated")
                              << LOG_KV("INFO", oss.str());
        return false;
    }
    if (req.idx == nodeIdx())
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InvalidViewChangeReq: own req")
                              << LOG_KV("INFO", oss.str());
        return false;
    }
    if (req.view + 1 < m_toView && req.idx == source)
    {
        catchupView(req, oss);
    }
    /// check view and block height
    if (req.height < m_highestBlock.number() || req.view <= m_view)
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InvalidViewChangeReq: invalid view or height")
                              << LOG_KV("INFO", oss.str());
        return false;
    }
    /// check block hash
    if ((req.height == m_highestBlock.number() && req.block_hash != m_highestBlock.hash()) ||
        (m_blockChain->getBlockByNumber(req.height) == nullptr))
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InvalidViewChangeReq, invalid hash")
                              << LOG_KV("highHash", m_highestBlock.hash().abridged())
                              << LOG_KV("INFO", oss.str());
        return false;
    }
    if (!checkSign(req))
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InvalidViewChangeReq: invalid sign")
                              << LOG_KV("INFO", oss.str());
        return false;
    }
    return true;
}

void PBFTEngine::catchupView(ViewChangeReq const& req, std::ostringstream& oss)
{
    if (req.view + 1 < m_toView)
    {
        PBFTENGINE_LOG(INFO) << LOG_DESC("catchupView") << LOG_KV("toView", m_toView)
                             << LOG_KV("INFO", oss.str());
        dev::network::NodeID nodeId;
        bool succ = getNodeIDByIndex(nodeId, req.idx);
        if (succ)
        {
            sendViewChangeMsg(nodeId);
        }
    }
}

void PBFTEngine::checkAndChangeView()
{
    IDXTYPE count = m_reqCache->getViewChangeSize(m_toView);
    if (count >= minValidNodes() - 1)
    {
        /// reach to consensue dure to fast view change
        if (m_timeManager.m_lastSignTime == 0)
        {
            m_fastViewChange = false;
        }
        auto orgChangeCycle = m_timeManager.m_changeCycle;

        /// problem:
        /// 1. there are 0, 1, 2, 3 four nodes, the 2nd and 3rd nodes are off, the 0th and 1st nodes
        /// are wait and increase the m_toView always
        /// 2. the 2nd start up and trigger fast view change, the three online nodes reach a
        /// consensus at a new view, but the current leader is 3rd
        /// 3. since 0th and 1st pay a lot of time to timeout and increase the toView, while 3rd
        /// pays little time to timeout and increase the toView, the toView of 0th and 1st will
        /// always less than 2nd, and the network can never reach consensus at a given view

        /// solution:
        /// 1. if m_toView is equal to (m_view + 1), that means that some exceptions happened to
        /// block execution, but the network is OK to reach a new view, which equal to m_view + 1,
        /// in this case, we shouldn't update m_changeCycle to 0 in consideration of block execution
        /// may cause more time

        /// 2. if m_toView is larger than (m_view + 1), that means that the network is not ok to
        /// reach a new view, and the related nodes have modifing toView always, and if the network
        /// recovers, we should reset m_changeCycle to 1 in case of more timeout wait if the leader
        /// if off after fast view change

        /// potential problem:
        /// execution speed differences among nodes may cause fast view change of the faster node
        /// the faster nodes may have a different cycle compared to other nodes
        if (m_toView > m_view + 1)
        {
            m_timeManager.m_changeCycle = 1;
            PBFTENGINE_LOG(INFO) << LOG_DESC("checkAndChangeView, update m_changeCycle to 1");
        }
        PBFTENGINE_LOG(INFO) << LOG_DESC("checkAndChangeView: Reach consensus")
                             << LOG_KV("org_view", m_view)
                             << LOG_KV("org_changeCycle", orgChangeCycle)
                             << LOG_KV("cur_changeCycle", m_timeManager.m_changeCycle)
                             << LOG_KV("to_view", m_toView);


        m_leaderFailed = false;
        m_timeManager.m_lastConsensusTime = utcTime();
        m_view = m_toView.load();
        m_notifyNextLeaderSeal = false;
        m_reqCache->triggerViewChange(m_view);
        m_blockSync->noteSealingBlockNumber(m_blockChain->number());
    }
}

/// collect all caches
void PBFTEngine::collectGarbage()
{
    Guard l(m_mutex);
    if (!m_highestBlock)
    {
        return;
    }
    Timer t;
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    if (now - m_timeManager.m_lastGarbageCollection >
        std::chrono::seconds(m_timeManager.CollectInterval))
    {
        m_reqCache->collectGarbage(m_highestBlock);
        m_timeManager.m_lastGarbageCollection = now;
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("collectGarbage")
                              << LOG_KV("Timecost", 1000 * t.elapsed());
    }
}

void PBFTEngine::checkTimeout()
{
    bool flag = false;
    {
        Guard l(m_mutex);
        // clear and destruct the executiveContext
        for (auto& exec : m_execContextForAsyncReset)
        {
            if (exec)
            {
                exec.reset();
            }
        }
        m_execContextForAsyncReset.clear();

        if (m_timeManager.isTimeout())
        {
            /// timeout not triggered by fast view change
            if (m_timeManager.m_lastConsensusTime != 0)
            {
                m_fastViewChange = false;
                m_timeManager.updateChangeCycle();
                /// notify sealer that the consensus has been timeout
                /// and the timeout is not caused by unworked-leader(the case that the node not
                /// receive the prepare packet)
                if (m_onTimeout && m_reqCache->prepareCache().height > m_highestBlock.number())
                {
                    m_onTimeout(sealingTxNumber());
                }
            }
            Timer t;
            m_toView += 1;
            m_leaderFailed = true;
            m_blockSync->noteSealingBlockNumber(m_blockChain->number());
            m_timeManager.m_lastConsensusTime = utcTime();
            flag = true;
            m_reqCache->removeInvalidViewChange(m_toView, m_highestBlock);

            if (!broadcastViewChangeReq())
            {
                return;
            }

            checkAndChangeView();
            PBFTENGINE_LOG(INFO) << LOG_DESC("checkTimeout Succ") << LOG_KV("view", m_view)
                                 << LOG_KV("toView", m_toView) << LOG_KV("nodeIdx", nodeIdx())
                                 << LOG_KV("changeCycle", m_timeManager.m_changeCycle)
                                 << LOG_KV("myNode", m_keyPair.pub().abridged())
                                 << LOG_KV("timecost", t.elapsed() * 1000);
        }
    }
    if (flag && m_onViewChange)
        m_onViewChange();
}

void PBFTEngine::handleMsg(PBFTMsgPacket const& pbftMsg)
{
    Guard l(m_mutex);
    PBFTMsg pbft_msg;
    std::string key;
    bool succ = false;
    switch (pbftMsg.packet_id)
    {
    case PrepareReqPacket:
    {
        PrepareReq prepare_req;
        succ = handlePrepareMsg(prepare_req, pbftMsg);
        key = prepare_req.uniqueKey();
        pbft_msg = prepare_req;
        break;
    }
    case SignReqPacket:
    {
        SignReq req;
        succ = handleSignMsg(req, pbftMsg);
        key = req.uniqueKey();
        pbft_msg = req;
        break;
    }
    case CommitReqPacket:
    {
        CommitReq req;
        succ = handleCommitMsg(req, pbftMsg);
        key = req.uniqueKey();
        pbft_msg = req;
        break;
    }
    case ViewChangeReqPacket:
    {
        ViewChangeReq req;
        succ = handleViewChangeMsg(req, pbftMsg);
        key = req.uniqueKey();
        pbft_msg = req;
        break;
    }
    default:
    {
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("handleMsg:  Err pbft message")
                              << LOG_KV("from", pbftMsg.node_idx) << LOG_KV("nodeIdx", nodeIdx())
                              << LOG_KV("myNode", m_keyPair.pub().abridged());
        return;
    }
    }

    if (pbftMsg.ttl == 1)
    {
        return;
    }
    bool height_flag = (pbft_msg.height > m_highestBlock.number()) ||
                       (m_highestBlock.number() - pbft_msg.height < 10);
    if (succ && key.size() > 0 && height_flag)
    {
        std::unordered_set<h512> filter;
        filter.insert(pbftMsg.node_id);
        /// get the origin gen node id of the request
        h512 gen_node_id = getSealerByIndex(pbft_msg.idx);
        if (gen_node_id != h512())
        {
            filter.insert(gen_node_id);
        }
        unsigned current_ttl = pbftMsg.ttl - 1;
        broadcastMsg(pbftMsg.packet_id, key, ref(pbftMsg.data), filter, current_ttl);
    }
}

/// start a new thread to handle the network-receivied message
void PBFTEngine::workLoop()
{
    while (isWorking())
    {
        try
        {
            std::pair<bool, PBFTMsgPacket> ret = m_msgQueue.tryPop(c_PopWaitSeconds);
            if (ret.first)
            {
                PBFTENGINE_LOG(TRACE)
                    << LOG_DESC("workLoop: handleMsg")
                    << LOG_KV("type", std::to_string(ret.second.packet_id))
                    << LOG_KV("fromIdx", ret.second.node_idx) << LOG_KV("nodeIdx", nodeIdx())
                    << LOG_KV("myNode", m_keyPair.pub().abridged());
                handleMsg(ret.second);
            }
            /// to avoid of cpu problem
            else if (m_reqCache->futurePrepareCacheSize() == 0)
            {
                std::unique_lock<std::mutex> l(x_signalled);
                m_signalled.wait_for(l, std::chrono::milliseconds(5));
            }
            if (nodeIdx() != MAXIDX)
            {
                checkTimeout();
                handleFutureBlock();
                collectGarbage();
            }
        }
        catch (std::exception& _e)
        {
            LOG(ERROR) << _e.what();
        }
    }
}


/// handle the prepareReq cached in the futurePrepareCache
void PBFTEngine::handleFutureBlock()
{
    Guard l(m_mutex);
    std::shared_ptr<PrepareReq> p_future_prepare =
        m_reqCache->futurePrepareCache(m_consensusBlockNumber);
    if (p_future_prepare && p_future_prepare->view == m_view)
    {
        PBFTENGINE_LOG(INFO) << LOG_DESC("handleFutureBlock")
                             << LOG_KV("reqNum", p_future_prepare->height)
                             << LOG_KV("curNum", m_highestBlock.number()) << LOG_KV("view", m_view)
                             << LOG_KV("conNum", m_consensusBlockNumber)
                             << LOG_KV("hash", p_future_prepare->block_hash.abridged())
                             << LOG_KV("nodeIdx", nodeIdx())
                             << LOG_KV("myNode", m_keyPair.pub().abridged());
        handlePrepareMsg(*p_future_prepare);
        m_reqCache->eraseHandledFutureReq(p_future_prepare->height);
    }
}

/// get the status of PBFT consensus
const std::string PBFTEngine::consensusStatus()
{
    Json::Value status(Json::arrayValue);
    Json::Value statusObj;
    getBasicConsensusStatus(statusObj);
    /// get other informations related to PBFT
    statusObj["connectedNodes"] = IDXTYPE(m_connectedNode);
    /// get the current view
    statusObj["currentView"] = VIEWTYPE(m_view);
    /// get toView
    statusObj["toView"] = VIEWTYPE(m_toView);
    /// get leader failed or not
    statusObj["leaderFailed"] = bool(m_leaderFailed);
    status.append(statusObj);
    /// get view of node id
    getAllNodesViewStatus(status);

    Json::FastWriter fastWriter;
    std::string status_str = fastWriter.write(status);
    return status_str;
}

void PBFTEngine::getAllNodesViewStatus(Json::Value& status)
{
    updateViewMap(nodeIdx(), m_view);
    Json::Value view_array(Json::arrayValue);
    ReadGuard l(x_viewMap);
    for (auto it : m_viewMap)
    {
        Json::Value view_obj;
        dev::network::NodeID node_id = getSealerByIndex(it.first);
        if (node_id != dev::network::NodeID())
        {
            view_obj["nodeId"] = dev::toHex(node_id);
            view_obj["view"] = it.second;
            view_array.append(view_obj);
        }
    }
    status.append(view_array);
}

}  // namespace consensus
}  // namespace dev
