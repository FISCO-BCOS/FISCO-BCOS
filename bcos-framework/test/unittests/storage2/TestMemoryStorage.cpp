#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/Storage.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-task/Wait.h>
#include <fmt/format.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2::memory_storage;

struct MemoryStorageFixture
{
};

template <>
struct std::hash<std::tuple<std::string, std::string>>
{
    auto operator()(std::tuple<std::string, std::string> const& str) const noexcept
    {
        auto hash = std::hash<std::string>{}(std::get<0>(str));
        return hash;
    }
};
BOOST_FIXTURE_TEST_SUITE(TestMemoryStorage, MemoryStorageFixture)

namespace other1::other2
{
template <class T>
struct MyStorage
{
    T value{};
    using Value = T;

    friend task::Task<std::vector<std::optional<int>>> tag_invoke(
        storage2::tag_t<storage2::readSome> /*unused*/, MyStorage& storage, auto&& keys)
    {
        std::vector<std::optional<int>> result;
        result.emplace_back(200);
        co_return result;
    }

    friend auto tag_invoke(storage2::tag_t<bcos::storage2::readOne> /*unused*/, MyStorage& storage,
        auto&& key) -> task::Task<std::optional<int>>
    {
        co_return std::make_optional(100);
    }
};

BOOST_AUTO_TEST_CASE(passbyDefault)
{
    task::syncWait([]() -> task::Task<void> {
        MyStorage<int> myStorage;

        auto num = co_await storage2::readOne(myStorage, 1);

        BOOST_CHECK_EQUAL(*num, 100);
    }());
}
}  // namespace other1::other2

BOOST_AUTO_TEST_CASE(writeReadModifyRemove)
{
    task::syncWait([]() -> task::Task<void> {
        constexpr static int count = 100;
        MemoryStorage<std::tuple<std::string, std::string>, storage::Entry, ORDERED> storage;
        co_await storage2::writeSome(storage,
            RANGES::iota_view<int, int>(0, count) | RANGES::views::transform([](auto num) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(num));
            }),
            RANGES::iota_view<int, int>(0, count) | RANGES::views::transform([](auto num) {
                storage::Entry entry;
                entry.set("Hello world!" + boost::lexical_cast<std::string>(num));
                return entry;
            }));

        auto values = co_await storage2::readSome(
            storage, RANGES::iota_view<int, int>(0, count) | RANGES::views::transform([](int i) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(i));
            }));

        for (auto&& [i, value] : RANGES::views::enumerate(values))
        {
            BOOST_REQUIRE(value);
            BOOST_CHECK_EQUAL(value->get(), "Hello world!" + boost::lexical_cast<std::string>(i));
        }

        // modify
        storage::Entry newEntry;
        newEntry.set("Hello map!");
        co_await storage2::writeOne(
            storage, std::tuple<std::string, std::string>("table", "key:5"), std::move(newEntry));

        auto result = co_await storage2::readOne(
            storage, std::tuple<std::string, std::string>("table", "key:5"));
        BOOST_REQUIRE(result);
        BOOST_CHECK_EQUAL(result->get(), "Hello map!");

        BOOST_CHECK_NO_THROW(co_await storage2::removeSome(
            storage, RANGES::iota_view<int, int>(10, 20) | RANGES::views::transform([](int i) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(i));
            })));

        // Check if records had erased
        auto values2 = co_await storage2::readSome(
            storage, RANGES::iota_view<int, int>(0, count) | RANGES::views::transform([](int i) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(i));
            }));

        for (auto&& [i, value] : RANGES::views::enumerate(values2))
        {
            if (i >= 10 && i < 20)
            {
                BOOST_REQUIRE(!value);
            }
            else
            {
                BOOST_REQUIRE(value);
                if (i == 5)
                {
                    BOOST_CHECK_EQUAL(value->get(), "Hello map!");
                }
                else
                {
                    BOOST_CHECK_EQUAL(
                        value->get(), "Hello world!" + boost::lexical_cast<std::string>(i));
                }
            }
        }

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(mru)
{
    task::syncWait([]() -> task::Task<void> {
        MemoryStorage<int, storage::Entry, Attribute(ORDERED | MRU), std::hash<int>> storage(1);
        storage.setMaxCapacity(1000);

        // write 10 100byte value
        storage::Entry entry;
        entry.set(std::string(100, 'a'));
        co_await storage2::writeSome(
            storage, RANGES::iota_view<int, int>(0, 10), RANGES::repeat_view(entry));

        // ensure 10 are useable
        auto values = co_await storage2::readSome(storage, RANGES::iota_view<int, int>(0, 10));
        for (auto&& [i, value] : RANGES::views::enumerate(values))
        {
            BOOST_REQUIRE(value);
        }

        // ensure 0 is erased
        co_await storage2::writeOne(storage, 10, entry);
        auto notExists = co_await storage2::readOne(storage, 0);
        BOOST_REQUIRE(!notExists);

        // ensure another still exists
        auto values2 = co_await storage2::readSome(storage, RANGES::iota_view<int, int>(1, 11));
        for (auto&& value : values2)
        {
            BOOST_REQUIRE(value);
        }

        // ensure all
        auto range = co_await storage2::range(storage);
        BOOST_CHECK_EQUAL(range.size(), 10);
    }());
}

BOOST_AUTO_TEST_CASE(logicalDeletion)
{
    task::syncWait([]() -> task::Task<void> {
        MemoryStorage<int, storage::Entry, Attribute(ORDERED | LOGICAL_DELETION)> storage;

        // Write 100 items
        co_await storage2::writeSome(storage, RANGES::iota_view<int, int>(0, 100),
            RANGES::iota_view<int, int>(0, 100) | RANGES::views::transform([](int num) {
                storage::Entry entry;
                entry.set(fmt::format("Item: {}", num));
                return entry;
            }));

        // Delete half of items
        co_await storage2::removeSome(
            storage, RANGES::iota_view<int, int>(0, 50) |
                         RANGES::views::transform([](int num) { return num * 2; }));

        // Query and check if deleted items
        int i = 0;
        auto values = co_await storage2::range(storage);
        for (auto&& [i, tuple] : RANGES::views::enumerate(values))
        {
            auto [key, value] = tuple;
            if ((i + 2) % 2 == 0)
            {
                BOOST_CHECK(!value);
            }
            else
            {
                BOOST_CHECK(value);
                BOOST_CHECK_EQUAL(value->get(), fmt::format("Item: {}", i));
            }
        }

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(range)
{
    task::syncWait([]() -> task::Task<void> {
        constexpr static int count = 100;

        MemoryStorage<std::tuple<std::string, std::string>, storage::Entry, ORDERED> storage;
        co_await storage2::writeSome(storage,
            RANGES::iota_view<int, int>(0, count) | RANGES::views::transform([](auto num) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(num));
            }),
            RANGES::iota_view<int, int>(0, count) | RANGES::views::transform([](auto num) {
                storage::Entry entry;
                entry.set("Hello world!" + boost::lexical_cast<std::string>(num));
                return entry;
            }));

        auto readRange = co_await storage2::readSome(
            storage, RANGES::iota_view<int, int>(0, 10) | RANGES::views::transform([](int i) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(i));
            }));
        for (auto&& [value, num] : RANGES::views::zip(readRange, RANGES::iota_view<size_t>(0)))
        {
            BOOST_CHECK(value);
            BOOST_CHECK_EQUAL(value->get(), "Hello world!" + boost::lexical_cast<std::string>(num));
            BOOST_CHECK_LT(num, 10);
        }

        auto seekRange = co_await storage2::range(storage);
        BOOST_CHECK_EQUAL(RANGES::size(seekRange), 100);
        for (auto&& [kv, num] : RANGES::views::zip(seekRange, RANGES::iota_view<size_t>(0)))
        {
            auto& [key, value] = kv;
            BOOST_CHECK(key);
            BOOST_CHECK(value);

            auto& [tableName, keyName] = *key;
            // BOOST_CHECK_EQUAL(tableName, "table");
            // BOOST_CHECK_EQUAL(keyName, "key:" + boost::lexical_cast<std::string>(num));
            // BOOST_CHECK_EQUAL(value->get(), "Hello world!" +
            // boost::lexical_cast<std::string>(num));
            BOOST_CHECK_LT(num, 100);
        }
    }());
}

BOOST_AUTO_TEST_CASE(merge)
{
    task::syncWait([]() -> task::Task<void> {
        MemoryStorage<int, int, ORDERED> storage1;
        MemoryStorage<int, int, ORDERED> storage2;

        co_await storage2::writeSome(
            storage1, RANGES::iota_view<int, int>(0, 10), RANGES::repeat_view<int>(100));
        co_await storage2::writeSome(
            storage2, RANGES::iota_view<int, int>(9, 19), RANGES::repeat_view<int>(200));

        co_await storage2::merge(storage1, storage2);
        auto values = co_await storage2::range(storage1);
        BOOST_CHECK_EQUAL(RANGES::size(values), 19);
        for (auto&& [i, tuple] : RANGES::views::enumerate(values))
        {
            auto [key, value] = tuple;
            auto keyNum = *key;
            auto valueNum = *value;

            BOOST_CHECK_EQUAL(keyNum, i);
            if (keyNum > 8)
            {
                BOOST_CHECK_EQUAL(valueNum, 200);
            }
            else
            {
                BOOST_CHECK_EQUAL(valueNum, 100);
            }
        }
    }());
}

BOOST_AUTO_TEST_SUITE_END()