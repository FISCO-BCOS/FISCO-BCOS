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
 * (c) 2016-2019 fisco-dev contributors.
 */

/**
 * @brief : RotatingPBFTEngineTest
 * @author: yujiechen
 * @date: 2019-10-12
 */
#pragma once
#include <libconsensus/rotating_pbft/RotatingPBFTEngine.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libconsensus/FakePBFTEngine.h>
#include <test/unittests/libconsensus/PBFTEngine.h>
#include <test/unittests/libtxpool/FakeBlockChain.h>
#include <boost/test/unit_test.hpp>

using namespace dev::consensus;
using namespace dev::p2p;
using namespace dev::txpool;
using namespace dev::sync;
using namespace dev::blockchain;
using namespace dev::blockverifier;
using namespace dev;

namespace dev
{
namespace test
{
// override RotatingPBFTEngine to expose the protected interfaces
class FakeRotatingPBFTEngine : public dev::consensus::RotatingPBFTEngine
{
public:
    using Ptr = std::shared_ptr<FakeRotatingPBFTEngine>;
    FakeRotatingPBFTEngine(P2PInterface::Ptr _service, TxPoolInterface::Ptr _txPool,
        BlockChainInterface::Ptr _blockChain, SyncInterface::Ptr _blockSync,
        BlockVerifierInterface::Ptr _blockVerifier, PROTOCOL_ID const& _protocolId,
        h512s const& _sealerList = h512s(), KeyPair const& _keyPair = KeyPair::create())
      : RotatingPBFTEngine(_service, _txPool, _blockChain, _blockSync, _blockVerifier, _protocolId,
            _keyPair, _sealerList),
        m_fakedBlockChain(std::dynamic_pointer_cast<FakeBlockChain>(m_blockChain))
    {
        m_rotatingIntervalEnableNumber = 0;
        setLeaderFailed(false);
        BlockHeader highest = m_blockChain->getBlockByNumber(m_blockChain->number())->header();
        setHighest(highest);
        setMaxTTL(1);
        setEmptyBlockGenTime(1000);
        createPBFTMsgFactory();
        m_blockFactory = std::make_shared<dev::eth::BlockFactory>();
        m_reqCache = std::make_shared<PBFTReqCache>();
        m_broacastTargetsFilter = boost::bind(&RotatingPBFTEngine::getIndexBySealer, this, _1);
    }
    // override the virtual function
    std::pair<bool, IDXTYPE> getLeader() const override { return PBFTEngine::getLeader(); }

    void resetChosedConsensusNodes() override
    {
        return RotatingPBFTEngine::resetChosedConsensusNodes();
    }

    void chooseConsensusNodes() override { return RotatingPBFTEngine::chooseConsensusNodes(); }

    void updateConsensusInfo() override { return RotatingPBFTEngine::updateConsensusInfo(); }

    IDXTYPE minValidNodes() const override { return RotatingPBFTEngine::minValidNodes(); }
    bool updateEpochSize() override { return false; }
    bool updateRotatingInterval() override { return false; }

    int64_t epochSize() { return m_epochSize; }
    int64_t rotatingInterval() { return m_rotatingInterval; }
    std::set<dev::h512> chosedConsensusNodes() { return *m_chosedConsensusNodes; }
    IDXTYPE startNodeIdx() { return m_startNodeIdx; }
    int64_t rotatingRound() { return m_rotatingRound; }
    int64_t sealersNum() { return m_sealersNum; }
    // called when the node set up or the sealer list are updated
    void setSealerListUpdated(bool const& _sealerListUpdated)
    {
        m_sealerListUpdated = _sealerListUpdated;
    }

    std::shared_ptr<FakeBlockChain> fakedBlockChain() { return m_fakedBlockChain; }
    void setNodeIdx(IDXTYPE const& _idx) { m_idx = _idx; }
    IDXTYPE f() const { return m_f; }
    std::shared_ptr<P2PInterface> mutableService() { return m_service; }
    void updateConsensusNodeList() override {}

    void resetConfigWrapper() { return RotatingPBFTEngine::resetConfig(); }

    void setSealersNum(int64_t const& _sealersNum) { m_sealersNum = _sealersNum; }

    void clearStartIdx() { m_startNodeIdx = -1; }

    void onRecvPBFTMessage(dev::p2p::NetworkException _exception,
        std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message) override
    {
        return RotatingPBFTEngine::onRecvPBFTMessage(_exception, _session, _message);
    }
    void clearAllMsgCache() { m_broadCastCache->clearAll(); }
    std::shared_ptr<dev::sync::TreeTopology> treeRouter() { return m_treeRouter; }
    void wrapperOnRecvPBFTMessage(
        NetworkException exception, std::shared_ptr<P2PSession> session, P2PMessage::Ptr message)
    {
        RotatingPBFTEngine::onRecvPBFTMessage(exception, session, message);
    }

    KeyPair const& keyPair() const { return m_keyPair; }
    void setKeyPair(KeyPair const& _keyPair) { m_keyPair = _keyPair; }
    void setView(VIEWTYPE const& _view) { m_view = _view; }
    void setSealerList(dev::h512s const& sealerList) { m_sealerList = sealerList; }
    CheckResult isValidPrepare(PrepareReq const& req) const
    {
        std::ostringstream oss;
        return RotatingPBFTEngine::isValidPrepare(req, oss);
    }

    void setLeaderFailed(bool leaderFailed) { m_leaderFailed = leaderFailed; }

    PrepareReq::Ptr wrapperConstructPrepareReq(dev::eth::Block::Ptr _block)
    {
        return RotatingPBFTEngine::constructPrepareReq(_block);
    }

    void setHighest(BlockHeader const& header) { m_highestBlock = header; }
    std::shared_ptr<PBFTReqCache> reqCache() { return m_reqCache; }
    void setConsensusBlockNumber(int64_t const& number) { m_consensusBlockNumber = number; }
    std::shared_ptr<BlockChainInterface> blockChain() { return m_blockChain; }
    int64_t consensusBlockNumber() const { return m_consensusBlockNumber; }
    void resetBlock(Block& block)
    {
        block.resetCurrentBlock(m_blockChain->getBlockByNumber(m_blockChain->number())->header());
        block.header().setTimestamp(utcTime());
        block.header().setSealerList(sealerList());
        block.header().setSealer(u256(nodeIdx()));
        block.header().setLogBloom(LogBloom());
        block.header().setGasUsed(u256(0));
    }

    void setLocatedInConsensusNodes(bool _locatedInConsensusNodes)
    {
        m_locatedInConsensusNodes = _locatedInConsensusNodes;
    }

    dev::network::NodeID getSealerByIndex(size_t const& _index) const override
    {
        return PBFTEngine::getSealerByIndex(_index);
    }

    void setChosedConsensusNodes(std::shared_ptr<std::set<dev::h512>> _chosedConsensusNodes)
    {
        m_chosedConsensusNodes = _chosedConsensusNodes;
    }

    void wrapperSendRawPrepareStatusRandomly(PBFTMsg::Ptr _rawPrepareReq)
    {
        return RotatingPBFTEngine::sendRawPrepareStatusRandomly(_rawPrepareReq);
    }
    void wrapperHandleP2PMessage(dev::p2p::NetworkException _exception,
        std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message)
    {
        return RotatingPBFTEngine::handleP2PMessage(_exception, _session, _message);
    }
    unsigned prepareStatusBroadcastPercent() const { return m_prepareStatusBroadcastPercent; }
    RPBFTReqCache::Ptr rpbftReqCache() { return m_rpbftReqCache; }
    void wrapperOnReceiveRawPrepareStatus(
        std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message)
    {
        return RotatingPBFTEngine::onReceiveRawPrepareStatus(_session, _message);
    }

    void wrapperOnReceiveRawPrepareResponse(
        std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message)
    {
        return RotatingPBFTEngine::onReceiveRawPrepareResponse(_session, _message);
    }

    bool wrapperGetNodeIDByIndex(dev::network::NodeID& nodeId, const IDXTYPE& idx) const
    {
        return PBFTEngine::getNodeIDByIndex(nodeId, idx);
    }

private:
    std::shared_ptr<FakeBlockChain> m_fakedBlockChain;
};

// fixture to create the FakeRotatingPBFTEngine(with faked service, sync, txPool, etc.)
class RotatingPBFTEngineFixture
{
public:
    using Ptr = std::shared_ptr<RotatingPBFTEngineFixture>;
    // create the PBFTEngine with {_sealersNum} sealers
    explicit RotatingPBFTEngineFixture(size_t const& _sealersNum) : m_sealersNum(_sealersNum)
    {
        m_fakePBFT =
            std::make_shared<FakeConsensus<FakeRotatingPBFTEngine>>(m_sealersNum, ProtocolID::PBFT);
        m_rotatingPBFT = std::dynamic_pointer_cast<FakeRotatingPBFTEngine>(m_fakePBFT->consensus());
        m_rotatingPBFT->setMaxRequestPrepareWaitTime(10);
    }

    FakeRotatingPBFTEngine::Ptr rotatingPBFT() { return m_rotatingPBFT; }
    std::shared_ptr<FakeConsensus<FakeRotatingPBFTEngine>> fakePBFTSuite() { return m_fakePBFT; }
    // get sealersNum
    size_t sealersNum() { return m_sealersNum; }

private:
    FakeRotatingPBFTEngine::Ptr m_rotatingPBFT;
    std::shared_ptr<FakeConsensus<FakeRotatingPBFTEngine>> m_fakePBFT;
    size_t m_sealersNum = 4;
};
}  // namespace test
}  // namespace dev