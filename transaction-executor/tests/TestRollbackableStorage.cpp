#include "../bcos-transaction-executor/RollbackableStorage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <iostream>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace std::string_view_literals;

class TestRollbackableStorageFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestRollbackableStorage, TestRollbackableStorageFixture)

BOOST_AUTO_TEST_CASE(addRollback)
{
    task::syncWait([]() -> task::Task<void> {
        memory_storage::MemoryStorage<StateKey, StateValue, memory_storage::ORDERED> memoryStorage;

        Rollbackable rollbackableStorage(memoryStorage);
        static_assert(storage2::ReadableStorage<decltype(rollbackableStorage)>, "No match type!");

        std::string_view tableID = "table1";
        auto point = rollbackableStorage.current();
        storage::Entry entry;
        entry.set("OK!");
        co_await rollbackableStorage.write(
            RANGES::views::single(StateKeyView{tableID, "Key1"}), RANGES::views::single(entry));

        storage::Entry entry2;
        entry2.set("OK2!");
        co_await rollbackableStorage.write(
            RANGES::views::single(StateKeyView{tableID, "Key2"}), RANGES::views::single(entry2));

        // Check the entry exists
        std::vector<StateKey> keys{StateKey{tableID, "Key1"sv}, StateKey{"table1"sv, "Key2"sv}};
        auto it = co_await rollbackableStorage.read(keys);
        auto count = 0;
        while (co_await it.next())
        {
            BOOST_REQUIRE(co_await it.hasValue());
            auto&& key = co_await it.key();
            BOOST_CHECK_EQUAL(
                static_cast<std::string>(static_cast<std::string>(std::get<0>(key))), tableID);
            BOOST_CHECK_EQUAL(
                static_cast<std::string>(static_cast<std::string>(std::get<0>(key))), "table1"sv);
            ++count;
        }
        BOOST_CHECK_EQUAL(count, 2);
        co_await rollbackableStorage.rollback(point);

        // Query again
        std::vector<StateKey> keys2{StateKey{tableID, "Key1"sv}, StateKey{"table1"sv, "Key2"sv}};
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
        memory_storage::MemoryStorage<StateKey, StateValue, memory_storage::ORDERED> memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        std::string_view tableID = "table1";
        auto point = rollbackableStorage.current();
        storage::Entry entry;
        entry.set("OK!");
        co_await rollbackableStorage.write(
            RANGES::views::single(StateKey{tableID, "Key1"sv}), RANGES::views::single(entry));

        storage::Entry entry2;
        entry2.set("OK2!");
        co_await rollbackableStorage.write(
            RANGES::views::single(StateKey{tableID, "Key2"sv}), RANGES::views::single(entry2));

        // Check the entry exists
        std::vector<StateKey> keys{StateKey{tableID, "Key1"sv}, StateKey{"table1"sv, "Key2"sv}};
        auto it = co_await rollbackableStorage.read(keys);
        auto count = 0;
        while (co_await it.next())
        {
            BOOST_REQUIRE(co_await it.hasValue());
            auto&& key = co_await it.key();
            BOOST_CHECK_EQUAL(static_cast<std::string>(std::get<0>(key)), tableID);
            BOOST_CHECK_EQUAL(static_cast<std::string>(std::get<0>(key)), "table1"sv);
            ++count;
        }
        BOOST_CHECK_EQUAL(count, 2);
        co_await rollbackableStorage.rollback(point);

        // Query again
        std::vector<StateKey> keys2{StateKey{tableID, "Key1"sv}, StateKey{"table1"sv, "Key2"sv}};
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
        storage2::memory_storage::MemoryStorage<transaction_executor::StateKey, int,
            storage2::memory_storage::ORDERED>
            storage;

        co_await storage.write(
            RANGES::single_view<transaction_executor::StateKey>(RANGES::in_place, "table"sv, "0"sv),
            RANGES::single_view(0));
        co_await storage.write(
            RANGES::single_view<transaction_executor::StateKey>(RANGES::in_place, "table"sv, "1"sv),
            RANGES::single_view(1));
        co_await storage.write(
            RANGES::single_view<transaction_executor::StateKey>(RANGES::in_place, "table"sv, "2"sv),
            RANGES::single_view(2));

        auto it = co_await storage.read(
            RANGES::iota_view<int, int>(0, 3) | RANGES::views::transform([](int num) {
                return StateKey("table"sv, boost::lexical_cast<std::string>(num));
            }));
        int i = 0;
        while (co_await it.next())
        {
            auto& key = co_await it.key();
            auto& value = co_await it.value();
            auto view = std::get<1>(key);
            auto str = boost::lexical_cast<std::string>(i);
            BOOST_CHECK_EQUAL(static_cast<std::string>(view), std::string_view(str));
            BOOST_CHECK_EQUAL(value, i);
            ++i;
        }
        BOOST_CHECK_EQUAL(i, 3);
    }());
}

BOOST_AUTO_TEST_SUITE_END()