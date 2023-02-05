#include "bcos-framework/storage2/MemoryStorage.h"
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/MultiLayerStorage.h>
#include <fmt/format.h>
#include <boost/test/unit_test.hpp>
#include <range/v3/view/transform.hpp>
#include <type_traits>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace bcos::transaction_scheduler;

struct TableNameHash
{
    size_t operator()(const bcos::transaction_executor::StateKey& key) const
    {
        auto const& tableID = std::get<0>(key);
        return std::hash<bcos::transaction_executor::TableNameID>{}(tableID);
    }
};

class TestLevelStorageFixture
{
public:
    using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        TableNameHash>;

    TestLevelStorageFixture() : levelStorage(backendStorage) {}

    TableNamePool tableNamePool;
    BackendStorage backendStorage;
    MultiLayerStorage<MutableStorage, BackendStorage> levelStorage;
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
        BOOST_CHECK_THROW(levelStorage.pushMutableToImmutableFront(), NotExistsMutableStorage);

        levelStorage.newMutable();
        StateKey key{storage2::string_pool::makeStringID(tableNamePool, "test_table"), "test_key"};

        storage::Entry entry;
        entry.set("Hello world!");
        co_await storage2::writeOne(levelStorage, key, entry);

        RANGES::single_view keyViews(key);
        auto it = co_await levelStorage.read(keyViews);

        co_await it.next();
        const auto& iteratorKey = co_await it.key();
        BOOST_CHECK_EQUAL(std::get<0>(iteratorKey), std::get<0>(key));
        BOOST_CHECK_EQUAL(std::get<1>(iteratorKey).toStringView(), std::get<1>(key).toStringView());

        const auto& iteratorValue = co_await it.value();
        BOOST_CHECK_EQUAL(iteratorValue.get(), entry.get());

        BOOST_CHECK_NO_THROW(levelStorage.pushMutableToImmutableFront());
        BOOST_CHECK_THROW(
            co_await storage2::writeOne(levelStorage, key, entry), NotExistsMutableStorage);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(merge)
{
    task::syncWait([this]() -> task::Task<void> {
        BOOST_CHECK_THROW(levelStorage.pushMutableToImmutableFront(), NotExistsMutableStorage);

        levelStorage.newMutable();

        auto toKey = RANGES::views::transform([tableNamePool = &tableNamePool](int num) {
            return StateKey{storage2::string_pool::makeStringID(*tableNamePool, "test_table"),
                fmt::format("key: {}", num)};
        });
        auto toValue = RANGES::views::transform([](int num) {
            storage::Entry entry;
            entry.set(fmt::format("value: {}", num));

            return entry;
        });

        co_await levelStorage.write(RANGES::iota_view<int, int>(0, 100) | toKey,
            RANGES::iota_view<int, int>(0, 100) | toValue);

        BOOST_CHECK_THROW(
            co_await levelStorage.mergeAndPopImmutableBack(), NotExistsImmutableStorage);

        levelStorage.pushMutableToImmutableFront();
        co_await levelStorage.mergeAndPopImmutableBack();

        auto keys = RANGES::iota_view<int, int>(0, 100) | toKey;
        auto it = co_await levelStorage.read(keys);

        int i = 0;
        while (co_await it.next())
        {
            auto const& key = co_await it.key();
            BOOST_CHECK_EQUAL(*std::get<0>(key),
                *(storage2::string_pool::makeStringID(tableNamePool, "test_table")));
            BOOST_CHECK_EQUAL(std::get<1>(key).toStringView(), fmt::format("key: {}", i));

            auto const& value = co_await it.value();
            BOOST_CHECK_EQUAL(value.get(), fmt::format("value: {}", i));
            ++i;
        }
        BOOST_CHECK_EQUAL(i, 100);

        levelStorage.newMutable();
        co_await levelStorage.remove(RANGES::iota_view<int, int>(20, 30) | toKey);
        levelStorage.pushMutableToImmutableFront();
        co_await levelStorage.mergeAndPopImmutableBack();

        auto removedIt = co_await levelStorage.read(keys);
        i = 0;
        while (co_await removedIt.next())
        {
            if (i >= 20 && i < 30)
            {
                BOOST_CHECK(!co_await removedIt.hasValue());
            }
            else
            {
                BOOST_CHECK(co_await removedIt.hasValue());
            }
            ++i;
        }

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()