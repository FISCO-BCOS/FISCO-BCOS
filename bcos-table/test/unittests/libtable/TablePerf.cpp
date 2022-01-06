#include "Hash.h"
#include "bcos-table/StateStorage.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <future>

namespace bcos::test
{
using namespace bcos::storage;

struct TablePerfFixture
{
    TablePerfFixture()
    {
        // auto hashImpl = std::make_shared<bcos::crypto::Header256Hash>();
        auto memoryStorage = std::make_shared<StateStorage>(nullptr);
        BOOST_TEST(memoryStorage != nullptr);
        tableFactory = std::make_shared<StateStorage>(memoryStorage);
        BOOST_TEST(tableFactory != nullptr);
    }

    std::vector<std::tuple<std::string, Entry>> createTestData(Table table)
    {
        std::vector<std::tuple<std::string, Entry>> entries;
        entries.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            auto entry = table.newEntry();
            entry.setField(0, "value1");

            entries.emplace_back("key_" + boost::lexical_cast<std::string>(i), std::move(entry));
        }

        return entries;
    }

    std::shared_ptr<StateStorage> tableFactory;
    size_t count = 100 * 1000;
};

BOOST_FIXTURE_TEST_SUITE(TablePerf, TablePerfFixture)

BOOST_AUTO_TEST_CASE(syncGet)
{
    tableFactory->createTable("test_table", "field1");
    auto table = tableFactory->openTable("test_table");

    auto entries = createTestData(*table);

    for (auto& [key, entry] : entries)
    {
        table->setRow(key, entry);
    }

    auto now = bcos::utcSteadyTime();
    for (size_t i = 0; i < count; ++i)
    {
        std::string key = "key_" + boost::lexical_cast<std::string>(i);
        auto entry = table->getRow(key);

        BOOST_CHECK_EQUAL(entry->getField(0), "value1");
    }

    std::cout << "sync cost: " << bcos::utcSteadyTime() - now << std::endl;
}

BOOST_AUTO_TEST_CASE(asyncGet)
{
    tableFactory->createTable("test_table", "field1,field2,field3");
    auto table = tableFactory->openTable("test_table");

    auto entries = createTestData(*table);

    for (auto& [key, entry] : entries)
    {
        table->setRow(key, entry);
    }

    auto total = count;

    auto now = bcos::utcSteadyTime();
    std::promise<bool> finished;
    std::atomic<size_t> done(0);
    for (size_t i = 0; i < count; ++i)
    {
        std::string key = "key_" + boost::lexical_cast<std::string>(i);
        table->asyncGetRow(key, [&total, &finished, &done](auto&&, auto&& entry) {
            BOOST_CHECK_EQUAL(entry->getField(0), "value1");

            auto current = done.fetch_add(1);
            if (current + 1 >= total)
            {
                finished.set_value(true);
            }
        });
    }
    finished.get_future().get();

    std::cout << "async cost: " << bcos::utcSteadyTime() - now << std::endl;
}

BOOST_AUTO_TEST_CASE(asyncToSyncGet)
{
    tableFactory->createTable("test_table", "field1,field2,field3");
    auto table = tableFactory->openTable("test_table");

    auto entries = createTestData(*table);

    for (auto& [key, entry] : entries)
    {
        table->setRow(key, entry);
    }

    auto now = bcos::utcSteadyTime();
    for (size_t i = 0; i < count; ++i)
    {
        std::string key = "key_" + boost::lexical_cast<std::string>(i);
        std::promise<bool> finished;
        table->asyncGetRow(key, [&finished](auto&&, auto&& entry) {
            BOOST_CHECK_EQUAL(entry->getField(0), "value1");
            finished.set_value(true);
        });
        finished.get_future().get();
    }

    std::cout << "asyncToSync cost: " << bcos::utcSteadyTime() - now << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
