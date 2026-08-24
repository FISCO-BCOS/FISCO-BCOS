#include "bcos-framework/storage/LegacyStorageMethods.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-task/Wait.h"
#include "transaction-executor/StateKey.h"
#include <pthread.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <map>
#include <string>
#include <thread>

using namespace bcos;

namespace
{
// Inline-completing mock shaped like the local RocksDB backend: every async*
// method runs the op and fires the callback before returning.
class InlineStorage : public storage::StorageInterface
{
public:
    std::map<std::pair<std::string, std::string>, std::string> rows;
    bool failWrites = false;

    ~InlineStorage() override;

    void asyncGetPrimaryKeys(std::string_view, std::optional<storage::Condition const> const&,
        std::function<void(bcos::Error::UniquePtr, std::vector<std::string>)> _callback) override
    {
        _callback(nullptr, {});
    }

    void asyncGetRow(std::string_view table, std::string_view key,
        std::function<void(bcos::Error::UniquePtr, std::optional<storage::Entry>)> _callback)
        override
    {
        auto it = rows.find({std::string(table), std::string(key)});
        if (it == rows.end())
        {
            _callback(nullptr, {});
            return;
        }
        storage::Entry entry;
        entry.set(it->second);
        _callback(nullptr, std::move(entry));
    }

    void asyncGetRows(std::string_view table,
        ::ranges::any_view<std::string_view, ::ranges::category::input |
                                                 ::ranges::category::random_access |
                                                 ::ranges::category::sized>
            keys,
        std::function<void(bcos::Error::UniquePtr, std::vector<std::optional<storage::Entry>>)>
            _callback) override
    {
        std::vector<std::optional<storage::Entry>> entries;
        for (auto key : keys)
        {
            std::optional<storage::Entry> entry;
            if (auto it = rows.find({std::string(table), std::string(key)}); it != rows.end())
            {
                entry.emplace();
                entry->set(it->second);
            }
            entries.emplace_back(std::move(entry));
        }
        _callback(nullptr, std::move(entries));
    }

    void asyncSetRow(std::string_view table, std::string_view key, storage::Entry entry,
        std::function<void(bcos::Error::UniquePtr)> _callback) override
    {
        if (failWrites)
        {
            _callback(BCOS_ERROR_UNIQUE_PTR(-1, "mock write failure"));
            return;
        }
        rows[{std::string(table), std::string(key)}] = std::string(entry.get());
        _callback(nullptr);
    }

    bool isSynchronousCompletion() const noexcept override { return true; }
};

// Out-of-line definition anchors the vtable/typeinfo in this TU (the interface
// itself has no key function, so weak emissions alone do not resolve at link).
InlineStorage::~InlineStorage() = default;

// Deferred-completing mock shaped like a network backend (TiKV): the callback
// always fires on another thread, i.e. strictly after asyncSetRow returned.
class AsyncStorage : public InlineStorage
{
public:
    ~AsyncStorage() override;

    bool isSynchronousCompletion() const noexcept override { return false; }

    void asyncSetRow(std::string_view table, std::string_view key, storage::Entry entry,
        std::function<void(bcos::Error::UniquePtr)> _callback) override
    {
        std::thread([this, table = std::string(table), key = std::string(key),
                        value = std::string(entry.get()),
                        _callback = std::move(_callback)]() mutable {
            rows[{table, key}] = value;
            _callback(nullptr);
        }).detach();
    }
};

AsyncStorage::~AsyncStorage() = default;

struct LegacyStorageMethodsFixture
{
    InlineStorage storage;
};
}  // namespace

BOOST_AUTO_TEST_SUITE(TestLegacyStorageMethods)

// Values round-trip through the storage2 bridge on an inline-completing backend.
BOOST_FIXTURE_TEST_CASE(InlineBackendRoundTrip, LegacyStorageMethodsFixture)
{
    task::syncWait([&]() -> task::Task<void> {
        storage::Entry entry;
        entry.set("hello");
        co_await storage2::writeOne(
            storage, executor_v1::StateKey{"test_table", "k1"}, std::move(entry));

        auto loaded =
            co_await storage2::readOne(storage, executor_v1::StateKeyView{"test_table", "k1"});
        BOOST_REQUIRE(loaded.has_value());
        BOOST_CHECK_EQUAL(loaded->get(), "hello");

        auto missing =
            co_await storage2::readOne(storage, executor_v1::StateKeyView{"test_table", "absent"});
        BOOST_CHECK(!missing.has_value());
    }());
}

// Backend errors surface as exceptions through the synchronous fast path.
BOOST_FIXTURE_TEST_CASE(InlineBackendErrorPropagates, LegacyStorageMethodsFixture)
{
    storage.failWrites = true;
    BOOST_CHECK_THROW(task::syncWait([&]() -> task::Task<void> {
        storage::Entry entry;
        entry.set("x");
        co_await storage2::writeOne(
            storage, executor_v1::StateKey{"test_table", "k2"}, std::move(entry));
    }()),
        std::exception);
}

// Deferred-completing backends keep the suspend + cross-thread resume path.
BOOST_AUTO_TEST_CASE(AsyncBackendStillSuspends)
{
    AsyncStorage storage;
    task::syncWait([&]() -> task::Task<void> {
        storage::Entry entry;
        entry.set("async-value");
        co_await storage2::writeOne(
            storage, executor_v1::StateKey{"async_table", "k1"}, std::move(entry));

        auto loaded =
            co_await storage2::readOne(storage, executor_v1::StateKeyView{"async_table", "k1"});
        BOOST_REQUIRE(loaded.has_value());
        BOOST_CHECK_EQUAL(loaded->get(), "async-value");
    }());
}

// Regression (fresh-genesis stack overflow, 2026-08): a sequential co_await
// writeOne loop used to nest one inline resume per row — ~20k genesis rows
// overflowed the default 8MB stack. The synchronous fast path never suspends,
// so 20k rows must complete on a 512KB-stack thread.
BOOST_AUTO_TEST_CASE(SequentialLoopStaysFlat)
{
    auto* storage = new InlineStorage();
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 512 * 1024);
    pthread_t tid{};
    auto routine = [](void* arg) -> void* {
        auto* storage = static_cast<InlineStorage*>(arg);
        task::syncWait([&]() -> task::Task<void> {
            for (int i = 0; i < 20000; ++i)
            {
                storage::Entry entry;
                entry.set("row-" + std::to_string(i));
                co_await storage2::writeOne(*storage,
                    executor_v1::StateKey{"flat_table", std::to_string(i)}, std::move(entry));
            }
        }());
        return nullptr;
    };
    BOOST_CHECK_EQUAL(pthread_create(&tid, &attr, routine, storage), 0);
    pthread_attr_destroy(&attr);
    BOOST_CHECK_EQUAL(pthread_join(tid, nullptr), 0);
    BOOST_CHECK_EQUAL(storage->rows.size(), 20000U);
    delete storage;
}

BOOST_AUTO_TEST_SUITE_END()
