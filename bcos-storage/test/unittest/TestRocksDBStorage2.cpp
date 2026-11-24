#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Wait.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-storage/CheckpointRocksDBStorage.h>
#include <bcos-storage/RocksDBStorage2.h>
#include <bcos-storage/StateKVResolver.h>
#include <fmt/format.h>
#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/iota.hpp>
#include <range/v3/view/repeat.hpp>
#include <range/v3/view/zip.hpp>
#include <stdexcept>
#include <string_view>

using namespace bcos;
using namespace bcos::storage2::rocksdb;
using namespace bcos::executor_v1;
using namespace std::string_view_literals;

using TestCheckpointStorage =
    CheckpointRocksDBStorage<StateKey, StateValue, StateKeyResolver, StateValueResolver>;

struct TestRocksDBStorage2Fixture
{
    std::string path = "./rocksdbtestdb" + std::to_string(std::random_device{}());

    TestRocksDBStorage2Fixture()
    {
        ::rocksdb::Options options;
        options.create_if_missing = true;

        rocksdb::DB* db;
        rocksdb::Status s = rocksdb::DB::Open(options, path, &db);
        BOOST_CHECK_EQUAL(s.ok(), true);

        originRocksDB.reset(db);
    }

    ~TestRocksDBStorage2Fixture() { boost::filesystem::remove_all(path); }
    std::unique_ptr<rocksdb::DB> originRocksDB;

    static void populate(const std::string& dbPath, std::string_view table, std::string_view key,
        std::string_view value)
    {
        boost::filesystem::create_directories(dbPath);

        ::rocksdb::Options options;
        options.create_if_missing = true;

        rocksdb::DB* db = nullptr;
        auto status = rocksdb::DB::Open(options, dbPath, &db);
        BOOST_REQUIRE(status.ok());
        std::unique_ptr<rocksdb::DB> guard(db);

        RocksDBStorage2<StateKey, StateValue, StateKeyResolver, StateValueResolver> storage(
            *guard, StateKeyResolver{}, StateValueResolver{});
        task::syncWait(
            storage2::writeOne(storage, StateKey{table, key}, storage::Entry(std::string(value))));
    }
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
        auto gotValues = co_await rocksDB.readSomeRaw(queryKeys);
        for (auto&& [i, value] : ::ranges::views::enumerate(gotValues))
        {
            if (i < 100)
            {
                BOOST_CHECK(std::holds_alternative<storage::Entry>(value));
                BOOST_CHECK_EQUAL(std::get<storage::Entry>(value).get(),
                    fmt::format("Entry value is: i am a value!!!!!!! {}", i));
            }
            else
            {
                BOOST_CHECK(std::holds_alternative<storage2::NOT_EXISTS_TYPE>(value));
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

        auto gotValues2 = co_await rocksDB.readSomeRaw(queryKeys);
        for (auto&& [i, value] : ::ranges::views::enumerate(gotValues2))
        {
            if (i >= 50 && i < 70)
            {
                BOOST_CHECK(std::holds_alternative<storage2::NOT_EXISTS_TYPE>(value));
            }
            else if (i < 100)
            {
                BOOST_CHECK(std::holds_alternative<storage::Entry>(value));
                BOOST_CHECK_EQUAL(std::get<storage::Entry>(value).get(),
                    fmt::format("Entry value is: i am a value!!!!!!! {}", i));
            }
            else
            {
                BOOST_CHECK(std::holds_alternative<storage2::NOT_EXISTS_TYPE>(value));
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
                          ::ranges::views::repeat(storage::Entry{"Hello"})));
        co_await storage2::writeSome(
            storage4, ::ranges::views::zip(
                          ::ranges::views::iota(10, 20) | ::ranges::views::transform([](int i) {
                              return StateKey{"Hello"sv, fmt::format("key{}", i)};
                          }),
                          ::ranges::views::repeat(storage::Entry{"Hello"})));
        co_await storage2::merge(rocksDB2, storage3, storage4);

        auto values2 = co_await rocksDB2.readSomeRaw(
            ::ranges::views::iota(0, 20) | ::ranges::views::transform([](int i) {
                return StateKey{"Hello"sv, fmt::format("key{}", i)};
            }));
        BOOST_CHECK_EQUAL(values2.size(), 20);
        for (auto& value : values2)
        {
            BOOST_CHECK(std::holds_alternative<storage::Entry>(value));
            BOOST_CHECK_EQUAL(std::get<storage::Entry>(value).get(), "Hello");
        }

        co_return;
    }());
}

// merge() folds its sources into ONE WriteBatch in ARGUMENT ORDER, so when two sources carry
// the same key the LAST one wins on disk. MultiLayerStorage::mergeBackStorage leans on exactly
// this: it passes the committing block's own layer first and commit's prewrite storage second,
// so a row both of them hold — SYS_NUMBER_2_BLOCK_HEADER, written unsigned at execute time and
// signed at commit time — persists in the prewrite (signed) version. The MultiLayerStorage-level
// test lives in TestMPTSchedulerWiring; this one pins the property at the physical RocksDB layer.
BOOST_AUTO_TEST_CASE(mergeAppliesSourcesInArgumentOrder)
{
    task::syncWait([this]() -> task::Task<void> {
        RocksDBStorage2<StateKey, StateValue, StateKeyResolver, StateValueResolver> rocksDB(
            *originRocksDB, StateKeyResolver{}, StateValueResolver{});

        storage2::memory_storage::MemoryStorage<StateKey, StateValue,
            storage2::memory_storage::ORDERED>
            firstSource;
        storage2::memory_storage::MemoryStorage<StateKey, StateValue,
            storage2::memory_storage::ORDERED>
            secondSource;

        StateKey const sharedKey{"s_number_2_header"sv, "7"sv};
        co_await storage2::writeOne(firstSource, sharedKey, storage::Entry{"executed-unsigned"});
        co_await storage2::writeOne(secondSource, sharedKey, storage::Entry{"committed-signed"});
        // A key only the first source holds must survive the merge untouched.
        StateKey const firstOnlyKey{"s_number_2_hash"sv, "7"sv};
        co_await storage2::writeOne(firstSource, firstOnlyKey, storage::Entry{"hash-row"});

        co_await storage2::merge(rocksDB, firstSource, secondSource);

        auto shared = co_await storage2::readOne(rocksDB, sharedKey);
        BOOST_REQUIRE(shared);
        BOOST_CHECK_EQUAL(shared->get(), "committed-signed");

        auto firstOnly = co_await storage2::readOne(rocksDB, firstOnlyKey);
        BOOST_REQUIRE(firstOnly);
        BOOST_CHECK_EQUAL(firstOnly->get(), "hash-row");

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(finalizedSnapshotsKeepTheOldBackendAndBypassTheCache)
{
    using namespace storage2::memory_storage;
    using Layer = MemoryStorage<StateKey, StateValue, ORDERED | LOGICAL_DELETION>;
    using Cache = MemoryStorage<StateKey, StateValue, CONCURRENT | LRU>;
    TestCheckpointStorage checkpoint(path + "/checkpoints");
    Cache cache(1);
    cache.setMaxCapacity(1024 * 1024);
    storage2::MultiLayerStorage<Layer, Cache, TestCheckpointStorage> storage(checkpoint, cache);

    StateKey const key{"state"sv, "account"sv};
    StateKey const added{"state"sv, "winner-only"sv};
    Layer block;
    task::syncWait(storage2::writeOne(block, key, bcos::storage::Entry{"old"}));
    task::syncWait(storage.mergeFinalized(block));
    auto before = storage.forkCommittedSnapshot();

    task::syncWait(storage2::writeOne(block, key, bcos::storage::Entry{"new"}));
    task::syncWait(storage2::writeOne(block, added, bcos::storage::Entry{"visible after publish"}));
    task::syncWait(storage.mergeFinalized(block));
    auto during = storage.forkCommittedSnapshot(&before);
    auto after = storage.forkCommittedSnapshot();

    auto const old = task::syncWait(storage2::readOne(before, key));
    auto const duringRows = task::syncWait(storage2::readSome(during, std::array{key, added}));
    auto const current = task::syncWait(storage2::readOne(after, key));
    BOOST_REQUIRE(old && duringRows[0] && current);
    BOOST_CHECK_EQUAL(old->get(), "old");
    BOOST_CHECK_EQUAL(duringRows[0]->get(), "old");
    BOOST_CHECK(!duringRows[1]);
    BOOST_CHECK_EQUAL(current->get(), "new");
    auto const cached = task::syncWait(storage2::readOne(cache, key));
    BOOST_REQUIRE(cached);
    BOOST_CHECK_EQUAL(cached->get(), "new");
}

BOOST_AUTO_TEST_CASE(failedFinalizedWriteLeavesTheCacheUnchanged)
{
    struct ReadOnlyCheckpoint : TestCheckpointStorage
    {
        using TestCheckpointStorage::open;
        using TestCheckpointStorage::TestCheckpointStorage;

        Storage open()
        {
            ::rocksdb::DB* db = nullptr;
            auto status = ::rocksdb::DB::OpenForReadOnly({}, latestPath(), &db);
            BOOST_REQUIRE_MESSAGE(status.ok(), status.ToString());
            return Storage(std::unique_ptr<::rocksdb::DB>(db));
        }
    };

    using namespace storage2::memory_storage;
    using Layer = MemoryStorage<StateKey, StateValue, ORDERED | LOGICAL_DELETION>;
    using Cache = MemoryStorage<StateKey, StateValue, CONCURRENT | LRU>;
    ReadOnlyCheckpoint checkpoint(path + "/checkpoints");
    auto const key = StateKey{"state"sv, "account"sv};
    {
        auto writable = checkpoint.TestCheckpointStorage::open();
        task::syncWait(storage2::writeOne(writable, key, storage::Entry{"old"}));
    }
    Cache cache(1);
    cache.setMaxCapacity(1024 * 1024);
    task::syncWait(storage2::writeOne(cache, key, storage::Entry{"old"}));
    storage2::MultiLayerStorage<Layer, Cache, ReadOnlyCheckpoint> mls(checkpoint, cache);

    Layer block;
    task::syncWait(storage2::writeOne(block, key, storage::Entry{"new"}));
    BOOST_CHECK_THROW(task::syncWait(mls.mergeFinalized(block)), RocksDBException);
    auto const cached = task::syncWait(storage2::readOne(cache, key));
    auto const disk = task::syncWait(storage2::readOne(mls.latestBackend(), key));
    BOOST_REQUIRE(cached && disk);
    BOOST_CHECK_EQUAL(cached->get(), "old");
    BOOST_CHECK_EQUAL(disk->get(), "old");
}

BOOST_AUTO_TEST_CASE(failedFinalizedCacheRefreshClearsTheCache)
{
    using namespace storage2::memory_storage;
    using Layer = MemoryStorage<StateKey, StateValue, ORDERED | LOGICAL_DELETION>;
    using Cache = MemoryStorage<StateKey, StateValue, CONCURRENT | LRU>;
    struct FailingCache : Cache
    {
        using Cache::Cache;
        bool wrotePartialEntry = false;

        task::Task<void> merge(Layer& block)
        {
            StateKey const key{"state"sv, "account"sv};
            auto entry = co_await storage2::readOne(block, key);
            co_await storage2::writeOne(*this, key, std::move(entry.value()));
            wrotePartialEntry = true;
            throw std::runtime_error("Injected cache refresh failure");
        }
    };

    TestCheckpointStorage checkpoint(path + "/checkpoints");
    FailingCache cache(1);
    cache.setMaxCapacity(1024 * 1024);
    storage2::MultiLayerStorage<Layer, FailingCache, TestCheckpointStorage> mls(checkpoint, cache);

    StateKey const priorKey{"state"sv, "cached-account"sv};
    StateKey const updatedKey{"state"sv, "account"sv};
    task::syncWait(storage2::writeOne(cache, priorKey, storage::Entry{"cached"}));
    Layer block;
    task::syncWait(storage2::writeOne(block, updatedKey, storage::Entry{"finalized"}));

    BOOST_CHECK_NO_THROW(task::syncWait(mls.mergeFinalized(block)));
    BOOST_REQUIRE(cache.wrotePartialEntry);
    BOOST_CHECK(!task::syncWait(storage2::readOne(cache, priorKey)));
    BOOST_CHECK(!task::syncWait(storage2::readOne(cache, updatedKey)));

    auto view = mls.forkCommitted();
    auto const finalized = task::syncWait(storage2::readOne(view, updatedKey));
    BOOST_REQUIRE(finalized);
    BOOST_CHECK_EQUAL(finalized->get(), "finalized");
}

BOOST_AUTO_TEST_CASE(openLatestOrCheckpointByDirectory)
{
    auto root = path + "_checkpoint_root";
    TestCheckpointStorage checkpointRocksDBStorage(root, StateKeyResolver{}, StateValueResolver{});
    auto latestPath = checkpointRocksDBStorage.latestPath();
    auto firstCheckpointName =
        h256("1111111111111111111111111111111111111111111111111111111111111111");
    auto secondCheckpointName =
        h256("2222222222222222222222222222222222222222222222222222222222222222");
    auto firstCheckpointPath = checkpointRocksDBStorage.checkpointPath(firstCheckpointName);
    auto secondCheckpointPath = checkpointRocksDBStorage.checkpointPath(secondCheckpointName);

    task::syncWait([&]() -> task::Task<void> {
        auto latestStorage = checkpointRocksDBStorage.open();
        co_await storage2::writeOne(
            latestStorage, StateKey{"sys"sv, "key"sv}, storage::Entry("latest-value-1"));
        auto latestValue = co_await storage2::readOne(latestStorage, StateKey{"sys"sv, "key"sv});
        BOOST_REQUIRE(latestValue);
        BOOST_CHECK_EQUAL(latestValue->get(), "latest-value-1");
        BOOST_CHECK_EQUAL(checkpointRocksDBStorage.latestPath(), latestPath);
        BOOST_CHECK(!checkpointRocksDBStorage.latestCheckpointName());
        BOOST_CHECK(!checkpointRocksDBStorage.oldestCheckpointName());

        checkpointRocksDBStorage.createCheckpoint(latestStorage, firstCheckpointName);
        auto firstCheckpointTime = std::filesystem::file_time_type::clock::now();
        std::filesystem::last_write_time(firstCheckpointPath, firstCheckpointTime);
        BOOST_REQUIRE(checkpointRocksDBStorage.latestCheckpointName());
        BOOST_REQUIRE(checkpointRocksDBStorage.oldestCheckpointName());
        BOOST_CHECK_EQUAL(*checkpointRocksDBStorage.latestCheckpointName(), firstCheckpointName);
        BOOST_CHECK_EQUAL(*checkpointRocksDBStorage.oldestCheckpointName(), firstCheckpointName);

        auto firstCheckpointStorage = checkpointRocksDBStorage.open(firstCheckpointName);
        auto firstCheckpointValue =
            co_await storage2::readOne(firstCheckpointStorage, StateKey{"sys"sv, "key"sv});
        BOOST_REQUIRE(firstCheckpointValue);
        BOOST_CHECK_EQUAL(firstCheckpointValue->get(), "latest-value-1");
        BOOST_CHECK_EQUAL(
            checkpointRocksDBStorage.checkpointPath(firstCheckpointName), firstCheckpointPath);

        co_await storage2::writeOne(
            latestStorage, StateKey{"sys"sv, "key"sv}, storage::Entry("latest-value-2"));
        checkpointRocksDBStorage.createCheckpoint(latestStorage, secondCheckpointName);
        auto secondCheckpointTime = firstCheckpointTime + std::chrono::seconds(1);
        std::filesystem::last_write_time(secondCheckpointPath, secondCheckpointTime);
        BOOST_REQUIRE(checkpointRocksDBStorage.latestCheckpointName());
        BOOST_REQUIRE(checkpointRocksDBStorage.oldestCheckpointName());
        BOOST_CHECK_EQUAL(*checkpointRocksDBStorage.latestCheckpointName(), secondCheckpointName);
        BOOST_CHECK_EQUAL(*checkpointRocksDBStorage.oldestCheckpointName(), firstCheckpointName);

        auto secondCheckpointStorage = checkpointRocksDBStorage.open(secondCheckpointName);
        auto secondCheckpointValue =
            co_await storage2::readOne(secondCheckpointStorage, StateKey{"sys"sv, "key"sv});
        BOOST_REQUIRE(secondCheckpointValue);
        BOOST_CHECK_EQUAL(secondCheckpointValue->get(), "latest-value-2");
        BOOST_CHECK_EQUAL(
            checkpointRocksDBStorage.checkpointPath(secondCheckpointName), secondCheckpointPath);

        auto latestValueAfterSecondCheckpoint =
            co_await storage2::readOne(latestStorage, StateKey{"sys"sv, "key"sv});
        BOOST_REQUIRE(latestValueAfterSecondCheckpoint);
        BOOST_CHECK_EQUAL(latestValueAfterSecondCheckpoint->get(), "latest-value-2");

        checkpointRocksDBStorage.deleteCheckpoint(firstCheckpointName);
        BOOST_REQUIRE(checkpointRocksDBStorage.latestCheckpointName());
        BOOST_REQUIRE(checkpointRocksDBStorage.oldestCheckpointName());
        BOOST_CHECK_EQUAL(*checkpointRocksDBStorage.latestCheckpointName(), secondCheckpointName);
        BOOST_CHECK_EQUAL(*checkpointRocksDBStorage.oldestCheckpointName(), secondCheckpointName);

        checkpointRocksDBStorage.deleteCheckpoint(secondCheckpointName);
        BOOST_CHECK(!checkpointRocksDBStorage.latestCheckpointName());
        BOOST_CHECK(!checkpointRocksDBStorage.oldestCheckpointName());

        co_return;
    }());

    boost::filesystem::remove_all(root);
}

BOOST_AUTO_TEST_SUITE_END()
