#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-transaction-scheduler/MultiLayerStorage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <boost/test/unit_test.hpp>
#include <range/v3/view/transform.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace bcos::transaction_scheduler;

template <class Storage>
struct MockExecutor
{
    MockExecutor([[maybe_unused]] auto&& storage, [[maybe_unused]] auto&& receiptFactory,
        [[maybe_unused]] auto&& tableNamePool)
    {}

    task::Task<std::shared_ptr<bcos::protocol::TransactionReceipt>> execute(
        auto&& blockHeader, auto&& transaction, [[maybe_unused]] int contextID)
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
        multiLayerStorage(backendStorage),
        scheduler(multiLayerStorage, receiptFactory, tableNamePool)
    {}

    TableNamePool tableNamePool;
    BackendStorage backendStorage;
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory;

    MultiLayerStorage<MutableStorage, void, BackendStorage> multiLayerStorage;

    SchedulerSerialImpl<decltype(multiLayerStorage), MockExecutor> scheduler;

    crypto::Hash::Ptr hashImpl = std::make_shared<bcos::crypto::Keccak256>();
};

BOOST_FIXTURE_TEST_SUITE(TestSchedulerSerial, TestSchedulerSerialFixture)

BOOST_AUTO_TEST_CASE(executeBlock)
{
    task::syncWait([&, this]() -> task::Task<void> {
        scheduler.start();
        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        auto transactions =
            RANGES::iota_view<int, int>(0, 100) | RANGES::views::transform([](int index) {
                return std::make_unique<bcostars::protocol::TransactionImpl>(
                    [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
            }) |
            RANGES::to<std::vector<std::unique_ptr<bcostars::protocol::TransactionImpl>>>();

        auto receipts = co_await scheduler.execute(blockHeader,
            transactions | RANGES::views::transform([](auto& ptr) -> auto& { return *ptr; }));
        co_await scheduler.finish(blockHeader, *hashImpl);
        co_await scheduler.commit();

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()