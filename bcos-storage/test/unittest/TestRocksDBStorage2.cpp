#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-storage/RocksDBStorage2.h>
#include <bcos-storage/StateKVResolver.h>
#include <fmt/format.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <string_view>

using namespace bcos;
using namespace bcos::storage2::rocksdb;
using namespace bcos::executor_v1;
using namespace std::string_view_literals;

struct TestRocksDBStorage2Fixture
{
    TestRocksDBStorage2Fixture()
    {
        constexpr static std::string_view path = "./rocksdbtestdb";

        ::rocksdb::Options options;
        options.create_if_missing = true;

        rocksdb::DB* db;
        rocksdb::Status s = rocksdb::DB::Open(options, std::string(path), &db);
        BOOST_CHECK_EQUAL(s.ok(), true);

        originRocksDB.reset(db);
    }

    ~TestRocksDBStorage2Fixture() { boost::filesystem::remove_all("./rocksdbtestdb"); }
    std::unique_ptr<rocksdb::DB> originRocksDB;
};

BOOST_FIXTURE_TEST_SUITE(TestRocksDBStorage2, TestRocksDBStorage2Fixture)

BOOST_AUTO_TEST_CASE(kvResolver)
{
    std::string_view mergedKey = "test_table!!!:key100";
    auto decodedKey = bcos::storage2::rocksdb::StateKeyResolver::decode(mergedKey);

    StateKey key("test_table!!!"sv, "key100"sv);
    BOOST_CHECK(key == decodedKey);
}

BOOST_AUTO_TEST_CASE(readWriteRemoveSeek)
{
    task::syncWait([this]() -> task::Task<void> {
        RocksDBStorage2<StateKey, StateValue, StateKeyResolver,
            bcos::storage2::rocksdb::StateValueResolver>
            rocksDB(*originRocksDB, StateKeyResolver{}, StateValueResolver{});

        auto notExistsValue =
            co_await storage2::readOne(rocksDB, StateKey{"Non exists table"sv, "Non exists key"sv});
        BOOST_REQUIRE(!notExistsValue);

        auto keys = ::ranges::views::iota(0, 100) | ::ranges::views::transform([](int num) {
            auto tableName = fmt::format("Table~{}", num % 10);
            auto key = fmt::format("Key~{}", num);
            auto stateKey = StateKey{std::string_view(tableName), std::string_view(key)};
            return stateKey;
        });
        auto values = ::ranges::views::iota(0, 100) | ::ranges::views::transform([](int num) {
            storage::Entry entry;
            entry.set(fmt::format("Entry value is: i am a value!!!!!!! {}", num));
            return entry;
        });

        BOOST_CHECK_NO_THROW(
            co_await storage2::writeSome(rocksDB, ::ranges::views::zip(keys, values)));

        auto queryKeys = ::ranges::views::iota(0, 150) | ::ranges::views::transform([](int num) {
            auto tableName = fmt::format("Table~{}", num % 10);
            auto key = fmt::format("Key~{}", num);
            auto stateKey = StateKey{std::string_view(tableName), std::string_view(key)};
            return stateKey;
        });
        auto gotValues = co_await storage2::readSome(rocksDB, queryKeys);
        for (auto&& [i, value] : ::ranges::views::enumerate(gotValues))
        {
            if (i < 100)
            {
                BOOST_CHECK(value);
                BOOST_CHECK_EQUAL(
                    value->get(), fmt::format("Entry value is: i am a value!!!!!!! {}", i));
            }
            else
            {
                BOOST_CHECK(!value);
            }
        }

        // Remove some
        auto removeKeys = ::ranges::views::iota(50, 70) | ::ranges::views::transform([](int num) {
            auto tableName = fmt::format("Table~{}", num % 10);
            auto key = fmt::format("Key~{}", num);
            StateKey stateKey{std::string_view(tableName), std::string_view(key)};
            return stateKey;
        });
        co_await storage2::removeSome(rocksDB, removeKeys);

        auto gotValues2 = co_await storage2::readSome(rocksDB, queryKeys);
        for (auto&& [i, value] : ::ranges::views::enumerate(gotValues2))
        {
            if (i >= 50 && i < 70)
            {
                BOOST_CHECK(!value);
            }
            else if (i < 100)
            {
                BOOST_CHECK(value);
                BOOST_CHECK_EQUAL(
                    value->get(), fmt::format("Entry value is: i am a value!!!!!!! {}", i));
            }
            else
            {
                BOOST_CHECK(!value);
            }
        }

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(merge)
{
    task::syncWait([this]() -> task::Task<void> {
        storage2::memory_storage::MemoryStorage<StateKey, StateValue,
            storage2::memory_storage::ORDERED>
            memoryStorage;

        auto keys = ::ranges::views::iota(0, 100) | ::ranges::views::transform([](int num) {
            auto tableName = fmt::format("Table~{}", num % 10);
            auto key = fmt::format("Key~{}", num);
            auto stateKey = StateKey{std::string_view(tableName), std::string_view(key)};
            return stateKey;
        });
        auto values = ::ranges::views::iota(0, 100) | ::ranges::views::transform([](int num) {
            storage::Entry entry;
            entry.set(fmt::format("Entry value is: i am a value!!!!!!! {}", num));
            return entry;
        });
        co_await storage2::writeSome(memoryStorage, ::ranges::views::zip(keys, values));

        RocksDBStorage2<StateKey, StateValue, StateKeyResolver,
            bcos::storage2::rocksdb::StateValueResolver>
            rocksDB(*originRocksDB, StateKeyResolver{}, StateValueResolver{});
        co_await storage2::merge(rocksDB, memoryStorage);

        auto seekIt = co_await storage2::range(rocksDB);

        int i = 0;
        while (auto keyValue = co_await seekIt.next())
        {
            ++i;
        }
        BOOST_CHECK_EQUAL(i, 100);

        StateKeyView seekKeyView{"Table~5"sv, "Key~5"sv};
        auto seekIt2 = co_await storage2::range(rocksDB, storage2::RANGE_SEEK, seekKeyView);

        int j = 54;
        while (auto keyValue = co_await seekIt2.next())
        {
            ++j;
        }
        BOOST_CHECK_EQUAL(j, 100);

        storage2::memory_storage::MemoryStorage<StateKey, StateValue,
            storage2::memory_storage::ORDERED>
            storage3;
        storage2::memory_storage::MemoryStorage<StateKey, StateValue,
            storage2::memory_storage::ORDERED>
            storage4;
        RocksDBStorage2<StateKey, StateValue, StateKeyResolver,
            bcos::storage2::rocksdb::StateValueResolver>
            rocksDB2(*originRocksDB, StateKeyResolver{}, StateValueResolver{});

        co_await storage2::writeSome(
            storage3, ::ranges::views::zip(
                          ::ranges::views::iota(0, 10) | ::ranges::views::transform([](int i) {
                              return StateKey{"Hello"sv, fmt::format("key{}", i)};
                          }),
                          ::ranges::repeat_view<storage::Entry>(storage::Entry{"Hello"})));
        co_await storage2::writeSome(
            storage4, ::ranges::views::zip(
                          ::ranges::views::iota(10, 20) | ::ranges::views::transform([](int i) {
                              return StateKey{"Hello"sv, fmt::format("key{}", i)};
                          }),
                          ::ranges::repeat_view<storage::Entry>(storage::Entry{"Hello"})));
        co_await storage2::merge(rocksDB2, storage3, storage4);

        auto values2 = co_await storage2::readSome(
            rocksDB2, ::ranges::views::iota(0, 20) | ::ranges::views::transform([](int i) {
                return StateKey{"Hello"sv, fmt::format("key{}", i)};
            }));
        BOOST_CHECK_EQUAL(values2.size(), 20);
        for (auto& value : values2)
        {
            BOOST_CHECK_EQUAL(value.value().get(), "Hello");
        }

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
