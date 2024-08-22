#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-transaction-scheduler/MultiLayerStorage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-protocol/TransactionSubmitResultFactoryImpl.h>
#include <bcos-transaction-scheduler/BaselineScheduler.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <boost/test/unit_test.hpp>
#include <future>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace bcos::transaction_scheduler;

struct MockExecutorBaseline
{
    friend task::Task<protocol::TransactionReceipt::Ptr> tag_invoke(
        bcos::transaction_executor::tag_t<
            bcos::transaction_executor::executeTransaction> /*unused*/,
        MockExecutorBaseline& executor, auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int contextID, ledger::LedgerConfig const&,
        auto&& waitOperator)
    {
        co_return std::shared_ptr<protocol::TransactionReceipt>();
    }
};
struct MockScheduler
{
    friend task::Task<std::vector<protocol::TransactionReceipt::Ptr>> tag_invoke(
        transaction_scheduler::tag_t<transaction_scheduler::executeBlock> /*unused*/,
        MockScheduler& /*unused*/, auto& storage, auto& executor,
        protocol::BlockHeader const& blockHeader, RANGES::input_range auto const& transactions,
        ledger::LedgerConfig const&)
    {
        auto receipts =
            RANGES::iota_view<size_t, size_t>(0, RANGES::size(transactions)) |
            RANGES::views::transform([](size_t index) -> protocol::TransactionReceipt::Ptr {
                auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
                    [inner = bcostars::TransactionReceipt()]() mutable {
                        return std::addressof(inner);
                    });
                constexpr static std::string_view str = "abc";
                receipt->mutableInner().dataHash.assign(str.begin(), str.end());
                return receipt;
            }) |
            RANGES::to<std::vector<protocol::TransactionReceipt::Ptr>>();

        co_return receipts;
    }
};

struct MockLedger
{
};

inline task::AwaitableValue<void> tag_invoke(ledger::tag_t<bcos::ledger::prewriteBlock> /*unused*/,
    MockLedger& ledger, bcos::protocol::ConstTransactionsPtr transactions,
    bcos::protocol::Block::ConstPtr block, bool withTransactionsAndReceipts, auto& storage)
{
    return {};
}

inline task::AwaitableValue<ledger::LedgerConfig::Ptr> tag_invoke(
    ledger::tag_t<bcos::ledger::getLedgerConfig> /*unused*/, MockLedger& ledger)
{
    return {std::make_shared<ledger::LedgerConfig>()};
}

task::AwaitableValue<void> tag_invoke(ledger::tag_t<ledger::storeTransactionsAndReceipts>,
    MockLedger& ledger, bcos::protocol::ConstTransactionsPtr blockTxs,
    bcos::protocol::Block::ConstPtr block)
{
    return {};
}

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
    using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
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
        multiLayerStorage(backendStorage),
        baselineScheduler(multiLayerStorage, mockScheduler, mockExecutor, *blockHeaderFactory,
            mockLedger, mockTxPool, *transactionSubmitResultFactory, *hashImpl)
    {}

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
    MultiLayerStorage<MutableStorage, void, BackendStorage> multiLayerStorage;
    MockExecutorBaseline mockExecutor;
    BaselineScheduler<decltype(multiLayerStorage), MockExecutorBaseline, MockScheduler, MockLedger>
        baselineScheduler;
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

BOOST_AUTO_TEST_CASE(sameBlock)
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

    std::promise<bcos::Error::Ptr> end;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    baselineScheduler.executeBlock(block, false,
        [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr blockHeader, bool sysBlock) {
            BOOST_CHECK(!error);
            BOOST_CHECK(blockHeader);
            BOOST_CHECK(!sysBlock);

            executedHeader = blockHeader;
            end.set_value(error);
        });
    auto error = end.get_future().get();
    BOOST_CHECK(!error);

    std::promise<bcos::Error::Ptr> end2;
    baselineScheduler.executeBlock(block, false,
        [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr blockHeader, bool sysBlock) {
            BOOST_CHECK(!error);
            BOOST_CHECK(blockHeader);
            BOOST_CHECK(!sysBlock);

            BOOST_CHECK_EQUAL(blockHeader.get(), executedHeader.get());
            BOOST_CHECK_EQUAL(blockHeader->hash(), executedHeader->hash());
            BOOST_CHECK_EQUAL(blockHeader->number(), executedHeader->number());
            end2.set_value(error);
        });
    auto error2 = end2.get_future().get();
    BOOST_CHECK(!error2);
}

BOOST_AUTO_TEST_SUITE_END()