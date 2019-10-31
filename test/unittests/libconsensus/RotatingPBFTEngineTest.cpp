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
#include <libconsensus/rotating_pbft/RotatingPBFTEngine.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libconsensus/FakePBFTEngine.h>
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
class FakeRotatingPBFTEngine : public RotatingPBFTEngine
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
    }
    // override the virtual function
    std::pair<bool, IDXTYPE> getLeader() const override { return RotatingPBFTEngine::getLeader(); }

    bool locatedInChosedConsensensusNodes() const
    {
        return RotatingPBFTEngine::locatedInChosedConsensensusNodes();
    }

    void resetChosedConsensusNodes() override
    {
        return RotatingPBFTEngine::resetChosedConsensusNodes();
    }

    void chooseConsensusNodes() override { return RotatingPBFTEngine::chooseConsensusNodes(); }

    void updateConsensusInfoForTreeRouter() override
    {
        return RotatingPBFTEngine::updateConsensusInfoForTreeRouter();
    }

    IDXTYPE minValidNodes() const override { return RotatingPBFTEngine::minValidNodes(); }
    bool updateGroupSize() override { return false; }
    bool updateRotatingInterval() override { return false; }

    int64_t groupSize() { return m_groupSize; }
    int64_t rotatingInterval() { return m_rotatingInterval; }
    std::set<dev::h512> chosedConsensusNodes() { return m_chosedConsensusNodes; }
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

    void resetConfig() { return RotatingPBFTEngine::resetConfig(); }

    void setSealersNum(int64_t const& _sealersNum) { m_sealersNum = _sealersNum; }

    void clearStartIdx() { m_startNodeIdx = -1; }

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
        m_fakePBFT = std::make_shared<FakeConsensus<FakeRotatingPBFTEngine> >(
            m_sealersNum, ProtocolID::PBFT);
        m_rotatingPBFT = std::dynamic_pointer_cast<FakeRotatingPBFTEngine>(m_fakePBFT->consensus());
    }

    FakeRotatingPBFTEngine::Ptr rotatingPBFT() { return m_rotatingPBFT; }

    // get sealersNum
    size_t sealersNum() { return m_sealersNum; }

private:
    FakeRotatingPBFTEngine::Ptr m_rotatingPBFT;
    std::shared_ptr<FakeConsensus<FakeRotatingPBFTEngine> > m_fakePBFT;
    size_t m_sealersNum = 4;
};

std::set<dev::h512> getExpectedSelectedNodes(
    RotatingPBFTEngineFixture::Ptr _fixture, int64_t const& _round, int64_t const& _groupSize)
{
    std::set<dev::h512> expectedSelectedNodes;
    auto currentSealerList = _fixture->rotatingPBFT()->sealerList();
    for (auto i = _round; i < (_round + _groupSize); i++)
    {
        expectedSelectedNodes.insert(currentSealerList[i % currentSealerList.size()]);
    }
    return expectedSelectedNodes;
}

void checkResetConfig(RotatingPBFTEngineFixture::Ptr _fixture, int64_t const& _round,
    int64_t const& _rotatingInterval, int64_t const& _groupSize, int64_t const& _sealersNum)
{
    int64_t startBlockNumber = _round * _rotatingInterval;
    auto expectedSelectedNodes = getExpectedSelectedNodes(_fixture, _round, _groupSize);
    // block number range: [startBlockNumber, startBlockNumber + _rotatingInterval - 1](the
    // {_round}th rotating round)
    int64_t groupSize = _groupSize;
    if (_groupSize > _sealersNum)
    {
        groupSize = _groupSize;
    }
    for (auto i = startBlockNumber; i < (startBlockNumber + _rotatingInterval); i++)
    {
        // update the blockNumber
        _fixture->rotatingPBFT()->fakedBlockChain()->setBlockNumber(i + 1);
        _fixture->rotatingPBFT()->resetConfig();
        BOOST_CHECK(_fixture->rotatingPBFT()->sealersNum() == _sealersNum);
        BOOST_CHECK(_fixture->rotatingPBFT()->startNodeIdx() == _round % _sealersNum);
        BOOST_CHECK(_fixture->rotatingPBFT()->rotatingRound() == _round);
        auto selectedConsensusNodes = _fixture->rotatingPBFT()->chosedConsensusNodes();
        BOOST_CHECK(selectedConsensusNodes.size() == (size_t)groupSize);
        BOOST_CHECK(selectedConsensusNodes == expectedSelectedNodes);
        _fixture->rotatingPBFT()->setSealerListUpdated(false);
    }
}

// UT for RotatingPBFTEngine
BOOST_FIXTURE_TEST_SUITE(RotatingPBFTEngineTest, TestOutputHelperFixture)

// case1: sealers are not updated
BOOST_AUTO_TEST_CASE(testConstantSealers)
{
    // case1: eight sealers with two groups(every group with four members), rotating interval is 10
    auto fixture = std::make_shared<RotatingPBFTEngineFixture>(8);
    fixture->rotatingPBFT()->setSealersNum(8);
    fixture->rotatingPBFT()->resetConfig();
    int64_t rotatingInterval = 10;
    for (auto groupSize = 4; groupSize < 10; groupSize++)
    {
        // the node set up: update m_sealerListUpdated
        fixture->rotatingPBFT()->clearStartIdx();
        fixture->rotatingPBFT()->setSealerListUpdated(true);

        fixture->rotatingPBFT()->setGroupSize(groupSize);
        fixture->rotatingPBFT()->setRotatingInterval(rotatingInterval);
        BOOST_CHECK(fixture->rotatingPBFT()->rotatingInterval() == rotatingInterval);
        auto tmpGroupSize = groupSize;
        if (groupSize <= 8)
        {
            BOOST_CHECK(fixture->rotatingPBFT()->groupSize() == groupSize);
        }
        else
        {
            tmpGroupSize = 8;
            BOOST_CHECK(fixture->rotatingPBFT()->groupSize() == 8);
        }

        auto currentSealerList = fixture->rotatingPBFT()->sealerList();
        auto sealersNum = currentSealerList.size();
        // check [10, 29] round
        for (auto round = 10; round < 30; round++)
        {
            checkResetConfig(fixture, round, rotatingInterval, tmpGroupSize, sealersNum);
        }
        // check falut-tolerance
        BOOST_CHECK(fixture->rotatingPBFT()->f() == (tmpGroupSize - 1) / 3);
        // check minValidNodes
        BOOST_CHECK(fixture->rotatingPBFT()->minValidNodes() ==
                    (tmpGroupSize - fixture->rotatingPBFT()->f()));
    }
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev