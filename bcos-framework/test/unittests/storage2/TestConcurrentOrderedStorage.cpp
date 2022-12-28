#include "bcos-framework/storage/Entry.h"
#include <bcos-framework/storage2/ConcurrentOrderedStorage.h>
#include <bcos-task/Wait.h>
#include <boost/function.hpp>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <range/v3/view/transform.hpp>

using namespace bcos;
using namespace bcos::storage2::concurrent_ordered_storage;

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

BOOST_AUTO_TEST_CASE(writeReadRemove)
{
    task::syncWait([]() -> task::Task<void> {
        constexpr static int count = 100;

        ConcurrentOrderedStorage<false, std::tuple<std::string, std::string>, storage::Entry>
            storage;
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
            BOOST_CHECK_EQUAL(std::get<0>(key.get()), std::get<0>(exceptKey));
            BOOST_CHECK_EQUAL(std::get<1>(key.get()), std::get<1>(exceptKey));

            BOOST_CHECK_EQUAL((co_await it.value()).get().get(),
                "Hello world!" + boost::lexical_cast<std::string>(i));
            ++i;
        }
        BOOST_CHECK_EQUAL(i, count);
        it.release();

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
                BOOST_CHECK_EQUAL(std::get<0>(key.get()), std::get<0>(exceptKey));
                BOOST_CHECK_EQUAL(std::get<1>(key.get()), std::get<1>(exceptKey));

                BOOST_CHECK_EQUAL((co_await it.value()).get().get(),
                    "Hello world!" + boost::lexical_cast<std::string>(i));
            }
            ++i;
        }
        BOOST_CHECK_EQUAL(i, count);

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()