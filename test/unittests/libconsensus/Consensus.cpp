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
 * @brief: unit test for the base class of consensus module(libconsensus/Consensus.*)
 * @file: consensus.cpp
 * @author: yujiechen
 * @date: 2018-10-09
 */
#include "FakePBFTEngine.h"
#include <libconsensus/Sealer.h>
#include <libconsensus/pbft/PBFTSealer.h>
#include <libdevcrypto/CryptoInterface.h>
#include <libethcore/Protocol.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::txpool;
using namespace dev::blockchain;

namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(consensusTest, TestOutputHelperFixture)

class SealerFixture
{
public:
    SealerFixture()
    {
        m_sync = std::make_shared<FakeBlockSync>();
        m_blockVerifier = std::make_shared<FakeBlockverifier>();
        m_txpoolCreator = std::make_shared<TxPoolFixture>(5, 5);
        u256 value = u256(100);
        u256 gas = u256(100000000);
        u256 gasPrice = u256(0);
        Address dst = toAddress(KeyPair::create().pub());
        std::string str = "transaction";
        bytes data(str.begin(), str.end());
        auto keyPair = KeyPair::create();
        ///< Summit 10 transactions to txpool.
        const size_t txCnt = 10;
        for (size_t i = 0; i < txCnt; i++)
        {
            auto tx = std::make_shared<Transaction>(value, gasPrice, gas, dst, data);
            tx->setNonce(tx->nonce() + utcTime() + u256(i));
            tx->setBlockLimit(m_txpoolCreator->m_blockChain->number() + 2);
            auto sig = crypto::Sign(keyPair, tx->hash(WithoutSignature));
            tx->updateSignature(sig);
            m_txpoolCreator->m_txPool->submitTransactions(tx);
        }
        BOOST_CHECK(m_txpoolCreator->m_txPool->pendingSize() == txCnt);
        m_fakePBFT = std::make_shared<FakePBFTSealer>(m_txpoolCreator->m_topicService,
            m_txpoolCreator->m_txPool, m_txpoolCreator->m_blockChain, m_sync, m_blockVerifier, 10);
    }

    std::shared_ptr<FakePBFTSealer> fakePBFT() { return m_fakePBFT; }

    std::shared_ptr<FakeBlockSync> m_sync;
    std::shared_ptr<BlockVerifierInterface> m_blockVerifier;
    std::shared_ptr<TxPoolFixture> m_txpoolCreator;
    std::shared_ptr<FakePBFTSealer> m_fakePBFT;
};

BOOST_AUTO_TEST_CASE(testLoadTransactions)
{
    SealerFixture sealerFixture;
    std::shared_ptr<FakePBFTSealer> fake_pbft = sealerFixture.fakePBFT();
    ///< Load 4 transactions in txpool.
    fake_pbft->loadTransactions(4);
    ///< The following two checks ensure that the size of transactions is 4.
    fake_pbft->engine()->mutableTimeManager().m_lastConsensusTime = utcSteadyTime();
    BOOST_CHECK(fake_pbft->checkTxsEnough(4) == true);
    fake_pbft->engine()->mutableTimeManager().m_lastConsensusTime = utcSteadyTime();
    BOOST_CHECK(fake_pbft->checkTxsEnough(5) == false);
    ///< Load 10 transactions in txpool, critical magnitude.
    fake_pbft->engine()->mutableTimeManager().m_lastConsensusTime = utcSteadyTime();
    fake_pbft->loadTransactions(10);
    ///< The following two checks ensure that the size of transactions is 10.
    fake_pbft->engine()->mutableTimeManager().m_lastConsensusTime = utcSteadyTime();
    BOOST_CHECK(fake_pbft->checkTxsEnough(10) == true);
    fake_pbft->engine()->mutableTimeManager().m_lastConsensusTime = utcSteadyTime();
    /// the transaction pool is empty, stop sealing
    BOOST_CHECK(fake_pbft->checkTxsEnough(11) == false);
    ///< Load 12 transactions in txpool, actually only 10.
    fake_pbft->loadTransactions(12);
    ///< The following two checks ensure that the size of transactions is 10.
    fake_pbft->engine()->mutableTimeManager().m_lastConsensusTime = utcSteadyTime();
    BOOST_CHECK(fake_pbft->checkTxsEnough(10) == true);
    fake_pbft->engine()->mutableTimeManager().m_lastConsensusTime = utcSteadyTime();
    /// the transaction pool is empty, stop sealing
    BOOST_CHECK(fake_pbft->checkTxsEnough(11) == false);
}

/// test shouldSeal
BOOST_AUTO_TEST_CASE(testShouldSeal)
{
    SealerFixture sealerFixture;
    std::shared_ptr<FakePBFTSealer> fake_pbft = sealerFixture.fakePBFT();

    /// not start consensus, should not seal
    BOOST_CHECK(fake_pbft->shouldSeal() == false);
    fake_pbft->setStartConsensus(true);

    /// not the sealer, should not seal
    fake_pbft->engine()->setAccountType(NodeAccountType::ObserverAccount);
    BOOST_CHECK(fake_pbft->shouldSeal() == false);

    /// m_sealing has not been sealed yet
    fake_pbft->resetSealingBlock();
    fake_pbft->engine()->setAccountType(NodeAccountType::SealerAccount);
    BOOST_CHECK(fake_pbft->shouldSeal() == true);

    /// the block has been sealed, should not seal
    fake_pbft->setBlock();
    BOOST_CHECK(fake_pbft->shouldSeal() == false);
    fake_pbft->resetSealingBlock();
    BOOST_CHECK(fake_pbft->shouldSeal() == true);

    /// is block syncing, should not seal
    sealerFixture.m_sync->setSyncing(true);
    BOOST_CHECK(fake_pbft->shouldSeal() == false);

    /// stop block syncing, start seal
    sealerFixture.m_sync->setSyncing(false);
    BOOST_CHECK(fake_pbft->shouldSeal() == true);
}

/// test for reportNewBlock
BOOST_AUTO_TEST_CASE(testReportNewBlock)
{
    SealerFixture sealerFixture;
    std::shared_ptr<FakePBFTSealer> fake_pbft = sealerFixture.fakePBFT();
    fake_pbft->engine()->setNodeIdx(0);
    /// no block changed, should not trigger reportNewBlock
    uint64_t orgSealingHeight = fake_pbft->getSealingBlockNumber();
    BOOST_CHECK(fake_pbft->syncBlock() == false);
    fake_pbft->reportNewBlock();
    BOOST_CHECK(fake_pbft->syncBlock() == false);
    BOOST_CHECK(fake_pbft->getSealingBlockNumber() == orgSealingHeight);

    /// block changed, should trigger reportNewBlock
    fake_pbft->resetSealingBlock();
    fake_pbft->onBlockChanged();
    BOOST_CHECK(fake_pbft->syncBlock() == true);
    fake_pbft->reportNewBlock();
    /// must reset nodeIdx after reportNewBlock since it will reset the current nodeIdx to 65535
    fake_pbft->engine()->setNodeIdx(0);
    BOOST_CHECK(fake_pbft->syncBlock() == false);
    BOOST_CHECK(fake_pbft->getSealingBlockNumber() ==
                (uint64_t)(sealerFixture.m_txpoolCreator->m_blockChain->number() + 1));

    /// test for shouldResetSealing
    /// seal the block and callback reportNewBlock again, expected: the block has not been reseted
    /// for no new block generated
    /// 1. seal a block according to the highest block
    fake_pbft->loadTransactions(1);
    fake_pbft->setBlock();
    fake_pbft->reportNewBlock();
    fake_pbft->engine()->setNodeIdx(0);

    BOOST_CHECK(fake_pbft->sealing().block->isSealed() == true);
    /// 2. note new block
    fake_pbft->onBlockChanged();
    BOOST_CHECK(fake_pbft->syncBlock() == true);
    /// 3. call reportNewBlock again
    fake_pbft->reportNewBlock();
    fake_pbft->engine()->setNodeIdx(0);
    BOOST_CHECK(fake_pbft->syncBlock() == false);
    /// expected: the block has been reseted for it has been sealed
    BOOST_CHECK(fake_pbft->sealing().block->isSealed() == false);
    BOOST_CHECK(fake_pbft->sealing().block->transactions()->size() == 0);

    /// test resetBlock for the next leader
    /// 1. sealing for the next leader
    fake_pbft->resetSealingBlock(dev::h256Hash(), true);
    fake_pbft->loadTransactions(5);
    /// note new block
    fake_pbft->onBlockChanged();
    BOOST_CHECK(fake_pbft->syncBlock() == true);
    /// reportNewBlock
    fake_pbft->reportNewBlock();
    fake_pbft->engine()->setNodeIdx(0);
    /// expected: should not reset the sealing for the next leader
    BOOST_CHECK(fake_pbft->sealing().block->isSealed() == false);
    BOOST_CHECK(fake_pbft->sealing().block->transactions()->size() == 5);
}
/// test reachBlockIntervalTime
BOOST_AUTO_TEST_CASE(testReachBlockIntervalTime)
{
    SealerFixture sealerFixture;
    std::shared_ptr<FakePBFTSealer> fake_pbft = sealerFixture.fakePBFT();

    /// test reachBlockIntervalTime
    fake_pbft->engine()->mutableTimeManager().m_lastConsensusTime = 0;
    BOOST_CHECK(fake_pbft->reachBlockIntervalTime() == true);

    /// test reach the min block generation time, while transaction num is 0
    fake_pbft->engine()->mutableTimeManager().m_lastConsensusTime = utcSteadyTime() - 600;
    BOOST_CHECK(fake_pbft->reachBlockIntervalTime() == false);
    /// load one transaction
    fake_pbft->loadTransactions(1);
    BOOST_CHECK(fake_pbft->reachBlockIntervalTime() == true);

    /// test not reach the min block generation time
    fake_pbft->engine()->mutableTimeManager().m_lastConsensusTime = utcSteadyTime() - 100;
    BOOST_CHECK(fake_pbft->reachBlockIntervalTime() == false);
}

/// test doWork
BOOST_AUTO_TEST_CASE(testDoWork)
{
    SealerFixture sealerFixture;
    std::shared_ptr<FakePBFTSealer> fake_pbft = sealerFixture.fakePBFT();
    /// set this node to be the leader
    fake_pbft->engine()->setNodeIdx(fake_pbft->engine()->getLeader().second);
    fake_pbft->setStartConsensus(true);
    fake_pbft->engine()->mutableTimeManager().m_lastConsensusTime = utcSteadyTime() - 600;
    /// m_sealing has not been sealed yet
    fake_pbft->resetSealingBlock();
    fake_pbft->engine()->setAccountType(NodeAccountType::SealerAccount);
    fake_pbft->doWork(true);

    /// check txpool status
    BOOST_CHECK(sealerFixture.m_txpoolCreator->m_txPool->pendingSize() == 10);
    /// check m_sealing is sealed
    BOOST_CHECK(fake_pbft->sealing().block->isSealed() == true);
    /// check the blockNumber of the sealed block
    BOOST_CHECK(fake_pbft->sealing().block->blockHeader().number() ==
                (sealerFixture.m_txpoolCreator->m_blockChain->number() + 1));
    /// check the sealer
    BOOST_CHECK(
        fake_pbft->sealing().block->blockHeader().sealer() == fake_pbft->engine()->nodeIdx());
    /// check transaction size
    BOOST_CHECK(fake_pbft->sealing().block->transactions()->size() == 10);
    /// check parent hash
    uint64_t blockNumber = sealerFixture.m_txpoolCreator->m_blockChain->number();
    BOOST_CHECK(fake_pbft->sealing().block->blockHeader().parentHash() ==
                (sealerFixture.m_txpoolCreator->m_blockChain->numberHash(blockNumber)));
}

/// test updateConsensusNodeList
BOOST_AUTO_TEST_CASE(testUpdateConsensusNodeList)
{
    SealerFixture sealerFixture;
    std::shared_ptr<FakePBFTSealer> fake_pbft = sealerFixture.fakePBFT();
    /// set SealerList
    /// fake sealer list
    dev::h512s sealerList;
    size_t sealerSize = 5;
    for (size_t i = 0; i < sealerSize; i++)
    {
        sealerList.push_back(KeyPair::create().pub());
    }
    /// fake observer list
    dev::h512s observerList;
    size_t observerSize = 2;
    for (size_t i = 0; i < observerSize; i++)
    {
        observerList.push_back(KeyPair::create().pub());
    }
    std::sort(sealerList.begin(), sealerList.end());
    sealerFixture.m_txpoolCreator->m_blockChain->setSealerList(sealerList);
    sealerFixture.m_txpoolCreator->m_blockChain->setObserverList(observerList);
    fake_pbft->engine()->fakeUpdateConsensusNodeList();
    /// check sealerList
    BOOST_CHECK(fake_pbft->engine()->sealerList() == sealerList);
}

/// test start
BOOST_AUTO_TEST_CASE(testStart)
{
    SealerFixture sealerFixture;
    std::shared_ptr<FakePBFTSealer> fake_pbft = sealerFixture.fakePBFT();
    uint64_t blockNumber = sealerFixture.m_txpoolCreator->m_blockChain->number();
    fake_pbft->engine()->setHighest(
        sealerFixture.m_txpoolCreator->m_blockChain->getBlockByNumber(blockNumber - 1)
            ->blockHeader());

    fake_pbft->setStartConsensus(false);
    BOOST_CHECK(fake_pbft->getStartConsensus() == false);

    fake_pbft->start();
    BOOST_CHECK(fake_pbft->getStartConsensus() == true);
    BOOST_CHECK(fake_pbft->engine()->consensusBlockNumber() ==
                (sealerFixture.m_txpoolCreator->m_blockChain->number() + 1));
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
