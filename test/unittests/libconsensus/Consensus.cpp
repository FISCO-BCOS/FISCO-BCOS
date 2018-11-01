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
BOOST_AUTO_TEST_CASE(testLoadTransactions)
{
    std::shared_ptr<SyncInterface> sync = std::make_shared<FakeBlockSync>();
    std::shared_ptr<BlockVerifierInterface> blockVerifier = std::make_shared<FakeBlockverifier>();
    std::shared_ptr<TxPoolFixture> txpool_creator = std::make_shared<TxPoolFixture>(5, 5);

    u256 value = u256(100);
    u256 gas = u256(100000000);
    u256 gasPrice = u256(0);
    Address dst = toAddress(KeyPair::create().pub());
    std::string str = "transaction";
    bytes data(str.begin(), str.end());
    Transaction tx(value, gasPrice, gas, dst, data);
    Secret sec = KeyPair::create().secret();
    ///< Summit 10 transactions to txpool.
    const size_t txCnt = 10;
    for (size_t i = 0; i < txCnt; i++)
    {
        tx.setNonce(tx.nonce() + u256(i));
        tx.setBlockLimit(txpool_creator->m_blockChain->number() + 2);
        dev::Signature sig = sign(sec, tx.sha3(WithoutSignature));
        tx.updateSignature(SignatureStruct(sig));
        txpool_creator->m_txPool->submit(tx);
    }
    BOOST_CHECK(txpool_creator->m_txPool->pendingSize() == txCnt);

    /*FakeConsensus<FakePBFTEngine> fake_engine(
        1, ProtocolID::PBFT, sync, blockVerifier, txpool_creator);*/
    FakePBFTSealer fake_pbft(txpool_creator->m_topicService, txpool_creator->m_txPool,
        txpool_creator->m_blockChain, sync, blockVerifier, 10);


    ///< Load 4 transactions in txpool.
    fake_pbft.loadTransactions(4);
    ///< The following two checks ensure that the size of transactions is 4.
    fake_pbft.engine()->mutableTimeManager().m_lastConsensusTime = utcTime();
    BOOST_CHECK(fake_pbft.checkTxsEnough(4) == true);
    fake_pbft.engine()->mutableTimeManager().m_lastConsensusTime = utcTime();
    BOOST_CHECK(fake_pbft.checkTxsEnough(5) == false);
    ///< Load 10 transactions in txpool, critical magnitude.
    fake_pbft.engine()->mutableTimeManager().m_lastConsensusTime = utcTime();
    fake_pbft.loadTransactions(10);
    ///< The following two checks ensure that the size of transactions is 10.
    fake_pbft.engine()->mutableTimeManager().m_lastConsensusTime = utcTime();
    BOOST_CHECK(fake_pbft.checkTxsEnough(10) == true);
    fake_pbft.engine()->mutableTimeManager().m_lastConsensusTime = utcTime();
    BOOST_CHECK(fake_pbft.checkTxsEnough(11) == false);
    ///< Load 12 transactions in txpool, actually only 10.
    fake_pbft.loadTransactions(12);
    ///< The following two checks ensure that the size of transactions is 10.
    fake_pbft.engine()->mutableTimeManager().m_lastConsensusTime = utcTime();
    BOOST_CHECK(fake_pbft.checkTxsEnough(10) == true);
    fake_pbft.engine()->mutableTimeManager().m_lastConsensusTime = utcTime();
    BOOST_CHECK(fake_pbft.checkTxsEnough(11) == false);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
