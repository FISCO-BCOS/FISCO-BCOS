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

class TestMultiLayerStorageFixture
{
public:
    using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        TableNameHash>;

    TestMultiLayerStorageFixture() : multiLayerStorage(backendStorage) {}

    TableNamePool tableNamePool;
    BackendStorage backendStorage;
    MultiLayerStorage<MutableStorage, void, BackendStorage> multiLayerStorage;

    static_assert(
        storage2::ReadableStorage<decltype(multiLayerStorage.fork(true))>, "No match storage!");
};

BOOST_FIXTURE_TEST_SUITE(TestMultiLayerStorage, TestMultiLayerStorageFixture)

BOOST_AUTO_TEST_CASE(noMutable)
{
    task::syncWait([this]() -> task::Task<void> {
        auto view = multiLayerStorage.fork(true);
        storage::Entry entry;
        BOOST_CHECK_THROW(
            co_await storage2::writeOne(view,
                StateKey{
                    storage2::string_pool::makeStringID(tableNamePool, "test_table"), "test_key"},
                std::move(entry)),
            NotExistsMutableStorageError);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(readWriteMutable)
{
    task::syncWait([this]() -> task::Task<void> {
        BOOST_CHECK_THROW(
            multiLayerStorage.pushMutableToImmutableFront(), NotExistsMutableStorageError);

        multiLayerStorage.newMutable();
        auto view = std::make_optional(multiLayerStorage.fork(true));
        StateKey key{storage2::string_pool::makeStringID(tableNamePool, "test_table"), "test_key"};

        storage::Entry entry;
        entry.set("Hello world!");
        co_await storage2::writeOne(*view, key, entry);

        RANGES::single_view keyViews(key);
        auto it = co_await view->read(keyViews);

        co_await it.next();
        const auto& iteratorValue = co_await it.value();
        BOOST_CHECK_EQUAL(iteratorValue.get(), entry.get());

        BOOST_CHECK_NO_THROW(multiLayerStorage.pushMutableToImmutableFront());
        view.reset();
        auto view2 = multiLayerStorage.fork(true);
        BOOST_CHECK_THROW(
            co_await storage2::writeOne(view2, key, entry), NotExistsMutableStorageError);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(merge)
{
    task::syncWait([this]() -> task::Task<void> {
        BOOST_CHECK_THROW(
            multiLayerStorage.pushMutableToImmutableFront(), NotExistsMutableStorageError);

        multiLayerStorage.newMutable();
        auto view = std::make_optional(multiLayerStorage.fork(true));
        auto toKey = RANGES::views::transform([tableNamePool = &tableNamePool](int num) {
            return StateKey{storage2::string_pool::makeStringID(*tableNamePool, "test_table"),
                fmt::format("key: {}", num)};
        });
        auto toValue = RANGES::views::transform([](int num) {
            storage::Entry entry;
            entry.set(fmt::format("value: {}", num));

            return entry;
        });

        co_await view->write(RANGES::iota_view<int, int>(0, 100) | toKey,
            RANGES::iota_view<int, int>(0, 100) | toValue);

        BOOST_CHECK_THROW(
            co_await multiLayerStorage.mergeAndPopImmutableBack(), NotExistsImmutableStorageError);

        multiLayerStorage.pushMutableToImmutableFront();
        co_await multiLayerStorage.mergeAndPopImmutableBack();
        view.reset();

        auto view2 = multiLayerStorage.fork(false);
        auto keys = RANGES::iota_view<int, int>(0, 100) | toKey;
        auto it = co_await view2.read(keys);

        int i = 0;
        while (co_await it.next())
        {
            auto const& value = co_await it.value();
            BOOST_CHECK_EQUAL(value.get(), fmt::format("value: {}", i));
            ++i;
        }
        BOOST_CHECK_EQUAL(i, 100);

        multiLayerStorage.newMutable();

        auto view3 = multiLayerStorage.fork(true);
        co_await view3.remove(RANGES::iota_view<int, int>(20, 30) | toKey);
        multiLayerStorage.pushMutableToImmutableFront();
        co_await multiLayerStorage.mergeAndPopImmutableBack();

        auto removedIt = co_await view3.read(keys);
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

BOOST_AUTO_TEST_CASE(oneMutable)
{
    multiLayerStorage.newMutable();
    auto view1 = std::make_optional(multiLayerStorage.fork(true));
    auto view2 = multiLayerStorage.fork(false);
    BOOST_CHECK_THROW(auto view3 = multiLayerStorage.fork(true), DuplicateMutableViewError);

    view1.reset();
    auto view4 = multiLayerStorage.fork(true);
}

BOOST_AUTO_TEST_SUITE_END()