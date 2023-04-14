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

class MockStorage
{
public:
    using Key = std::tuple<std::string, std::string>;
    using Value = storage::Entry;

    struct SeekIterator
    {
        using Key = std::tuple<std::string_view, std::string_view>;
        using Value = const storage::Entry*;

        static task::Task<bool> hasValue() { co_return true; }
        task::Task<bool> next() { co_return (++m_it) != m_end; }
        task::Task<Key> key() const { co_return m_it->first; }
        task::Task<Value> value() const { co_return &(m_it->second); }

        std::map<std::tuple<std::string, std::string>, storage::Entry>::iterator m_it;
        std::map<std::tuple<std::string, std::string>, storage::Entry>::iterator m_end;
    };

    struct ReadIterator
    {
        using Key = std::tuple<std::string_view, std::string_view>;
        using Value = const storage::Entry*;

        task::Task<bool> hasValue() const { co_return *m_listIt != m_end; }
        task::Task<bool> next() { co_return (++m_listIt) != m_valueIts.end(); }
        task::Task<Key> key() const { co_return (*m_listIt)->first; }
        task::Task<Value> value() const { co_return std::addressof(((*m_listIt)->second)); }

        std::vector<std::map<std::tuple<std::string, std::string>,
            storage::Entry>::iterator>::iterator m_listIt;
        std::vector<std::map<std::tuple<std::string, std::string>, storage::Entry>::iterator>
            m_valueIts;
        std::map<std::tuple<std::string, std::string>, storage::Entry>::iterator m_end;
    };

    task::Task<ReadIterator> read(RANGES::range auto const& keys)
    {
        ReadIterator readIt;
        readIt.m_valueIts.reserve(RANGES::size(keys));

        for (auto&& keyPair : keys)
        {
            auto const& [tableName, key] = keyPair;
            auto it = m_values.find(keyPair);
            if (it != m_values.end())
            {
                readIt.m_valueIts.emplace_back(it);
            }
            else
            {
                readIt.m_valueIts.emplace_back(m_values.end());
            }
        }
        readIt.m_listIt = readIt.m_valueIts.begin();
        --readIt.m_listIt;
        readIt.m_end = m_values.end();

        co_return readIt;
    }

    task::Task<SeekIterator> seek(std::tuple<std::string_view, std::string_view> key)
    {
        auto it = m_values.lower_bound(key);
        co_return SeekIterator{--it, m_values.end()};
    }

    task::Task<void> write(RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
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

    task::Task<void> remove(RANGES::input_range auto const& keys)
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
            co_await mock.write(
                RANGES::iota_view<int, int>(0, 100) | RANGES::views::transform([](auto num) {
                    return std::tuple<std::string, std::string>(
                        "table", "key:" + boost::lexical_cast<std::string>(num));
                }),
                RANGES::iota_view<int, int>(0, 100) | RANGES::views::transform([](auto num) {
                    storage::Entry entry;
                    entry.set("Hello world! " + boost::lexical_cast<std::string>(num));
                    return entry;
                })));

        auto it = co_await mock.seek(std::tuple{"table", "key:20"});
        int count = 0;
        while (co_await it.next())
        {
            auto const& [table, key] = co_await it.key();
            BOOST_CHECK_EQUAL(table, "table");
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
        co_await mock.write(storage2::single(std::tuple<std::string, std::string>("table", "key")),
            storage2::single(std::move(newEntry)));

        storage::Entry newEntry2;
        newEntry2.set("fine!");
        co_await mock.write(storage2::single(std::tuple<std::string, std::string>("table", "key2")),
            storage2::single(std::move(newEntry2)));

        auto result = co_await mock.read(
            storage2::single(std::tuple<std::string_view, std::string_view>("table", "key2")));

        co_await result.next();
        BOOST_REQUIRE(co_await result.hasValue());
        BOOST_CHECK_EQUAL((co_await result.value())->get(), "fine!");
    }());
}

BOOST_AUTO_TEST_CASE(update)
{
    task::syncWait([]() -> task::Task<void> {
        storage::Entry newEntry;
        newEntry.set("Hello world!");

        MockStorage mock;
        co_await mock.write(storage2::single(std::tuple<std::string, std::string>("table", "key")),
            storage2::single(std::move(newEntry)));

        storage::Entry newEntry2;
        newEntry2.set("fine!");
        co_await mock.write(storage2::single(std::tuple<std::string, std::string>("table", "key")),
            storage2::single(std::move(newEntry2)));

        auto result = co_await mock.read(
            storage2::single(std::tuple<std::string_view, std::string_view>("table", "key")));

        co_await result.next();
        BOOST_REQUIRE(co_await result.hasValue());
        BOOST_CHECK_EQUAL((co_await result.value())->get(), "fine!");
    }());
}

BOOST_AUTO_TEST_CASE(remove)
{
    task::syncWait([]() -> task::Task<void> {
        storage::Entry newEntry;
        newEntry.set("Hello world!");

        MockStorage mock;
        co_await mock.write(storage2::single(std::tuple<std::string, std::string>("table", "key")),
            storage2::single((std::move(newEntry))));

        co_await mock.remove(
            storage2::single((std::tuple<std::string_view, std::string_view>("table", "key"))));

        auto result = co_await mock.read(
            storage2::single((std::tuple<std::string_view, std::string_view>("table", "key"))));

        co_await result.next();
        BOOST_REQUIRE(!co_await result.hasValue());
    }());
}

BOOST_AUTO_TEST_SUITE_END()
