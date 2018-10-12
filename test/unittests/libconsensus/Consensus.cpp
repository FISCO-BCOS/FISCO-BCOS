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
#include "FakeConsensus.h"
#include <libconsensus/Consensus.h>
#include <libconsensus/pbft/PBFTConsensus.h>
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
/// test constructor (invalid ProtocolID will cause exception)
BOOST_AUTO_TEST_CASE(testConstructor)
{
    BOOST_CHECK_THROW(FakeConsensus<Consensus>(1, 0), InvalidProtocolID);
    BOOST_REQUIRE_NO_THROW(FakeConsensus<Consensus>(1, ProtocolID::PBFT));
}

BOOST_AUTO_TEST_CASE(testLoadTransactions)
{
    std::shared_ptr<SyncInterface> sync = std::make_shared<FakeBlockSync>();
    std::shared_ptr<BlockVerifierInterface> blockVerifier = std::make_shared<FakeBlockverifier>();
    std::shared_ptr<TxPoolFixture> txpool_creator = std::make_shared<TxPoolFixture>(5, 5);

    bytes rlpBytes = fromHex(
        "f8aa8401be1a7d80830f4240941dc8def0867ea7e3626e03acee3eb40ee17251c880b84494e78a100000000000"
        "000000000000003ca576d469d7aa0244071d27eb33c5629753593e000000000000000000000000000000000000"
        "00000000000000000000000013881ba0f44a5ce4a1d1d6c2e4385a7985cdf804cb10a7fb892e9c08ff6d62657c"
        "4da01ea01d4c2af5ce505f574a320563ea9ea55003903ca5d22140155b3c2c968df0509464");
    Transaction tx(ref(rlpBytes), CheckTransaction::Everything);
    Secret sec = KeyPair::create().secret();

    ///< Summit 10 transactions to txpool.
    const size_t txCnt = 10;
    for (size_t i = 0; i < txCnt; i++)
    {
        tx.setNonce(tx.nonce() + i);
        dev::Signature sig = sign(sec, tx.sha3(WithoutSignature));
        tx.updateSignature(SignatureStruct(sig));
        txpool_creator->m_txPool->submit(tx);
    }
    BOOST_CHECK(txpool_creator->m_txPool->pendingSize() == txCnt);

    FakeConsensus<FakePBFTConsensus> fake_pbft(
        1, ProtocolID::PBFT, sync, blockVerifier, txpool_creator);
    ///< Load 4 transactions in txpool.
    fake_pbft.consensus()->loadTransactions(4);
    ///< The following two checks ensure that the size of transactions is 4.
    BOOST_CHECK(fake_pbft.consensus()->checkTxsEnough(4) == true);
    BOOST_CHECK(fake_pbft.consensus()->checkTxsEnough(5) == false);
    ///< Load 10 transactions in txpool, critical magnitude.
    fake_pbft.consensus()->loadTransactions(10);
    ///< The following two checks ensure that the size of transactions is 10.
    BOOST_CHECK(fake_pbft.consensus()->checkTxsEnough(10) == true);
    BOOST_CHECK(fake_pbft.consensus()->checkTxsEnough(11) == false);
    ///< Load 12 transactions in txpool, actually only 10.
    fake_pbft.consensus()->loadTransactions(12);
    ///< The following two checks ensure that the size of transactions is 10.
    BOOST_CHECK(fake_pbft.consensus()->checkTxsEnough(10) == true);
    BOOST_CHECK(fake_pbft.consensus()->checkTxsEnough(11) == false);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
