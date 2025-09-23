
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

// 异常情况测试用例

BOOST_AUTO_TEST_CASE(rollbackEmptyStorage)
{
    task::syncWait([]() -> task::Task<void> {
        MutableStorage memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        // 测试空存储的回滚操作，应该正常工作且不产生任何效果
        auto savepoint = rollbackableStorage.current();
        BOOST_CHECK_EQUAL(savepoint, 0);

        // 回滚空存储应该正常完成
        co_await rollbackableStorage.rollback(savepoint);

        // 再次检查current应该还是0
        BOOST_CHECK_EQUAL(rollbackableStorage.current(), 0);
    }());
}

BOOST_AUTO_TEST_CASE(rollbackToFutureSavepoint)
{
    task::syncWait([]() -> task::Task<void> {
        MutableStorage memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        std::string_view tableID = "table1";

        // 写入一些数据
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key1"sv}, storage::Entry{"Value1"});
        auto savepoint1 = rollbackableStorage.current();
        BOOST_CHECK_EQUAL(savepoint1, 1);

        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key2"sv}, storage::Entry{"Value2"});
        auto savepoint2 = rollbackableStorage.current();
        BOOST_CHECK_EQUAL(savepoint2, 2);

        // 尝试回滚到未来的savepoint（大于当前的savepoint）
        // 这应该不会产生任何效果，因为rollback只处理index > savepoint的情况
        constexpr auto FUTURE_OFFSET = 10;
        co_await rollbackableStorage.rollback(savepoint2 + FUTURE_OFFSET);

        // 验证数据仍然存在
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

        // 第一层操作
        auto savepoint0 = rollbackableStorage.current();
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key1"sv}, storage::Entry{"Value1"});

        // 第二层操作
        auto savepoint1 = rollbackableStorage.current();
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key2"sv}, storage::Entry{"Value2"});

        // 第三层操作
        auto savepoint2 = rollbackableStorage.current();
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key3"sv}, storage::Entry{"Value3"});

        // 验证所有数据都存在
        auto value1 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key1"sv});
        auto value2 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key2"sv});
        auto value3 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key3"sv});
        BOOST_CHECK(value1 && value2 && value3);

        // 回滚到savepoint2，应该只移除Key3
        co_await rollbackableStorage.rollback(savepoint2);
        value1 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key1"sv});
        value2 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key2"sv});
        value3 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key3"sv});
        BOOST_CHECK(value1 && value2 && !value3);

        // 回滚到savepoint1，应该移除Key2
        co_await rollbackableStorage.rollback(savepoint1);
        value1 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key1"sv});
        value2 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key2"sv});
        BOOST_CHECK(value1 && !value2);

        // 回滚到savepoint0，应该移除Key1
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

        // 写入数据
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key1"sv}, storage::Entry{"Value1"});
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key2"sv}, storage::Entry{"Value2"});

        // 验证数据存在
        auto value1 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key1"sv});
        auto value2 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key2"sv});
        BOOST_CHECK(value1 && value2);

        // 第一次回滚
        co_await rollbackableStorage.rollback(savepoint);
        value1 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key1"sv});
        value2 = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key2"sv});
        BOOST_CHECK(!value1 && !value2);

        // 再次回滚到同一个savepoint应该不产生任何效果
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

        // 验证数据已被移除
        auto value = co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key1"sv});
        BOOST_CHECK(!value);

        // 回滚后继续操作
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "Key2"sv}, storage::Entry{"NewValue"});
        auto newSavepoint = rollbackableStorage.current();

        // 验证新操作成功
        auto newValue =
            co_await storage2::readOne(rollbackableStorage, StateKey{tableID, "Key2"sv});
        BOOST_CHECK(newValue);
        BOOST_CHECK_EQUAL(newValue->get(), "NewValue");

        // 再次回滚应该移除新的操作
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

        // 批量写入操作
        constexpr auto BATCH_SIZE = 100;
        std::vector<std::pair<StateKey, storage::Entry>> keyValues;
        keyValues.reserve(BATCH_SIZE);
        for (int i = 0; i < BATCH_SIZE; ++i)
        {
            keyValues.emplace_back(StateKey{tableID, "Key" + std::to_string(i)},
                storage::Entry{"Value" + std::to_string(i)});
        }

        co_await storage2::writeSome(rollbackableStorage, keyValues);

        // 验证数据存在
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

        // 批量回滚
        co_await rollbackableStorage.rollback(savepoint);

        // 验证所有数据都被移除
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

        // 先写入一些数据
        for (int i = 0; i < TOTAL_KEYS; ++i)
        {
            co_await storage2::writeOne(rollbackableStorage,
                StateKey{tableID, "Key" + std::to_string(i)},
                storage::Entry{"Value" + std::to_string(i)});
        }

        auto savepoint = rollbackableStorage.current();

        // 批量删除部分数据
        std::vector<StateKey> keysToRemove;
        keysToRemove.reserve(KEYS_TO_REMOVE);
        for (int i = 0; i < KEYS_TO_REMOVE; ++i)
        {
            keysToRemove.emplace_back(StateKey{tableID, "Key" + std::to_string(i)});
        }

        co_await storage2::removeSome(rollbackableStorage, keysToRemove);

        // 验证数据被删除
        auto values = co_await storage2::readSome(rollbackableStorage, keysToRemove);
        for (auto&& value : values)
        {
            BOOST_CHECK(!value);
        }

        // 验证未删除的数据仍然存在
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

        // 回滚删除操作
        co_await rollbackableStorage.rollback(savepoint);

        // 验证被删除的数据恢复了
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

        // 预先写入一些数据作为基础
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "BaseKey1"sv}, storage::Entry{"BaseValue1"});
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "BaseKey2"sv}, storage::Entry{"BaseValue2"});

        auto savepoint = rollbackableStorage.current();

        // 混合操作：更新、新增、删除
        // 更新现有数据
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "BaseKey1"sv}, storage::Entry{"UpdatedValue1"});

        // 新增数据
        co_await storage2::writeOne(
            rollbackableStorage, StateKey{tableID, "NewKey1"sv}, storage::Entry{"NewValue1"});

        // 删除数据
        co_await storage2::removeOne(rollbackableStorage, StateKey{tableID, "BaseKey2"sv});

        // 验证操作结果
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

        // 回滚所有操作
        co_await rollbackableStorage.rollback(savepoint);

        // 验证回滚结果
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