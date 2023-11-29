#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-transaction-scheduler/MultiLayerStorage.h"
#include <bcos-task/Wait.h>
#include <fmt/format.h>
#include <boost/test/unit_test.hpp>
#include <type_traits>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace bcos::transaction_scheduler;
using namespace std::string_view_literals;

class TestMultiLayerStorageFixture
{
public:
    using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>;

    TestMultiLayerStorageFixture() : multiLayerStorage(backendStorage) {}

    BackendStorage backendStorage;
    MultiLayerStorage<MutableStorage, void, BackendStorage> multiLayerStorage;

    // static_assert(HasReadOneDirect<MultiLayerStorage<MutableStorage, void, BackendStorage>>);
};

BOOST_FIXTURE_TEST_SUITE(TestMultiLayerStorage, TestMultiLayerStorageFixture)

BOOST_AUTO_TEST_CASE(noMutable)
{
    task::syncWait([this]() -> task::Task<void> {
        auto view = multiLayerStorage.fork(true);
        storage::Entry entry;
        BOOST_CHECK_THROW(co_await storage2::writeOne(
                              view, StateKey{"test_table"sv, "test_key"sv}, std::move(entry)),
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
        StateKey key{"test_table"sv, "test_key"sv};

        storage::Entry entry;
        entry.set("Hello world!");
        co_await storage2::writeOne(*view, key, entry);

        RANGES::single_view keyViews(key);
        auto values = co_await storage2::readSome(*view, keyViews);

        BOOST_CHECK_EQUAL(values[0]->get(), entry.get());

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
        auto toKey = RANGES::views::transform([](int num) {
            return StateKey{"test_table"sv, fmt::format("key: {}", num)};
        });
        auto toValue = RANGES::views::transform([](int num) {
            storage::Entry entry;
            entry.set(fmt::format("value: {}", num));

            return entry;
        });

        co_await storage2::writeSome(*view, RANGES::iota_view<int, int>(0, 100) | toKey,
            RANGES::iota_view<int, int>(0, 100) | toValue);

        BOOST_CHECK_THROW(
            co_await multiLayerStorage.mergeAndPopImmutableBack(), NotExistsImmutableStorageError);

        multiLayerStorage.pushMutableToImmutableFront();
        co_await multiLayerStorage.mergeAndPopImmutableBack();
        view.reset();

        auto view2 = multiLayerStorage.fork(false);
        auto keys = RANGES::iota_view<int, int>(0, 100) | toKey;
        auto values = co_await storage2::readSome(view2, keys);

        for (auto&& [index, value] : RANGES::views::enumerate(values))
        {
            BOOST_CHECK_EQUAL(value->get(), fmt::format("value: {}", index));
        }
        BOOST_CHECK_EQUAL(RANGES::size(values), 100);

        multiLayerStorage.newMutable();

        auto view3 = multiLayerStorage.fork(true);
        co_await storage2::removeSome(view3, RANGES::iota_view<int, int>(20, 30) | toKey);
        multiLayerStorage.pushMutableToImmutableFront();
        co_await multiLayerStorage.mergeAndPopImmutableBack();

        auto values2 = co_await storage2::readSome(view3, keys);
        for (auto&& [index, value] : RANGES::views::enumerate(values2))
        {
            if (index >= 20 && index < 30)
            {
                BOOST_CHECK(!value);
            }
            else
            {
                BOOST_CHECK(value);
            }
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