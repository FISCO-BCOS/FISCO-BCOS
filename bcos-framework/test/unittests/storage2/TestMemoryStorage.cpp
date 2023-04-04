#include "bcos-framework/storage/Entry.h"
#include "storage2/StringPool.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
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

BOOST_AUTO_TEST_CASE(writeReadModifyRemove)
{
    task::syncWait([]() -> task::Task<void> {
        constexpr static int count = 100;

        MemoryStorage<std::tuple<std::string, std::string>, storage::Entry, ORDERED> storage;
        co_await storage.write(
            RANGES::iota_view<int, int>(0, count) | RANGES::views::transform([](auto num) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(num));
            }),
            RANGES::iota_view<int, int>(0, count) | RANGES::views::transform([](auto num) {
                storage::Entry entry;
                entry.set("Hello world!" + boost::lexical_cast<std::string>(num));
                return entry;
            }));

        auto it = co_await storage.read(
            RANGES::iota_view<int, int>(0, count) | RANGES::views::transform([](int i) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(i));
            }));

        int i = 0;
        while (co_await it.next())
        {
            BOOST_REQUIRE(co_await it.hasValue());
            auto exceptKey = std::tuple<std::string, std::string>(
                "table", "key:" + boost::lexical_cast<std::string>(i));
            auto key = co_await it.key();
            BOOST_CHECK_EQUAL(std::get<0>(key), std::get<0>(exceptKey));
            BOOST_CHECK_EQUAL(std::get<1>(key), std::get<1>(exceptKey));

            BOOST_CHECK_EQUAL(
                (co_await it.value()).get(), "Hello world!" + boost::lexical_cast<std::string>(i));
            ++i;
        }
        BOOST_CHECK_EQUAL(i, count);
        it.release();

        // modify
        storage::Entry newEntry;
        newEntry.set("Hello map!");
        co_await storage.write(
            storage2::singleView(std::tuple<std::string, std::string>("table", "key:5")),
            storage2::singleView(std::move(newEntry)));

        auto result = co_await storage.read(
            storage2::singleView(std::tuple<std::string, std::string>("table", "key:5")));
        co_await result.next();
        BOOST_REQUIRE(co_await result.hasValue());
        BOOST_CHECK_EQUAL((co_await result.value()).get(), "Hello map!");
        result.release();

        BOOST_CHECK_NO_THROW(co_await storage.remove(
            RANGES::iota_view<int, int>(10, 20) | RANGES::views::transform([](int i) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(i));
            })));

        // Check if records had erased
        it = co_await storage.read(
            RANGES::iota_view<int, int>(0, count) | RANGES::views::transform([](int i) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(i));
            }));

        i = 0;
        while (co_await it.next())
        {
            if (i >= 10 && i < 20)
            {
                BOOST_REQUIRE(!(co_await it.hasValue()));
            }
            else
            {
                BOOST_REQUIRE(co_await it.hasValue());
                auto exceptKey = std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(i));

                auto key = co_await it.key();
                BOOST_CHECK_EQUAL(std::get<0>(key), std::get<0>(exceptKey));
                BOOST_CHECK_EQUAL(std::get<1>(key), std::get<1>(exceptKey));

                if (i == 5)
                {
                    BOOST_CHECK_EQUAL((co_await it.value()).get(), "Hello map!");
                }
                else
                {
                    BOOST_CHECK_EQUAL((co_await it.value()).get(),
                        "Hello world!" + boost::lexical_cast<std::string>(i));
                }
            }
            ++i;
        }
        BOOST_CHECK_EQUAL(i, count);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(mru)
{
    task::syncWait([]() -> task::Task<void> {
        MemoryStorage<int, storage::Entry, Attribute(ORDERED | CONCURRENT | MRU), std::hash<int>>
            storage(1);
        storage.setMaxCapacity(1000);

        // write 10 100byte value
        storage::Entry entry;
        entry.set(std::string(100, 'a'));
        co_await storage.write(RANGES::iota_view<int, int>(0, 10), RANGES::repeat_view(entry));

        // ensure 10 are useable
        auto it = co_await storage.read(RANGES::iota_view<int, int>(0, 10));
        int i = 0;
        while (co_await it.next())
        {
            BOOST_REQUIRE(co_await it.hasValue());
            BOOST_CHECK_EQUAL(co_await it.key(), i++);
        }
        BOOST_CHECK_EQUAL(i, 10);
        it.release();

        // ensure 0 is erased
        co_await storage.write(storage2::singleView(10), storage2::singleView(entry));
        auto notExists = co_await storage.read(storage2::singleView(0));
        co_await notExists.next();
        BOOST_REQUIRE(!co_await notExists.hasValue());
        notExists.release();

        // ensure another still exists
        auto it2 = co_await storage.read(RANGES::iota_view<int, int>(1, 11));
        i = 1;
        while (co_await it2.next())
        {
            BOOST_REQUIRE(co_await it2.hasValue());
            BOOST_CHECK_EQUAL(co_await it2.key(), i++);
        }
        BOOST_CHECK_EQUAL(i, 11);
        it2.release();

        // ensure all
        auto seekIt = co_await storage.seek(1);
        i = 0;
        while (co_await seekIt.next())
        {
            auto key = co_await seekIt.key();
            BOOST_CHECK_EQUAL(key, i + 1);
            ++i;
        }
        BOOST_CHECK_EQUAL(i, 10);
    }());
}

BOOST_AUTO_TEST_CASE(logicalDeletion)
{
    task::syncWait([]() -> task::Task<void> {
        MemoryStorage<int, storage::Entry, Attribute(ORDERED | LOGICAL_DELETION)> storage;

        // Write 100 items
        co_await storage.write(RANGES::iota_view<int, int>(0, 100),
            RANGES::iota_view<int, int>(0, 100) | RANGES::views::transform([](int num) {
                storage::Entry entry;
                entry.set(fmt::format("Item: {}", num));
                return entry;
            }));

        // Delete half of items
        co_await storage.remove(RANGES::iota_view<int, int>(0, 50) |
                                RANGES::views::transform([](int num) { return num * 2; }));

        // Query and check if deleted items
        int i = 0;
        auto it = co_await storage.seek(0);
        while (co_await it.next())
        {
            if ((i + 2) % 2 == 0)
            {
                BOOST_CHECK(!co_await it.hasValue());
            }
            else
            {
                BOOST_CHECK(co_await it.hasValue());
                BOOST_CHECK_EQUAL(co_await it.key(), i);
                auto const& entry = co_await it.value();
                BOOST_CHECK_EQUAL(entry.get(), fmt::format("Item: {}", i));
            }
            ++i;
        }

        BOOST_CHECK_EQUAL(i, 100);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(range)
{
    task::syncWait([]() -> task::Task<void> {
        constexpr static int count = 100;

        MemoryStorage<std::tuple<std::string, std::string>, storage::Entry, ORDERED> storage;
        co_await storage.write(
            RANGES::iota_view<int, int>(0, count) | RANGES::views::transform([](auto num) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(num));
            }),
            RANGES::iota_view<int, int>(0, count) | RANGES::views::transform([](auto num) {
                storage::Entry entry;
                entry.set("Hello world!" + boost::lexical_cast<std::string>(num));
                return entry;
            }));

        auto readIt = co_await storage.read(
            RANGES::iota_view<int, int>(0, 10) | RANGES::views::transform([](int i) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(i));
            }));
        auto readRange = readIt.range();
        for (auto&& [kv, num] : RANGES::views::zip(readRange, RANGES::iota_view<size_t>(0)))
        {
            auto& [key, value] = kv;
            BOOST_CHECK(key);
            BOOST_CHECK(value);

            auto& [tableName, keyName] = *key;
            BOOST_CHECK_EQUAL(tableName, "table");
            BOOST_CHECK_EQUAL(keyName, "key:" + boost::lexical_cast<std::string>(num));
            BOOST_CHECK_EQUAL(value->get(), "Hello world!" + boost::lexical_cast<std::string>(num));
            BOOST_CHECK_LT(num, 10);
        }

        auto seekIt = co_await storage.seek(storage2::STORAGE_BEGIN);
        auto seekRange = seekIt.range();

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

        storage1.write(RANGES::iota_view<int, int>(0, 10), RANGES::repeat_view<int>(100));
        storage2.write(RANGES::iota_view<int, int>(9, 19), RANGES::repeat_view<int>(200));

        co_await storage1.merge(storage2);
        auto it = co_await storage1.seek(bcos::storage2::STORAGE_BEGIN);
        int i = 0;
        while (co_await it.next())
        {
            auto keyNum = co_await it.key();
            auto valueNum = co_await it.value();

            BOOST_CHECK_EQUAL(keyNum, i);
            if (keyNum > 8)
            {
                BOOST_CHECK_EQUAL(valueNum, 200);
            }
            else
            {
                BOOST_CHECK_EQUAL(valueNum, 100);
            }
            ++i;
        }
        BOOST_CHECK_EQUAL(i, 19);
    }());
}

BOOST_AUTO_TEST_SUITE_END()