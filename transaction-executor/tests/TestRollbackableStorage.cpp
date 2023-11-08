#include "../bcos-transaction-executor/RollbackableStorage.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/storage2/StorageMethods.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-task/Task.h"
#include "bcos-task/Trait.h"
#include "bcos-task/Wait.h"
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

using BackendStorage1 =
    memory_storage::MemoryStorage<StateKey, StateValue, memory_storage::ORDERED>;
using BackendStorag2 =
    memory_storage::MemoryStorage<StateKey, int, storage2::memory_storage::ORDERED>;

auto tag_invoke(storage2::tag_t<storage2::readSome>&& /*unused*/, BackendStorage1& storage,
    RANGES::input_range auto const& keys, ReadDirect&& /*unused*/)
    -> task::Task<task::AwaitableReturnType<decltype(storage2::readSome(storage, keys))>>
{
    co_return storage2::readSome(storage, keys);
}

// static_assert(HasReadOneDirect<BackendStorage1>);

BOOST_AUTO_TEST_CASE(addRollback)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage1 memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        std::string_view tableID = "table1";
        auto point = rollbackableStorage.current();
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
        // while (co_await it.next())
        for (auto&& [key, value] : RANGES::views::zip(keys, values))
        {
            BOOST_REQUIRE(value);
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
        auto values2 = co_await storage2::readSome(rollbackableStorage, keys2);
        for (auto&& [key, value] : RANGES::views::zip(keys2, values2))
        {
            BOOST_REQUIRE(!value);
        }

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(removeRollback)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage1 memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        std::string_view tableID = "table1";
        auto point = rollbackableStorage.current();
        storage::Entry entry;
        entry.set("OK!");
        co_await storage2::writeOne(rollbackableStorage, StateKey{tableID, "Key1"sv}, entry);

        storage::Entry entry2;
        entry2.set("OK2!");
        co_await storage2::writeOne(rollbackableStorage, StateKey{tableID, "Key2"sv}, entry2);

        // Check the entry exists
        std::vector<StateKey> keys{StateKey{tableID, "Key1"sv}, StateKey{"table1"sv, "Key2"sv}};
        auto values = co_await storage2::readSome(rollbackableStorage, keys);

        for (auto&& [key, value] : RANGES::views::zip(keys, values))
        {
            BOOST_REQUIRE(value);
            BOOST_CHECK_EQUAL(static_cast<std::string>(std::get<0>(key)), tableID);
            BOOST_CHECK_EQUAL(static_cast<std::string>(std::get<0>(key)), "table1"sv);
        }
        co_await rollbackableStorage.rollback(point);

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