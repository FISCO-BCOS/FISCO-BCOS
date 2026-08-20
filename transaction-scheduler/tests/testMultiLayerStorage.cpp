#include "TrivialCheckpointStorage.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-scheduler/ReadWriteSetStorage.h"
#include <fmt/format.h>
#include <boost/test/unit_test.hpp>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/repeat.hpp>
#include <variant>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::scheduler_v1;
using namespace std::string_view_literals;

class TestMultiLayerStorageFixture
{
public:
    using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>;
    using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendStorage>;

    TestMultiLayerStorageFixture()
      : checkpointBackend(backendStorage), multiLayerStorage(checkpointBackend)
    {}

    BackendStorage backendStorage;
    CheckpointBackend checkpointBackend;
    MultiLayerStorage<MutableStorage, void, CheckpointBackend> multiLayerStorage;
};

BOOST_FIXTURE_TEST_SUITE(TestMultiLayerStorage, TestMultiLayerStorageFixture)

BOOST_AUTO_TEST_CASE(noMutable)
{
    task::syncWait([this]() -> task::Task<void> {
        auto view = multiLayerStorage.fork();
        storage::Entry entry;
        BOOST_CHECK_THROW(co_await storage2::writeOne(
                              view, StateKey{"test_table"sv, "test_key"sv}, std::move(entry)),
            NotExistsMutableStorageError);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(readWriteMutable)
{
    task::syncWait([this]() -> task::Task<void> {
        auto view = std::make_optional(multiLayerStorage.fork());
        view->newMutable();
        StateKey key{"test_table"sv, "test_key"sv};

        storage::Entry entry;
        entry.set("Hello world!");
        co_await storage2::writeOne(*view, key, entry);

        ::ranges::single_view keyViews(key);
        auto values = co_await storage2::readSome(*view, keyViews);

        BOOST_REQUIRE(values[0].has_value());
        BOOST_CHECK_EQUAL(values[0]->get(), entry.get());
        BOOST_CHECK_NO_THROW(multiLayerStorage.pushView(std::move(*view)));

        auto view2 = multiLayerStorage.fork();
        BOOST_CHECK_THROW(
            co_await storage2::writeOne(view2, key, entry), NotExistsMutableStorageError);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(merge)
{
    task::syncWait([this]() -> task::Task<void> {
        auto view = std::make_optional(multiLayerStorage.fork());
        view->newMutable();
        auto toKey = ::ranges::views::transform([](int num) {
            return StateKey{"test_table"sv, fmt::format("key: {}", num)};
        });
        auto toValue = ::ranges::views::transform([](int num) {
            storage::Entry entry;
            entry.set(fmt::format("value: {}", num));
            return entry;
        });

        co_await storage2::writeSome(
            *view, ::ranges::views::zip(::ranges::iota_view<int, int>(0, 100) | toKey,
                       ::ranges::iota_view<int, int>(0, 100) | toValue));

        BOOST_CHECK_THROW(
            co_await multiLayerStorage.mergeBackStorage(), NotExistsImmutableStorageError);

        multiLayerStorage.pushView(std::move(*view));
        co_await multiLayerStorage.mergeBackStorage();

        auto view2 = multiLayerStorage.fork();
        auto keys = ::ranges::iota_view<int, int>(0, 100) | toKey;
        auto values = co_await storage2::readSome(view2, keys);

        for (auto&& [index, value] : ::ranges::views::enumerate(values))
        {
            BOOST_REQUIRE(value.has_value());
            BOOST_CHECK_EQUAL(value->get(), fmt::format("value: {}", index));
        }
        BOOST_CHECK_EQUAL(::ranges::size(values), 100);

        auto view3 = multiLayerStorage.fork();
        view3.newMutable();
        co_await storage2::removeSome(view3, ::ranges::iota_view<int, int>(20, 30) | toKey);
        multiLayerStorage.pushView(std::move(view3));
        co_await multiLayerStorage.mergeBackStorage();

        auto values2 = co_await storage2::readSome(view3, keys);
        for (auto&& [index, value] : ::ranges::views::enumerate(values2))
        {
            if (index >= 20 && index < 30)
            {
                BOOST_CHECK(!value);
            }
            else
            {
                BOOST_CHECK(value);
            }
        }

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(mergeViewPersistsToBackend)
{
    task::syncWait([this]() -> task::Task<void> {
        auto view = std::make_optional(multiLayerStorage.fork());
        view->newMutable();
        StateKey key{"test_table"sv, "test_key"sv};
        storage::Entry entry;
        entry.set("Hello mergeView!");
        co_await storage2::writeOne(*view, key, entry);

        // mergeView 落盘：先入栈再合并最旧层 → backend（m_latestBackend）
        co_await multiLayerStorage.mergeView(std::move(*view));

        // 数据经 backend 层读到——证明已落盘,非仅内存层栈
        // （merge 后栈空,fork() 读等价 backend 读;直接 latestBackend() 最严格）
        auto backendRead = co_await storage2::readOne(multiLayerStorage.latestBackend(), key);
        BOOST_REQUIRE(backendRead.has_value());
        BOOST_CHECK_EQUAL(backendRead->get(), entry.get());

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(mergeViewNoMutableIsNoop)
{
    task::syncWait([this]() -> task::Task<void> {
        auto view = multiLayerStorage.fork();  // 未 newMutable → m_mutableStorage 为空
        BOOST_CHECK_NO_THROW(co_await multiLayerStorage.mergeView(std::move(view)));
        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(mergeViewMergesOldestLayer)
{
    // 审查修正（spec §4）：mergeBackStorage 合并最旧层（FIFO,MultiLayerStorage.h:570）。
    // 验证前置条件"push 前栈空才立即落盘"与栈积压时最近层留存的 one-block lag 语义。
    // ⚠️ 相对 brief 补一行：fork() 只把当前栈拷贝进 view 的 immutable storages，
    //    写 mutable 不会入栈——须先 pushView(v1) 使栈为 [v1]，mergeView(v2) 后才
    //    有「最旧层 v1 落 backend、最近层 v2 留内存」的 one-block lag 可断言。
    task::syncWait([this]() -> task::Task<void> {
        auto v1 = std::make_optional(multiLayerStorage.fork());
        v1->newMutable();
        StateKey k1{"test_table"sv, "k1"sv};
        storage::Entry e1;
        e1.set("v1");
        co_await storage2::writeOne(*v1, k1, e1);
        multiLayerStorage.pushView(std::move(*v1));  // 入栈 → 栈 [v1]

        auto v2 = std::make_optional(multiLayerStorage.fork());
        v2->newMutable();
        StateKey k2{"test_table"sv, "k2"sv};
        storage::Entry e2;
        e2.set("v2");
        co_await storage2::writeOne(*v2, k2, e2);

        // 栈为 [v2, v1]（push_front 次序）。mergeView(v2) → push v2 后 merge 最旧层 v1。
        co_await multiLayerStorage.mergeView(std::move(*v2));

        // 最旧层 v1 落 backend；v2 仍在内存前端,未落（one-block lag）
        auto r1 = co_await storage2::readOne(multiLayerStorage.latestBackend(), k1);
        BOOST_REQUIRE(r1.has_value());
        BOOST_CHECK_EQUAL(r1->get(), e1.get());
        auto r2 = co_await storage2::readOne(multiLayerStorage.latestBackend(), k2);
        BOOST_CHECK(!r2.has_value());  // v2 未落

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(rangeMulti)
{
    using MutableStorage = memory_storage::MemoryStorage<int, int,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    using BackendStorage = memory_storage::MemoryStorage<int, int,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LRU)>;
    using CheckpointBackend = TrivialCheckpointStorage<int, int, BackendStorage>;

    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        CheckpointBackend checkpointBackend(backendStorage);
        co_await storage2::writeSome(backendStorage,
            ::ranges::views::zip(::ranges::views::iota(0, 4), ::ranges::views::repeat(0)));

        MultiLayerStorage<MutableStorage, void, CheckpointBackend> myMultiLayerStorage(
            checkpointBackend);

        auto view1 = myMultiLayerStorage.fork();
        view1.newMutable();
        co_await storage2::writeSome(
            view1, ::ranges::views::zip(::ranges::views::iota(2, 6), ::ranges::views::repeat(1)));
        co_await storage2::removeOne(view1, 2);
        myMultiLayerStorage.pushView(std::move(view1));

        auto view2 = myMultiLayerStorage.fork();
        view2.newMutable();
        co_await storage2::writeSome(
            view2, ::ranges::views::zip(::ranges::views::iota(4, 8), ::ranges::views::repeat(2)));

        auto resultList = co_await storage2::readSome(view2, ::ranges::views::iota(0, 8));
        auto vecList = resultList |
                       ::ranges::views::transform([](auto input) { return input.value_or(-1); }) |
                       ::ranges::to<std::vector>();
        BOOST_CHECK_EQUAL(resultList.size(), 8);
        auto expectList = std::vector<int>({0, 0, -1, 1, 2, 2, 2, 2});
        BOOST_CHECK_EQUAL_COLLECTIONS(
            vecList.begin(), vecList.end(), expectList.begin(), expectList.end());

        auto range = co_await storage2::range(view2);
        auto i = 0;
        std::vector<int> vecList2;
        while (auto keyValue = co_await range.next())
        {
            auto& [key, value] = *keyValue;
            if (auto* ptr = std::get_if<int>(std::addressof(value)))
            {
                std::cout << fmt::format("key: {}, value: {}\n", key, *ptr);
                BOOST_CHECK_EQUAL(key, i);
                vecList2.emplace_back(*ptr);
            }
            else
            {
                vecList2.emplace_back(-1);
            }
            ++i;
        }
        BOOST_CHECK_EQUAL_COLLECTIONS(
            vecList2.begin(), vecList2.end(), expectList.begin(), expectList.end());

        ReadWriteSetStorage<decltype(view2)> readWriteSetStorage(view2);
        auto range2 = co_await storage2::range(readWriteSetStorage);
    }());
}

BOOST_AUTO_TEST_CASE(deletedEntry)
{
    task::syncWait([this]() -> task::Task<void> {
        auto view = multiLayerStorage.fork();
        view.newMutable();
        StateKey key{"test_table"sv, "test_key"sv};

        storage::Entry entry;
        entry.set("Hello world!");
        co_await storage2::writeOne(view, key, entry);
        StateKey key2{"test_table"sv, "test_key2"sv};
        co_await storage2::writeOne(view, key2, entry);

        multiLayerStorage.pushView(std::move(view));

        auto view2 = multiLayerStorage.fork();
        view2.newMutable();
        co_await storage2::removeOne(view2, key);

        BOOST_CHECK(!co_await storage2::existsOne(view2, key));

        auto values = co_await storage2::readSome(view2, ::ranges::views::single(key));
        BOOST_CHECK(!values[0]);

        auto range = co_await storage2::range(view2);
        auto keyValue = co_await range.next();
        BOOST_CHECK(keyValue);
        BOOST_CHECK_EQUAL(std::get<0>(*keyValue), key);
        BOOST_CHECK(std::holds_alternative<DELETED_TYPE>(std::get<1>(*keyValue)));
        keyValue = co_await range.next();
        BOOST_CHECK(keyValue);
        BOOST_CHECK_EQUAL(std::get<0>(*keyValue), key2);
    }());
}

// BYPASS_MULTILAYER: readOneRaw stops at the first layer that exists and
// returns NOT_EXISTS without falling through. The untagged overload walks
// the full chain (mutable → immutable → cache → backend) and only returns
// NOT_EXISTS when no layer holds the key.
BOOST_AUTO_TEST_CASE(bypassMultiLayerReadOneRaw)
{
    task::syncWait([this]() -> task::Task<void> {
        // Pre-populate the backend with a key
        StateKey backendKey{"test_table"sv, "backend_key"sv};
        storage::Entry backendEntry;
        backendEntry.set("backend_value");
        co_await storage2::writeOne(backendStorage, backendKey, backendEntry);

        // Fork a view with a mutable layer holding a different key
        auto view = multiLayerStorage.fork();
        view.newMutable();
        StateKey mutableKey{"test_table"sv, "mutable_key"sv};
        storage::Entry mutableEntry;
        mutableEntry.set("mutable_value");
        co_await storage2::writeOne(view, mutableKey, mutableEntry);

        // Both paths agree on a key present in the mutable layer
        auto mutableByBypass = co_await view.readOneRaw(mutableKey, storage2::BYPASS_MULTILAYER);
        BOOST_CHECK(std::holds_alternative<storage::Entry>(mutableByBypass));
        BOOST_CHECK_EQUAL(std::get<storage::Entry>(mutableByBypass).get(), "mutable_value"sv);

        auto mutableByNormal = co_await view.readOneRaw(mutableKey);
        BOOST_CHECK(std::holds_alternative<storage::Entry>(mutableByNormal));
        BOOST_CHECK_EQUAL(std::get<storage::Entry>(mutableByNormal).get(), "mutable_value"sv);

        // Divergence on a key that only exists in the backend:
        // BYPASS_MULTILAYER → NOT_EXISTS (stops at mutable, unconditional co_return)
        auto backendByBypass = co_await view.readOneRaw(backendKey, storage2::BYPASS_MULTILAYER);
        BOOST_CHECK(std::holds_alternative<storage2::NOT_EXISTS_TYPE>(backendByBypass));

        // No tag → falls through the full chain, reaches backend
        auto backendByNormal = co_await view.readOneRaw(backendKey);
        BOOST_CHECK(std::holds_alternative<storage::Entry>(backendByNormal));
        BOOST_CHECK_EQUAL(std::get<storage::Entry>(backendByNormal).get(), "backend_value"sv);

        co_return;
    }());
}

// Same layer-resolution test for the batch path (readSomeRaw).
BOOST_AUTO_TEST_CASE(bypassMultiLayerReadSomeRaw)
{
    task::syncWait([this]() -> task::Task<void> {
        StateKey backendKey{"test_table"sv, "backend_key"sv};
        storage::Entry backendEntry;
        backendEntry.set("backend_value");
        co_await storage2::writeOne(backendStorage, backendKey, backendEntry);

        auto view = multiLayerStorage.fork();
        view.newMutable();
        StateKey mutableKey{"test_table"sv, "mutable_key"sv};
        storage::Entry mutableEntry;
        mutableEntry.set("mutable_value");
        co_await storage2::writeOne(view, mutableKey, mutableEntry);

        // BYPASS_MULTILAYER: backend-only key → NOT_EXISTS
        auto bypassValues = co_await view.readSomeRaw(
            std::vector{mutableKey, backendKey}, storage2::BYPASS_MULTILAYER);
        BOOST_CHECK(std::holds_alternative<storage::Entry>(bypassValues[0]));
        BOOST_CHECK_EQUAL(std::get<storage::Entry>(bypassValues[0]).get(), "mutable_value"sv);
        BOOST_CHECK(std::holds_alternative<storage2::NOT_EXISTS_TYPE>(bypassValues[1]));

        // No tag: backend-only key → resolved from backend
        auto normalValues = co_await view.readSomeRaw(std::vector{mutableKey, backendKey});
        BOOST_CHECK(std::holds_alternative<storage::Entry>(normalValues[0]));
        BOOST_CHECK_EQUAL(std::get<storage::Entry>(normalValues[0]).get(), "mutable_value"sv);
        BOOST_CHECK(std::holds_alternative<storage::Entry>(normalValues[1]));
        BOOST_CHECK_EQUAL(std::get<storage::Entry>(normalValues[1]).get(), "backend_value"sv);

        co_return;
    }());
}

// BYPASS_READ_SET composed with BYPASS_MULTILAYER: the two tags travel
// together and each layer picks the one it cares about.
BOOST_AUTO_TEST_CASE(composedTagsReadOneRaw)
{
    task::syncWait([this]() -> task::Task<void> {
        StateKey backendKey{"test_table"sv, "backend_key"sv};
        storage::Entry backendEntry;
        backendEntry.set("backend_value");
        co_await storage2::writeOne(backendStorage, backendKey, backendEntry);

        auto view = multiLayerStorage.fork();
        view.newMutable();

        // Both tags together: BYPASS_MULTILAYER determines layer resolution
        // (topmost-only), BYPASS_READ_SET is irrelevant to this View but
        // forwarded to lower layers.
        auto result = co_await view.readOneRaw(
            backendKey, storage2::BYPASS_READ_SET, storage2::BYPASS_MULTILAYER);
        BOOST_CHECK(std::holds_alternative<storage2::NOT_EXISTS_TYPE>(result));

        co_return;
    }());
}

// Regression: Rollbackable pre-image capture through MultiLayerStorage.
//
// Rollbackable::storeOldValues / writeOne capture the pre-image of a key
// before writing, so rollback can restore the old value.  On base the read
// used DIRECT (= topmost-layer-only): a key whose value exists only in the
// backend (from an earlier block) and is first-written inside the current
// mutable layer captures NOT_EXISTS.  Rollback then erases the key from
// the mutable layer → the key does NOT appear in the block delta.
//
// Without BYPASS_MULTILAYER the pre-image resolves via the full layer
// chain and captures the backend value.  Rollback writes that value back
// → a same-value row appears in the block delta → XOR state root changes.
// The trigger is any tx with a reverted frame touching a pre-existing slot.
//
// This test asserts that BYPASS_READ_SET | BYPASS_MULTILAYER delivers
// NOT_EXISTS (the old DIRECT behaviour), while the untagged overload
// resolves through the full chain — so the divergence is visible and the
// tag composition works as designed.
BOOST_AUTO_TEST_CASE(preImageCaptureBypassMultiLayerRegression)
{
    task::syncWait([this]() -> task::Task<void> {
        // Simulate a key that exists in the backend from a previous block
        StateKey key{"test_table"sv, "preimage_key"sv};
        storage::Entry backendEntry;
        backendEntry.set("value_from_previous_block");
        co_await storage2::writeOne(backendStorage, key, backendEntry);

        // Fork a new view with a mutable layer (new transaction frame).
        // The key has NOT been written in this frame yet.
        auto view = multiLayerStorage.fork();
        view.newMutable();

        // Pre-image capture with BYPASS_READ_SET | BYPASS_MULTILAYER:
        // BYPASS_MULTILAYER → stops at mutable layer → NOT_EXISTS.
        // This is the old DIRECT behaviour and must NOT change.
        auto bypassResult =
            co_await view.readOneRaw(key, storage2::BYPASS_READ_SET, storage2::BYPASS_MULTILAYER);
        BOOST_CHECK(std::holds_alternative<storage2::NOT_EXISTS_TYPE>(bypassResult));

        // Untagged read: walks the full chain → reaches the backend.
        auto normalResult = co_await view.readOneRaw(key);
        BOOST_CHECK(std::holds_alternative<storage::Entry>(normalResult));
        BOOST_CHECK_EQUAL(
            std::get<storage::Entry>(normalResult).get(), "value_from_previous_block"sv);

        // Verify the batch path as well (storeOldValues uses readSomeRaw)
        auto bypassBatch = co_await view.readSomeRaw(
            std::vector{key}, storage2::BYPASS_READ_SET, storage2::BYPASS_MULTILAYER);
        BOOST_CHECK_EQUAL(bypassBatch.size(), 1u);
        BOOST_CHECK(std::holds_alternative<storage2::NOT_EXISTS_TYPE>(bypassBatch[0]));

        auto normalBatch = co_await view.readSomeRaw(std::vector{key});
        BOOST_CHECK_EQUAL(normalBatch.size(), 1u);
        BOOST_CHECK(std::holds_alternative<storage::Entry>(normalBatch[0]));
        BOOST_CHECK_EQUAL(
            std::get<storage::Entry>(normalBatch[0]).get(), "value_from_previous_block"sv);

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()