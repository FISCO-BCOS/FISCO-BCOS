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
 * @file: FakePBFTEngine.cpp
 * @author: yujiechen
 * @date: 2018-10-10
 */
#pragma once
#include <libconsensus/Sealer.h>
#include <libconsensus/pbft/PBFTEngine.h>
#include <libconsensus/pbft/PBFTSealer.h>
#include <libdevcore/TopicInfo.h>
#include <libethcore/BlockFactory.h>
#include <test/unittests/libblockverifier/FakeBlockVerifier.h>
#include <test/unittests/libsync/FakeBlockSync.h>
#include <test/unittests/libtxpool/FakeBlockChain.h>
#include <memory>
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::txpool;
using namespace dev::blockchain;
using namespace dev::consensus;

namespace dev
{
namespace test
{
/// fake class of PBFTEngine
class FakePBFTEngine : public PBFTEngine
{
public:
    FakePBFTEngine(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        PROTOCOL_ID const& _protocolId, h512s const& _sealerList = h512s(),
        std::string const& _baseDir = "./", KeyPair const& _key_pair = KeyPair::create())
      : PBFTEngine(_service, _txPool, _blockChain, _blockSync, _blockVerifier, _protocolId,
            _key_pair, _sealerList)
    {
        setLeaderFailed(false);
        BlockHeader highest = m_blockChain->getBlockByNumber(m_blockChain->number())->header();
        setHighest(highest);
        setOmitEmpty(true);
        setMaxTTL(1);
        setEmptyBlockGenTime(1000);
        setNodeNum(3);
        setBaseDir(_baseDir);
        setMaxBlockTransactions(300000000);
        createPBFTMsgFactory();
        m_blockFactory = std::make_shared<dev::eth::BlockFactory>();
        m_reqCache = std::make_shared<PBFTReqCache>();
    }
    void updateConsensusNodeList() override {}
    void fakeUpdateConsensusNodeList() { return PBFTEngine::updateConsensusNodeList(); }
    KeyPair const& keyPair() const { return m_keyPair; }
    const std::shared_ptr<PBFTBroadcastCache> broadCastCache() const { return m_broadCastCache; }
    const std::shared_ptr<PBFTReqCache> reqCache() const { return m_reqCache; }
    TimeManager const& timeManager() const { return m_timeManager; }
    TimeManager& mutableTimeManager() { return m_timeManager; }
    const std::shared_ptr<dev::db::LevelDB> backupDB() const { return m_backupDB; }
    int64_t consensusBlockNumber() const { return m_consensusBlockNumber; }
    void setConsensusBlockNumber(int64_t const& number) { m_consensusBlockNumber = number; }

    void setToView(VIEWTYPE const& view) { m_toView = view; }

    bool isDiskSpaceEnough(std::string const& path) override
    {
        std::size_t pos = path.find("invalid");
        if (pos != std::string::npos)
            return false;
        return true;
    }
    void resetSealingHeader(BlockHeader& header)
    {
        /// import block
        header.setTimestamp(utcTime());
        header.setSealerList(sealerList());
        header.setSealer(u256(nodeIdx()));
        header.setLogBloom(LogBloom());
        header.setGasUsed(u256(0));
    }


    void resetBlock(Block& block)
    {
        block.resetCurrentBlock(m_blockChain->getBlockByNumber(m_blockChain->number())->header());
        resetSealingHeader(block.header());
    }

    IDXTYPE f() const { return m_f; }
    PBFTMsgQueue& mutableMsgQueue() { return m_msgQueue; }
    void onRecvPBFTMessage(NetworkException exception, std::shared_ptr<P2PSession> session,
        P2PMessage::Ptr message) override
    {
        PBFTEngine::onRecvPBFTMessage(exception, session, message);
    }

    P2PMessage::Ptr transDataToMessageWrapper(bytesConstRef data, PACKET_TYPE const& packetType,
        PROTOCOL_ID const& protocolId, unsigned const& ttl)
    {
        return PBFTEngine::transDataToMessage(data, packetType, protocolId, ttl);
    }
    bool broadcastMsgWrapper(unsigned const& packetType, std::string const& key, bytesConstRef data,
        std::unordered_set<h512> const& filter = std::unordered_set<h512>(),
        unsigned const& ttl = 0)
    {
        return PBFTEngine::broadcastMsg(packetType, key, data, 0, filter, ttl);
    }

    bool broadcastFilter(h512 const& nodeId, unsigned const& packetType, std::string const& key)
    {
        return PBFTEngine::broadcastFilter(nodeId, packetType, key);
    }
    void setSealerList(dev::h512s const& sealerList) { m_sealerList = sealerList; }

    void setMaxBlockTransactions(size_t const& maxTrans) { m_maxBlockTransactions = maxTrans; }
    void updateMaxBlockTransactions() override {}
    bool checkBlock(dev::eth::Block const& block)
    {
        auto orgNumber = m_blockChain->number();
        std::shared_ptr<FakeBlockChain> blockChain =
            std::dynamic_pointer_cast<FakeBlockChain>(m_blockChain);
        blockChain->setBlockNumber(0);
        bool ret = PBFTEngine::checkBlock(block);
        blockChain->setBlockNumber(orgNumber);
        return ret;
    }
    std::shared_ptr<P2PInterface> mutableService() { return m_service; }

    std::shared_ptr<BlockChainInterface> blockChain() { return m_blockChain; }
    std::shared_ptr<TxPoolInterface> txPool() { return m_txPool; }
    bool broadcastSignReq(PrepareReq const& req) { return PBFTEngine::broadcastSignReq(req); }
    VIEWTYPE view() const override { return m_view; }
    void setView(VIEWTYPE const& _view) { m_view = _view; }
    void checkAndSave() { return PBFTEngine::checkAndSave(); }

    void setHighest(BlockHeader const& header) { m_highestBlock = header; }
    BlockHeader& mutableHighest() { return m_highestBlock; }
    void setNodeNum(IDXTYPE const& nodeNum) { m_nodeNum = nodeNum; }
    IDXTYPE nodeNum() const { return m_nodeNum; }
    IDXTYPE fValue() const { return m_f; }
    void setF(IDXTYPE const& fValue) { m_f = fValue; }
    bool leaderFailed() const { return m_leaderFailed; }
    bool cfgErr() const { return m_cfgErr; }

    void initPBFTEnv(unsigned _view_timeout) { return PBFTEngine::initPBFTEnv(_view_timeout); }

    void resetConfig() override
    {
        bool needClear = false;
        /// at least one sealer when resetConfig
        if (m_sealerList.size() == 0)
        {
            m_sealerList.push_back(KeyPair::create().pub());
            needClear = true;
        }
        PBFTEngine::resetConfig();
        if (needClear)
        {
            m_sealerList.clear();
        }
    }

    void onNotifyNextLeaderReset()
    {
        PBFTEngine::onNotifyNextLeaderReset(
            boost::bind(&FakePBFTEngine::resetBlockForNextLeaderTest, this, _1));
    }
    void resetBlockForNextLeaderTest(dev::h256Hash const&) {}

    void checkAndCommit() { return PBFTEngine::checkAndCommit(); }
    static std::string const& backupKeyCommitted() { return PBFTEngine::c_backupKeyCommitted; }
    bool broadcastCommitReq(PrepareReq const& req) { return PBFTEngine::broadcastCommitReq(req); }
    bool broadcastViewChangeReq() { return PBFTEngine::broadcastViewChangeReq(); }
    void checkTimeout() { return PBFTEngine::checkTimeout(); }
    void checkAndChangeView() { return PBFTEngine::checkAndChangeView(); }
    CheckResult isValidPrepare(PrepareReq const& req) const
    {
        std::ostringstream oss;
        return PBFTEngine::isValidPrepare(req, oss);
    }

    bool isValidViewChangeReq(ViewChangeReq const& req, IDXTYPE const& source)
    {
        std::ostringstream oss;
        return PBFTEngine::isValidViewChangeReq(req, source, oss);
    }

    void setLeaderFailed(bool leaderFailed) { m_leaderFailed = leaderFailed; }
    inline std::pair<bool, IDXTYPE> getLeader() const override { return PBFTEngine::getLeader(); }

    void handleMsg(PBFTMsgPacket const& pbftMsg)
    {
        std::shared_ptr<PBFTMsgPacket> pbftMsgPtr = std::make_shared<PBFTMsgPacket>(pbftMsg);
        return PBFTEngine::handleMsg(pbftMsgPtr);
    }

    void notifySealing(dev::eth::Block const& block) { return PBFTEngine::notifySealing(block); }
    bool handlePrepareMsg(PrepareReq::Ptr prepareReq, std::string const& ip = "self")
    {
        return PBFTEngine::handlePrepareMsg(prepareReq, ip);
    }

    void setEnablePrepareWithTxsHash(bool const& _enablePrepareWithTxsHash)
    {
        m_enablePrepareWithTxsHash = _enablePrepareWithTxsHash;
    }

    std::shared_ptr<PBFTReqCache> reqCache() { return m_reqCache; }

    PrepareReq::Ptr wrapperConstructPrepareReq(dev::eth::Block::Ptr _block)
    {
        return PBFTEngine::constructPrepareReq(_block);
    }

    void setOmitEmpty(bool value) { m_omitEmptyBlock = value; }

    /// handle sign
    bool handleSignMsg(SignReq::Ptr sign_req, PBFTMsgPacket const& pbftMsg)
    {
        return PBFTEngine::handleSignMsg(sign_req, pbftMsg);
    }

    /// handle viewchange
    bool handleViewChangeMsg(ViewChangeReq::Ptr viewChange_req, PBFTMsgPacket const& pbftMsg)
    {
        return PBFTEngine::handleViewChangeMsg(viewChange_req, pbftMsg);
    }

    CheckResult isValidSignReq(SignReq const& req) const
    {
        std::ostringstream oss;
        SignReq::Ptr signReq = std::make_shared<SignReq>(req);
        return PBFTEngine::isValidSignReq(signReq, oss);
    }
    CheckResult isValidCommitReq(CommitReq const& req) const
    {
        std::ostringstream oss;
        CommitReq::Ptr commitReq = std::make_shared<CommitReq>(req);
        return PBFTEngine::isValidCommitReq(commitReq, oss);
    }

    bool handleCommitMsg(CommitReq::Ptr commit_req, PBFTMsgPacket const& pbftMsg)
    {
        return PBFTEngine::handleCommitMsg(commit_req, pbftMsg);
    }

    bool shouldSeal() { return PBFTEngine::shouldSeal(); }

    void setNodeIdx(IDXTYPE const& _idx) { m_idx = _idx; }
    void collectGarbage() { return PBFTEngine::collectGarbage(); }
    void handleFutureBlock() { return PBFTEngine::handleFutureBlock(); }
    /// NodeAccountType accountType() override { return m_accountType; }
    void setAccountType(NodeAccountType const& accountType) { m_accountType = accountType; }

    bool notifyNextLeaderSeal() { return m_notifyNextLeaderSeal; }
    IDXTYPE getNextLeader() const { return PBFTEngine::getNextLeader(); }
    void setKeyPair(KeyPair const& _keyPair) { m_keyPair = _keyPair; }

    PartiallyPBFTReqCache::Ptr partiallyReqCache() { return m_partiallyPrepareCache; }

    void wrapperHandleP2PMessage(dev::p2p::NetworkException _exception,
        std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message)
    {
        return PBFTEngine::handleP2PMessage(_exception, _session, _message);
    }
};

template <typename T>
class FakeConsensus
{
public:
    FakeConsensus(size_t sealerSize, PROTOCOL_ID protocolID,
        std::shared_ptr<SyncInterface> sync = std::make_shared<FakeBlockSync>(),
        std::shared_ptr<BlockVerifierInterface> blockVerifier =
            std::make_shared<FakeBlockverifier>(),
        std::shared_ptr<TxPoolFixture> txpool_creator = std::make_shared<TxPoolFixture>(5, 5))
    {
        m_consensus = std::make_shared<T>(txpool_creator->m_topicService, txpool_creator->m_txPool,
            txpool_creator->m_blockChain, sync, blockVerifier, protocolID, m_sealerList);
        /// fake sealerList
        FakeSealerList(sealerSize);
        resetSessionInfo();
    }

    void resetSessionInfo()
    {
        FakeService* service = dynamic_cast<FakeService*>(m_consensus->mutableService().get());
        service->clearSessionInfo();
        for (size_t i = 0; i < m_sealerList.size(); i++)
        {
            NodeIPEndpoint m_endpoint(bi::address::from_string("127.0.0.1"), 30303);
            dev::network::NodeInfo node_info;
            node_info.nodeID = m_sealerList[i];
            std::set<dev::TopicItem> topicList;
            P2PSessionInfo info(node_info, m_endpoint, topicList);
            service->appendSessionInfo(info);
        }
    }

    /// fake sealer list
    void FakeSealerList(size_t sealerSize)
    {
        m_sealerList.clear();
        for (size_t i = 0; i < sealerSize; i++)
        {
            KeyPair key_pair = KeyPair::create();
            m_sealerList.push_back(key_pair.pub());
            m_secrets.push_back(key_pair.secret());
            m_keyPair.push_back(key_pair);
            m_consensus->appendSealer(key_pair.pub());
        }
    }
    std::shared_ptr<T> consensus() { return m_consensus; }

public:
    h512s m_sealerList;
    std::vector<Secret> m_secrets;
    std::vector<KeyPair> m_keyPair;

private:
    std::shared_ptr<T> m_consensus;
};

/// fake class of PBFTConsensus
class FakePBFTSealer : public PBFTSealer
{
public:
    FakePBFTSealer(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        int16_t const& _protocolId, std::string const& _baseDir = "",
        KeyPair const& _key_pair = KeyPair::create(), h512s const& _sealerList = h512s())
      : PBFTSealer(_txPool, _blockChain, _blockSync)
    {
        m_consensusEngine = std::make_shared<FakePBFTEngine>(_service, _txPool, _blockChain,
            _blockSync, _blockVerifier, _protocolId, _sealerList, _baseDir, _key_pair);
        m_pbftEngine = std::dynamic_pointer_cast<PBFTEngine>(m_consensusEngine);
        auto blockFactory = std::make_shared<BlockFactory>();
        setBlockFactory(blockFactory);
    }

    void loadTransactions(uint64_t const& transToFetch)
    {
        return PBFTSealer::loadTransactions(transToFetch);
    }

    bool checkTxsEnough(uint64_t maxTxsCanSeal) override
    {
        return PBFTSealer::checkTxsEnough(maxTxsCanSeal);
    }

    std::shared_ptr<FakePBFTEngine> engine()
    {
        std::shared_ptr<FakePBFTEngine> fake_pbft =
            std::dynamic_pointer_cast<FakePBFTEngine>(m_pbftEngine);
        assert(fake_pbft);
        return fake_pbft;
    }

    void setStartConsensus(bool startConsensus) { m_startConsensus = startConsensus; }
    bool getStartConsensus() { return m_startConsensus; }

    bool syncBlock() { return m_syncBlock; }
    uint64_t getSealingBlockNumber() { return m_sealing.block->blockHeader().number(); }
    Sealing const& sealing() const { return m_sealing; }
    void reportNewBlock() { return PBFTSealer::reportNewBlock(); }
    bool shouldSeal() override { return Sealer::shouldSeal(); }
    bool canHandleBlockForNextLeader() override
    {
        return PBFTSealer::canHandleBlockForNextLeader();
    }
    bool reachBlockIntervalTime() override { return PBFTSealer::reachBlockIntervalTime(); }
    void doWork(bool wait) override { return PBFTSealer::doWork(wait); }
    bool shouldHandleBlock() override { return PBFTSealer::shouldHandleBlock(); }
    void resetSealingBlock(h256Hash const& filter = h256Hash(), bool resetNextLeader = false)
    {
        return PBFTSealer::resetSealingBlock(filter, resetNextLeader);
    }
    void resetBlock(dev::eth::Block& block, bool resetNextLeader = false)
    {
        std::shared_ptr<dev::eth::Block> p_block = std::make_shared<dev::eth::Block>(block);
        return PBFTSealer::resetBlock(p_block, resetNextLeader);
    }
    void setBlock() { return PBFTSealer::setBlock(); }
    void start() override { return Sealer::start(); }
    bool shouldResetSealing() override { return Sealer::shouldResetSealing(); }
};
}  // namespace test
}  // namespace dev
