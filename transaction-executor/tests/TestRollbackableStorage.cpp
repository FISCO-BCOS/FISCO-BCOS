#include "../bcos-transaction-executor/RollbackableStorage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>

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
        string_pool::StringPool<> pool;
        memory_storage::MemoryStorage<StateKey, StateValue, memory_storage::ORDERED> memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        const auto* tableID = pool.add("table1");
        auto point = rollbackableStorage.current();
        storage::Entry entry;
        entry.set("OK!");
        co_await rollbackableStorage.write(single(StateKey{tableID, "Key1"}), single(entry));

        storage::Entry entry2;
        entry2.set("OK2!");
        co_await rollbackableStorage.write(single(StateKey{tableID, "Key2"}), single(entry2));

        // Check the entry exists
        std::vector<StateKey> keys{StateKey{tableID, "Key1"}, StateKey{pool.add("table1"), "Key2"}};
        auto it = co_await rollbackableStorage.read(keys);
        auto count = 0;
        while (co_await it.next())
        {
            BOOST_REQUIRE(co_await it.hasValue());
            auto&& key = co_await it.key();
            BOOST_CHECK_EQUAL(std::get<0>(key), tableID);
            BOOST_CHECK_EQUAL(std::get<0>(key), pool.add("table1"));
            ++count;
        }
        BOOST_CHECK_EQUAL(count, 2);
        co_await rollbackableStorage.rollback(point);

        // Query again
        std::vector<StateKey> keys2{StateKey{tableID, "Key1"}, StateKey{pool.add("table1"), "Key2"}};
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
        string_pool::StringPool<> pool;
        memory_storage::MemoryStorage<StateKey, StateValue, memory_storage::ORDERED> memoryStorage;
        Rollbackable rollbackableStorage(memoryStorage);

        const auto* tableID = pool.add("table1");
        auto point = rollbackableStorage.current();
        storage::Entry entry;
        entry.set("OK!");
        co_await rollbackableStorage.write(single(StateKey{tableID, "Key1"}), single(entry));

        storage::Entry entry2;
        entry2.set("OK2!");
        co_await rollbackableStorage.write(single(StateKey{tableID, "Key2"}), single(entry2));

        // Check the entry exists
        std::vector<StateKey> keys{StateKey{tableID, "Key1"}, StateKey{pool.add("table1"), "Key2"}};
        auto it = co_await rollbackableStorage.read(keys);
        auto count = 0;
        while (co_await it.next())
        {
            BOOST_REQUIRE(co_await it.hasValue());
            auto&& key = co_await it.key();
            BOOST_CHECK_EQUAL(std::get<0>(key), tableID);
            BOOST_CHECK_EQUAL(std::get<0>(key), pool.add("table1"));
            ++count;
        }
        BOOST_CHECK_EQUAL(count, 2);
        co_await rollbackableStorage.rollback(point);

        // Query again
        std::vector<StateKey> keys2{StateKey{tableID, "Key1"}, StateKey{pool.add("table1"), "Key2"}};
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

BOOST_AUTO_TEST_SUITE_END()