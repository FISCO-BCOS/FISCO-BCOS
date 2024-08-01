
#include "../bcos-transaction-executor/RollbackableStorage.h"
#include "TestMemoryStorage.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace std::string_literals;
using namespace std::string_view_literals;

class TestRollbackableStorageFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestRollbackableStorage, TestRollbackableStorageFixture)

using BackendStorag2 =
    memory_storage::MemoryStorage<StateKey, int, storage2::memory_storage::ORDERED>;

BOOST_AUTO_TEST_CASE(addRollback)
{
    task::syncWait([]() -> task::Task<void> {
        MutableStorage memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        auto view = RANGES::single_view(StateKey{"table1"sv, "Key1"sv});
        co_await storage2::readSome(memoryStorage, view, storage2::DIRECT);

        std::string_view tableID = "table1";
        auto point = current(rollbackableStorage);
        storage::Entry entry;
        entry.set("OK!");
        co_await storage2::writeOne(rollbackableStorage, StateKey{tableID, "Key1"sv}, entry);

        storage::Entry entry2;
        entry2.set("OK2!");
        co_await storage2::writeOne(rollbackableStorage, StateKey{tableID, "Key2"sv}, entry2);

        // Check the entry exists
        std::vector<StateKey> keys{StateKey{tableID, "Key1"sv}, StateKey{"table1"sv, "Key2"sv}};
        auto values = co_await storage2::readSome(rollbackableStorage, keys);
        auto count = 0;
        for (auto&& [key, value] : RANGES::views::zip(keys, values))
        {
            BOOST_REQUIRE(value);
            BOOST_CHECK_EQUAL(key, StateKey(tableID, "Key" + std::to_string(count + 1)));
            ++count;
        }
        BOOST_CHECK_EQUAL(count, 2);
        co_await rollback(rollbackableStorage, point);

        // Query again
        std::vector<StateKey> keys2{StateKey{tableID, "Key1"sv}, StateKey{"table1"sv, "Key2"sv}};
        auto values2 = co_await storage2::readSome(rollbackableStorage, keys2);
        for (auto&& [key, value] : RANGES::views::zip(keys2, values2))
        {
            BOOST_REQUIRE(!value);
        }

        // Update and check
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key1"sv}, storage::Entry{"OK!"});

        auto savepoint2 = current(rollbackableStorage);
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key1"s}, storage::Entry{"OK3!"});
        auto value3 =
            co_await storage2::readOne(rollbackableStorage, StateKeyView{tableID, "Key1"sv});
        BOOST_CHECK(value3);
        BOOST_CHECK_EQUAL(value3->get(), "OK3!");

        co_await rollback(rollbackableStorage, savepoint2);
        auto value4 =
            co_await storage2::readOne(rollbackableStorage, StateKeyView{tableID, "Key1"sv});
        BOOST_CHECK(value4);
        BOOST_CHECK_EQUAL(value4->get(), "OK!");

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(removeRollback)
{
    task::syncWait([]() -> task::Task<void> {
        MutableStorage memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        std::string_view tableID = "table1";
        auto point = current(rollbackableStorage);
        storage::Entry entry;
        entry.set("OK!");
        co_await storage2::writeOne(rollbackableStorage, StateKey{tableID, "Key1"sv}, entry);

        storage::Entry entry2;
        entry2.set("OK2!");
        co_await storage2::writeOne(rollbackableStorage, StateKey{tableID, "Key2"sv}, entry2);

        // Check the entry exists
        std::vector<StateKey> keys{StateKey{tableID, "Key1"sv}, StateKey{"table1"sv, "Key2"sv}};
        auto values = co_await storage2::readSome(rollbackableStorage, keys);

        int count = 0;
        for (auto&& [key, value] : RANGES::views::zip(keys, values))
        {
            BOOST_REQUIRE(value);
            BOOST_CHECK_EQUAL(key, StateKey(tableID, "Key" + std::to_string(count + 1)));
            ++count;
        }
        co_await rollback(rollbackableStorage, point);

        // Query again
        std::vector<StateKey> keys2{StateKey{tableID, "Key1"sv}, StateKey{"table1"sv, "Key2"sv}};
        auto values2 = co_await storage2::readSome(rollbackableStorage, keys2);
        for (auto&& [key, value] : RANGES::views::zip(keys2, values2))
        {
            BOOST_REQUIRE(!value);
        }

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(equal)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorag2 storage;

        co_await storage2::writeOne(storage, transaction_executor::StateKey("table"sv, "0"sv), 0);
        co_await storage2::writeOne(storage, transaction_executor::StateKey("table"sv, "1"sv), 1);
        co_await storage2::writeOne(storage, transaction_executor::StateKey("table"sv, "2"sv), 2);

        auto keys = RANGES::iota_view<int, int>(0, 3) | RANGES::views::transform([](int num) {
            return StateKey("table"sv, boost::lexical_cast<std::string>(num));
        });
        auto values = co_await storage2::readSome(storage, keys);
        int i = 0;
        for (auto&& [key, value] : RANGES::views::zip(keys, values))
        {
            auto view = StateKeyView(key);
            auto str = boost::lexical_cast<std::string>(i);
            BOOST_CHECK_EQUAL(view, StateKeyView("table"sv, std::string_view(str)));
            BOOST_CHECK_EQUAL(*value, i);
            ++i;
        }
        BOOST_CHECK_EQUAL(i, 3);
    }());
}

BOOST_AUTO_TEST_SUITE_END()