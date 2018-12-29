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
        PROTOCOL_ID const& _protocolId, h512s const& _minerList = h512s(),
        std::string const& _baseDir = "./", KeyPair const& _key_pair = KeyPair::create())
      : PBFTEngine(_service, _txPool, _blockChain, _blockSync, _blockVerifier, _protocolId,
            _baseDir, _key_pair, _minerList)
    {}
    void updateConsensusNodeList() override {}
    KeyPair const& keyPair() const { return m_keyPair; }
    const std::shared_ptr<PBFTBroadcastCache> broadCastCache() const { return m_broadCastCache; }
    const std::shared_ptr<PBFTReqCache> reqCache() const { return m_reqCache; }
    TimeManager const& timeManager() const { return m_timeManager; }
    TimeManager& mutableTimeManager() { return m_timeManager; }
    const std::shared_ptr<dev::db::LevelDB> backupDB() const { return m_backupDB; }
    bool const& leaderFailed() const { return m_leaderFailed; }
    int64_t const& consensusBlockNumber() const { return m_consensusBlockNumber; }
    VIEWTYPE const& toView() const { return m_toView; }
    VIEWTYPE const& view() const { return m_view; }

    bool isDiskSpaceEnough(std::string const& path) override
    {
        std::size_t pos = path.find("invalid");
        if (pos != std::string::npos)
            return false;
        return boost::filesystem::space(path).available > 1024;
    }
    void resetSealingHeader(BlockHeader& header)
    {
        /// import block
        header.setTimestamp(utcTime());
        header.setSealerList(minerList());
        header.setSealer(u256(nodeIdx()));
        header.setLogBloom(LogBloom());
        header.setGasUsed(u256(0));
    }

    void resetBlock(Block& block)
    {
        block.resetCurrentBlock(m_blockChain->getBlockByNumber(m_blockChain->number())->header());
        resetSealingHeader(block.header());
    }

    IDXTYPE const& f() { return m_f; }
    void resetConfig() { PBFTEngine::resetConfig(); }
    PBFTMsgQueue& mutableMsgQueue() { return m_msgQueue; }
    void onRecvPBFTMessage(
        NetworkException exception, std::shared_ptr<P2PSession> session, P2PMessage::Ptr message)
    {
        PBFTEngine::onRecvPBFTMessage(exception, session, message);
    }

    P2PMessage::Ptr transDataToMessage(bytesConstRef data, PACKET_TYPE const& packetType,
        PROTOCOL_ID const& protocolId, unsigned const& ttl)
    {
        return PBFTEngine::transDataToMessage(data, packetType, protocolId, ttl);
    }

    bool broadcastMsg(unsigned const& packetType, std::string const& key, bytesConstRef data,
        std::unordered_set<h512> const& filter = std::unordered_set<h512>(),
        unsigned const& ttl = 0)
    {
        return PBFTEngine::broadcastMsg(packetType, key, data, filter, ttl);
    }

    bool broadcastFilter(h512 const& nodeId, unsigned const& packetType, std::string const& key)
    {
        return PBFTEngine::broadcastFilter(nodeId, packetType, key);
    }
    std::shared_ptr<P2PInterface> mutableService() { return m_service; }
    std::shared_ptr<BlockChainInterface> blockChain() { return m_blockChain; }
    std::shared_ptr<TxPoolInterface> txPool() { return m_txPool; }
    bool broadcastSignReq(PrepareReq const& req) { return PBFTEngine::broadcastSignReq(req); }
    VIEWTYPE view() { return m_view; }
    void setView(VIEWTYPE const& _view) { m_view = _view; }
    void checkAndSave() { return PBFTEngine::checkAndSave(); }

    void setHighest(BlockHeader const& header) { m_highestBlock = header; }
    BlockHeader& mutableHighest() { return m_highestBlock; }
    void setNodeNum(IDXTYPE const& nodeNum) { m_nodeNum = nodeNum; }
    IDXTYPE const& nodeNum() { return m_nodeNum; }
    IDXTYPE const& fValue() { return m_f; }
    void setF(IDXTYPE const& fValue) { m_f = fValue; }
    int64_t& mutableConsensusNumber() { return m_consensusBlockNumber; }
    bool const& leaderFailed() { return m_leaderFailed; }
    bool const& cfgErr() { return m_cfgErr; }

    void initPBFTEnv(unsigned _view_timeout) { return PBFTEngine::initPBFTEnv(_view_timeout); }
    void checkAndCommit() { return PBFTEngine::checkAndCommit(); }
    static std::string const& backupKeyCommitted() { return PBFTEngine::c_backupKeyCommitted; }
    bool broadcastCommitReq(PrepareReq const& req) { return PBFTEngine::broadcastCommitReq(req); }
    bool broadcastViewChangeReq() { return PBFTEngine::broadcastViewChangeReq(); }
    void checkTimeout() { return PBFTEngine::checkTimeout(); }
    void checkAndChangeView() { return PBFTEngine::checkAndChangeView(); }
    bool isValidPrepare(PrepareReq const& req) const
    {
        std::ostringstream oss;
        return PBFTEngine::isValidPrepare(req, oss);
    }
    bool& mutableLeaderFailed() { return m_leaderFailed; }
    inline std::pair<bool, IDXTYPE> getLeader() const { return PBFTEngine::getLeader(); }
    void handlePrepareMsg(PrepareReq const& prepareReq, std::string const& ip = "self")
    {
        return PBFTEngine::handlePrepareMsg(prepareReq, ip);
    }
    void setOmitEmpty(bool value) { m_omitEmptyBlock = value; }
    void handleSignMsg(SignReq& sign_req, PBFTMsgPacket const& pbftMsg)
    {
        return PBFTEngine::handleSignMsg(sign_req, pbftMsg);
    }
    bool isValidSignReq(SignReq const& req) const
    {
        std::ostringstream oss;
        return PBFTEngine::isValidSignReq(req, oss);
    }
    bool isValidCommitReq(CommitReq const& req) const
    {
        std::ostringstream oss;
        return PBFTEngine::isValidCommitReq(req, oss);
    }

    void handleCommitMsg(CommitReq& commit_req, PBFTMsgPacket const& pbftMsg)
    {
        return PBFTEngine::handleCommitMsg(commit_req, pbftMsg);
    }

    bool shouldSeal() { return PBFTEngine::shouldSeal(); }

    void setNodeIdx(IDXTYPE const& _idx) { m_idx = _idx; }
    void collectGarbage() { return PBFTEngine::collectGarbage(); }
    void handleFutureBlock() { return PBFTEngine::handleFutureBlock(); }
};

template <typename T>
class FakeConsensus
{
public:
    FakeConsensus(size_t minerSize, PROTOCOL_ID protocolID,
        std::shared_ptr<SyncInterface> sync = std::make_shared<FakeBlockSync>(),
        std::shared_ptr<BlockVerifierInterface> blockVerifier =
            std::make_shared<FakeBlockverifier>(),
        std::shared_ptr<TxPoolFixture> txpool_creator = std::make_shared<TxPoolFixture>(5, 5))
    {
        m_consensus = std::make_shared<T>(txpool_creator->m_topicService, txpool_creator->m_txPool,
            txpool_creator->m_blockChain, sync, blockVerifier, protocolID, m_minerList);
        /// fake minerList
        FakeMinerList(minerSize);
        resetSessionInfo();
    }

    void resetSessionInfo()
    {
        FakeService* service = dynamic_cast<FakeService*>(m_consensus->mutableService().get());
        service->clearSessionInfo();
        for (size_t i = 0; i < m_minerList.size(); i++)
        {
            NodeIPEndpoint m_endpoint(bi::address::from_string("127.0.0.1"), 30303, 30303);
            P2PSessionInfo info(m_minerList[i], m_endpoint, std::set<std::string>());
            service->appendSessionInfo(info);
        }
    }

    /// fake miner list
    void FakeMinerList(size_t minerSize)
    {
        m_minerList.clear();
        for (size_t i = 0; i < minerSize; i++)
        {
            KeyPair key_pair = KeyPair::create();
            m_minerList.push_back(key_pair.pub());
            m_secrets.push_back(key_pair.secret());
            m_consensus->appendMiner(key_pair.pub());
        }
    }
    std::shared_ptr<T> consensus() { return m_consensus; }

public:
    h512s m_minerList;
    std::vector<Secret> m_secrets;

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
        KeyPair const& _key_pair = KeyPair::create(), h512s const& _minerList = h512s())
      : PBFTSealer(_service, _txPool, _blockChain, _blockSync, _blockVerifier, _protocolId,
            _baseDir, _key_pair, _minerList)
    {
        m_pbftEngine = std::make_shared<FakePBFTEngine>(_service, _txPool, _blockChain, _blockSync,
            _blockVerifier, _protocolId, _minerList, _baseDir, _key_pair);
    }

    void loadTransactions(uint64_t const& transToFetch)
    {
        return PBFTSealer::loadTransactions(transToFetch);
    }
    virtual bool checkTxsEnough(uint64_t maxTxsCanSeal)
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
};
}  // namespace test
}  // namespace dev
