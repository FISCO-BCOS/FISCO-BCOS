#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-storage/RocksDBStorage2.h>
#include <bcos-storage/StateKVResolver.h>
#include <fmt/format.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <string_view>

using namespace bcos;
using namespace bcos::storage2::rocksdb;
using namespace bcos::transaction_executor;
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

BOOST_AUTO_TEST_CASE(writeBatch)
{
    size_t totalReservedLength = ROCKSDB_SEP_HEADER_SIZE;
    constexpr static auto key1 = "Hello world!"sv;
    constexpr static auto value1 = "I am a value!"sv;

    constexpr static auto key2 = "key2"sv;
    constexpr static auto value2 = "value2"sv;

    constexpr static auto totalSize = key1.size() + key2.size() + value1.size() + value2.size();

    totalReservedLength += getRocksDBKeyPairSize(false, RANGES::size(key1), RANGES::size(value1));
    totalReservedLength += getRocksDBKeyPairSize(false, RANGES::size(key2), RANGES::size(value2));
    ::rocksdb::WriteBatch writeBatch(totalReservedLength);
    const auto* address = writeBatch.Data().data();

    writeBatch.Put(
        ::rocksdb::Slice(key1.data(), key1.size()), ::rocksdb::Slice(value1.data(), value1.size()));
    writeBatch.Put(
        ::rocksdb::Slice(key2.data(), key2.size()), ::rocksdb::Slice(value2.data(), value2.size()));
    BOOST_CHECK_EQUAL(totalReservedLength, writeBatch.Data().size());
    BOOST_CHECK_EQUAL(address, writeBatch.Data().data());
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

        auto keys = RANGES::views::iota(0, 100) | RANGES::views::transform([](int num) {
            auto tableName = fmt::format("Table~{}", num % 10);
            auto key = fmt::format("Key~{}", num);
            auto stateKey = StateKey{std::string_view(tableName), std::string_view(key)};
            return stateKey;
        });
        auto values = RANGES::views::iota(0, 100) | RANGES::views::transform([](int num) {
            storage::Entry entry;
            entry.set(fmt::format("Entry value is: i am a value!!!!!!! {}", num));
            return entry;
        });

        BOOST_CHECK_NO_THROW(co_await storage2::writeSome(rocksDB, keys, values));

        auto queryKeys = RANGES::views::iota(0, 150) | RANGES::views::transform([](int num) {
            auto tableName = fmt::format("Table~{}", num % 10);
            auto key = fmt::format("Key~{}", num);
            auto stateKey = StateKey{std::string_view(tableName), std::string_view(key)};
            return stateKey;
        });
        auto gotValues = co_await storage2::readSome(rocksDB, queryKeys);
        for (auto&& [i, value] : RANGES::views::enumerate(gotValues))
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
        auto removeKeys = RANGES::views::iota(50, 70) | RANGES::views::transform([](int num) {
            auto tableName = fmt::format("Table~{}", num % 10);
            auto key = fmt::format("Key~{}", num);
            StateKey stateKey{std::string_view(tableName), std::string_view(key)};
            return stateKey;
        });
        co_await storage2::removeSome(rocksDB, removeKeys);

        auto gotValues2 = co_await storage2::readSome(rocksDB, queryKeys);
        for (auto&& [i, value] : RANGES::views::enumerate(gotValues2))
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

        auto keys = RANGES::views::iota(0, 100) | RANGES::views::transform([](int num) {
            auto tableName = fmt::format("Table~{}", num % 10);
            auto key = fmt::format("Key~{}", num);
            auto stateKey = StateKey{std::string_view(tableName), std::string_view(key)};
            return stateKey;
        });
        auto values = RANGES::views::iota(0, 100) | RANGES::views::transform([](int num) {
            storage::Entry entry;
            entry.set(fmt::format("Entry value is: i am a value!!!!!!! {}", num));
            return entry;
        });
        co_await storage2::writeSome(memoryStorage, keys, values);

        RocksDBStorage2<StateKey, StateValue, StateKeyResolver,
            bcos::storage2::rocksdb::StateValueResolver>
            rocksDB(*originRocksDB, StateKeyResolver{}, StateValueResolver{});
        co_await storage2::merge(rocksDB, memoryStorage);

        auto seekIt = co_await storage2::range(rocksDB);

        int i = 0;
        while (auto keyValue = co_await seekIt.next())
        {
            // auto&& [key, value] = *keyValue;
            // auto [tableName, keyName] = StateKeyView(key);
            // BOOST_CHECK_EQUAL(tableName, fmt::format("Table~{}", i % 10));
            // BOOST_CHECK_EQUAL(keyName, fmt::format("Key~{}", i));
            // BOOST_CHECK_EQUAL(
            //     value.get(), fmt::format("Entry value is: i am a value!!!!!!! {}", i));
            ++i;
        }
        BOOST_CHECK_EQUAL(i, 100);

        StateKeyView seekKeyView{"Table~5"sv, "Key~5"sv};
        auto seekIt2 = co_await storage2::range(rocksDB, storage2::RANGE_SEEK, seekKeyView);

        int j = 54;
        while (auto keyValue = co_await seekIt2.next())
        {
            // auto&& [key, value] = *keyValue;
            // auto [tableName, keyName] = StateKeyView(key);
            // BOOST_CHECK_EQUAL(tableName, fmt::format("Table~{}", j % 10));
            // BOOST_CHECK_EQUAL(keyName, fmt::format("Key~{}", j));
            // BOOST_CHECK_EQUAL(
            //     value.get(), fmt::format("Entry value is: i am a value!!!!!!! {}", j));
            ++j;
        }
        BOOST_CHECK_EQUAL(j, 100);

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
