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
 * @brief : sync master test
 * @author: jimmyshi
 * @date: 2018-10-15
 */

#include <json/json.h>
#include <libethcore/Transaction.h>
#include <libsync/Common.h>
#include <libsync/SyncMaster.h>
#include <libsync/SyncMsgEngine.h>
#include <libsync/SyncStatus.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libblockverifier/FakeBlockVerifier.h>
#include <test/unittests/libtxpool/FakeBlockChain.h>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::sync;
using namespace dev::blockverifier;

namespace dev
{
namespace test
{
class FakeSyncMaster : public SyncMaster
{
public:
    FakeSyncMaster(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        PROTOCOL_ID const& _protocolId, NodeID const& _nodeId, h256 const& _genesisHash,
        unsigned const& _idleWaitMs = 200, int64_t const& _gossipInterval = 1000,
        int64_t const& _gossipPeers = 3, bool const& _enableSendTxsByTree = false,
        bool const& _enableSendBlockStatusByTree = false)
      : SyncMaster(_service, _txPool, _blockChain, _blockVerifier, _protocolId, _nodeId,
            _genesisHash, _idleWaitMs, _gossipInterval, _gossipPeers, _enableSendTxsByTree,
            _enableSendBlockStatusByTree)
    {}

    /// start blockSync
    void start() override
    {
        auto& state = workerState();
        state = WorkerState::Started;
    }
};

class SyncFixture : public TestOutputHelperFixture
{
public:
    const PROTOCOL_ID c_protocolId = 66;
    const bytes c_txBytes = fromHex(
        "f8aa8401be1a7d80830f4240941dc8def0867ea7e3626e03acee3eb40ee17251c880b84494e78a100000000000"
        "000000000000003ca576d469d7aa0244071d27eb33c5629753593e000000000000000000000000000000000000"
        "00000000000000000000000013881ba0f44a5ce4a1d1d6c2e4385a7985cdf804cb10a7fb892e9c08ff6d62657c"
        "4da01ea01d4c2af5ce505f574a320563ea9ea55003903ca5d22140155b3c2c968df0509464");
    const u256 c_maxBlockLimit = u256(1000);

public:
    SyncFixture()
    {
        m_sec = KeyPair::create().secret();
        m_genesisHash = h256(0);
    }

    struct FakeSyncToolsSet
    {
        std::shared_ptr<FakeSyncMaster> sync;
        std::shared_ptr<FakeService> service;
        std::shared_ptr<TxPoolInterface> txPool;
        std::shared_ptr<BlockChainInterface> blockChain;
        std::shared_ptr<BlockVerifierInterface> verifier;
        std::shared_ptr<DownloadingTxsQueue> txQueue;
    };

    FakeSyncToolsSet fakeSyncToolsSet(uint64_t _blockNum, size_t const& _transSize,
        NodeID const& _nodeId, Secret const& sec = KeyPair::create().secret())
    {
        TxPoolFixture txpool_creator(_blockNum, _transSize, sec);
        m_genesisHash = txpool_creator.m_blockChain->getBlockByNumber(0)->headerHash();

        std::shared_ptr<BlockVerifierInterface> blockVerifier =
            std::make_shared<FakeBlockverifier>();
        std::shared_ptr<FakeSyncMaster> fakeSyncMaster =
            std::make_shared<FakeSyncMaster>(txpool_creator.m_topicService, txpool_creator.m_txPool,
                txpool_creator.m_blockChain, blockVerifier, c_protocolId, _nodeId, m_genesisHash);
        fakeSyncMaster->registerConsensusVerifyHandler([](dev::eth::Block const&) { return true; });
        std::shared_ptr<DownloadingTxsQueue> txQueue =
            std::make_shared<DownloadingTxsQueue>(c_protocolId, _nodeId);
        return FakeSyncToolsSet{fakeSyncMaster, txpool_creator.m_topicService,
            txpool_creator.m_txPool, txpool_creator.m_blockChain, blockVerifier, txQueue};
    }

public:
    Secret m_sec;
    h256 m_genesisHash;
};

BOOST_FIXTURE_TEST_SUITE(SyncMasterTest, SyncFixture)

BOOST_AUTO_TEST_CASE(syncInfoFormatTest)
{
    std::shared_ptr<SyncMaster> sync = fakeSyncToolsSet(5, 5, NodeID(100)).sync;
    sync->syncStatus()->newSyncPeerStatus(
        SyncPeerInfo{NodeID(101), 0, m_genesisHash, m_genesisHash});
    sync->syncStatus()->newSyncPeerStatus(
        SyncPeerInfo{NodeID(102), 0, m_genesisHash, m_genesisHash});

    std::string info = sync->syncInfo();

    Json::Reader reader;
    Json::Value syncInfo;
    BOOST_CHECK(reader.parse(info, syncInfo));  // return string can parse to json obj
}

BOOST_AUTO_TEST_CASE(ConstructorTest)
{
    std::shared_ptr<SyncMaster> sync = fakeSyncToolsSet(5, 5, NodeID(100)).sync;
    BOOST_CHECK_EQUAL(sync->protocolId(), c_protocolId);
    BOOST_CHECK_EQUAL(sync->nodeId(), NodeID(100));
    BOOST_CHECK_EQUAL(sync->genesisHash(), m_genesisHash);
}

BOOST_AUTO_TEST_CASE(MaintainTransactionsTest)
{
    int64_t currentBlockNumber = 4;
    FakeSyncToolsSet syncTools = fakeSyncToolsSet(currentBlockNumber + 1, 5, NodeID(100));
    std::shared_ptr<SyncMaster> sync = syncTools.sync;
    std::shared_ptr<FakeService> service = syncTools.service;
    std::shared_ptr<TxPoolInterface> txPool = syncTools.txPool;

    sync->syncStatus()->newSyncPeerStatus(
        SyncPeerInfo{NodeID(101), 0, m_genesisHash, m_genesisHash});
    sync->syncStatus()->newSyncPeerStatus(
        SyncPeerInfo{NodeID(102), 0, m_genesisHash, m_genesisHash});

    std::shared_ptr<FakeBlock> fakedBlock = std::make_shared<FakeBlock>();
    shared_ptr<Transactions> txs = fakedBlock->fakeTransactions(2, currentBlockNumber);
    for (auto& tx : *txs)
        txPool->submitTransactions(tx);

    sync->maintainTransactions();
    cout << "Msg number: " << service->getAsyncSendSizeByNodeID(NodeID(101)) << endl;
    cout << "Msg number: " << service->getAsyncSendSizeByNodeID(NodeID(102)) << endl;

    // no sealer packet number is 0
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(101)), 0);
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(102)), 0);

    // Set sealer
    sync->syncStatus()->foreachPeer([&](shared_ptr<SyncPeerStatus> _p) {
        _p->isSealer = true;
        return true;
    });

    txs = fakedBlock->fakeTransactions(2, currentBlockNumber);
    for (auto& tx : *txs)
        txPool->submitTransactions(tx);

    sync->maintainTransactions();
    cout << "Msg number: " << service->getAsyncSendSizeByNodeID(NodeID(101)) << endl;
    cout << "Msg number: " << service->getAsyncSendSizeByNodeID(NodeID(102)) << endl;

    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(101)), 1);
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(102)), 1);

    // Message::Ptr msg = service->getAsyncSendMessageByNodeID(NodeID(101));

    // test transaction has send logic
    txs = fakedBlock->fakeTransactions(2, currentBlockNumber);
    for (auto& tx : *txs)
        txPool->submitTransactions(tx);
    sync->maintainTransactions();
    cout << "Msg number: " << service->getAsyncSendSizeByNodeID(NodeID(101)) << endl;
    cout << "Msg number: " << service->getAsyncSendSizeByNodeID(NodeID(102)) << endl;

    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(101)), 2);
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(102)), 2);

    // test transaction known by peer logic
    txs = fakedBlock->fakeTransactions(1, currentBlockNumber);
    for (auto tx : *txs)
    {
        txPool->submitTransactions(tx);
        txPool->setTransactionIsKnownBy(tx->sha3(), NodeID(101));
    }
    sync->maintainTransactions();
    cout << "Msg number: " << service->getAsyncSendSizeByNodeID(NodeID(101)) << endl;
    cout << "Msg number: " << service->getAsyncSendSizeByNodeID(NodeID(102)) << endl;

    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(101)), 2);
    // the transaction won't be sent to other nodes if received from P2P
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(102)), 3);

    // test transaction already sent
    sync->maintainTransactions();
    cout << "Msg number: " << service->getAsyncSendSizeByNodeID(NodeID(101)) << endl;
    cout << "Msg number: " << service->getAsyncSendSizeByNodeID(NodeID(102)) << endl;

    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(101)), 2);
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(102)), 3);
}

BOOST_AUTO_TEST_CASE(MaintainBlocksTest)
{
    int64_t currentBlockNumber = 4;
    FakeSyncToolsSet syncTools = fakeSyncToolsSet(currentBlockNumber + 1, 5, NodeID(100));
    std::shared_ptr<SyncMaster> sync = syncTools.sync;
    std::shared_ptr<FakeService> service = syncTools.service;

    sync->syncStatus()->newSyncPeerStatus(
        SyncPeerInfo{NodeID(101), 0, m_genesisHash, m_genesisHash});
    sync->syncStatus()->newSyncPeerStatus(
        SyncPeerInfo{NodeID(102), 0, m_genesisHash, m_genesisHash});

    sync->maintainBlocks();
    cout << "Msg number: " << service->getAsyncSendSizeByNodeID(NodeID(101)) << endl;

    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(101)), 1);
}

BOOST_AUTO_TEST_CASE(MaintainPeersStatusTest)
{
    int64_t currentBlockNumber = 4;
    FakeSyncToolsSet syncTools = fakeSyncToolsSet(currentBlockNumber + 1, 5, NodeID(100));
    std::shared_ptr<SyncMaster> sync = syncTools.sync;
    std::shared_ptr<FakeService> service = syncTools.service;

    // Can recieve 5 req
    sync->syncStatus()->newSyncPeerStatus(SyncPeerInfo{
        NodeID(101), c_maxRequestBlocks * 5 + currentBlockNumber, m_genesisHash, m_genesisHash});
    // Can recieve 1 req
    sync->syncStatus()->newSyncPeerStatus(SyncPeerInfo{
        NodeID(102), c_maxRequestBlocks + currentBlockNumber, m_genesisHash, m_genesisHash});
    // Can recieve 10(at least 5)req
    sync->syncStatus()->newSyncPeerStatus(SyncPeerInfo{
        NodeID(103), c_maxRequestBlocks * 10 + currentBlockNumber, m_genesisHash, m_genesisHash});

    sync->maintainPeersStatus();
    cout << "Msg number: " << service->getAsyncSendSizeByNodeID(NodeID(103)) << endl;

    size_t reqPacketSum = service->getAsyncSendSizeByNodeID(NodeID(101)) +
                          service->getAsyncSendSizeByNodeID(NodeID(102)) +
                          service->getAsyncSendSizeByNodeID(NodeID(103));
    BOOST_CHECK_EQUAL(reqPacketSum, c_maxRequestShards);

    // Reset peers test
    sync->syncStatus()->foreachPeer([&](shared_ptr<SyncPeerStatus> _p) {
        _p->number = currentBlockNumber - 1;  // set peers number smaller than myself
        return true;
    });
    sync->maintainPeersStatus();
    reqPacketSum = service->getAsyncSendSizeByNodeID(NodeID(101)) +
                   service->getAsyncSendSizeByNodeID(NodeID(102)) +
                   service->getAsyncSendSizeByNodeID(NodeID(103));
    BOOST_CHECK_EQUAL(reqPacketSum, c_maxRequestShards);  //
}

BOOST_AUTO_TEST_CASE(MaintainDownloadingQueueTest)
{
    int64_t currentBlockNumber = 0;
    int64_t latestNumber = 6;
    Secret sec = dev::KeyPair::create().secret();
    FakeSyncToolsSet syncTools = fakeSyncToolsSet(currentBlockNumber + 1, 5, NodeID(100), sec);
    std::shared_ptr<SyncMaster> sync = syncTools.sync;
    std::shared_ptr<SyncMasterStatus> status = sync->syncStatus();
    std::shared_ptr<BlockChainInterface> blockChain = syncTools.blockChain;

    FakeBlockChain latestBlockChain(latestNumber + 1, 5, sec);
    status->knownHighestNumber = latestNumber;
    status->knownLatestHash = latestBlockChain.getBlockByNumber(latestNumber)->headerHash();

    vector<shared_ptr<Block>> blocks;

    cout << " latestNumber: 6  curr:[0] -> downloadqueue:[1] -> curr:[0, 1]  not finish " << endl;
    cout << "Number: " << blockChain->number() << endl;
    shared_ptr<Block> b1 = latestBlockChain.getBlockByNumber(1);
    blocks.clear();
    blocks.emplace_back(b1);
    status->bq().push(blocks);
    status->bq().flushBufferToQueue();
    BOOST_CHECK_EQUAL(blockChain->number(), 0);
    sync->start();
    BOOST_CHECK_EQUAL(sync->maintainDownloadingQueue(), false);  // Not finish
    BOOST_CHECK_EQUAL(blockChain->number(), 1);

    cout << " latestNumber: 6  curr:[0, 1] -> downloadqueue:[3, 4] -> curr:[0, 1]  not finish"
         << endl;
    cout << "Number: " << blockChain->number() << endl;
    shared_ptr<Block> b3 = latestBlockChain.getBlockByNumber(3);
    shared_ptr<Block> b4 = latestBlockChain.getBlockByNumber(4);
    blocks.clear();
    blocks.emplace_back(b3);
    blocks.emplace_back(b4);
    status->bq().push(blocks);
    status->bq().flushBufferToQueue();
    BOOST_CHECK_EQUAL(blockChain->number(), 1);
    BOOST_CHECK_EQUAL(sync->maintainDownloadingQueue(), false);  // Not finish
    BOOST_CHECK_EQUAL(blockChain->number(), 1);

    cout << " latestNumber: 6  curr:[0, 1] -> downloadqueue:[1, 3, 3, 4] -> curr:[0, 1]  not finish"
         << endl;
    cout << "Number: " << blockChain->number() << endl;
    b1 = latestBlockChain.getBlockByNumber(1);
    b3 = latestBlockChain.getBlockByNumber(3);
    blocks.clear();
    blocks.emplace_back(b1);
    blocks.emplace_back(b3);
    status->bq().push(blocks);
    status->bq().flushBufferToQueue();
    BOOST_CHECK_EQUAL(blockChain->number(), 1);
    BOOST_CHECK_EQUAL(sync->maintainDownloadingQueue(), false);  // Not finish
    BOOST_CHECK_EQUAL(blockChain->number(), 1);

    cout << " latestNumber: 6  curr:[0, 1] -> downloadqueue:[2, 3, 3, 4] -> curr:[0, 4]  not finish"
         << endl;
    cout << "Number: " << blockChain->number() << endl;
    shared_ptr<Block> b2 = latestBlockChain.getBlockByNumber(2);
    blocks.clear();
    blocks.emplace_back(b2);
    status->bq().push(blocks);
    status->bq().flushBufferToQueue();
    BOOST_CHECK_EQUAL(blockChain->number(), 1);
    BOOST_CHECK_EQUAL(sync->maintainDownloadingQueue(), false);  // Not finish
    BOOST_CHECK_EQUAL(blockChain->number(), 4);

    cout << " latestNumber: 6  curr:[0, 4] -> downloadqueue:[5, 6] -> curr:[0, 6]  finish" << endl;
    cout << "Number: " << blockChain->number() << endl;
    shared_ptr<Block> b5 = latestBlockChain.getBlockByNumber(5);
    shared_ptr<Block> b6 = latestBlockChain.getBlockByNumber(6);
    blocks.clear();
    blocks.emplace_back(b5);
    blocks.emplace_back(b6);
    status->bq().push(blocks);
    status->bq().flushBufferToQueue();
    BOOST_CHECK_EQUAL(blockChain->number(), 4);
    BOOST_CHECK_EQUAL(sync->maintainDownloadingQueue(), true);  // finish
    BOOST_CHECK_EQUAL(blockChain->number(), 6);
    cout << "Number: " << blockChain->number() << endl;

    // Check download hash and expected hash are the same
    for (int64_t i = 0; i <= latestNumber; ++i)
    {
        h256 const& downloadHash = blockChain->getBlockByNumber(i)->headerHash();
        h256 const& expectedHash = latestBlockChain.getBlockByNumber(i)->headerHash();

        cout << "Block: " << i << endl;
        cout << "Download hash: " << downloadHash << endl;
        cout << "Expected hash: " << expectedHash << endl;
        BOOST_CHECK_EQUAL(downloadHash, expectedHash);
    }
}

inline void maintainAllBlockRequest(std::shared_ptr<SyncMaster> _sync)
{
    for (size_t i = 0; i < 20; i++)  // Repeat 20 times is enough to reach all peers.
    {
        _sync->maintainBlockRequest();  // random select one peer to maintain
    }
}

BOOST_AUTO_TEST_CASE(maintainBlockRequestTest)
{
    int64_t currentBlockNumber = 4;
    FakeSyncToolsSet syncTools = fakeSyncToolsSet(currentBlockNumber + 1, 5, NodeID(100));
    std::shared_ptr<SyncMaster> sync = syncTools.sync;
    std::shared_ptr<FakeService> service = syncTools.service;

    sync->syncStatus()->newSyncPeerStatus(
        SyncPeerInfo{NodeID(101), 0, m_genesisHash, m_genesisHash});
    sync->syncStatus()->newSyncPeerStatus(
        SyncPeerInfo{NodeID(102), 0, m_genesisHash, m_genesisHash});

    maintainAllBlockRequest(sync);
    // No request, msg is 0
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(101)), 0);
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(102)), 0);

    // current number 4, node 101 req: 1,2,3,4  ok
    sync->syncStatus()->peerStatus(NodeID(101))->reqQueue.push(1, 4);
    maintainAllBlockRequest(sync);
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(101)), 1);
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(102)), 0);

    // current number 4, node 102 req: 5  not ok
    sync->syncStatus()->peerStatus(NodeID(102))->reqQueue.push(5, 1);
    maintainAllBlockRequest(sync);
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(101)), 1);
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(102)), 0);

    // current number 4, node 102 req: 2,3,4,5  ok:2,3,4  not ok 5
    sync->syncStatus()->peerStatus(NodeID(102))->reqQueue.push(2, 4);
    maintainAllBlockRequest(sync);
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(101)), 1);
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(102)), 1);

    // current number 4, node 102 req: 3,4  ok
    sync->syncStatus()->peerStatus(NodeID(102))->reqQueue.push(3, 2);
    maintainAllBlockRequest(sync);
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(101)), 1);
    BOOST_CHECK_EQUAL(service->getAsyncSendSizeByNodeID(NodeID(102)), 2);
}

BOOST_AUTO_TEST_CASE(DoWorkTest)
{
    int64_t currentBlockNumber = 0;
    FakeSyncToolsSet syncTools = fakeSyncToolsSet(currentBlockNumber + 1, 5, NodeID(100));
    std::shared_ptr<SyncMaster> sync = syncTools.sync;

    sync->noteNewTransactions();
    sync->noteNewBlocks();
    sync->doWork();

    sync->noteDownloadingBegin();
    BOOST_CHECK(sync->status().state == SyncState::Downloading);
    sync->doWork();
    // BOOST_CHECK(sync->status().state == SyncState::Idle);

    sync->noteDownloadingBegin();
    BOOST_CHECK(sync->status().state == SyncState::Downloading);
    sync->noteDownloadingFinish();
    BOOST_CHECK(sync->status().state == SyncState::Idle);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
