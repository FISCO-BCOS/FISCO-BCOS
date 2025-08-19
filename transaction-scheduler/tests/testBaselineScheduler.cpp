#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-ledger/LedgerMethods.h"
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
using namespace bcos::executor_v1;
using namespace bcos::scheduler_v1;

struct MockExecutorBaseline
{
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(auto& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        if (transaction.version() == 99)
        {
            BOOST_CHECK_EQUAL(blockHeader.timestamp(), 10088);
        }
        co_return std::shared_ptr<protocol::TransactionReceipt>();
    }

    template <class Storage>
    struct ExecuteContext
    {
        template <int step>
        task::Task<protocol::TransactionReceipt::Ptr> executeStep()
        {
            co_return {};
        }
    };

    auto createExecuteContext(auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int32_t contextID,
        ledger::LedgerConfig const& ledgerConfig, bool call)
        -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        co_return {};
    }
};
struct MockScheduler
{
    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(auto& storage,
        auto& executor, protocol::BlockHeader const& blockHeader,
        ::ranges::input_range auto const& transactions, ledger::LedgerConfig const& /*unused*/)
    {
        auto receipts =
            ::ranges::iota_view<size_t, size_t>(0, ::ranges::size(transactions)) |
            ::ranges::views::transform([](size_t index) -> protocol::TransactionReceipt::Ptr {
                auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
                    [inner = bcostars::TransactionReceipt()]() mutable {
                        return std::addressof(inner);
                    });
                constexpr static std::string_view str = "abc";
                receipt->mutableInner().dataHash.assign(str.begin(), str.end());
                return receipt;
            }) |
            ::ranges::to<std::vector<protocol::TransactionReceipt::Ptr>>();

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

inline task::AwaitableValue<void> tag_invoke(
    ledger::tag_t<bcos::ledger::getLedgerConfig> /*unused*/, MockLedger& ledger,
    ledger::LedgerConfig& ledgerConfig)
{
    return {};
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
    std::tuple<bcos::protocol::Block::Ptr, bcos::protocol::Block::Ptr> sealTxs(
        uint64_t _txsLimit) override
    {
        return {};
    }
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

    task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        ::ranges::any_view<bcos::h256, ::ranges::category::mask | ::ranges::category::sized> hashes)
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
    using MyMultiLayerStorage = MultiLayerStorage<MutableStorage, void, BackendStorage>;

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
        baselineScheduler(multiLayerStorage, mockScheduler, mockExecutor, *blockFactory, mockLedger,
            mockTxPool, *transactionSubmitResultFactory, *hashImpl)
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
    MyMultiLayerStorage multiLayerStorage;
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

            task::syncWait([&]() -> task::Task<void> {
                auto view = multiLayerStorage.fork();

                auto blockHash =
                    co_await ledger::getBlockHash(view, blockHeader->number(), ledger::fromStorage);
                BOOST_CHECK_EQUAL(blockHash.value(), blockHeader->hash());

                auto blockNumber =
                    co_await ledger::getBlockNumber(view, blockHeader->hash(), ledger::fromStorage);
                BOOST_CHECK_EQUAL(blockNumber.value(), blockHeader->number());
            }());
            end.set_value();
        });

    end.get_future().get();
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

BOOST_AUTO_TEST_CASE(resultCache)
{
    std::vector<protocol::Block::Ptr> blocks;

    for (auto i = 100; i < 110; ++i)
    {
        auto block = blocks.emplace_back(std::make_shared<bcostars::protocol::BlockImpl>());
        auto blockHeader = block->blockHeader();
        blockHeader->setNumber(i);
        blockHeader->setVersion(200);
        blockHeader->calculateHash(*hashImpl);
        bcos::bytes input;
        block->appendTransaction(transactionFactory->createTransaction(
            0, "to", input, "12345", 100, "chain", "group", 0));

        baselineScheduler.executeBlock(block, false,
            [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr gotBlockHeader,
                bool sysBlock) {
                BOOST_CHECK(!error);
                BOOST_CHECK(gotBlockHeader);
                BOOST_CHECK(!sysBlock);
                BOOST_CHECK(!error);
            });
    }

    // Try get same block
    for (auto& block : blocks)
    {
        baselineScheduler.executeBlock(block, false,
            [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr gotBlockHeader,
                bool sysBlock) {
                BOOST_CHECK(!error);
                BOOST_CHECK_EQUAL(gotBlockHeader->number(), block->blockHeader()->number());
            });
    }

    // Try smaller block
    auto smallBlock = std::make_shared<bcostars::protocol::BlockImpl>();
    auto smallBlockHeader = smallBlock->blockHeader();
    smallBlockHeader->setNumber(99);
    smallBlockHeader->setVersion(200);
    smallBlockHeader->calculateHash(*hashImpl);
    bcos::bytes input;
    smallBlock->appendTransaction(
        transactionFactory->createTransaction(0, "to", input, "12345", 100, "chain", "group", 0));

    baselineScheduler.executeBlock(smallBlock, false,
        [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr gotBlockHeader,
            bool sysBlock) {
            BOOST_CHECK(error);
            BOOST_CHECK(error->errorCode() == bcos::scheduler::SchedulerError::InvalidBlockNumber);
        });

    // Try Bigger block
    auto bigBlock = std::make_shared<bcostars::protocol::BlockImpl>();
    auto bigBlockHeader = bigBlock->blockHeader();
    bigBlockHeader->setNumber(111);
    bigBlockHeader->setVersion(200);
    bigBlockHeader->calculateHash(*hashImpl);
    bigBlock->appendTransaction(
        transactionFactory->createTransaction(0, "to", input, "12345", 100, "chain", "group", 0));

    baselineScheduler.executeBlock(bigBlock, false,
        [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr gotBlockHeader,
            bool sysBlock) {
            BOOST_CHECK(error);
            BOOST_CHECK(error->errorCode() == bcos::scheduler::SchedulerError::InvalidBlockNumber);
        });

    // Try expect block
    {
        auto expectBlock = std::make_shared<bcostars::protocol::BlockImpl>();
        auto expectBlockHeader = expectBlock->blockHeader();
        expectBlockHeader->setNumber(110);
        expectBlockHeader->setVersion(200);
        expectBlockHeader->calculateHash(*hashImpl);
        expectBlock->appendTransaction(transactionFactory->createTransaction(
            0, "to", input, "12345", 100, "chain", "group", 0));

        baselineScheduler.executeBlock(expectBlock, false,
            [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr gotBlockHeader,
                bool sysBlock) { BOOST_CHECK(!error); });
    }
}

BOOST_AUTO_TEST_CASE(emptyBlock)
{
    auto block = std::make_shared<bcostars::protocol::BlockImpl>();
    auto blockHeader = block->blockHeader();
    blockHeader->setNumber(111);
    blockHeader->setVersion(200);
    blockHeader->calculateHash(*hashImpl);

    baselineScheduler.executeBlock(block, false,
        [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr gotBlockHeader,
            bool sysBlock) {
            BOOST_CHECK(!error);
            BOOST_CHECK(gotBlockHeader);
            BOOST_CHECK(!sysBlock);

            BOOST_CHECK_EQUAL(blockHeader->txsRoot(), bcos::crypto::HashType{});
            BOOST_CHECK_EQUAL(blockHeader->receiptsRoot(), bcos::crypto::HashType{});
            BOOST_CHECK_EQUAL(blockHeader->stateRoot(), bcos::crypto::HashType{});
        });
}

BOOST_AUTO_TEST_CASE(call)
{
    auto block = std::make_shared<bcostars::protocol::BlockImpl>();
    auto blockHeader = block->blockHeader();
    blockHeader->setNumber(111);
    blockHeader->setVersion(200);
    blockHeader->setTimestamp(10088);
    blockHeader->calculateHash(*hashImpl);
    task::syncWait(ledger::prewriteBlock(mockLedger,
        std::make_shared<bcos::protocol::ConstTransactions>(), block, false, backendStorage));
    bytes headerBuffer;
    blockHeader->encode(headerBuffer);

    storage::Entry number2HeaderEntry;
    number2HeaderEntry.importFields({std::move(headerBuffer)});
    task::syncWait(storage2::writeOne(backendStorage,
        StateKey{ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(blockHeader->number())},
        std::move(number2HeaderEntry)));

    task::syncWait(storage2::writeOne(backendStorage,
        StateKey{ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER},
        storage::Entry{std::to_string(blockHeader->number())}));

    bcos::bytes input;
    auto transaction =
        transactionFactory->createTransaction(99, "to", input, "12345", 100, "chain", "group", 0);

    std::promise<void> end;
    baselineScheduler.call(
        transaction, [&](Error::Ptr&& error, protocol::TransactionReceipt::Ptr&& receipt) {
            BOOST_CHECK(!error);
            end.set_value();
        });

    end.get_future().get();
}

BOOST_AUTO_TEST_SUITE_END()