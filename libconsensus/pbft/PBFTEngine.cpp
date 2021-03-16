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
#include "libdevcrypto/CryptoInterface.h"
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/CommonJS.h>
#include <libethcore/CommonJS.h>
#include <libtxpool/TxPool.h>
using namespace dev::eth;
using namespace dev::db;
using namespace dev::blockverifier;
using namespace dev::blockchain;
using namespace dev::p2p;
using namespace dev::storage;
using namespace rocksdb;

namespace dev
{
namespace consensus
{
const std::string PBFTEngine::c_backupKeyCommitted = "committed";
const std::string PBFTEngine::c_backupMsgDirName = "pbftMsgBackup/RocksDB";

void PBFTEngine::start()
{
    // create PBFTMsgFactory
    createPBFTMsgFactory();
    createPBFTReqCache();
    assert(m_reqCache);
    // set checkSignCallback for reqCache
    m_reqCache->setCheckSignCallback(boost::bind(&PBFTEngine::checkSign, this, _1));

    // register P2P callback after create PBFTMsgFactory
    m_service->registerHandlerByProtoclID(
        m_protocolId, boost::bind(&PBFTEngine::handleP2PMessage, this, _1, _2, _3));
    registerDisconnectHandler();
    ConsensusEngineBase::start();
    initPBFTEnv(3 * getEmptyBlockGenTime());
    PBFTENGINE_LOG(INFO) << "[Start PBFTEngine...]";
}

void PBFTEngine::registerDisconnectHandler()
{
    try
    {
        // register disconnect callback
        auto self = std::weak_ptr<PBFTEngine>(shared_from_this());
        m_service->registerDisconnectHandlerByProtocolID(
            m_protocolId, [self](dev::network::NetworkException,
                              std::shared_ptr<dev::p2p::P2PSession> _p2pSession) {
                try
                {
                    auto pbftEngine = self.lock();
                    if (!pbftEngine)
                    {
                        return;
                    }
                    ssize_t nodeIndex = pbftEngine->getIndexBySealer(_p2pSession->nodeID());
                    if (nodeIndex < 0)
                    {
                        return;
                    }
                    Guard l(pbftEngine->m_mutex);
                    PBFTENGINE_LOG(DEBUG)
                        << LOG_DESC("eraseLatestViewChangeCache for disconnected node")
                        << LOG_KV("node", _p2pSession->nodeID().abridged())
                        << LOG_KV("idx", nodeIndex);
                    pbftEngine->m_reqCache->eraseLatestViewChangeCacheForNodeUpdated(nodeIndex);
                }
                catch (std::exception const& e)
                {
                    PBFTENGINE_LOG(WARNING)
                        << LOG_DESC("call disconnect handler for PBFTEngine failed")
                        << LOG_KV("e", boost::diagnostic_information(e));
                }
            });
    }
    catch (std::exception const& e)
    {
        PBFTENGINE_LOG(ERROR) << LOG_DESC("registerDisconnectHandler for PBFTEngine failed")
                              << LOG_KV("e", boost::diagnostic_information(e));
    }
}

void PBFTEngine::createPBFTReqCache()
{
    // init enablePrepareWithTxsHash
    if (m_enablePrepareWithTxsHash)
    {
        m_partiallyPrepareCache = std::make_shared<PartiallyPBFTReqCache>();
        m_reqCache = m_partiallyPrepareCache;
    }
    else
    {
        m_reqCache = std::make_shared<PBFTReqCache>();
    }
}

void PBFTEngine::stop()
{
    if (m_startConsensusEngine)
    {
        // remove the registered handler when stop the pbftEngine
        if (m_service)
        {
            m_service->removeHandlerByProtocolID(m_protocolId);
            m_service->removeDisconnectHandlerByProtocolID(m_protocolId);
        }
        if (m_threadPool)
        {
            m_threadPool->stop();
        }
        if (m_prepareWorker)
        {
            m_prepareWorker->stop();
        }
        if (m_messageHandler)
        {
            m_messageHandler->stop();
        }
        ConsensusEngineBase::stop();
    }
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
    m_timeManager.initTimerManager(view_timeout);
    reportBlock(*block);
    initBackupDB();
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
        if (m_reqCache->rawPrepareCacheHeight() != m_consensusBlockNumber)
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
                         << LOG_KV("nodeId", m_keyPair.pub().abridged()) << LOG_KV("view", m_view)
                         << LOG_KV("hash", req.block_hash.abridged()) << LOG_KV("H", req.height);
    m_broadCastCache->clearAll();
    std::shared_ptr<PrepareReq> prepareReq =
        std::make_shared<PrepareReq>(req, m_keyPair, m_view, nodeIdx());

    auto self = std::weak_ptr<PBFTEngine>(shared_from_this());
    m_threadPool->enqueue([self, prepareReq]() {
        try
        {
            auto pbftEngine = self.lock();
            if (!pbftEngine)
            {
                return;
            }
            std::shared_ptr<bytes> prepare_data = std::make_shared<bytes>();
            // when rehandle the committedPrepareCache, broadcast prepare directly
            prepareReq->encode(*prepare_data);
            pbftEngine->broadcastMsg(PrepareReqPacket, *prepareReq, ref(*prepare_data));
        }
        catch (std::exception const& e)
        {
            PBFTENGINE_LOG(ERROR) << LOG_DESC("broadcastPrepare exceptioned")
                                  << LOG_KV("errorInfo", boost::diagnostic_information(e));
        }
    });
    handlePrepareMsg(prepareReq);
    /// note blockSync to the latest number, in case of the block number of other nodes is larger
    /// than this node
    m_blockSync->noteSealingBlockNumber(m_blockChain->number());
}

/// init pbftMsgBackup
void PBFTEngine::initBackupDB()
{
    /// try-catch has already been considered by Initializer::init and RPC calls startByGroupID
    std::string path = getBackupMsgPath();
    boost::filesystem::path path_handler = boost::filesystem::path(path);
    if (!boost::filesystem::exists(path_handler))
    {
        boost::filesystem::create_directories(path_handler);
    }
    m_backupDB = std::make_shared<BasicRocksDB>();
    auto options = getRocksDBOptions();
    m_backupDB->Open(options, path_handler.string());
    if (g_BCOSConfig.diskEncryption.enable)
    {
        PBFTENGINE_LOG(INFO) << LOG_DESC(
            "diskEncryption enabled: set encrypt and decrypt handler for pbftBackup");
        m_backupDB->setEncryptHandler(
            getEncryptHandler(asBytes(g_BCOSConfig.diskEncryption.dataKey)));
        m_backupDB->setDecryptHandler(
            getDecryptHandler(asBytes(g_BCOSConfig.diskEncryption.dataKey)));
    }

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
        std::string value;
        auto status = m_backupDB->Get(ReadOptions(), key, value);
        if (!status.ok() && !status.IsNotFound())
        {
            PBFTENGINE_LOG(ERROR) << LOG_DESC("reloadMsg PBFTBackup failed")
                                  << LOG_KV("status", status.ToString());
            BOOST_THROW_EXCEPTION(DatabaseError() << errinfo_comment(
                                      "reloadMsg failed, status = " + status.ToString()));
        }
        bytes data = fromHex(value);
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
void PBFTEngine::backupMsg(std::string const& _key, std::shared_ptr<bytes> _msg)
{
    if (!m_backupDB)
    {
        return;
    }
    try
    {
        WriteBatch batch;
        m_backupDB->Put(batch, _key, toHex(*_msg));
        WriteOptions options;
        m_backupDB->Write(options, batch);
    }
    catch (DatabaseError const& e)
    {
        PBFTENGINE_LOG(ERROR) << LOG_BADGE("DatabaseError")
                              << LOG_DESC("store backupMsg to db failed")
                              << LOG_KV("EINFO", boost::diagnostic_information(e));
        raise(SIGTERM);
        BOOST_THROW_EXCEPTION(std::invalid_argument(" store backupMsg to rocksdb failed."));
    }
    catch (std::exception const& e)
    {
        PBFTENGINE_LOG(ERROR) << LOG_DESC("store backupMsg to db failed")
                              << LOG_KV("EINFO", boost::diagnostic_information(e));
        raise(SIGTERM);
        BOOST_THROW_EXCEPTION(std::invalid_argument(" store backupMsg to rocksdb failed."));
    }
}

PrepareReq::Ptr PBFTEngine::constructPrepareReq(dev::eth::Block::Ptr _block)
{
    dev::eth::Block::Ptr engineBlock = m_blockFactory->createBlock();
    *engineBlock = std::move(*_block);
    PrepareReq::Ptr prepareReq = std::make_shared<PrepareReq>(
        engineBlock, m_keyPair, m_view, nodeIdx(), m_enablePrepareWithTxsHash);
    if (prepareReq->pBlock->transactions()->size() == 0)
    {
        prepareReq->isEmpty = true;
    }
    // the non-empty block only broadcast hash when enable-prepare-with-txs-hash
    if (m_enablePrepareWithTxsHash && prepareReq->pBlock->transactions()->size() > 0)
    {
        // addPreRawPrepare to response to the request-sealers
        m_partiallyPrepareCache->addPreRawPrepare(prepareReq);
        // encode prepareReq with uncompleted transactions into sendedData
        std::shared_ptr<bytes> sendedData = std::make_shared<bytes>();
        prepareReq->encode(*sendedData);
        auto self = std::weak_ptr<PBFTEngine>(shared_from_this());
        m_threadPool->enqueue([self, prepareReq, sendedData]() {
            try
            {
                auto pbftEngine = self.lock();
                if (!pbftEngine)
                {
                    return;
                }
                pbftEngine->sendPrepareMsgFromLeader(
                    prepareReq, ref(*sendedData), PartiallyPreparePacket);
            }
            catch (std::exception const& e)
            {
                PBFTENGINE_LOG(ERROR) << LOG_DESC("broadcastPrepare exceptioned")
                                      << LOG_KV("errorInfo", boost::diagnostic_information(e));
            }
        });
        // re-encode the block with completed transactions
        prepareReq->pBlock->encode(*prepareReq->block);
    }
    // not enable-prepare-with-txs-hash or the empty block
    else
    {
        auto self = std::weak_ptr<PBFTEngine>(shared_from_this());
        m_threadPool->enqueue([self, prepareReq, engineBlock]() {
            try
            {
                auto pbftEngine = self.lock();
                if (!pbftEngine)
                {
                    return;
                }
                std::shared_ptr<bytes> prepare_data = std::make_shared<bytes>();
                prepareReq->encode(*prepare_data);
                pbftEngine->sendPrepareMsgFromLeader(prepareReq, ref(*prepare_data));
            }
            catch (std::exception const& e)
            {
                PBFTENGINE_LOG(ERROR) << LOG_DESC("broadcastPrepare exceptioned")
                                      << LOG_KV("errorInfo", boost::diagnostic_information(e));
            }
        });
    }
    return prepareReq;
}

// broadcast prepare message to all the other nodes
void PBFTEngine::sendPrepareMsgFromLeader(
    PrepareReq::Ptr _prepareReq, bytesConstRef _data, dev::PACKET_TYPE const& _p2pPacketType)
{
    broadcastMsg(PrepareReqPacket, *_prepareReq, _data, _p2pPacketType);
}

/// sealing the generated block into prepareReq and push its to msgQueue
bool PBFTEngine::generatePrepare(dev::eth::Block::Ptr _block)
{
    // fix the deadlock cases below
    // 1. the sealer has sealed enough txs and is handling the block, but stucked at the
    // generatePrepare for the PBFTEngine is checking timeout and ready to change view
    // 2. the PBFTEngine trigger view change and release the m_mutex, the leader has been changed
    // 3. the PBFTEngine calls handlePrepare for receive the PBFT prepare message from the leader,
    // and handle the block
    // 4. the next leader is the node-self, the PBFTEngine tries to notify the node to seal the next
    // block
    // 5. since the x_sealing is stucked at step 1, the PBFTEngine has been stucked at notifySeal
    // Solution:
    // if the sealer execute step1 (m_generatePrepare is equal to true), won't trigger notifySeal
    m_generatePrepare = true;
    Guard l(m_mutex);
    // the leader has been changed
    if (!getLeader().first || getLeader().second != nodeIdx())
    {
        m_generatePrepare = false;
        return true;
    }
    m_notifyNextLeaderSeal = false;
    auto prepareReq = constructPrepareReq(_block);

    if (prepareReq->pBlock->getTransactionSize() == 0 && m_omitEmptyBlock)
    {
        m_leaderFailed = true;
        changeViewForFastViewChange();
        m_timeManager.m_changeCycle = 0;
        return true;
    }
    handlePrepareMsg(prepareReq);

    /// reset the block according to broadcast result
    PBFTENGINE_LOG(INFO) << LOG_DESC("generateLocalPrepare")
                         << LOG_KV("hash", prepareReq->block_hash.abridged())
                         << LOG_KV("H", prepareReq->height) << LOG_KV("nodeIdx", nodeIdx())
                         << LOG_KV("myNode", m_keyPair.pub().abridged());
    m_signalled.notify_all();
    m_generatePrepare = false;
    return true;
}

/**
 * @brief : 1. generate and broadcast signReq according to given prepareReq,
 *          2. add the generated signReq into the cache
 * @param req: specified PrepareReq used to generate signReq
 */
bool PBFTEngine::broadcastSignReq(PrepareReq const& req)
{
    SignReq::Ptr sign_req = std::make_shared<SignReq>(req, m_keyPair, nodeIdx());
    bytes sign_req_data;
    sign_req->encode(sign_req_data);
    bool succ = broadcastMsg(SignReqPacket, *sign_req, ref(sign_req_data));
    m_reqCache->addSignReq(sign_req);
    return succ;
}

bool PBFTEngine::getNodeIDByIndex(h512& nodeID, const IDXTYPE& idx) const
{
    nodeID = getSealerByIndex(idx);
    if (nodeID == h512())
    {
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("getNodeIDByIndex: not sealer") << LOG_KV("Idx", idx)
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
        return dev::crypto::Verify(
                   node_id, dev::crypto::SignatureFromBytes(req.sig), req.block_hash) &&
               dev::crypto::Verify(
                   node_id, dev::crypto::SignatureFromBytes(req.sig2), req.fieldsWithoutBlock());
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
    CommitReq::Ptr commit_req = std::make_shared<CommitReq>(req, m_keyPair, nodeIdx());
    bytes commit_req_data;
    commit_req->encode(commit_req_data);
    bool succ = broadcastMsg(CommitReqPacket, *commit_req, ref(commit_req_data));
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
    ViewChangeReq::Ptr req = std::make_shared<ViewChangeReq>(
        m_keyPair, m_highestBlock.number(), m_toView, nodeIdx(), m_highestBlock.hash());
    // add the viewChangeReq
    m_reqCache->addViewChangeReq(req, m_blockChain->number());
    PBFTENGINE_LOG(DEBUG) << LOG_DESC("broadcastViewChangeReq ") << LOG_KV("v", m_view)
                          << LOG_KV("toV", m_toView) << LOG_KV("curNum", m_highestBlock.number())
                          << LOG_KV("hash", req->block_hash.abridged())
                          << LOG_KV("nodeIdx", nodeIdx())
                          << LOG_KV("myNode", m_keyPair.pub().abridged());
    /// view change not caused by fast view change
    if (!m_fastViewChange)
    {
        PBFTENGINE_LOG(WARNING) << LOG_DESC("ViewChangeWarning: not caused by omit empty block ")
                                << LOG_KV("v", m_view) << LOG_KV("toV", m_toView)
                                << LOG_KV("curNum", m_highestBlock.number())
                                << LOG_KV("hash", req->block_hash.abridged())
                                << LOG_KV("nodeIdx", nodeIdx())
                                << LOG_KV("myNode", m_keyPair.pub().abridged());
        // print the disconnected info
        getForwardNodes(true);
    }

    bytes view_change_data;
    req->encode(view_change_data);
    return broadcastMsg(ViewChangeReqPacket, *req, ref(view_change_data));
}

/// set default ttl to 1 to in case of forward-broadcast
bool PBFTEngine::sendMsg(dev::network::NodeID const& nodeId, unsigned const& packetType,
    std::string const& key, bytesConstRef data, unsigned const& ttl,
    std::shared_ptr<dev::h512s> forwardNodes)
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
                session.nodeID(), transDataToMessage(data, packetType, ttl, forwardNodes), nullptr);
            PBFTENGINE_LOG(DEBUG) << LOG_DESC("sendMsg") << LOG_KV("packetType", packetType)
                                  << LOG_KV("dstNodeId", nodeId.abridged())
                                  << LOG_KV("remote_endpoint", session.nodeIPEndpoint)
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
bool PBFTEngine::broadcastMsg(unsigned const& packetType, PBFTMsg const& _pbftMsg,
    bytesConstRef data, PACKET_TYPE const& _p2pPacketType,
    std::unordered_set<dev::network::NodeID> const& filter, unsigned const& ttl,
    std::function<ssize_t(dev::network::NodeID const&)> const& filterFunction)
{
    auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    m_connectedNode = sessions.size();
    NodeIDs nodeIdList;
    std::string key = _pbftMsg.uniqueKey();
    for (auto session : sessions)
    {
        /// get node index of the sealer from m_sealerList failed ?
        if (filterFunction(session.nodeID()) < 0)
        {
            continue;
        }
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
                              << LOG_KV("dstIp", session.nodeIPEndpoint)
                              << LOG_KV("ttl", (ttl == 0 ? maxTTL : ttl))
                              << LOG_KV("nodeIdx", nodeIdx())
                              << LOG_KV("toNode", session.nodeID().abridged());
        nodeIdList.push_back(session.nodeID());
        broadcastMark(session.nodeID(), packetType, key);
    }
    /// send messages according to node id
    broadcastMsg(nodeIdList, data, packetType, ttl, _p2pPacketType, _pbftMsg);
    return true;
}

void PBFTEngine::broadcastMsg(dev::h512s const& _targetNodes, bytesConstRef _data,
    unsigned const& _packetType, unsigned const& _ttl, PACKET_TYPE const& _p2pPacketType,
    PBFTMsg const& _pbftMsg)
{
    std::shared_ptr<dev::h512s> forwardNodes = nullptr;
    if (m_enableTTLOptimize)
    {
        // get the forwardNodes
        forwardNodes = getForwardNodes();
    }
    // set prepareWithEmptyBlock and extend prepareWithEmptyBlock into the 8th bit of ttl field
    auto pbftPacket = createPBFTMsgPacket(_data, _packetType, _ttl, forwardNodes);
    if (_pbftMsg.isEmpty)
    {
        pbftPacket->prepareWithEmptyBlock = true;
    }
    std::shared_ptr<bytes> encodedData = std::make_shared<bytes>();
    pbftPacket->encode(*encodedData);
    auto p2pMessage = toP2PMessage(encodedData, _p2pPacketType);
    m_service->asyncMulticastMessageByNodeIDList(_targetNodes, p2pMessage);
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
    // Note: we should try to decrease the size of duplicated
    if (m_reqCache->isExistPrepare(req))
    {
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("InvalidPrepare: Duplicated Prep")
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
    // Since the empty block is not placed on the disk,
    // pbftBackup is checked only when a non-empty prepare is received
    // in case that:
    // Some nodes cache pbftBackup，but the consensus failed because no enough commit requests were
    // collected
    // 2. view change occurred in the system, switch to the new leader without pbftBackup
    // 3. the new leader generate an empty-block, and reset the changeCycle to 0
    // 4. the other 2*f nodes received the prepare with empty block, but rejected the prepare for
    // isHashSavedAfterCommit check failed
    // 5. the changeCycle of the other nodes with pbftBackup are larger than the new leader, the
    // system suffers from view-catchup
    if (req.isEmpty && req.height == m_reqCache->committedPrepareCache().height)
    {
        // here for debug
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("receive empty block while pbft-backup exists")
                              << LOG_KV("reqHeight", req.height)
                              << LOG_KV("reqHash", req.block_hash.abridged())
                              << LOG_KV("reqView", req.view) << LOG_KV("view", m_view)
                              << LOG_KV("pbftBackupHash",
                                     m_reqCache->committedPrepareCache().block_hash.abridged());
    }
    if (!req.isEmpty && !isHashSavedAfterCommit(req))
    {
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("InvalidPrepare: not saved after commit")
                              << LOG_KV("EINFO", oss.str());
        return CheckResult::INVALID;
    }
    if (isFuturePrepare(req))
    {
        PBFTENGINE_LOG(INFO) << LOG_DESC("FutureBlock") << LOG_KV("EINFO", oss.str());
        return CheckResult::FUTURE;
    }
    if (!isValidLeader(req))
    {
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
    auto sealers = consensusList();
    if (sealers != block.blockHeader().sealerList())
    {
        PBFTENGINE_LOG(ERROR) << LOG_DESC("checkSealerList: wrong sealers")
                              << LOG_KV("Nsealer", sealers.size())
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
    // the current sealer list
    auto sealers = consensusList();
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
    for (auto const& sign : *sig_list)
    {
        auto nodeIndex = sign.first.convert_to<IDXTYPE>();
        if (!checkSign(nodeIndex, block.blockHeader().hash(), sign.second))
        {
            PBFTENGINE_LOG(ERROR) << LOG_DESC("checkBlock: checkSign failed")
                                  << LOG_KV("sealerIdx", nodeIndex)
                                  << LOG_KV("blockHash", block.blockHeader().hash().abridged())
                                  << LOG_KV("signature", toHex(sign.second));
            return false;
        }
    }  /// end of check sign

    /// Check whether the number of transactions in block exceeds the limit
    if (block.transactions()->size() > maxBlockTransactions())
    {
        PBFTENGINE_LOG(ERROR) << LOG_DESC("checkBlock: check maxBlockTransactions failed")
                              << LOG_KV("blkTxsNum", block.transactions()->size())
                              << LOG_KV("maxBlockTransactions", maxBlockTransactions());
        return false;
    }
    return true;
}

bool PBFTEngine::checkSign(IDXTYPE const& _idx, dev::h256 const& _hash, bytes const& _sig)
{
    h512 nodeId;
    if (getNodeIDByIndex(nodeId, _idx))
    {
        return dev::crypto::Verify(nodeId, dev::crypto::SignatureFromBytes(_sig), _hash);
    }
    return false;
}

/**
 * @brief: notify the seal module to seal block if the current node is the next leader
 * @param block: block obtained from the prepare packet, used to filter transactions
 */
void PBFTEngine::notifySealing(dev::eth::Block const& block)
{
    if (!m_onNotifyNextLeaderReset || m_generatePrepare)
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
            filter.insert(trans->hash());
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

void PBFTEngine::execBlock(Sealing& sealing, PrepareReq::Ptr _req, std::ostringstream&)
{
    /// no need to decode the local generated prepare packet
    auto start_time = utcTime();
    auto record_time = utcTime();
    if (_req->pBlock)
    {
        sealing.block = _req->pBlock;
    }
    /// decode the network received prepare packet
    else
    {
        // without receipt, with transaction hash(parallel calc txs' hash)
        sealing.block->decode(ref(*_req->block), CheckTransaction::None, false, true);
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
    checkTransactionsValid(sealing.block, _req);
    auto check_time_cost = utcTime() - record_time;
    record_time = utcTime();

    /// notify the next leader seal a new block
    /// this if condition to in case of dead-lock when generate local prepare and notifySealing
    if (_req->idx != nodeIdx())
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
        << LOG_KV("reqIdx", _req->idx) << LOG_KV("hash", sealing.block->header().hash().abridged())
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
void PBFTEngine::pushValidPBFTMsgIntoQueue(NetworkException, std::shared_ptr<P2PSession> session,
    P2PMessage::Ptr message, std::function<void(PBFTMsgPacket::Ptr)> const& _f)
{
    if (nodeIdx() == MAXIDX)
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC(
            "onRecvPBFTMessage: I'm an observer or not in the group, drop the PBFT message packets "
            "directly");
        return;
    }
    PBFTMsgPacket::Ptr pbft_msg = m_pbftMsgFactory->createPBFTMsgPacket();
    bool valid = decodePBFTMsgPacket(pbft_msg, message, session);
    if (!valid)
    {
        return;
    }
    // calls callback if _f is not null
    if (_f)
    {
        _f(pbft_msg);
    }
    if (pbft_msg->packet_id <= ViewChangeReqPacket)
    {
        m_msgQueue.push(pbft_msg);
        /// notify to handleMsg after push new PBFTMsgPacket into m_msgQueue
        m_signalled.notify_all();
    }
    else
    {
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("onRecvPBFTMessage: illegal msg ")
                              << LOG_KV("fromId", pbft_msg->packet_id)
                              << LOG_KV("fromIp", pbft_msg->endpoint)
                              << LOG_KV("nodeIdx", nodeIdx())
                              << LOG_KV("myNode", m_keyPair.pub().abridged());
    }
}

void PBFTEngine::onRecvPBFTMessage(dev::p2p::NetworkException _exception,
    std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message)
{
    return pushValidPBFTMsgIntoQueue(_exception, _session, _message, nullptr);
}

bool PBFTEngine::handlePrepareMsg(PrepareReq::Ptr prepare_req, PBFTMsgPacket const& pbftMsg)
{
    bool valid = decodeToRequests(*prepare_req, ref(pbftMsg.data));
    // set isEmpty flag for the prepareReq
    if (pbftMsg.prepareWithEmptyBlock)
    {
        prepare_req->isEmpty = true;
    }
    if (!valid)
    {
        return false;
    }
    return handlePrepareMsg(prepare_req, pbftMsg.endpoint);
}

void PBFTEngine::clearPreRawPrepare()
{
    if (m_partiallyPrepareCache)
    {
        m_partiallyPrepareCache->clearPreRawPrepare();
    }
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
bool PBFTEngine::handlePrepareMsg(PrepareReq::Ptr prepareReq, std::string const& endpoint)
{
    std::ostringstream oss;
    oss << LOG_DESC("handlePrepareMsg") << LOG_KV("reqIdx", prepareReq->idx)
        << LOG_KV("view", prepareReq->view) << LOG_KV("reqNum", prepareReq->height)
        << LOG_KV("curNum", m_highestBlock.number()) << LOG_KV("consNum", m_consensusBlockNumber)
        << LOG_KV("curView", m_view) << LOG_KV("fromIp", endpoint)
        << LOG_KV("hash", prepareReq->block_hash.abridged()) << LOG_KV("nodeIdx", nodeIdx())
        << LOG_KV("myNode", m_keyPair.pub().abridged())
        << LOG_KV("curChangeCycle", m_timeManager.m_changeCycle);
    /// check the prepare request is valid or not
    auto valid_ret = isValidPrepare(*prepareReq, oss);
    if (valid_ret == CheckResult::INVALID)
    {
        clearPreRawPrepare();
        return false;
    }
    /// update the view for given idx
    updateViewMap(prepareReq->idx, prepareReq->view);

    if (valid_ret == CheckResult::FUTURE)
    {
        clearPreRawPrepare();
        m_reqCache->addFuturePrepareCache(prepareReq);
        return true;
    }
    // clear preRawPrepare before addRawPrepare when enable_block_with_txs_hash
    clearPreRawPrepare();
    /// add raw prepare request
    addRawPrepare(prepareReq);

    return execPrepareAndGenerateSignMsg(prepareReq, oss);
}

void PBFTEngine::addRawPrepare(PrepareReq::Ptr _prepareReq)
{
    /// add raw prepare request
    m_reqCache->addRawPrepare(_prepareReq);
}

bool PBFTEngine::execPrepareAndGenerateSignMsg(
    PrepareReq::Ptr _prepareReq, std::ostringstream& _oss)
{
    Timer t;
    Sealing workingSealing(m_blockFactory);
    try
    {
        // update the latest time of receiving the rawPrepare and ready to execute the block
        m_timeManager.m_lastAddRawPrepareTime = utcSteadyTime();

        execBlock(workingSealing, _prepareReq, _oss);

        // update the latest execution time when processed the block execution
        m_timeManager.m_lastExecTime = utcSteadyTime();

        // old block (has already executed correctly by block sync)
        if (workingSealing.p_execContext == nullptr &&
            workingSealing.block->getTransactionSize() > 0)
        {
            return false;
        }
    }
    catch (std::exception& e)
    {
        PBFTENGINE_LOG(WARNING) << LOG_DESC("Block execute failed") << LOG_KV("INFO", _oss.str())
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
    PrepareReq::Ptr sign_prepare =
        std::make_shared<PrepareReq>(*_prepareReq, workingSealing, m_keyPair);

    // destroy ExecutiveContext in m_destructorThread
    auto execContext = m_reqCache->prepareCache().p_execContext;
    HolderForDestructor<dev::blockverifier::ExecutiveContext> holder(std::move(execContext));
    m_destructorThread->enqueue(std::move(holder));

    m_reqCache->addPrepareReq(sign_prepare);
    PBFTENGINE_LOG(DEBUG) << LOG_DESC("handlePrepareMsg: add prepare cache and broadcastSignReq")
                          << LOG_KV("reqNum", sign_prepare->height)
                          << LOG_KV("hash", sign_prepare->block_hash.abridged())
                          << LOG_KV("nodeIdx", nodeIdx())
                          << LOG_KV("addPrepareTime", utcTime() - startT)
                          << LOG_KV("myNode", m_keyPair.pub().abridged());

    /// broadcast the re-generated signReq(add the signReq to cache)
    broadcastSignReq(*sign_prepare);

    checkAndCommit();
    PBFTENGINE_LOG(INFO) << LOG_DESC("handlePrepareMsg Succ")
                         << LOG_KV("Timecost", 1000 * t.elapsed()) << LOG_KV("INFO", _oss.str());
    return true;
}


void PBFTEngine::checkAndCommit()
{
    auto minValidNodeSize = minValidNodes();
    size_t sign_size =
        m_reqCache->getSigCacheSize(m_reqCache->prepareCache().block_hash, minValidNodeSize);
    /// must be equal to minValidNodes:in case of callback checkAndCommit repeatly in a round of
    /// PBFT consensus
    if (sign_size == minValidNodeSize)
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
        auto self = std::weak_ptr<PBFTEngine>(shared_from_this());
        m_threadPool->enqueue([self]() {
            try
            {
                auto pbftEngine = self.lock();
                if (!pbftEngine)
                {
                    return;
                }
                auto committedPrepareMsg = pbftEngine->m_reqCache->committedPrepareBytes();
                pbftEngine->backupMsg(c_backupKeyCommitted, committedPrepareMsg);
            }
            catch (std::exception const& _e)
            {
                PBFTENGINE_LOG(WARNING)
                    << LOG_DESC("checkAndCommit: backup c`ommittedPrepareMsg failed")
                    << LOG_KV("e", boost::diagnostic_information(_e));
            }
        });

        PBFTENGINE_LOG(DEBUG) << LOG_DESC("checkAndCommit: broadcastCommitReq")
                              << LOG_KV("prepareHeight", m_reqCache->prepareCache().height)
                              << LOG_KV("hash", m_reqCache->prepareCache().block_hash.abridged())
                              << LOG_KV("nodeIdx", nodeIdx())
                              << LOG_KV("myNode", m_keyPair.pub().abridged());

        if (!broadcastCommitReq(m_reqCache->prepareCache()))
        {
            PBFTENGINE_LOG(WARNING) << LOG_DESC("checkAndCommit: broadcastCommitReq failed");
        }

        m_timeManager.m_lastSignTime = utcSteadyTime();
        checkAndSave();
    }
}

/// if collect >= 2/3 SignReq and CommitReq, then callback this function to commit block
/// check whether view and height is valid, if valid, then commit the block and clear the context
void PBFTEngine::checkAndSave()
{
    auto start_commit_time = utcTime();
    auto record_time = utcTime();
    auto minValidNodeSize = minValidNodes();
    size_t sign_size =
        m_reqCache->getSigCacheSize(m_reqCache->prepareCache().block_hash, minValidNodeSize);
    size_t commit_size =
        m_reqCache->getCommitCacheSize(m_reqCache->prepareCache().block_hash, minValidNodeSize);
    if (sign_size >= minValidNodeSize && commit_size >= minValidNodeSize)
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
                m_reqCache->delCache(m_reqCache->prepareCache().pBlock->blockHeader());
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
    ConsensusEngineBase::reportBlock(block);
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
        /// update the highest block
        m_highestBlock = block.blockHeader();
        if (m_highestBlock.number() >= m_consensusBlockNumber)
        {
            m_view = m_toView = 0;
            m_leaderFailed = false;
            m_timeManager.m_lastConsensusTime = utcSteadyTime();
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
        m_reqCache->delCache(m_highestBlock);
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
bool PBFTEngine::handleSignMsg(SignReq::Ptr sign_req, PBFTMsgPacket const& pbftMsg)
{
    Timer t;
    bool valid = decodeToRequests(*sign_req, ref(pbftMsg.data));
    if (!valid)
    {
        return false;
    }
    std::ostringstream oss;
    oss << LOG_DESC("handleSignMsg") << LOG_KV("num", sign_req->height)
        << LOG_KV("curNum", m_highestBlock.number()) << LOG_KV("GenIdx", sign_req->idx)
        << LOG_KV("Sview", sign_req->view) << LOG_KV("view", m_view)
        << LOG_KV("fromIdx", pbftMsg.node_idx) << LOG_KV("fromNode", pbftMsg.node_id.abridged())
        << LOG_KV("fromIp", pbftMsg.endpoint) << LOG_KV("hash", sign_req->block_hash.abridged())
        << LOG_KV("nodeIdx", nodeIdx()) << LOG_KV("myNode", m_keyPair.pub().abridged());
    auto check_ret = isValidSignReq(sign_req, oss);
    if (check_ret == CheckResult::INVALID)
    {
        return false;
    }
    updateViewMap(sign_req->idx, sign_req->view);

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
CheckResult PBFTEngine::isValidSignReq(SignReq::Ptr req, std::ostringstream& oss) const
{
    if (m_reqCache->isExistSign(*req))
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InValidSignReq: Duplicated sign")
                              << LOG_KV("INFO", oss.str());
        return CheckResult::INVALID;
    }
    if (hasConsensused(*req))
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("Sign requests have been consensused")
                              << LOG_KV("INFO", oss.str());
        return CheckResult::INVALID;
    }
    CheckResult result = checkReq(*req, oss);
    /// to ensure that the collected signature size is equal to minValidNodes
    /// so that checkAndCommit can be called, and the committed request backup can be stored
    if (result == CheckResult::FUTURE)
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
bool PBFTEngine::handleCommitMsg(CommitReq::Ptr commit_req, PBFTMsgPacket const& pbftMsg)
{
    Timer t;
    bool valid = decodeToRequests(*commit_req, ref(pbftMsg.data));
    if (!valid)
    {
        return false;
    }
    std::ostringstream oss;
    oss << LOG_DESC("handleCommitMsg") << LOG_KV("reqNum", commit_req->height)
        << LOG_KV("curNum", m_highestBlock.number()) << LOG_KV("GenIdx", commit_req->idx)
        << LOG_KV("Cview", commit_req->view) << LOG_KV("view", m_view)
        << LOG_KV("fromIdx", pbftMsg.node_idx) << LOG_KV("fromNode", pbftMsg.node_id.abridged())
        << LOG_KV("fromIp", pbftMsg.endpoint) << LOG_KV("hash", commit_req->block_hash.abridged())
        << LOG_KV("nodeIdx", nodeIdx()) << LOG_KV("myNode", m_keyPair.pub().abridged());
    auto valid_ret = isValidCommitReq(commit_req, oss);
    if (valid_ret == CheckResult::INVALID)
    {
        return false;
    }
    /// update the view for given idx
    updateViewMap(commit_req->idx, commit_req->view);

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
CheckResult PBFTEngine::isValidCommitReq(CommitReq::Ptr req, std::ostringstream& oss) const
{
    if (m_reqCache->isExistCommit(*req))
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InvalidCommitReq: Duplicated")
                              << LOG_KV("INFO", oss.str());
        return CheckResult::INVALID;
    }
    if (hasConsensused(*req))
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InvalidCommitReq: has consensued")
                              << LOG_KV("INFO", oss.str());
        return CheckResult::INVALID;
    }
    CheckResult result = checkReq(*req, oss);
    if (result == CheckResult::FUTURE)
    {
        m_reqCache->addCommitReq(req);
    }
    return result;
}

bool PBFTEngine::handleViewChangeMsg(
    ViewChangeReq::Ptr viewChange_req, PBFTMsgPacket const& pbftMsg)
{
    bool valid = decodeToRequests(*viewChange_req, ref(pbftMsg.data));
    if (!valid)
    {
        return false;
    }
    std::ostringstream oss;
    oss << LOG_KV("reqNum", viewChange_req->height) << LOG_KV("curNum", m_highestBlock.number())
        << LOG_KV("GenIdx", viewChange_req->idx) << LOG_KV("Cview", viewChange_req->view)
        << LOG_KV("view", m_view) << LOG_KV("fromIdx", pbftMsg.node_idx)
        << LOG_KV("fromNode", pbftMsg.node_id.abridged()) << LOG_KV("fromIp", pbftMsg.endpoint)
        << LOG_KV("hash", viewChange_req->block_hash.abridged()) << LOG_KV("nodeIdx", nodeIdx())
        << LOG_KV("myNode", m_keyPair.pub().abridged());
    valid = isValidViewChangeReq(*viewChange_req, pbftMsg.node_idx, oss);
    if (!valid)
    {
        return false;
    }

    m_reqCache->addViewChangeReq(viewChange_req, m_blockChain->number());
    bool success = checkAndChangeView(viewChange_req->view);
    // try to trigger fast view change
    if (!success && viewChange_req->view > m_toView)
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
    // move here to in case of the node send message with the current view to the syncing node
    if (req.height < m_highestBlock.number())
    {
        PBFTENGINE_LOG(TRACE) << LOG_DESC("InvalidViewChangeReq: invalid height")
                              << LOG_KV("INFO", oss.str());
        return false;
    }
    if (req.view + 1 < m_toView && req.idx == source)
    {
        catchupView(req, oss);
    }
    /// check view and block height
    if (req.view <= m_view)
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
            // erase the cache
            m_reqCache->eraseLatestViewChangeCacheForNodeUpdated(req.idx);
        }
    }
}

bool PBFTEngine::checkAndChangeView(VIEWTYPE const& _view)
{
    IDXTYPE count = m_reqCache->getViewChangeSize(_view);
    if (count >= minValidNodes())
    {
        /// reach to consensue dure to fast view change
        if (m_timeManager.m_lastSignTime == 0)
        {
            m_fastViewChange = false;
        }
        PBFTENGINE_LOG(INFO) << LOG_DESC("checkAndChangeView: Reach consensus")
                             << LOG_KV("org_view", m_view)
                             << LOG_KV("cur_changeCycle", m_timeManager.m_changeCycle)
                             << LOG_KV("curView", m_view) << LOG_KV("view", _view);


        m_leaderFailed = false;
        m_timeManager.m_lastConsensusTime = utcSteadyTime();
        m_view = _view;
        m_toView.store(_view);
        m_notifyNextLeaderSeal = false;
        m_reqCache->triggerViewChange(m_view, m_blockChain->number());
        m_blockSync->noteSealingBlockNumber(m_blockChain->number());
        return true;
    }
    return false;
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
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - m_timeManager.m_lastGarbageCollection >
        std::chrono::seconds(m_timeManager.CollectInterval))
    {
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("collectGarbage")
                              << LOG_KV(
                                     "cachedForwardMsgSizeBeforeClear", m_cachedForwardMsg->size());
        m_reqCache->collectGarbage(m_highestBlock);
        // clear m_cachedForwardMsg directly
        m_cachedForwardMsg->clear();
        // clear all the future prepare directly

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
        if (m_timeManager.isTimeout())
        {
            /// timeout not triggered by fast view change
            if (m_timeManager.m_lastConsensusTime != 0)
            {
                m_fastViewChange = false;
                /// notify sealer that the consensus has been timeout
                /// and the timeout is not caused by unworked-leader(the case that the node not
                /// receive the prepare packet)
                if (m_onTimeout && m_reqCache->prepareCache().height > m_highestBlock.number())
                {
                    m_onTimeout(sealingTxNumber());
                }
            }
            m_timeManager.updateChangeCycle();
            Timer t;
            m_toView += 1;
            m_leaderFailed = true;
            m_blockSync->noteSealingBlockNumber(m_blockChain->number());
            m_timeManager.m_lastConsensusTime = utcSteadyTime();
            flag = true;
            m_reqCache->removeInvalidViewChange(m_toView, m_highestBlock);
            if (!broadcastViewChangeReq())
            {
                return;
            }

            checkAndChangeView(m_toView);
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

void PBFTEngine::handleMsg(PBFTMsgPacket::Ptr pbftMsg)
{
    Guard l(m_mutex);
    std::shared_ptr<PBFTMsg> pbft_msg;
    bool succ = false;
    switch (pbftMsg->packet_id)
    {
    case PrepareReqPacket:
    {
        PrepareReq::Ptr prepare_req = std::make_shared<PrepareReq>();
        succ = handlePrepareMsg(prepare_req, *pbftMsg);
        pbft_msg = prepare_req;
        break;
    }
    case SignReqPacket:
    {
        SignReq::Ptr req = std::make_shared<SignReq>();
        succ = handleSignMsg(req, *pbftMsg);
        pbft_msg = req;
        break;
    }
    case CommitReqPacket:
    {
        CommitReq::Ptr req = std::make_shared<CommitReq>();
        succ = handleCommitMsg(req, *pbftMsg);
        pbft_msg = req;
        break;
    }
    case ViewChangeReqPacket:
    {
        std::shared_ptr<ViewChangeReq> req = std::make_shared<ViewChangeReq>();
        succ = handleViewChangeMsg(req, *pbftMsg);
        pbft_msg = req;
        break;
    }
    default:
    {
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("handleMsg:  Err pbft message")
                              << LOG_KV("from", pbftMsg->node_idx) << LOG_KV("nodeIdx", nodeIdx())
                              << LOG_KV("myNode", m_keyPair.pub().abridged());
        return;
    }
    }

    if (!needForwardMsg(succ, pbftMsg, *pbft_msg))
    {
        return;
    }
    forwardMsg(pbftMsg, *pbft_msg);
}

// should forward message or not
bool PBFTEngine::needForwardMsg(
    bool const& _valid, PBFTMsgPacket::Ptr _pbftMsgPacket, PBFTMsg const& _pbftMsg)
{
    std::string key = _pbftMsg.uniqueKey();
    if (_pbftMsgPacket->forwardNodes && _pbftMsgPacket->forwardNodes->size() == 0)
    {
        return false;
    }
    if (!_valid || key.size() == 0)
    {
        return false;
    }
    // check ttl
    if (!m_enableTTLOptimize && _pbftMsgPacket->ttl == 1)
    {
        return false;
    }
    // check blockNumber
    return (_pbftMsg.height > m_highestBlock.number() ||
            (m_highestBlock.number() - _pbftMsg.height < 10));
}

// update ttl and forward the message
void PBFTEngine::forwardMsgByTTL(
    PBFTMsgPacket::Ptr _pbftMsgPacket, PBFTMsg const& _pbftMsg, bytesConstRef _data)
{
    std::unordered_set<h512> filter;
    filter.insert(_pbftMsgPacket->node_id);
    /// get the origin gen node id of the request
    h512 genNodeId = getSealerByIndex(_pbftMsg.idx);
    if (genNodeId != h512())
    {
        filter.insert(genNodeId);
    }
    unsigned current_ttl = _pbftMsgPacket->ttl - 1;
    broadcastMsg(_pbftMsgPacket->packet_id, _pbftMsg, _data, 0, filter, current_ttl);
}

/// start a new thread to handle the network-receivied message
void PBFTEngine::workLoop()
{
    while (isWorking())
    {
        try
        {
            if (!locatedInChosedConsensensusNodes())
            {
                waitSignal();
                continue;
            }
            std::pair<bool, PBFTMsgPacket::Ptr> ret = m_msgQueue.tryPop(c_PopWaitSeconds);
            if (ret.first)
            {
                PBFTENGINE_LOG(TRACE)
                    << LOG_DESC("workLoop: handleMsg")
                    << LOG_KV("type", std::to_string(ret.second->packet_id))
                    << LOG_KV("fromIdx", ret.second->node_idx) << LOG_KV("nodeIdx", nodeIdx())
                    << LOG_KV("myNode", m_keyPair.pub().abridged());
                handleMsg(ret.second);
            }
            /// to avoid of cpu problem
            else if (m_reqCache->futurePrepareCacheSize() == 0)
            {
                waitSignal();
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

void PBFTEngine::waitSignal()
{
    boost::unique_lock<boost::mutex> l(x_signalled);
    m_signalled.wait_for(l, boost::chrono::milliseconds(5));
}

/// handle the prepareReq cached in the futurePrepareCache
void PBFTEngine::handleFutureBlock()
{
    Guard l(m_mutex);
    // handle the future block with full-txs firstly
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
        handlePrepareMsg(p_future_prepare);
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


PBFTMsgPacket::Ptr PBFTEngine::createPBFTMsgPacket(bytesConstRef data,
    PACKET_TYPE const& packetType, unsigned const& ttl, std::shared_ptr<dev::h512s> _forwardNodes)
{
    PBFTMsgPacket::Ptr pbftPacket = m_pbftMsgFactory->createPBFTMsgPacket();
    pbftPacket->data = data.toBytes();
    pbftPacket->packet_id = packetType;
    if (ttl == 0)
        pbftPacket->ttl = maxTTL;
    else
        pbftPacket->ttl = ttl;
    // set forwardNodes when optimize ttl
    if (_forwardNodes && _forwardNodes->size() > 0)
    {
        pbftPacket->setForwardNodes(_forwardNodes);
    }
    return pbftPacket;
}

P2PMessage::Ptr PBFTEngine::transDataToMessage(bytesConstRef _data, PACKET_TYPE const& _packetType,
    unsigned const& _ttl, std::shared_ptr<dev::h512s> _forwardNodes)
{
    P2PMessage::Ptr message =
        std::dynamic_pointer_cast<P2PMessage>(m_service->p2pMessageFactory()->buildMessage());
    bytes ret_data;
    PBFTMsgPacket::Ptr pbftPacket = createPBFTMsgPacket(_data, _packetType, _ttl, _forwardNodes);
    pbftPacket->encode(ret_data);
    std::shared_ptr<dev::bytes> p_data = std::make_shared<dev::bytes>(std::move(ret_data));
    message->setBuffer(p_data);
    message->setProtocolID(m_protocolId);
    return message;
}

void PBFTEngine::createPBFTMsgFactory()
{
    if (m_enableTTLOptimize)
    {
        m_pbftMsgFactory = std::make_shared<OPBFTMsgFactory>();
    }
    else
    {
        m_pbftMsgFactory = std::make_shared<PBFTMsgFactory>();
    }
}

// get the forwardNodes
// _printLog is true when viewChangeWarning to show more detailed info
std::shared_ptr<dev::h512s> PBFTEngine::getForwardNodes(bool const& _printLog)
{
    auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    std::shared_ptr<dev::h512s> forwardNodes = nullptr;
    std::set<h512> consensusNodes;
    {
        ReadGuard l(x_consensusSet);
        consensusNodes = *m_consensusSet;
    }
    std::string connectedNodeList = "";
    // select the disconnected consensus nodes
    for (auto const& session : sessions)
    {
        if (consensusNodes.count(session.nodeID()))
        {
            if (_printLog)
            {
                connectedNodeList +=
                    boost::lexical_cast<std::string>(session.nodeIPEndpoint) + ", ";
            }
            consensusNodes.erase(session.nodeID());
        }
    }
    consensusNodes.erase(m_keyPair.pub());
    if (consensusNodes.size() > 0)
    {
        forwardNodes = std::make_shared<dev::h512s>();
        forwardNodes->resize(consensusNodes.size());
        std::copy(consensusNodes.begin(), consensusNodes.end(), forwardNodes->begin());
        if (_printLog)
        {
            std::string disconnectedNode;
            for (auto const& node : *forwardNodes)
            {
                disconnectedNode += node.abridged() + ", ";
            }
            PBFTENGINE_LOG(WARNING)
                << LOG_DESC("Find disconnectedNode")
                << LOG_KV("disconnectedNodeSize", forwardNodes->size())
                << LOG_KV("sessionSize", sessions.size())
                << LOG_KV("minValidNodes", minValidNodes())
                << LOG_KV("connectedNodeList", connectedNodeList)
                << LOG_KV("disconnectedNode", disconnectedNode) << LOG_KV("idx", nodeIdx());
        }
    }
    return forwardNodes;
}

void PBFTEngine::forwardMsgByNodeInfo(
    std::string const& _key, PBFTMsgPacket::Ptr _pbftMsgPacket, bytesConstRef _data)
{
    if (_pbftMsgPacket->forwardNodes->size() == 0)
    {
        return;
    }
    auto sessions = m_service->sessionInfosByProtocolID(m_protocolId);
    // get the forwardNodes from the _pbftMsgPacket
    // find the remaining forwardNodes
    std::shared_ptr<std::set<dev::h512>> remainingForwardNodes =
        std::make_shared<std::set<dev::h512>>(
            _pbftMsgPacket->forwardNodes->begin(), _pbftMsgPacket->forwardNodes->end());
    // send message to the forwardNodes
    for (auto const& session : sessions)
    {
        if (remainingForwardNodes->count(session.nodeID()))
        {
            remainingForwardNodes->erase(session.nodeID());
        }
    }
    // erase the node-self from the remaining forwardNodes
    if (remainingForwardNodes->count(m_keyPair.pub()))
    {
        remainingForwardNodes->erase(m_keyPair.pub());
    }

    std::shared_ptr<h512s> remainingForwardNodeList = nullptr;
    if (remainingForwardNodes->size() > 0)
    {
        remainingForwardNodeList = std::make_shared<h512s>(remainingForwardNodes->size());
        std::copy(remainingForwardNodes->begin(), remainingForwardNodes->end(),
            remainingForwardNodeList->begin());
    }
    // forward the message to corresponding nodes
    for (auto const& nodeID : *_pbftMsgPacket->forwardNodes)
    {
        sendMsg(nodeID, _pbftMsgPacket->packet_id, _key, _data, 1, remainingForwardNodeList);
    }
}

void PBFTEngine::forwardMsg(PBFTMsgPacket::Ptr _pbftMsgPacket, PBFTMsg const& _pbftMsg)
{
    std::string key = _pbftMsg.uniqueKey();
    if (m_enableTTLOptimize)
    {
        return forwardMsgByNodeInfo(key, _pbftMsgPacket, ref(_pbftMsgPacket->data));
    }
    return forwardMsgByTTL(_pbftMsgPacket, _pbftMsg, ref(_pbftMsgPacket->data));
}

void PBFTEngine::resetConfig()
{
    ConsensusEngineBase::resetConfig();
    // adjust consensus time at runtime
    resetConsensusTimeout();

    if (!m_sealerListUpdated)
    {
        return;
    }
    // for ttl-optimization
    WriteGuard l(x_consensusSet);
    // m_consensusSet
    m_consensusSet->clear();
    ReadGuard rl(m_sealerListMutex);
    m_consensusSet->insert(m_sealerList.begin(), m_sealerList.end());
}
dev::p2p::P2PMessage::Ptr PBFTEngine::toP2PMessage(
    std::shared_ptr<bytes> _data, PACKET_TYPE const& _packetType)
{
    dev::p2p::P2PMessage::Ptr message = std::dynamic_pointer_cast<dev::p2p::P2PMessage>(
        m_service->p2pMessageFactory()->buildMessage());
    message->setBuffer(_data);
    message->setPacketType(_packetType);
    message->setProtocolID(m_protocolId);
    return message;
}

dev::h512 PBFTEngine::selectNodeToRequestMissedTxs(PrepareReq::Ptr _prepareReq)
{
    // can't find the node that generate the prepareReq
    h512 targetNode;
    if (!getNodeIDByIndex(targetNode, _prepareReq->idx))
    {
        return dev::h512();
    }
    return targetNode;
}

bool PBFTEngine::handlePartiallyPrepare(PrepareReq::Ptr _prepareReq)
{
    std::ostringstream oss;
    oss << LOG_DESC("handlePartiallyPrepare") << LOG_KV("reqIdx", _prepareReq->idx)
        << LOG_KV("view", _prepareReq->view) << LOG_KV("reqNum", _prepareReq->height)
        << LOG_KV("curNum", m_highestBlock.number()) << LOG_KV("consNum", m_consensusBlockNumber)
        << LOG_KV("hash", _prepareReq->block_hash.abridged()) << LOG_KV("nodeIdx", nodeIdx())
        << LOG_KV("myNode", m_keyPair.pub().abridged())
        << LOG_KV("curChangeCycle", m_timeManager.m_changeCycle);
    PBFTENGINE_LOG(DEBUG) << oss.str();
    // check the PartiallyPrepare
    auto ret = isValidPrepare(*_prepareReq, oss);
    if (ret == CheckResult::INVALID)
    {
        return false;
    }
    /// update the view for given idx
    updateViewMap(_prepareReq->idx, _prepareReq->view);

    _prepareReq->pBlock = m_blockFactory->createBlock();
    assert(_prepareReq->pBlock);

    if (ret == CheckResult::FUTURE)
    {
        // decode the partiallyBlock
        _prepareReq->pBlock->decodeProposal(ref(*_prepareReq->block), true);
        bool allHit = m_txPool->initPartiallyBlock(_prepareReq->pBlock);
        // hit all the transactions
        if (allHit)
        {
            // re-encode the block into the completed block(for pbft-backup consideration)
            _prepareReq->pBlock->encode(*_prepareReq->block);
            m_partiallyPrepareCache->addFuturePrepareCache(_prepareReq);
            return true;
        }
        // request missed txs for the future prepare
        else
        {
            m_partiallyPrepareCache->addPartiallyFuturePrepare(_prepareReq);
            return requestMissedTxs(_prepareReq);
        }
    }
    if (!m_partiallyPrepareCache->addPartiallyRawPrepare(_prepareReq))
    {
        return false;
    }
    // decode the partiallyBlock
    _prepareReq->pBlock->decodeProposal(ref(*_prepareReq->block), true);
    bool allHit = m_txPool->initPartiallyBlock(_prepareReq->pBlock);
    // hit all transactions
    if (allHit)
    {
        PBFTENGINE_LOG(DEBUG) << LOG_DESC(
                                     "hit all the transactions, handle the rawPrepare directly")
                              << LOG_KV("txsSize", _prepareReq->pBlock->transactions()->size());
        m_partiallyPrepareCache->transPartiallyPrepareIntoRawPrepare();
        // begin to handlePrepare
        return execPrepareAndGenerateSignMsg(_prepareReq, oss);
    }
    return requestMissedTxs(_prepareReq);
}

bool PBFTEngine::requestMissedTxs(PrepareReq::Ptr _prepareReq)
{
    // can't find the node that generate the prepareReq
    h512 targetNode = selectNodeToRequestMissedTxs(_prepareReq);
    if (targetNode == dev::h512())
    {
        return false;
    }

    // miss some transactions, request the missed transaction
    PartiallyBlock::Ptr partiallyBlock =
        std::dynamic_pointer_cast<PartiallyBlock>(_prepareReq->pBlock);
    assert(partiallyBlock);
    std::shared_ptr<bytes> encodedMissTxsInfo = std::make_shared<bytes>();
    partiallyBlock->encodeMissedInfo(encodedMissTxsInfo);

    auto p2pMsg = toP2PMessage(encodedMissTxsInfo, GetMissedTxsPacket);
    p2pMsg->setPacketType(GetMissedTxsPacket);

    m_service->asyncSendMessageByNodeID(targetNode, p2pMsg, nullptr);

    PBFTENGINE_LOG(DEBUG) << LOG_DESC("send GetMissedTxsPacket to the leader")
                          << LOG_KV("targetIdx", _prepareReq->idx)
                          << LOG_KV("number", _prepareReq->height)
                          << LOG_KV("hash", _prepareReq->block_hash.abridged())
                          << LOG_KV("missedTxsSize", partiallyBlock->missedTxs()->size())
                          << LOG_KV("size", p2pMsg->length());
    return true;
}

/// BIP 152 logic related
// forward the message
void PBFTEngine::forwardPrepareMsg(PBFTMsgPacket::Ptr _pbftMsgPacket, PrepareReq::Ptr _prepareReq)
{
    // forward the message
    std::shared_ptr<dev::bytes> encodedBytes = std::make_shared<dev::bytes>();
    _prepareReq->pBlock->encode(*(_prepareReq->block));
    _prepareReq->encode(*encodedBytes);
    if (m_enableTTLOptimize)
    {
        forwardMsgByNodeInfo(_prepareReq->uniqueKey(), _pbftMsgPacket, ref(*encodedBytes));
    }
    else
    {
        forwardMsgByTTL(_pbftMsgPacket, *_prepareReq, ref(*encodedBytes));
    }
}

// receive the GetMissedTxsPacket request and response the requested-transactions
void PBFTEngine::onReceiveGetMissedTxsRequest(
    std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
{
    try
    {
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("onReceiveGetMissedTxsRequest")
                              << LOG_KV("size", _message->length())
                              << LOG_KV("peer", _session->nodeID().abridged());
        std::shared_ptr<bytes> _encodedBytes = std::make_shared<bytes>();
        if (!m_partiallyPrepareCache->fetchMissedTxs(_encodedBytes, ref(*(_message->buffer()))))
        {
            return;
        }
        // response the transaction to the request node
        auto p2pMsg = toP2PMessage(_encodedBytes, MissedTxsPacket);
        p2pMsg->setPacketType(MissedTxsPacket);

        m_service->asyncSendMessageByNodeID(_session->nodeID(), p2pMsg, nullptr);
    }
    catch (std::exception const& _e)
    {
        PBFTENGINE_LOG(WARNING) << LOG_DESC("onReceiveGetMissedTxsRequest exceptioned")
                                << LOG_KV("peer", _session->nodeID().abridged())
                                << LOG_KV("errorInfo", boost::diagnostic_information(_e));
    }
}

void PBFTEngine::handleP2PMessage(
    NetworkException _exception, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
{
    try
    {
        if (!m_enablePrepareWithTxsHash)
        {
            onRecvPBFTMessage(_exception, _session, _message);
            return;
        }
        auto self = std::weak_ptr<PBFTEngine>(shared_from_this());
        switch (_message->packetType())
        {
        case PartiallyPreparePacket:
            m_prepareWorker->enqueue([self, _session, _message]() {
                auto pbftEngine = self.lock();
                if (!pbftEngine)
                {
                    return;
                }
                try
                {
                    pbftEngine->handlePartiallyPrepare(_session, _message);
                }
                catch (std::exception const& e)
                {
                    PBFTENGINE_LOG(WARNING)
                        << LOG_DESC("handlePartiallyPrepare exceptioned")
                        << LOG_KV("peer", _session->nodeID().abridged())
                        << LOG_KV("errorInfo", boost::diagnostic_information(e));
                }
            });
            break;
        // receive getMissedPacket request, response missed transactions
        case GetMissedTxsPacket:
            m_messageHandler->enqueue([self, _session, _message]() {
                auto pbftEngine = self.lock();
                if (pbftEngine)
                {
                    pbftEngine->onReceiveGetMissedTxsRequest(_session, _message);
                }
            });
            break;
        // receive missed transactions, fill block
        case MissedTxsPacket:
            m_messageHandler->enqueue([self, _session, _message]() {
                auto pbftEngine = self.lock();
                if (pbftEngine)
                {
                    pbftEngine->onReceiveMissedTxsResponse(_session, _message);
                }
            });
            break;
        default:
            onRecvPBFTMessage(_exception, _session, _message);
            break;
        }
    }
    catch (std::exception const& _e)
    {
        PBFTENGINE_LOG(WARNING) << LOG_DESC("handleP2PMessage: invalid message")
                                << LOG_KV("peer", _session->nodeID().abridged())
                                << LOG_KV("errorInfo", boost::diagnostic_information(_e));
    }
}


bool PBFTEngine::handlePartiallyPrepare(
    std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message)
{
    return handleReceivedPartiallyPrepare(_session, _message, nullptr);
}

// handle Partially prepare
bool PBFTEngine::handleReceivedPartiallyPrepare(std::shared_ptr<P2PSession> _session,
    P2PMessage::Ptr _message, std::function<void(PBFTMsgPacket::Ptr)> const& _f)
{
    // decode the _message into prepareReq
    PBFTMsgPacket::Ptr pbftMsg = m_pbftMsgFactory->createPBFTMsgPacket();
    if (!decodePBFTMsgPacket(pbftMsg, _message, _session))
    {
        return false;
    }
    if (_f)
    {
        _f(pbftMsg);
    }
    PrepareReq::Ptr prepareReq = std::make_shared<PrepareReq>();
    if (!decodeToRequests(*prepareReq, ref(pbftMsg->data)))
    {
        return false;
    }
    Guard l(m_mutex);
    bool succ = handlePartiallyPrepare(prepareReq);
    // maybe return succ for addFuturePrepare
    if (!prepareReq->pBlock)
    {
        return false;
    }
    if (needForwardMsg(succ, pbftMsg, *prepareReq))
    {
        // all hit ?
        if (prepareReq->pBlock->txsAllHit())
        {
            forwardPrepareMsg(pbftMsg, prepareReq);
            return succ;
        }
        clearInvalidCachedForwardMsg();
        // pbftMsg->packet_id = PrepareReqPacket;
        m_cachedForwardMsg->insert(
            std::make_pair(prepareReq->block_hash, std::make_pair(prepareReq->height, pbftMsg)));
    }

    return succ;
}

void PBFTEngine::onReceiveMissedTxsResponse(
    std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message)
{
    try
    {
        Guard l(m_mutex);
        PBFTENGINE_LOG(DEBUG) << LOG_DESC("onReceiveMissedTxsResponse and fillBlock")
                              << LOG_KV("size", _message->length())
                              << LOG_KV("peer", _session->nodeID().abridged());
        RLP blockRLP(ref(*(_message->buffer())));
        // get blockHash of the response
        auto blockHash = blockRLP[1].toHash<h256>(RLP::VeryStrict);
        // the response is for the future prepare,
        // fill the future prepare and add it to the futurePrepareCache
        if (m_partiallyPrepareCache->existInFuturePrepare(blockHash))
        {
            m_partiallyPrepareCache->fillFutureBlock(blockRLP);
            return;
        }
        if (!m_partiallyPrepareCache->fillPrepareCacheBlock(blockRLP))
        {
            return;
        }
        // handlePrepare
        auto prepareReq = m_partiallyPrepareCache->partiallyRawPrepare();
        // re-encode the block into the completed block(for pbft-backup consideration)
        prepareReq->pBlock->encode(*prepareReq->block);
        bool ret = handlePrepareMsg(prepareReq);
        // forward the completed prepare message
        if (ret && m_cachedForwardMsg->count(prepareReq->block_hash))
        {
            auto pbftMsg = (*m_cachedForwardMsg)[prepareReq->block_hash].second;
            // forward the message
            forwardPrepareMsg(pbftMsg, prepareReq);
        }
        m_cachedForwardMsg->erase(prepareReq->block_hash);
    }
    catch (std::exception const& _e)
    {
        PBFTENGINE_LOG(WARNING) << LOG_DESC("onReceiveMissedTxsResponse exceptioned")
                                << LOG_KV("peer", _session->nodeID().abridged())
                                << LOG_KV("errorInfo", boost::diagnostic_information(_e));
    }
}

void PBFTEngine::clearInvalidCachedForwardMsg()
{
    for (auto it = m_cachedForwardMsg->begin(); it != m_cachedForwardMsg->end();)
    {
        if (it->second.first < m_highestBlock.number() &&
            m_highestBlock.number() - it->second.first >= 10)
        {
            it = m_cachedForwardMsg->erase(it);
        }
        else
        {
            it++;
        }
    }
}

void PBFTEngine::resetConsensusTimeout()
{
    if (!m_supportConsensusTimeAdjust)
    {
        return;
    }
    auto consensusTimeoutStr =
        m_blockChain->getSystemConfigByKey(dev::precompiled::SYSTEM_KEY_CONSENSUS_TIMEOUT);
    uint64_t consensusTimeout = boost::lexical_cast<uint64_t>(consensusTimeoutStr) * 1000;

    // Prevent external users from modifying the empty block time by modifying the code
    if (m_timeManager.m_emptyBlockGenTime > consensusTimeout)
    {
        m_timeManager.m_emptyBlockGenTime = consensusTimeout / 3;
    }
    // update emptyBlockGenTime
    if (m_timeManager.m_viewTimeout != consensusTimeout)
    {
        m_timeManager.resetConsensusTimeout(consensusTimeout);
        PBFTENGINE_LOG(INFO) << LOG_DESC("resetConsensusTimeout")
                             << LOG_KV("updatedConsensusTimeout", consensusTimeout)
                             << LOG_KV("minBlockGenTime", m_timeManager.m_minBlockGenTime);
    }
}

}  // namespace consensus
}  // namespace dev
