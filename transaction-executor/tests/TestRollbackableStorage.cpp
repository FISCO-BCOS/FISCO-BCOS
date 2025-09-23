
#include "../bcos-transaction-executor/RollbackableStorage.h"
#include "TestMemoryStorage.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/Task.h"
#include "bcos-task/Wait.h"
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
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

        auto view = ::ranges::single_view(StateKey{"table1"sv, "Key1"sv});
        co_await storage2::readSome(memoryStorage, view, storage2::DIRECT);

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
        for (auto&& [key, value] : ::ranges::views::zip(keys, values))
        {
            BOOST_REQUIRE(value);
            BOOST_CHECK_EQUAL(key, StateKey(tableID, "Key" + std::to_string(count + 1)));
            ++count;
        }
        BOOST_CHECK_EQUAL(count, 2);
        co_await rollbackableStorage.rollback(point);

        // Query again
        std::vector<StateKey> keys2{StateKey{tableID, "Key1"sv}, StateKey{"table1"sv, "Key2"sv}};
        auto values2 = co_await storage2::readSome(rollbackableStorage, keys2);
        for (auto&& [key, value] : ::ranges::views::zip(keys2, values2))
        {
            BOOST_REQUIRE(!value);
        }

        // Update and check
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key1"sv}, storage::Entry{"OK!"});

        auto savepoint2 = rollbackableStorage.current();
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key1"s}, storage::Entry{"OK3!"});
        auto value3 =
            co_await storage2::readOne(rollbackableStorage, StateKeyView{tableID, "Key1"sv});
        BOOST_CHECK(value3);
        BOOST_CHECK_EQUAL(value3->get(), "OK3!");

        co_await rollbackableStorage.rollback(savepoint2);
        auto value4 =
            co_await storage2::readOne(rollbackableStorage, StateKeyView{tableID, "Key1"sv});
        BOOST_CHECK(value4);
        BOOST_CHECK_EQUAL(value4->get(), "OK!");
    }());
}

BOOST_AUTO_TEST_CASE(removeRollback)
{
    task::syncWait([]() -> task::Task<void> {
        MutableStorage memoryStorage;
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

        int count = 0;
        for (auto&& [key, value] : ::ranges::views::zip(keys, values))
        {
            BOOST_REQUIRE(value);
            BOOST_CHECK_EQUAL(key, StateKey(tableID, "Key" + std::to_string(count + 1)));
            ++count;
        }
        co_await rollbackableStorage.rollback(point);

        // Query again
        std::vector<StateKey> keys2{StateKey{tableID, "Key1"sv}, StateKey{"table1"sv, "Key2"sv}};
        auto values2 = co_await storage2::readSome(rollbackableStorage, keys2);
        for (auto&& [key, value] : ::ranges::views::zip(keys2, values2))
        {
            BOOST_REQUIRE(!value);
        }
    }());
}

BOOST_AUTO_TEST_CASE(equal)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorag2 storage;

        co_await storage2::writeOne(storage, executor_v1::StateKey("table"sv, "0"sv), 0);
        co_await storage2::writeOne(storage, executor_v1::StateKey("table"sv, "1"sv), 1);
        co_await storage2::writeOne(storage, executor_v1::StateKey("table"sv, "2"sv), 2);

        auto keys = ::ranges::iota_view<int, int>(0, 3) | ::ranges::views::transform([](int num) {
            return StateKey("table"sv, boost::lexical_cast<std::string>(num));
        });
        auto values = co_await storage2::readSome(storage, keys);
        int i = 0;
        for (auto&& [key, value] : ::ranges::views::zip(keys, values))
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

// Test cases for exceptional situations
BOOST_AUTO_TEST_CASE(rollbackEmptyStorage)
{
    task::syncWait([]() -> task::Task<void> {
        MutableStorage memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        // Test rollback operation on empty storage, should work normally and have no effect
        auto savepoint = rollbackableStorage.current();
        BOOST_CHECK_EQUAL(savepoint, 0);

        // Rolling back empty storage should complete normally
        co_await rollbackableStorage.rollback(savepoint);

        // After rollback, current should still be 0
        BOOST_CHECK_EQUAL(rollbackableStorage.current(), 0);
    }());
}

BOOST_AUTO_TEST_CASE(rollbackToFutureSavepoint)
{
    task::syncWait([]() -> task::Task<void> {
        MutableStorage memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        std::string_view tableID = "table1";

        // Write some data
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key1"sv}, storage::Entry{"Value1"});
        auto savepoint1 = rollbackableStorage.current();
        BOOST_CHECK_EQUAL(savepoint1, 1);

        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key2"sv}, storage::Entry{"Value2"});
        auto savepoint2 = rollbackableStorage.current();
        BOOST_CHECK_EQUAL(savepoint2, 2);

        // Try to rollback to a future savepoint (greater than the current savepoint)
        // This should have no effect, since rollback only processes index > savepoint
        constexpr auto FUTURE_OFFSET = 10;
        co_await rollbackableStorage.rollback(savepoint2 + FUTURE_OFFSET);

        // Verify that the data still exists
        auto value1 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key1"sv});
        auto value2 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key2"sv});
        BOOST_CHECK(value1);
        BOOST_CHECK(value2);
        BOOST_CHECK_EQUAL(value1->get(), "Value1");
        BOOST_CHECK_EQUAL(value2->get(), "Value2");
    }());
}

BOOST_AUTO_TEST_CASE(nestedRollbacks)
{
    task::syncWait([]() -> task::Task<void> {
        MutableStorage memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        std::string_view tableID = "table1";

        // First level operation
        auto savepoint0 = rollbackableStorage.current();
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key1"sv}, storage::Entry{"Value1"});

        // Second level operation
        auto savepoint1 = rollbackableStorage.current();
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key2"sv}, storage::Entry{"Value2"});

        // Third level operation
        auto savepoint2 = rollbackableStorage.current();
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key3"sv}, storage::Entry{"Value3"});

        // Verify that all data exists
        auto value1 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key1"sv});
        auto value2 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key2"sv});
        auto value3 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key3"sv});
        BOOST_CHECK(value1 && value2 && value3);

        // Rollback to savepoint2, should only remove Key3
        co_await rollbackableStorage.rollback(savepoint2);
        value1 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key1"sv});
        value2 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key2"sv});
        value3 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key3"sv});
        BOOST_CHECK(value1 && value2 && !value3);

        // Rollback to savepoint1, should remove Key2
        co_await rollbackableStorage.rollback(savepoint1);
        value1 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key1"sv});
        value2 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key2"sv});
        BOOST_CHECK(value1 && !value2);

        // Rollback to savepoint0, should remove Key1
        co_await rollbackableStorage.rollback(savepoint0);
        value1 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key1"sv});
        BOOST_CHECK(!value1);
    }());
}

BOOST_AUTO_TEST_CASE(multipleRollbacksToSameSavepoint)
{
    task::syncWait([]() -> task::Task<void> {
        MutableStorage memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        std::string_view tableID = "table1";
        auto savepoint = rollbackableStorage.current();

        // Write data
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key1"sv}, storage::Entry{"Value1"});
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key2"sv}, storage::Entry{"Value2"});

        // Verify that the data exists
        auto value1 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key1"sv});
        auto value2 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key2"sv});
        BOOST_CHECK(value1 && value2);

        // First rollback
        co_await rollbackableStorage.rollback(savepoint);
        value1 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key1"sv});
        value2 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key2"sv});
        BOOST_CHECK(!value1 && !value2);

        // Rolling back to the same savepoint again should have no effect
        co_await rollbackableStorage.rollback(savepoint);
        value1 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key1"sv});
        value2 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key2"sv});
        BOOST_CHECK(!value1 && !value2);
    }());
}

BOOST_AUTO_TEST_CASE(operationsAfterRollback)
{
    task::syncWait([]() -> task::Task<void> {
        MutableStorage memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        std::string_view tableID = "table1";
        auto savepoint = rollbackableStorage.current();

        // 写入数据
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key1"sv}, storage::Entry{"Value1"});

        // 回滚
        co_await rollbackableStorage.rollback(savepoint);

        // Verify that the data has been removed
        auto value = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key1"sv});
        BOOST_CHECK(!value);

        // Continue operations after rollback
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key2"sv}, storage::Entry{"NewValue"});
        auto newSavepoint = rollbackableStorage.current();

        // Verify that the new operation succeeded
        auto newValue =
            co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key2"sv});
        BOOST_CHECK(newValue);
        BOOST_CHECK_EQUAL(newValue->get(), "NewValue");

        // Rolling back again should remove the new operation
        co_await rollbackableStorage.rollback(savepoint);
        newValue = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key2"sv});
        BOOST_CHECK(!newValue);
    }());
}

BOOST_AUTO_TEST_CASE(batchOperationsRollback)
{
    task::syncWait([]() -> task::Task<void> {
        MutableStorage memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        std::string_view tableID = "table1";
        auto savepoint = rollbackableStorage.current();

        // Batch write operations
        constexpr auto BATCH_SIZE = 100;
        std::vector<std::pair<StateKey, storage::Entry>> keyValues;
        keyValues.reserve(BATCH_SIZE);
        for (int i = 0; i < BATCH_SIZE; ++i)
        {
            keyValues.emplace_back(StateKey{tableID, "Key" + std::to_string(i)},
                storage::Entry{"Value" + std::to_string(i)});
        }

        co_await storage2::writeSome(rollbackableStorage, keyValues);

        // Verify that the data exists
        std::vector<StateKey> keys;
        keys.reserve(BATCH_SIZE);
        for (int i = 0; i < BATCH_SIZE; ++i)
        {
            keys.emplace_back(tableID, "Key" + std::to_string(i));
        }
        auto values = co_await storage2::readSome(rollbackableStorage, keys);

        int count = 0;
        for (auto&& value : values)
        {
            if (value)
            {
                ++count;
            }
        }
        BOOST_CHECK_EQUAL(count, BATCH_SIZE);

        // Batch rollback
        co_await rollbackableStorage.rollback(savepoint);

        // Verify that all data has been removed
        auto valuesAfterRollback = co_await storage2::readSome(rollbackableStorage, keys);
        count = 0;
        for (auto&& value : valuesAfterRollback)
        {
            if (value)
            {
                ++count;
            }
        }
        BOOST_CHECK_EQUAL(count, 0);
    }());
}

BOOST_AUTO_TEST_CASE(removeSomeRollback)
{
    task::syncWait([]() -> task::Task<void> {
        MutableStorage memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        std::string_view tableID = "table1";
        constexpr auto TOTAL_KEYS = 5;
        constexpr auto KEYS_TO_REMOVE = 3;

        // Write some data first
        for (int i = 0; i < TOTAL_KEYS; ++i)
        {
            co_await storage2::writeOne(rollbackableStorage,
                StateKey{tableID, "Key" + std::to_string(i)},
                storage::Entry{"Value" + std::to_string(i)});
        }

        auto savepoint = rollbackableStorage.current();

        // Batch delete some data
        std::vector<StateKey> keysToRemove;
        keysToRemove.reserve(KEYS_TO_REMOVE);
        for (int i = 0; i < KEYS_TO_REMOVE; ++i)
        {
            keysToRemove.emplace_back(StateKey{tableID, "Key" + std::to_string(i)});
        }

        co_await storage2::removeSome(rollbackableStorage, keysToRemove);

        // Verify that the data has been deleted
        auto values = co_await storage2::readSome(rollbackableStorage, keysToRemove);
        for (auto&& value : values)
        {
            BOOST_CHECK(!value);
        }

        // Verify that the undeleted data still exists
        std::vector<StateKey> remainingKeys;
        for (int i = KEYS_TO_REMOVE; i < TOTAL_KEYS; ++i)
        {
            remainingKeys.emplace_back(StateKey{tableID, "Key" + std::to_string(i)});
        }
        auto remainingValues = co_await storage2::readSome(rollbackableStorage, remainingKeys);
        for (auto&& value : remainingValues)
        {
            BOOST_CHECK(value);
        }

        // Rollback the delete operation
        co_await rollbackableStorage.rollback(savepoint);

        // Verify that the deleted data has been restored
        auto restoredValues = co_await storage2::readSome(rollbackableStorage, keysToRemove);
        for (auto&& value : restoredValues)
        {
            BOOST_CHECK(value);
        }
    }());
}

BOOST_AUTO_TEST_CASE(mixedOperationsRollback)
{
    task::syncWait([]() -> task::Task<void> {
        MutableStorage memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        std::string_view tableID = "table1";

        // Pre-write some data as the base
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "BaseKey1"sv}, storage::Entry{"BaseValue1"});
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "BaseKey2"sv}, storage::Entry{"BaseValue2"});

        auto savepoint = rollbackableStorage.current();

        // Mixed operations: update, insert, delete
        // Update existing data
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "BaseKey1"sv}, storage::Entry{"UpdatedValue1"});

        // Insert new data
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "NewKey1"sv}, storage::Entry{"NewValue1"});

        // Delete data
        co_await storage2::removeOne(rollbackableStorage, StateKey{tableID, "BaseKey2"sv});

        // Verify the operation results
        auto updatedValue =
            co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "BaseKey1"sv});
        auto newValue =
            co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "NewKey1"sv});
        auto deletedValue =
            co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "BaseKey2"sv});

        BOOST_CHECK(updatedValue);
        BOOST_CHECK_EQUAL(updatedValue->get(), "UpdatedValue1");
        BOOST_CHECK(newValue);
        BOOST_CHECK_EQUAL(newValue->get(), "NewValue1");
        BOOST_CHECK(!deletedValue);

        // Rollback all operations
        co_await rollbackableStorage.rollback(savepoint);

        // Verify the rollback results
        auto restoredOriginal =
            co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "BaseKey1"sv});
        auto restoredDeleted =
            co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "BaseKey2"sv});
        auto removedNew =
            co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "NewKey1"sv});

        BOOST_CHECK(restoredOriginal);
        BOOST_CHECK_EQUAL(restoredOriginal->get(), "BaseValue1");
        BOOST_CHECK(restoredDeleted);
        BOOST_CHECK_EQUAL(restoredDeleted->get(), "BaseValue2");
        BOOST_CHECK(!removedNew);
    }());
}

BOOST_AUTO_TEST_SUITE_END()