#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-transaction-scheduler/MultiLayerStorage.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/SchedulerParallelImpl.h>
#include <boost/test/unit_test.hpp>
#include <mutex>
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

class TestSchedulerParallelFixture
{
public:
    using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>;

    TestSchedulerParallelFixture()
      : cryptoSuite(std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr)),
        receiptFactory(cryptoSuite),
        multiLayerStorage(backendStorage)
    {}

    TableNamePool tableNamePool;
    BackendStorage backendStorage;
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory;
    MultiLayerStorage<MutableStorage, void, BackendStorage> multiLayerStorage;
    crypto::Hash::Ptr hashImpl = std::make_shared<bcos::crypto::Keccak256>();
};

BOOST_FIXTURE_TEST_SUITE(TestSchedulerParallel, TestSchedulerParallelFixture)

BOOST_AUTO_TEST_CASE(simple)
{
    task::syncWait([&, this]() -> task::Task<void> {
        SchedulerParallelImpl<decltype(multiLayerStorage), MockExecutor> scheduler(
            multiLayerStorage, receiptFactory, tableNamePool);

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
    // Wait for tbb
}

template <StateStorage Storage>
struct MockConflictExecutor
{
    MockConflictExecutor(
        Storage& storage, [[maybe_unused]] auto&& receiptFactory, TableNamePool& tableNamePool)
      : m_storage(storage), m_tableNamePool(tableNamePool)
    {}

    task::Task<std::shared_ptr<bcos::protocol::TransactionReceipt>> execute(auto&& blockHeader,
        protocol::IsTransaction auto const& transaction, [[maybe_unused]] int contextID)
    {
        auto input = transaction.input();
        auto inputNum =
            boost::lexical_cast<int>(std::string_view((const char*)input.data(), input.size()));

        StateKey key1{makeStringID(m_tableNamePool, "t_test"), std::string_view("key1")};
        // Read first and +1
        auto it = co_await m_storage.read(storage2::singleView(key1));
        co_await it.next();

        if (co_await it.hasValue())
        {
            auto& oldEntry = co_await it.value();
            auto oldView = oldEntry.get();
            auto oldNum = boost::lexical_cast<int>(oldView);

            assert(oldNum <= inputNum);
        }
        storage::Entry entry;
        entry.set(boost::lexical_cast<std::string>(inputNum));
        co_await m_storage.write(storage2::singleView(key1), storage2::singleView(entry));

        StateKey key2{makeStringID(m_tableNamePool, "t_test"), std::string_view("key2")};
        auto it2 = co_await m_storage.read(storage2::singleView(key2));
        co_await it2.next();
        if (co_await it2.hasValue())
        {
            auto& oldEntry = co_await it2.value();
            auto oldView = oldEntry.get();
            auto oldNum = boost::lexical_cast<int>(oldView);

            auto newNum = oldNum + 1;
            storage::Entry entry2;
            entry2.set(boost::lexical_cast<std::string>(newNum));

            co_await m_storage.write(storage2::singleView(key2), storage2::singleView(entry2));
        }
        else
        {
            // write 0 into storage
            storage::Entry entry2;
            entry2.set(boost::lexical_cast<std::string>(0));

            co_await m_storage.write(storage2::singleView(key2), storage2::singleView(entry2));
        }

        co_return std::shared_ptr<bcos::protocol::TransactionReceipt>();
    }

    Storage& m_storage;
    TableNamePool& m_tableNamePool;
};

BOOST_AUTO_TEST_CASE(conflict)
{
    task::syncWait([&, this]() -> task::Task<void> {
        SchedulerParallelImpl<decltype(multiLayerStorage), MockConflictExecutor> scheduler(
            multiLayerStorage, receiptFactory, tableNamePool);
        scheduler.setChunkSize(1);
        scheduler.setMaxToken(4);
        scheduler.start();
        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });

        constexpr static auto count = 128;
        auto transactions = RANGES::views::iota(0, count) | RANGES::views::transform([](int index) {
            auto transaction = std::make_unique<bcostars::protocol::TransactionImpl>(
                [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
            auto num = boost::lexical_cast<std::string>(index);
            transaction->mutableInner().data.input.assign(num.begin(), num.end());

            return transaction;
        }) | RANGES::to<std::vector<std::unique_ptr<bcostars::protocol::TransactionImpl>>>();

        auto receipts = co_await scheduler.execute(blockHeader,
            transactions | RANGES::views::transform([](auto& ptr) -> auto& { return *ptr; }));

        auto& mutableStorage = multiLayerStorage.mutableStorage();
        StateKey key1{makeStringID(tableNamePool, "t_test"), std::string_view("key1")};
        StateKey key2{makeStringID(tableNamePool, "t_test"), std::string_view("key2")};

        auto it = co_await mutableStorage.read(storage2::singleView(key1));
        co_await it.next();
        BOOST_CHECK(co_await it.hasValue());
        auto& entry1 = co_await it.value();
        auto result1 = boost::lexical_cast<int>(entry1.get());
        BOOST_CHECK_EQUAL(result1, count - 1);

        auto it2 = co_await mutableStorage.read(storage2::singleView(key2));
        co_await it2.next();
        BOOST_REQUIRE(co_await it2.hasValue());
        auto& entry2 = co_await it2.value();
        auto result2 = boost::lexical_cast<int>(entry2.get());
        BOOST_CHECK_EQUAL(result2, count - 1);

        // co_await scheduler.finish(blockHeader, *hashImpl);
        // co_await scheduler.commit();

        co_return;
    }());
    // Wait for tbb
}

BOOST_AUTO_TEST_SUITE_END()