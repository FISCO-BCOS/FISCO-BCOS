#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/ReadWriteSetStorage.h>
#include <fmt/format.h>
#include <boost/test/unit_test.hpp>
#include <optional>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace bcos::transaction_scheduler;

class TestReadWriteSetStorageFixture
{
public:
    using Storage =
        memory_storage::MemoryStorage<int, int, memory_storage::Attribute(memory_storage::ORDERED)>;
};

BOOST_FIXTURE_TEST_SUITE(TestReadWriteSetStorage, TestReadWriteSetStorageFixture)

BOOST_AUTO_TEST_CASE(readWriteSet)
{
    task::syncWait([]() -> task::Task<void> {
        Storage lhsStorage;
        ReadWriteSetStorage<decltype(lhsStorage), int> firstStorage(lhsStorage);

        Storage rhsStorage;
        ReadWriteSetStorage<decltype(rhsStorage), int> secondStorage(rhsStorage);

        co_await storage2::writeOne(firstStorage, 100, 1);
        co_await storage2::writeOne(firstStorage, 200, 1);
        co_await storage2::writeOne(firstStorage, 300, 1);

        co_await storage2::readOne(secondStorage, 400);
        co_await storage2::readOne(secondStorage, 500);
        co_await storage2::readOne(secondStorage, 600);

        BOOST_CHECK(!hasRAWIntersection(firstStorage, secondStorage));

        co_await storage2::readOne(secondStorage, 200);
        BOOST_CHECK(hasRAWIntersection(firstStorage, secondStorage));

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(rangeReadWrite)
{
    task::syncWait([]() -> task::Task<void> {
        Storage lhsStorage;
        ReadWriteSetStorage<decltype(lhsStorage), int> firstStorage(lhsStorage);

        co_await storage2::writeOne(firstStorage, 100, 1);
        co_await storage2::writeOne(firstStorage, 200, 1);
        co_await storage2::writeOne(firstStorage, 300, 1);

        std::vector<int> result;
        auto range = co_await storage2::range(firstStorage);
        while (auto keyValue = co_await range.next())
        {
            result.emplace_back(std::get<1>(*keyValue));
        }

        BOOST_CHECK_EQUAL(result.size(), 3);
    }());
}

struct MockStorage
{
    using Value = int;
};

task::Task<std::optional<int>> tag_invoke(
    storage2::tag_t<storage2::readOne> /*unused*/, MockStorage& storage, auto&& key)
{
    BOOST_FAIL("Unexcept to here!");
    co_return std::nullopt;
}

task::Task<std::optional<int>> tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/,
    MockStorage& storage, auto&& key, storage2::DIRECT_TYPE direct)
{
    co_return std::make_optional(100);
}

BOOST_AUTO_TEST_CASE(directFlag)
{
    task::syncWait([]() -> task::Task<void> {
        MockStorage lhsStorage;
        ReadWriteSetStorage<decltype(lhsStorage), int> firstStorage(lhsStorage);

        auto value = co_await storage2::readOne(firstStorage, 100, storage2::DIRECT);
        BOOST_CHECK(value);
        BOOST_CHECK_EQUAL(*value, 100);
    }());
}

BOOST_AUTO_TEST_SUITE_END()