#include "storage/Entry.h"
#include <bcos-concepts/Basic.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <range/v3/view/single.hpp>
#include <range/v3/view/transform.hpp>
#include <type_traits>

using namespace bcos;
using namespace bcos::storage2;

class MockIterator
{
public:
    using Key = std::tuple<std::string_view, std::string_view>;
    using Value = const storage::Entry*;

    task::Task<bool> next() { co_return (++m_it) != m_end; }
    task::Task<Key> key() const { co_return m_it->first; }
    task::Task<Value> value() const { co_return &(m_it->second); }

    std::map<std::tuple<std::string, std::string>, storage::Entry>::iterator m_it;
    std::map<std::tuple<std::string, std::string>, storage::Entry>::iterator m_end;
};

class MockStorage
  : public StorageBase<MockStorage, std::tuple<std::string, std::string>, storage::Entry>
{
public:
    task::Task<void> impl_read(
        RANGES::range auto const& keys, RANGES::output_range<std::optional<Value>> auto&& outputs)
    {
        for (auto&& [keyPair, output] : RANGES::zip_view(keys, outputs))
        {
            auto const& [tableName, key] = keyPair;
            auto it = m_values.find(keyPair);
            if (it != m_values.end())
            {
                output.emplace(it->second);
            }
        }

        co_return;
    }

    template <std::same_as<MockIterator> Iterator>
    task::Task<Iterator> impl_seek(std::tuple<std::string_view, std::string_view> key)
    {
        auto it = m_values.lower_bound(key);
        co_return Iterator{--it, m_values.end()};
    }

    task::Task<void> impl_write(RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
    {
        for (auto&& [key, entry] : RANGES::zip_view(keys, values))
        {
            auto it = m_values.find(key);
            if (it == m_values.end())
            {
                m_values.insert(std::make_pair(key, entry));
            }
            else
            {
                it->second = entry;
            }
        }

        co_return;
    }

    task::Task<void> impl_remove(RANGES::input_range auto const& keys)
    {
        for (auto&& key : keys)
        {
            auto it = m_values.find(key);
            if (it != m_values.end())
            {
                m_values.erase(it);
            }
        }

        co_return;
    }

    std::map<std::tuple<std::string, std::string>, storage::Entry, std::less<>> m_values;
};

class TestStorage2Fixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestStorage2, TestStorage2Fixture)

BOOST_AUTO_TEST_CASE(seek)
{
    task::syncWait([]() -> task::Task<void> {
        MockStorage mock;
        BOOST_REQUIRE_NO_THROW(
            // Write 100 keyvalues
            co_await mock.write(RANGES::iota_view(0, 100) | RANGES::views::transform([](auto num) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(num));
            }),
                RANGES::iota_view(0, 100) | RANGES::views::transform([](auto num) {
                    storage::Entry entry;
                    entry.set("Hello world! " + boost::lexical_cast<std::string>(num));
                    return entry;
                })));

        auto it = co_await mock.seek<MockIterator>(std::tuple{"table", "key:20"});
        int count = 0;
        while (co_await it.next())
        {
            auto const& [table, key] = co_await it.key();
            std::cout << "Table:" << table << " Key: " << key
                      << " Value: " << (co_await it.value())->get() << std::endl;
            ++count;
        }

        BOOST_CHECK_EQUAL(count, 87);
    }());
}

BOOST_AUTO_TEST_CASE(insert)
{
    task::syncWait([]() -> task::Task<void> {
        storage::Entry newEntry;
        newEntry.set("Hello world!");

        MockStorage mock;
        co_await mock.writeOne(
            std::tuple<std::string, std::string>("table", "key"), std::move(newEntry));

        storage::Entry newEntry2;
        newEntry2.set("fine!");
        co_await mock.writeOne(
            std::tuple<std::string, std::string>("table", "key2"), std::move(newEntry2));

        auto result =
            co_await mock.readOne(std::tuple<std::string_view, std::string_view>("table", "key2"));

        BOOST_REQUIRE(result);
        BOOST_CHECK_EQUAL(result->get(), "fine!");
    }());
}

BOOST_AUTO_TEST_CASE(update)
{
    task::syncWait([]() -> task::Task<void> {
        storage::Entry newEntry;
        newEntry.set("Hello world!");

        MockStorage mock;
        co_await mock.writeOne(
            std::tuple<std::string, std::string>("table", "key"), std::move(newEntry));

        storage::Entry newEntry2;
        newEntry2.set("fine!");
        co_await mock.writeOne(
            std::tuple<std::string, std::string>("table", "key"), std::move(newEntry2));

        auto result =
            co_await mock.readOne(std::tuple<std::string_view, std::string_view>("table", "key"));

        BOOST_REQUIRE(result);
        BOOST_CHECK_EQUAL(result->get(), "fine!");
    }());
}

BOOST_AUTO_TEST_CASE(remove)
{
    task::syncWait([]() -> task::Task<void> {
        storage::Entry newEntry;
        newEntry.set("Hello world!");

        MockStorage mock;
        co_await mock.writeOne(
            std::tuple<std::string, std::string>("table", "key"), std::move(newEntry));

        co_await mock.removeOne(std::tuple<std::string_view, std::string_view>("table", "key"));

        auto result =
            co_await mock.readOne(std::tuple<std::string_view, std::string_view>("table", "key"));

        BOOST_REQUIRE(!result);
    }());
}

BOOST_AUTO_TEST_SUITE_END()
