#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-transaction-scheduler/MultiLayerStorage.h"
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::scheduler_v1;

struct MockExecutorSerial
{
    struct Context
    {
    };

    friend task::Task<Context> tag_invoke(executor_v1::tag_t<createExecuteContext> /*unused*/,
        MockExecutorSerial& executor, auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int32_t contextID,
        ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        co_return {};
    }

    template <int step>
    friend task::Task<protocol::TransactionReceipt::Ptr> tag_invoke(
        executor_v1::tag_t<executeStep> /*unused*/, Context& executeContext)
    {
        co_return {};
    }

    friend task::Task<protocol::TransactionReceipt::Ptr> tag_invoke(
        executor_v1::tag_t<executor_v1::executeTransaction> /*unused*/,
        MockExecutorSerial& executor, auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int contextID, ledger::LedgerConfig const&,
        auto&& waitOperator)
    {
        co_return {};
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
        view.newMutable();
        ledger::LedgerConfig ledgerConfig;
        auto receipts =
            co_await bcos::scheduler_v1::executeBlock(scheduler, view, executor, blockHeader,
                transactions | RANGES::views::transform([](auto& ptr) -> auto& { return *ptr; }),
                ledgerConfig);
        BOOST_CHECK_EQUAL(transactions.size(), receipts.size());

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()