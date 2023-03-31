#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/StringPool.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-storage/RocksDBStorage2.h>
#include <bcos-storage/StateKVResolver.h>
#include <fmt/format.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <range/v3/iterator/concepts.hpp>

using namespace bcos;
using namespace bcos::storage2::rocksdb;
using namespace bcos::transaction_executor;

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
    storage2::string_pool::FixedStringPool stringPool;
};

BOOST_FIXTURE_TEST_SUITE(TestRocksDBStorage2, TestRocksDBStorage2Fixture)

BOOST_AUTO_TEST_CASE(kvResolver)
{
    StateKeyResolver keyResolver(stringPool);

    std::string_view mergedKey = "test_table!!!:key100";
    auto keyPair = keyResolver.decode(mergedKey);

    auto& [tableName, keyName] = keyPair;
    BOOST_CHECK_EQUAL(*tableName, "test_table!!!");
    BOOST_CHECK_EQUAL(keyName.toStringView(), "key100");
}

BOOST_AUTO_TEST_CASE(readWriteRemoveSeek)
{
    task::syncWait([this]() -> task::Task<void> {
        RocksDBStorage2<StateKey, StateValue, StateKeyResolver,
            bcos::storage2::rocksdb::StateValueResolver>
            rocksDB(*originRocksDB, StateKeyResolver(stringPool), StateValueResolver{});

        auto notExistsValue = co_await storage2::readOne(
            rocksDB, StateKey{storage2::string_pool::makeStringID(stringPool, "Non exists table"),
                         "Non exists key"});
        BOOST_REQUIRE(!notExistsValue);

        auto keys = RANGES::views::iota(0, 100) | RANGES::views::transform([this](int num) {
            auto tableName = fmt::format("Table~{}", num % 10);
            auto key = fmt::format("Key~{}", num);
            auto stateKey =
                StateKey{storage2::string_pool::makeStringID(stringPool, tableName), key};
            return stateKey;
        });
        auto values = RANGES::views::iota(0, 100) | RANGES::views::transform([](int num) {
            storage::Entry entry;
            entry.set(fmt::format("Entry value is: i am a value!!!!!!! {}", num));
            return entry;
        });

        BOOST_CHECK_NO_THROW(co_await rocksDB.write(keys, values));

        auto queryKeys = RANGES::views::iota(0, 150) | RANGES::views::transform([this](int num) {
            auto tableName = fmt::format("Table~{}", num % 10);
            auto key = fmt::format("Key~{}", num);
            auto stateKey =
                StateKey{storage2::string_pool::makeStringID(stringPool, tableName), key};
            return stateKey;
        });
        auto it = co_await rocksDB.read(queryKeys);
        int i = 0;
        while (co_await it.next())
        {
            if (i < 100)
            {
                BOOST_CHECK(co_await it.hasValue());

                auto value = co_await it.value();
                BOOST_CHECK_EQUAL(
                    value.get(), fmt::format("Entry value is: i am a value!!!!!!! {}", i));
            }
            else
            {
                BOOST_CHECK(!co_await it.hasValue());
            }

            ++i;
        }

        // Remove some
        auto removeKeys = RANGES::views::iota(50, 70) | RANGES::views::transform([this](int num) {
            auto tableName = fmt::format("Table~{}", num % 10);
            auto key = fmt::format("Key~{}", num);
            auto stateKey =
                StateKey{storage2::string_pool::makeStringID(stringPool, tableName), key};
            return stateKey;
        });
        co_await rocksDB.remove(removeKeys);

        auto it2 = co_await rocksDB.read(queryKeys);
        i = 0;
        while (co_await it2.next())
        {
            if (i >= 50 && i < 70)
            {
                BOOST_CHECK(!co_await it2.hasValue());
            }
            else if (i < 100)
            {
                BOOST_CHECK(co_await it2.hasValue());

                // BOOST_CHECK_THROW(co_await it2.key(), UnsupportedMethod);
                auto value = co_await it2.value();
                BOOST_CHECK_EQUAL(
                    value.get(), fmt::format("Entry value is: i am a value!!!!!!! {}", i));
            }
            else
            {
                BOOST_CHECK(!co_await it2.hasValue());
            }

            ++i;
        }

        // Seek to ensure 50 values
        auto seekIt = co_await rocksDB.seek(storage2::STORAGE_BEGIN);

        i = 0;
        while (co_await seekIt.next())
        {
            BOOST_CHECK(co_await seekIt.hasValue());

            auto key = co_await seekIt.key();
            auto& [tableName, keyName] = key;

            auto value = co_await seekIt.value();
            ++i;
        }
        BOOST_CHECK_EQUAL(i, 80);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(merge)
{
    task::syncWait([this]() -> task::Task<void> {
        storage2::memory_storage::MemoryStorage<StateKey, StateValue,
            storage2::memory_storage::ORDERED>
            memoryStorage;

        auto keys = RANGES::views::iota(0, 100) | RANGES::views::transform([this](int num) {
            auto tableName = fmt::format("Table~{}", num % 10);
            auto key = fmt::format("Key~{}", num);
            auto stateKey =
                StateKey{storage2::string_pool::makeStringID(stringPool, tableName), key};
            return stateKey;
        });
        auto values = RANGES::views::iota(0, 100) | RANGES::views::transform([](int num) {
            storage::Entry entry;
            entry.set(fmt::format("Entry value is: i am a value!!!!!!! {}", num));
            return entry;
        });
        memoryStorage.write(keys, values);

        RocksDBStorage2<StateKey, StateValue, StateKeyResolver,
            bcos::storage2::rocksdb::StateValueResolver>
            rocksDB(*originRocksDB, StateKeyResolver(stringPool), StateValueResolver{});
        co_await rocksDB.merge(memoryStorage);
        auto seekIt = co_await rocksDB.seek(storage2::STORAGE_BEGIN);

        int i = 0;
        while (co_await seekIt.next())
        {
            BOOST_CHECK(co_await seekIt.hasValue());

            auto key = co_await seekIt.key();
            auto& [tableName, keyName] = key;

            auto value = co_await seekIt.value();
            ++i;
        }
        BOOST_CHECK_EQUAL(i, 100);

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()