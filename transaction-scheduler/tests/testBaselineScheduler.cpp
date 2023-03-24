#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-scheduler/test/mock/MockLedger.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-protocol/TransactionSubmitResultFactoryImpl.h>
#include <bcos-transaction-scheduler/BaselineScheduler.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace bcos::transaction_scheduler;

struct MockScheduler
{
    void start() {}
    task::Task<std::vector<std::shared_ptr<bcostars::protocol::TransactionReceiptImpl>>> execute(
        auto&& blockHeader, auto&& transactions)
    {
        auto receipts =
            RANGES::iota_view<size_t, size_t>(0, RANGES::size(transactions)) |
            RANGES::views::transform([](size_t index) {
                auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
                    [inner = bcostars::TransactionReceipt()]() mutable {
                        return std::addressof(inner);
                    });
                constexpr static std::string_view str = "abc";
                receipt->mutableInner().dataHash.assign(str.begin(), str.end());
                return receipt;
            }) |
            RANGES::to<std::vector<std::shared_ptr<bcostars::protocol::TransactionReceiptImpl>>>();

        co_return receipts;
    }
    task::Task<bcos::h256> finish(auto&& block, auto&& hashImpl) { co_return bcos::h256{}; }
    task::Task<void> commit() { co_return; }

    task::Task<std::shared_ptr<bcostars::protocol::TransactionReceiptImpl>> call(
        auto&& block, auto&& transaction)
    {
        co_return nullptr;
    }
};

struct MockLedger
{
    template <class... Args>
    task::Task<void> setBlock(auto&& block)
    {
        co_return;
    }

    task::Task<bcos::concepts::ledger::Status> getStatus()
    {
        co_return bcos::concepts::ledger::Status{};
    }

    task::Task<ledger::LedgerConfig> getConfig()
    {
        auto ledgerConfig = ledger::LedgerConfig();
        co_return ledgerConfig;
    }
};

struct MockTxPool : public txpool::TxPoolInterface
{
    void start() override {}
    void stop() override {}
    void asyncSealTxs(uint64_t _txsLimit, bcos::txpool::TxsHashSetPtr _avoidTxs,
        std::function<void(Error::Ptr, bcos::protocol::Block::Ptr, bcos::protocol::Block::Ptr)>
            _sealCallback) override
    {}
    void asyncMarkTxs(bcos::crypto::HashListPtr _txsHash, bool _sealedFlag,
        bcos::protocol::BlockNumber _batchId, bcos::crypto::HashType const& _batchHash,
        std::function<void(Error::Ptr)> _onRecvResponse) override
    {}
    void asyncVerifyBlock(bcos::crypto::PublicPtr _generatedNodeID, bytesConstRef const& _block,
        std::function<void(Error::Ptr, bool)> _onVerifyFinished) override
    {}
    void asyncFillBlock(bcos::crypto::HashListPtr _txsHash,
        std::function<void(Error::Ptr, bcos::protocol::TransactionsPtr)> _onBlockFilled) override
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

    task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        RANGES::any_view<bcos::h256, RANGES::category::mask | RANGES::category::sized> hashes)
        override
    {
        co_return std::vector<protocol::Transaction::ConstPtr>{};
    }
};

class TestBaselineSchedulerFixture
{
public:
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>;

    TestBaselineSchedulerFixture()
      : cryptoSuite(std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr)),
        blockHeaderFactory(
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite)),
        transactionFactory(
            std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite)),
        receiptFactory(
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite)),
        blockFactory(std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory)),
        transactionSubmitResultFactory(
            std::make_shared<protocol::TransactionSubmitResultFactoryImpl>()),
        baselineScheduler(mockScheduler, *blockHeaderFactory, mockLedger, mockTxPool,
            *transactionSubmitResultFactory, *hashImpl)
    {}

    TableNamePool tableNamePool;
    BackendStorage backendStorage;
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> blockFactory;
    std::shared_ptr<protocol::TransactionSubmitResultFactoryImpl> transactionSubmitResultFactory;

    crypto::Hash::Ptr hashImpl = std::make_shared<bcos::crypto::Keccak256>();

    MockScheduler mockScheduler;
    MockLedger mockLedger;
    MockTxPool mockTxPool;
    BaselineScheduler<MockScheduler, MockLedger> baselineScheduler;
};

BOOST_FIXTURE_TEST_SUITE(TestBaselineScheduler, TestBaselineSchedulerFixture)

BOOST_AUTO_TEST_CASE(scheduleBlock)
{
    auto block = std::make_shared<bcostars::protocol::BlockImpl>();
    auto blockHeader = block->blockHeader();
    blockHeader->setNumber(500);
    blockHeader->setVersion(200);
    blockHeader->calculateHash(*hashImpl);

    // Prepare a transaction
    bcos::bytes input;
    block->appendTransaction(
        transactionFactory->createTransaction(0, "to", input, "12345", 100, "chain", "group", 0));

    std::promise<void> end;
    baselineScheduler.executeBlock(block, false,
        [&](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr&& blockHeader,
            bool sysBlock) {
            BOOST_CHECK(!error);
            BOOST_CHECK(blockHeader);
            BOOST_CHECK(!sysBlock);

            end.set_value();
        });

    end.get_future().get();
    // baselineScheduler.commitBlock(blockHeader, std::function<void (Error::Ptr &&,
    // ledger::LedgerConfig::Ptr &&)> callback)
}

BOOST_AUTO_TEST_SUITE_END()