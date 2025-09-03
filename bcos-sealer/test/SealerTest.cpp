#include "bcos-sealer/Sealer.h"
#include "bcos-crypto/bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

namespace bcos::test
{

struct MockTxPool : public txpool::TxPoolInterface
{
    bool trySynced = false;

    std::tuple<bcos::protocol::Block::Ptr, bcos::protocol::Block::Ptr> sealTxs(
        uint64_t _txsLimit) override
    {
        return {};
    }

    void tryToSyncTxsFromPeers() override { trySynced = true; }

    void start() override {}
    void stop() override {}

    void asyncMarkTxs(const bcos::crypto::HashList& _txsHash, bool _sealedFlag,
        bcos::protocol::BlockNumber _batchId, bcos::crypto::HashType const& _batchHash,
        std::function<void(Error::Ptr)> _onRecvResponse) override
    {}
    void asyncVerifyBlock(bcos::crypto::PublicPtr _generatedNodeID,
        protocol::Block::ConstPtr _block,
        std::function<void(Error::Ptr, bool)> _onVerifyFinished) override
    {}
    void asyncFillBlock(bcos::crypto::HashListPtr _txsHash,
        std::function<void(Error::Ptr, bcos::protocol::ConstTransactionsPtr)> _onBlockFilled)
        override
    {}
    void asyncNotifyBlockResult(bcos::protocol::BlockNumber _blockNumber,
        bcos::protocol::TransactionSubmitResultsPtr _txsResult,
        std::function<void(Error::Ptr)> _onNotifyFinished) override
    {}
    void asyncNotifyTxsSyncMessage(bcos::Error::Ptr _error, std::string const& _id,
        bcos::crypto::NodeIDPtr _nodeID, bytesConstRef _data,
        std::function<void(Error::Ptr _error)> _onRecv) override
    {}
    void notifyConsensusNodeList(bcos::consensus::ConsensusNodeList const& _consensusNodeList,
        std::function<void(Error::Ptr)> _onRecvResponse) override
    {}
    void notifyObserverNodeList(bcos::consensus::ConsensusNodeList const& _observerNodeList,
        std::function<void(Error::Ptr)> _onRecvResponse) override
    {}
    void asyncGetPendingTransactionSize(
        std::function<void(Error::Ptr, uint64_t)> _onGetTxsSize) override
    {}
    void asyncResetTxPool(std::function<void(Error::Ptr)> _onRecvResponse) override {}
    void notifyConnectedNodes(bcos::crypto::NodeIDSet const& _connectedNodes,
        std::function<void(Error::Ptr)> _onResponse) override
    {}
};

struct MockSealingManager : public sealer::SealingManager
{
    MockSealingManager(sealer::SealerConfig::Ptr config) : SealingManager(std::move(config)) {}
    FetchResult fetchTransactions() override { return FetchResult::NO_TRANSACTION; }
};

struct TestSealerFixture2
{
    TestSealerFixture2()
    {
        hashImpl = std::make_shared<crypto::Keccak256>();
        auto signatureImpl = std::make_shared<crypto::Secp256k1Crypto>();
        cryptoSuite = std::make_shared<crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);

        txpool = std::make_shared<MockTxPool>();

        auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, nullptr, nullptr, nullptr);
        sealerConfig = std::make_shared<sealer::SealerConfig>(blockFactory, txpool, nullptr);
        sealer = std::make_shared<sealer::Sealer>(sealerConfig);
    }

    crypto::Hash::Ptr hashImpl;
    crypto::Hash::Ptr smHashImpl;
    protocol::BlockFactory::Ptr blockFactory;
    crypto::CryptoSuite::Ptr cryptoSuite = nullptr;
    std::shared_ptr<MockTxPool> txpool;
    sealer::SealerConfig::Ptr sealerConfig;
    sealer::Sealer::Ptr sealer;
};

BOOST_FIXTURE_TEST_SUITE(TestSealer, TestSealerFixture2)

BOOST_AUTO_TEST_CASE(executeWorker)
{
    sealer->setSealingManager(std::make_shared<MockSealingManager>(nullptr));
    sealer->setFetchTimeout(1);
    sealer->executeWorker();
    BOOST_CHECK(!txpool->trySynced);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    sealer->executeWorker();
    BOOST_CHECK(txpool->trySynced);

    txpool->trySynced = false;
    sealer->executeWorker();
    BOOST_CHECK(!txpool->trySynced);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test