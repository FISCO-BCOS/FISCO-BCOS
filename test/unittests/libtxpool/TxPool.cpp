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
#include "FakeBlockManager.h"
#include <libdevcrypto/Common.h>
#include <libtxpool/TxPool.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libp2p/FakeHost.h>
#include <boost/test/unit_test.hpp>
using namespace dev;
using namespace dev::txpool;
using namespace dev::blockmanager;
namespace dev
{
namespace test
{
class TxPoolFixture : public TestOutputHelperFixture
{
public:
    TxPoolFixture() {}
    void FakeTxPoolFunc(uint64_t _blockNum, size_t const& transSize = 5)
    {
        /// fake block manager
        m_blockManager = std::make_shared<FakeBlockManager>(_blockNum, transSize);
        m_nonce = std::make_shared<NonceCheck>(m_blockManager);
        m_nonce->init();
        auto fake_blockManager = dynamic_cast<FakeBlockManager*>(m_blockManager.get());
        if (fake_blockManager)
            fake_blockManager->setNonceCheck(m_nonce);
        /// fake host of p2p module
        FakeHost* hostPtr = createFakeHostWithSession(clientVersion, listenIp, listenPort);
        m_host = std::shared_ptr<Host>(hostPtr);
        /// create p2pHandler
        m_p2pHandler = std::make_shared<P2PMsgHandler>();
        /// fake service of p2p module
        m_topicService = std::make_shared<Service>(m_host, m_p2pHandler);
        std::shared_ptr<BlockManagerInterface> blockManager =
            std::shared_ptr<BlockManagerInterface>(m_blockManager);
        /// fake txpool
        m_txPool = std::make_shared<FakeTxPool>(m_topicService, blockManager);
    }

    void setSessionData(std::string const& data_content)
    {
        for (auto session : m_host->sessions())
            dynamic_cast<FakeSessionForTest*>(session.second.get())->setDataContent(data_content);
    }
    std::shared_ptr<FakeTxPool> m_txPool;
    std::shared_ptr<Service> m_topicService;
    std::shared_ptr<P2PMsgHandler> m_p2pHandler;
    std::shared_ptr<FakeBlockManager> m_blockManager;
    std::shared_ptr<Host> m_host;
    std::shared_ptr<NonceCheck> m_nonce;
    std::string clientVersion = "2.0";
    std::string listenIp = "127.0.0.1";
    uint16_t listenPort = 30304;
};

BOOST_FIXTURE_TEST_SUITE(TxPoolTest, TxPoolFixture)
BOOST_AUTO_TEST_CASE(testSessionRead)
{
    FakeTxPoolFunc(5, 5);
    BOOST_CHECK(!!m_txPool);
    BOOST_CHECK(!!m_topicService);
    BOOST_CHECK(!!m_blockManager);
    ba::io_service m_ioservice(2);
    NodeIPEndpoint m_endpoint(bi::address::from_string("127.0.0.1"), 30303, 30303);
    std::shared_ptr<FakeSocket> fake_socket = std::make_shared<FakeSocket>(m_ioservice, m_endpoint);
    NodeID m_nodeId = KeyPair::create().pub();
    /// start peer session and doRead
    BOOST_CHECK_THROW(m_host->startPeerSession(m_nodeId, fake_socket), std::exception);
}
BOOST_AUTO_TEST_CASE(testImportAndSubmit)
{
    FakeTxPoolFunc(5, 5);
    BOOST_CHECK(!!m_txPool);
    BOOST_CHECK(!!m_topicService);
    BOOST_CHECK(!!m_blockManager);
    bytes trans_data =
        m_blockManager->transactions(m_blockManager->m_blockChain[0].headerHash())[0];
    /// import invalid transaction
    ImportResult result = m_txPool->import(ref(trans_data));
    BOOST_CHECK(result == ImportResult::NonceCheckFail);
    /// submit invalid transaction
    Transaction tx(trans_data, CheckTransaction::Everything);
    std::pair<h256, Address> ret_result = m_txPool->submit(tx);
    BOOST_CHECK(ret_result == std::make_pair(tx.sha3(), Address(1)));
    std::vector<bytes> transaction_vec =
        m_blockManager->transactions(m_blockManager->m_blockChain[0].headerHash());
    Secret sec = KeyPair::create().secret();
    /// set valid nonce
    for (size_t i = 0; i < transaction_vec.size(); i++)
    {
        bytes trans_bytes = transaction_vec[i];
        Transaction tx(trans_bytes, CheckTransaction::Everything);
        tx.setNonce(u256(tx.nonce() + i + 1));
        bytes trans_bytes2;
        tx.encode(trans_bytes2);
        BOOST_CHECK_THROW(m_txPool->import(ref(trans_bytes2)), InvalidSignature);
        /// resignature
        Signature sig = sign(sec, tx.sha3(WithoutSignature));
        tx.updateSignature(SignatureStruct(sig));
        tx.encode(trans_bytes2);
        result = m_txPool->import(ref(trans_bytes2));
        BOOST_CHECK(result == ImportResult::Success);
    }
    BOOST_CHECK(m_txPool->pendingSize() == 5);
    /// test ordering of txPool
    Transactions pending_list = m_txPool->pendingList();
    for (unsigned int i = 1; i < m_txPool->pendingSize(); i++)
    {
        BOOST_CHECK(pending_list[i - 1].importTime() <= pending_list[i].importTime());
    }
    /// test out of limit, clear the queue
    tx.setNonce(u256(tx.nonce() + 10));
    Signature sig = sign(sec, tx.sha3(WithoutSignature));
    tx.updateSignature(SignatureStruct(sig));
    tx.encode(trans_data);
    m_txPool->setTxPoolLimit(2);
    m_txPool->import(ref(trans_data));
    BOOST_CHECK(m_txPool->pendingSize() == 2);
    /// test status
    TxPoolStatus m_status = m_txPool->status();
    BOOST_CHECK(m_status.current == 2);
    BOOST_CHECK(m_status.dropped == 0);
    /// test drop
    bool ret = m_txPool->drop(pending_list[4].sha3());
    BOOST_CHECK(ret == false);
    ret = m_txPool->drop(pending_list[0].sha3());
    BOOST_CHECK(ret == true);
    BOOST_CHECK(m_txPool->pendingSize() == 1);
    m_status = m_txPool->status();
    BOOST_CHECK(m_status.current == 1);
    BOOST_CHECK(m_status.dropped == 1);

    /// test topTransactions
    Transactions top_transactions = m_txPool->topTransactions(20);
    BOOST_CHECK(top_transactions.size() == m_txPool->pendingSize());
    h256Hash avoid;
    avoid.insert(pending_list[1].sha3());
    top_transactions = m_txPool->topTransactions(20, avoid);
    BOOST_CHECK(top_transactions.size() == 0);
    /// check getProtocol id
    BOOST_CHECK(m_txPool->getProtocolId() == dev::eth::ProtocolID::TxPool);
    BOOST_CHECK(m_txPool->maxBlockLimit() == u256(1000));
    m_txPool->setMaxBlockLimit(100);
    BOOST_CHECK(m_txPool->maxBlockLimit() == u256(100));
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
