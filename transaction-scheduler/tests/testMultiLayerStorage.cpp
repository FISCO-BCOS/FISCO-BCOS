#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-scheduler/MultiLayerStorage.h"
#include "bcos-transaction-scheduler/ReadWriteSetStorage.h"
#include <fmt/format.h>
#include <boost/test/unit_test.hpp>

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
        auto view = fork(multiLayerStorage);
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
        auto view = std::make_optional(fork(multiLayerStorage));
        newMutable(*view);
        StateKey key{"test_table"sv, "test_key"sv};

        storage::Entry entry;
        entry.set("Hello world!");
        co_await storage2::writeOne(*view, key, entry);

        RANGES::single_view keyViews(key);
        auto values = co_await storage2::readSome(*view, keyViews);

        BOOST_CHECK_EQUAL(values[0]->get(), entry.get());
        BOOST_CHECK_NO_THROW(pushView(multiLayerStorage, std::move(*view)));

        auto view2 = fork(multiLayerStorage);
        BOOST_CHECK_THROW(
            co_await storage2::writeOne(view2, key, entry), NotExistsMutableStorageError);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(merge)
{
    task::syncWait([this]() -> task::Task<void> {
        auto view = std::make_optional(fork(multiLayerStorage));
        newMutable(*view);
        auto toKey = RANGES::views::transform(
            [](int num) { return StateKey{"test_table"sv, fmt::format("key: {}", num)}; });
        auto toValue = RANGES::views::transform([](int num) {
            storage::Entry entry;
            entry.set(fmt::format("value: {}", num));

            return entry;
        });

        co_await storage2::writeSome(*view, RANGES::iota_view<int, int>(0, 100) | toKey,
            RANGES::iota_view<int, int>(0, 100) | toValue);

        BOOST_CHECK_THROW(
            co_await mergeBackStorage(multiLayerStorage), NotExistsImmutableStorageError);

        pushView(multiLayerStorage, std::move(*view));
        co_await mergeBackStorage(multiLayerStorage);

        auto view2 = fork(multiLayerStorage);
        auto keys = RANGES::iota_view<int, int>(0, 100) | toKey;
        auto values = co_await storage2::readSome(view2, keys);

        for (auto&& [index, value] : RANGES::views::enumerate(values))
        {
            BOOST_CHECK_EQUAL(value->get(), fmt::format("value: {}", index));
        }
        BOOST_CHECK_EQUAL(RANGES::size(values), 100);

        auto view3 = fork(multiLayerStorage);
        newMutable(view3);
        co_await storage2::removeSome(view3, RANGES::iota_view<int, int>(20, 30) | toKey);
        pushView(multiLayerStorage, std::move(view3));
        co_await mergeBackStorage(multiLayerStorage);

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

BOOST_AUTO_TEST_CASE(rangeMulti)
{
    using MutableStorage = memory_storage::MemoryStorage<int, int,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    using BackendStorage = memory_storage::MemoryStorage<int, int,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LRU)>;

    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        co_await storage2::writeSome(
            backendStorage, RANGES::views::iota(0, 4), RANGES::views::repeat(0));

        MultiLayerStorage<MutableStorage, void, BackendStorage> myMultiLayerStorage(backendStorage);

        auto view1 = fork(myMultiLayerStorage);
        newMutable(view1);
        co_await storage2::writeSome(view1, RANGES::views::iota(2, 6), RANGES::views::repeat(1));
        co_await storage2::removeOne(view1, 2);
        pushView(myMultiLayerStorage, std::move(view1));

        auto view2 = fork(myMultiLayerStorage);
        newMutable(view2);
        co_await storage2::writeSome(view2, RANGES::views::iota(4, 8), RANGES::views::repeat(2));

        auto resultList = co_await storage2::readSome(view2, RANGES::views::iota(0, 8));
        auto vecList = resultList | RANGES::views::transform([](auto input) { return *input; }) |
                       RANGES::to<std::vector>();
        BOOST_CHECK_EQUAL(resultList.size(), 8);
        auto expectList = std::vector<int>({0, 0, 0, 1, 2, 2, 2, 2});
        BOOST_CHECK_EQUAL_COLLECTIONS(
            vecList.begin(), vecList.end(), expectList.begin(), expectList.end());

        auto range = co_await storage2::range(view2);
        auto i = 0;
        std::vector<int> vecList2;
        while (auto keyValue = co_await range.next())
        {
            auto& [key, value] = *keyValue;
            std::cout << fmt::format("key: {}, value: {}\n", key, value);
            BOOST_CHECK_EQUAL(key, i);
            ++i;

            vecList2.emplace_back(value);
        }
        BOOST_CHECK_EQUAL_COLLECTIONS(
            vecList2.begin(), vecList2.end(), expectList.begin(), expectList.end());

        ReadWriteSetStorage<decltype(view2), int> readWriteSetStorage(view2);
        auto range2 = co_await storage2::range(readWriteSetStorage);
    }());
}

BOOST_AUTO_TEST_SUITE_END()