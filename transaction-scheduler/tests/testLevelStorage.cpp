#include "../bcos-transaction-scheduler/LevelStorage.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <type_traits>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace bcos::transaction_scheduler;

class TestLevelStorageFixture
{
public:
    using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED)>;
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED)>;

    TestLevelStorageFixture() : levelStorage(backendStorage) {}

    TableNamePool tableNamePool;
    BackendStorage backendStorage;
    LevelStorage<MutableStorage, BackendStorage> levelStorage;
};

BOOST_FIXTURE_TEST_SUITE(TestLevelStorage, TestLevelStorageFixture)

BOOST_AUTO_TEST_CASE(noMutable)
{
    task::syncWait([this]() -> task::Task<void> {
        storage::Entry entry;
        BOOST_CHECK_THROW(
            co_await storage2::writeOne(levelStorage,
                StateKey{
                    storage2::string_pool::makeStringID(tableNamePool, "test_table"), "test_key"},
                std::move(entry)),
            NotExistsMutableStorage);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(readWriteMutable)
{
    task::syncWait([this]() -> task::Task<void> {
        levelStorage.newMutable();
        StateKey key{storage2::string_pool::makeStringID(tableNamePool, "test_table"), "test_key"};

        storage::Entry entry;
        co_await storage2::writeOne(levelStorage, key, std::move(entry));

        RANGES::single_view keysView(key);
        auto it = co_await levelStorage.read(keysView);

        co_await it.next();
        BOOST_CHECK_EQUAL(co_await it.key(), key);
        BOOST_CHECK_EQUAL(co_await it.value(), entry);

        // constexpr static bool isReadable = ReadableStorage<decltype(levelStorage), StateKey>;
        // co_await storage2::readOne(levelStorage, key);

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()