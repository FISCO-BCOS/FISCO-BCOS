#include "../bcos-transaction-executor/RollbackableStorage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <range/v3/view/transform.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;

class TestRollbackableStorageFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestRollbackableStorage, TestRollbackableStorageFixture)

BOOST_AUTO_TEST_CASE(addRollback)
{
    task::syncWait([]() -> task::Task<void> {
        string_pool::FixedStringPool pool;
        memory_storage::MemoryStorage<StateKey, StateValue, memory_storage::ORDERED> memoryStorage;

        Rollbackable rollbackableStorage(memoryStorage);
        static_assert(storage2::ReadableStorage<decltype(rollbackableStorage)>, "No match type!");

        auto tableID = makeStringID(pool, "table1");
        auto point = rollbackableStorage.current();
        storage::Entry entry;
        entry.set("OK!");
        co_await rollbackableStorage.write(
            singleView(StateKey{tableID, "Key1"}), singleView(entry));

        storage::Entry entry2;
        entry2.set("OK2!");
        co_await rollbackableStorage.write(
            singleView(StateKey{tableID, "Key2"}), singleView(entry2));

        // Check the entry exists
        std::vector<StateKey> keys{
            StateKey{tableID, "Key1"}, StateKey{makeStringID(pool, "table1"), "Key2"}};
        auto it = co_await rollbackableStorage.read(keys);
        auto count = 0;
        while (co_await it.next())
        {
            BOOST_REQUIRE(co_await it.hasValue());
            auto&& key = co_await it.key();
            BOOST_CHECK_EQUAL(std::get<0>(key), tableID);
            BOOST_CHECK_EQUAL(std::get<0>(key), makeStringID(pool, "table1"));
            ++count;
        }
        BOOST_CHECK_EQUAL(count, 2);
        co_await rollbackableStorage.rollback(point);

        // Query again
        std::vector<StateKey> keys2{
            StateKey{tableID, "Key1"}, StateKey{makeStringID(pool, "table1"), "Key2"}};
        auto it2 = co_await rollbackableStorage.read(keys2);
        auto count2 = 0;
        while (co_await it2.next())
        {
            BOOST_REQUIRE(!co_await it2.hasValue());
        }
        BOOST_CHECK_EQUAL(count2, 0);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(removeRollback)
{
    task::syncWait([]() -> task::Task<void> {
        string_pool::FixedStringPool pool;
        memory_storage::MemoryStorage<StateKey, StateValue, memory_storage::ORDERED> memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        auto tableID = makeStringID(pool, "table1");
        auto point = rollbackableStorage.current();
        storage::Entry entry;
        entry.set("OK!");
        co_await rollbackableStorage.write(
            singleView(StateKey{tableID, "Key1"}), singleView(entry));

        storage::Entry entry2;
        entry2.set("OK2!");
        co_await rollbackableStorage.write(
            singleView(StateKey{tableID, "Key2"}), singleView(entry2));

        // Check the entry exists
        std::vector<StateKey> keys{
            StateKey{tableID, "Key1"}, StateKey{makeStringID(pool, "table1"), "Key2"}};
        auto it = co_await rollbackableStorage.read(keys);
        auto count = 0;
        while (co_await it.next())
        {
            BOOST_REQUIRE(co_await it.hasValue());
            auto&& key = co_await it.key();
            BOOST_CHECK_EQUAL(std::get<0>(key), tableID);
            BOOST_CHECK_EQUAL(std::get<0>(key), makeStringID(pool, "table1"));
            ++count;
        }
        BOOST_CHECK_EQUAL(count, 2);
        co_await rollbackableStorage.rollback(point);

        // Query again
        std::vector<StateKey> keys2{
            StateKey{tableID, "Key1"}, StateKey{makeStringID(pool, "table1"), "Key2"}};
        auto it2 = co_await rollbackableStorage.read(keys2);
        auto count2 = 0;
        while (co_await it2.next())
        {
            BOOST_REQUIRE(!co_await it2.hasValue());
        }
        BOOST_CHECK_EQUAL(count2, 0);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(equal)
{
    task::syncWait([]() -> task::Task<void> {
        storage2::string_pool::FixedStringPool pool;
        storage2::memory_storage::MemoryStorage<transaction_executor::StateKey, int,
            storage2::memory_storage::ORDERED>
            storage;

        co_await storage.write(RANGES::single_view<transaction_executor::StateKey>(RANGES::in_place,
                                   makeStringID(pool, "table"), std::string_view("0")),
            RANGES::single_view(0));
        co_await storage.write(RANGES::single_view<transaction_executor::StateKey>(RANGES::in_place,
                                   makeStringID(pool, "table"), std::string_view("1")),
            RANGES::single_view(1));
        co_await storage.write(RANGES::single_view<transaction_executor::StateKey>(RANGES::in_place,
                                   makeStringID(pool, "table"), std::string_view("2")),
            RANGES::single_view(2));

        auto it = co_await storage.read(
            RANGES::iota_view<int, int>(0, 3) | RANGES::views::transform([&pool](int num) {
                return StateKey(makeStringID(pool, "table"), boost::lexical_cast<std::string>(num));
            }));
        int i = 0;
        while (co_await it.next())
        {
            auto& key = co_await it.key();
            auto& value = co_await it.value();
            auto view = std::get<1>(key).toStringView();
            auto str = boost::lexical_cast<std::string>(i);
            BOOST_CHECK_EQUAL(view, str);
            BOOST_CHECK_EQUAL(value, i);
            ++i;
        }
        BOOST_CHECK_EQUAL(i, 3);
    }());
}

BOOST_AUTO_TEST_SUITE_END()