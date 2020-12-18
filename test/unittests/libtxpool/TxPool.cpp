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
 * @brief: unit test for transaction pool
 * @file: TxPool.cpp
 * @author: yujiechen
 * @date: 2018-09-25
 */
#include "FakeBlockChain.h"
#include <libdevcrypto/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::blockchain;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(TxPoolTest, TestOutputHelperFixture)
BOOST_AUTO_TEST_CASE(testSessionRead)
{
    TxPoolFixture pool_test(5, 5);
    BOOST_CHECK(!!pool_test.m_txPool);
    BOOST_CHECK(!!pool_test.m_topicService);
    BOOST_CHECK(!!pool_test.m_blockChain);
    ba::io_service m_ioservice(2);
    NodeIPEndpoint m_endpoint(boost::asio::ip::make_address("127.0.0.1"), 30303);
    // std::shared_ptr<FakeSocket> fake_socket = std::make_shared<FakeSocket>(m_ioservice,
    // m_endpoint);
    /// NodeID m_nodeId = KeyPair::create().pub();
    /// start peer session and doRead
    // BOOST_REQUIRE_NO_THROW(pool_test.m_host->startPeerSession(m_nodeId, fake_socket));
}

BOOST_AUTO_TEST_CASE(testImportAndSubmit)
{
    TxPoolFixture pool_test(5, 5);
    BOOST_CHECK(!!pool_test.m_txPool);
    BOOST_CHECK(!!pool_test.m_topicService);
    BOOST_CHECK(!!pool_test.m_blockChain);
    std::shared_ptr<bcos::ThreadPool> threadPool =
        std::make_shared<bcos::ThreadPool>("SessionCallBackThreadPool", 2);

    Transactions trans =
        *(pool_test.m_blockChain->getBlockByHash(pool_test.m_blockChain->numberHash(0))
                ->transactions());
    trans[0]->setBlockLimit(pool_test.m_blockChain->number() + u256(1));
    auto sig = crypto::Sign(pool_test.m_blockChain->m_keyPair, trans[0]->hash(WithoutSignature));
    trans[0]->updateSignature(sig);
    bytes trans_data;
    trans[0]->encode(trans_data);
    /// submit invalid transaction
    Transaction::Ptr tx = std::make_shared<Transaction>(trans_data, CheckTransaction::Everything);
    BOOST_CHECK_THROW(pool_test.m_txPool->submitTransactions(tx), TransactionRefused);
    Transactions transaction_vec =
        *(pool_test.m_blockChain->getBlockByHash(pool_test.m_blockChain->numberHash(0))
                ->transactions());
    /// set valid nonce
    size_t i = 0;
    for (auto tx : transaction_vec)
    {
        tx->setNonce(tx->nonce() + u256(i) + u256(100));
        tx->setBlockLimit(pool_test.m_blockChain->number() + u256(100));
        bytes trans_bytes2;
        sig = crypto::Sign(pool_test.m_blockChain->m_keyPair, tx->hash(WithoutSignature));
        // tx->encode(trans_bytes2);
        /// resignature
        tx->updateSignature(sig);
        tx->encode(trans_bytes2);
        auto result = pool_test.m_txPool->import(tx);
        BOOST_CHECK(result == ImportResult::Success);
        i++;
    }
    BOOST_CHECK(pool_test.m_txPool->pendingSize() == 5);
    /// test ordering of txPool
    Transactions pending_list = *(pool_test.m_txPool->pendingList());
    for (unsigned int i = 1; i < pool_test.m_txPool->pendingSize(); i++)
    {
        BOOST_CHECK(pending_list[i - 1]->importTime() <= pending_list[i]->importTime());
    }
    /// test out of limit, clear the queue
    tx->setNonce(tx->nonce() + utcTime() + u256(100));
    tx->setBlockLimit(pool_test.m_blockChain->number() + u256(1));
    sig = crypto::Sign(pool_test.m_blockChain->m_keyPair, tx->hash(WithoutSignature));
    tx->updateSignature(sig);
    tx->encode(trans_data);
    pool_test.m_txPool->setTxPoolLimit(5);
    auto result = pool_test.m_txPool->import(tx);
    BOOST_CHECK(pool_test.m_txPool->pendingSize() == 5);
    BOOST_CHECK(result == ImportResult::TransactionPoolIsFull);
    /// test status
    TxPoolStatus m_status = pool_test.m_txPool->status();
    BOOST_CHECK(m_status.current == 5);
    BOOST_CHECK(m_status.dropped == 0);
    /// test drop
    bool ret = pool_test.m_txPool->drop(pending_list[0]->hash());
    BOOST_CHECK(ret == true);
    BOOST_CHECK(pool_test.m_txPool->pendingSize() == 4);
    m_status = pool_test.m_txPool->status();
    BOOST_CHECK(m_status.current == 4);
    BOOST_CHECK(m_status.dropped == 1);

    /// test topTransactions
    Transactions top_transactions = *(pool_test.m_txPool->topTransactions(20));
    BOOST_CHECK(top_transactions.size() == pool_test.m_txPool->pendingSize());
    h256Hash avoid;
    for (size_t i = 0; i < pool_test.m_txPool->pendingList()->size(); i++)
        avoid.insert((*pool_test.m_txPool->pendingList())[i]->hash());
    top_transactions = *(pool_test.m_txPool->topTransactions(20, avoid));
    BOOST_CHECK(top_transactions.size() == 0);
    /// check getProtocol id
    BOOST_CHECK(pool_test.m_txPool->getProtocolId() ==
                getGroupProtoclID(1, bcos::protocol::ProtocolID::TxPool));
    BOOST_CHECK(pool_test.m_txPool->maxBlockLimit() == 1000);
    pool_test.m_txPool->setMaxBlockLimit(100);
    BOOST_CHECK(pool_test.m_txPool->maxBlockLimit() == 100);
}

BOOST_AUTO_TEST_CASE(BlockLimitCheck)
{
    TxPoolFixture pool_test(5, 5);
    /// test out of limit, clear the queue
    Transactions trans =
        *(pool_test.m_blockChain->getBlockByHash(pool_test.m_blockChain->numberHash(0))
                ->transactions());
    bytes trans_data;
    trans[0]->encode(trans_data);
    Transaction::Ptr tx = std::make_shared<Transaction>(trans_data, CheckTransaction::Everything);
    tx->setNonce(tx->nonce() + utcTime() + u256(100));
    tx->setBlockLimit(pool_test.m_blockChain->number() + u256(10000));
    auto sig = crypto::Sign(pool_test.m_blockChain->m_keyPair, tx->hash(WithoutSignature));
    tx->updateSignature(sig);
    tx->encode(trans_data);
    pool_test.m_txPool->setTxPoolLimit(5);
    ImportResult result = pool_test.m_txPool->import(tx);
    BOOST_CHECK(result == ImportResult::BlockLimitCheckFailed);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
