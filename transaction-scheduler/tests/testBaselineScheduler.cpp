#include "bcos-scheduler/test/mock/MockLedger.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-transaction-scheduler/BaselineScheduler.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace bcos::transaction_scheduler;

struct MockScheduler
{
    task::Task<void> start() { co_return; }
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
                return receipt;
            }) |
            RANGES::to<std::vector<std::shared_ptr<bcostars::protocol::TransactionReceiptImpl>>>();

        co_return std::vector<std::shared_ptr<bcostars::protocol::TransactionReceiptImpl>>{};
    }
    task::Task<bcos::h256> finish(auto&& block, auto&& hashImpl) { co_return bcos::h256{}; }
    task::Task<void> commit() { co_return; }

    task::Task<std::shared_ptr<bcostars::protocol::TransactionReceiptImpl>> call(auto&& transaction)
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
        baselineScheduler(mockScheduler, *blockHeaderFactory, mockLedger, *hashImpl)
    {}

    TableNamePool tableNamePool;
    BackendStorage backendStorage;
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> blockFactory;
    crypto::Hash::Ptr hashImpl = std::make_shared<bcos::crypto::Keccak256>();

    MockScheduler mockScheduler;
    MockLedger mockLedger;
    BaselineScheduler<MockScheduler, bcostars::protocol::BlockHeaderFactoryImpl, MockLedger>
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

    baselineScheduler.executeBlock(block, false,
        [](bcos::Error::Ptr&& error, bcos::protocol::BlockHeader::Ptr&& blockHeader,
            bool sysBlock) {
            BOOST_CHECK(!error);
            BOOST_CHECK(blockHeader);
            BOOST_CHECK(!sysBlock);
        });
}

BOOST_AUTO_TEST_SUITE_END()