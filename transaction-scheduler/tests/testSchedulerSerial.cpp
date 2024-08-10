#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-task/Generator.h"
#include "bcos-transaction-scheduler/MultiLayerStorage.h"
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace bcos::transaction_scheduler;

struct MockExecutorSerial
{
    friend task::Generator<protocol::TransactionReceipt::Ptr> tag_invoke(
        transaction_executor::tag_t<execute3Step> /*unused*/, MockExecutorSerial& executor,
        auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int contextID,
        ledger::LedgerConfig const& ledgerConfig, auto&& waitOperator)
    {
        co_yield std::shared_ptr<bcos::protocol::TransactionReceipt>();
        co_yield std::shared_ptr<bcos::protocol::TransactionReceipt>();
        co_yield std::shared_ptr<bcos::protocol::TransactionReceipt>();
    }

    friend task::Task<protocol::TransactionReceipt::Ptr> tag_invoke(
        transaction_executor::tag_t<transaction_executor::executeTransaction> /*unused*/,
        MockExecutorSerial& executor, auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int contextID, ledger::LedgerConfig const&,
        auto&& waitOperator)
    {
        co_return std::shared_ptr<bcos::protocol::TransactionReceipt>();
    }
};

class TestSchedulerSerialFixture
{
public:
    using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>;

    TestSchedulerSerialFixture()
      : cryptoSuite(std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr)),
        receiptFactory(cryptoSuite),
        multiLayerStorage(backendStorage)
    {}

    BackendStorage backendStorage;
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory;
    MultiLayerStorage<MutableStorage, void, BackendStorage> multiLayerStorage;
    SchedulerSerialImpl scheduler;

    crypto::Hash::Ptr hashImpl = std::make_shared<bcos::crypto::Keccak256>();
};

BOOST_FIXTURE_TEST_SUITE(TestSchedulerSerial, TestSchedulerSerialFixture)

BOOST_AUTO_TEST_CASE(executeBlock)
{
    task::syncWait([&, this]() -> task::Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        auto transactions =
            RANGES::iota_view<int, int>(0, 100) | RANGES::views::transform([](int index) {
                return std::make_unique<bcostars::protocol::TransactionImpl>(
                    [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
            }) |
            RANGES::to<std::vector<std::unique_ptr<bcostars::protocol::TransactionImpl>>>();

        MockExecutorSerial executor;
        auto view = fork(multiLayerStorage);
        newMutable(view);
        ledger::LedgerConfig ledgerConfig;
        auto receipts = co_await bcos::transaction_scheduler::executeBlock(scheduler, view,
            executor, blockHeader,
            transactions | RANGES::views::transform([](auto& ptr) -> auto& { return *ptr; }),
            ledgerConfig);
        BOOST_CHECK_EQUAL(transactions.size(), receipts.size());

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()