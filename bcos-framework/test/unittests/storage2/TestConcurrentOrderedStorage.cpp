#include "bcos-framework/storage/Entry.h"
#include <bcos-framework/storage2/ConcurrentStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Wait.h>
#include <boost/function.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <range/v3/view/repeat.hpp>
#include <range/v3/view/transform.hpp>

using namespace bcos;
using namespace bcos::storage2::concurrent_storage;

struct Storage2ImplFixture
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
BOOST_FIXTURE_TEST_SUITE(TestStorage2, Storage2ImplFixture)

BOOST_AUTO_TEST_CASE(writeReadModifyRemove)
{
    task::syncWait([]() -> task::Task<void> {
        constexpr static int count = 100;

        ConcurrentStorage<std::tuple<std::string, std::string>, storage::Entry, true> storage;
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
            storage2::single(std::tuple<std::string, std::string>("table", "key:5")),
            storage2::single(std::move(newEntry)));

        auto result = co_await storage.read(
            storage2::single(std::tuple<std::string, std::string>("table", "key:5")));
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
        ConcurrentStorage<int, storage::Entry, true, true, true> storage(1);
        storage.setMaxCapacity(1000);

        // write 10 100byte value
        storage::Entry entry;
        entry.set(std::string('a', 10));
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
        co_await storage.write(storage2::single(10), storage2::single(entry));
        auto notExists = co_await storage.read(storage2::single(0));
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

BOOST_AUTO_TEST_SUITE_END()