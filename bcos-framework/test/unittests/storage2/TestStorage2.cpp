#include <bcos-concepts/Basic.h>
#include <bcos-framework/storage2/Storage2.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <range/v3/view/single.hpp>
#include <range/v3/view/transform.hpp>

using namespace bcos;
using namespace bcos::storage2;

class MockStorage : public bcos::storage2::Storage2Base<MockStorage>
{
public:
    task::Task<void> impl_getRows(
        std::string_view tableName, InputKeys auto const& keys, OutputEntries auto& out)
    {
        std::cout << "Get entry" << std::endl;

        bcos::concepts::resizeTo(out, RANGES::size(keys));
        std::cout << "Input size: " << RANGES::size(keys) << " output size: " << RANGES::size(out)
                  << std::endl;
        for (const auto& [key, entry] : RANGES::zip_view(keys, out))
        {
            auto keyPair = std::make_tuple(std::string(tableName), std::string(key));
            auto it = m_values.find(keyPair);
            if (it != m_values.end())
            {
                std::cout << "Found!" << std::endl;
                entry.emplace(it->second);
            }
        }

        co_return;
    }

    task::Task<void> impl_setRows(
        std::string_view tableName, InputKeys auto const& keys, InputEntries auto const& entries)
    {
        std::cout << "Set entry" << std::endl;

        for (const auto& [key, entry] : RANGES::zip_view(keys, entries))
        {
            auto keyPair = std::make_tuple(std::string(tableName), std::string(key));
            m_values.insert(std::make_pair(keyPair, entry));
        }

        co_return;
    }

    std::map<std::tuple<std::string, std::string>, storage::Entry> m_values;
};

class TestStorage2Fixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestStorage2, TestStorage2Fixture)

BOOST_AUTO_TEST_CASE(getSet)
{
    std::string content = "Hello world!";

    storage::Entry newEntry;
    newEntry.set(content);

    MockStorage mock;
    task::syncWait(mock.setRow("tableName!", "key!", newEntry));

    auto gotEntry = task::syncWait(mock.getRow("tableName!", "key!"));
    BOOST_CHECK(gotEntry);
    BOOST_CHECK_EQUAL(gotEntry->get(), content);

    gotEntry = task::syncWait(mock.getRow("tableName!", "key2!"));
    BOOST_CHECK(!gotEntry);

    gotEntry = task::syncWait(mock.getRow("tableName2!", "key!"));
    BOOST_CHECK(!gotEntry);
}

BOOST_AUTO_TEST_CASE(sigleView)
{
    int i = 0;
    RANGES::single_view<int*> view(&i);

    auto mutableView = view | RANGES::views::transform([](int* number) -> int& { return *number; });

    mutableView[0] = 2;
    BOOST_CHECK_EQUAL(i, 2);
}

BOOST_AUTO_TEST_SUITE_END()