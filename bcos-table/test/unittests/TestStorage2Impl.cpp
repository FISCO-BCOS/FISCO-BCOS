#include "../../src/Storage2Impl.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/Storage2.h"
#include <bcos-task/Wait.h>
#include <boost/function.hpp>
#include <boost/test/unit_test.hpp>
#include <range/v3/view/transform.hpp>

using namespace bcos;
struct Storage2ImplFixture
{
    Storage2ImplFixture() {}

    ~Storage2ImplFixture() {}
};

BOOST_FIXTURE_TEST_SUITE(TestStorage2, Storage2ImplFixture)

BOOST_AUTO_TEST_CASE(set)
{
    task::syncWait([]() -> task::Task<void> {
        storage2::ConcurrentStorage<false, storage::Entry> storage;
        std::string_view table = "table";
        for (auto i : RANGES::iota_view<int, int>(0, 100))
        {
            auto key = "key:" + boost::lexical_cast<std::string>(i);

            storage::Entry entry;
            entry.set("Hello world!" + boost::lexical_cast<std::string>(i));
            co_await storage.setRow("table", key, std::move(entry));
        }

        std::vector<storage2::OptionalEntry> results;
        co_await storage.getRows(table,
            RANGES::iota_view(0, 100) | RANGES::views::transform([](int i) {
                return "key:" + boost::lexical_cast<std::string>(i);
            }),
            results);

        BOOST_CHECK_EQUAL(results.size(), 100);
        for (auto [i, entry] : RANGES::zip_view(RANGES::iota_view(0U, results.size()), results))
        {
            BOOST_REQUIRE(entry);
            auto value = entry->get();
            BOOST_CHECK_EQUAL(value, "Hello world!" + boost::lexical_cast<std::string>(i));
        }

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()