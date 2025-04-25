#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/Storage.h"
#include "transaction-executor/StateKey.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-task/Wait.h>
#include <fmt/format.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <functional>
#include <ostream>
#include <range/v3/view/transform.hpp>
#include <string>
#include <variant>

using namespace bcos;
using namespace bcos::storage2::memory_storage;

template <>
struct std::hash<std::tuple<std::string, std::string>>
{
    auto operator()(std::tuple<std::string, std::string> const& str) const noexcept
    {
        auto hash = std::hash<std::string>{}(std::get<0>(str));
        return hash;
    }
};


struct MemoryStorageFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestMemoryStorage, MemoryStorageFixture)

namespace other1::other2
{
template <class T>
struct MyStorage
{
    T value{};
    using Value = T;

    friend task::Task<std::vector<std::optional<int>>> tag_invoke(
        storage2::tag_t<storage2::readSome> /*unused*/, MyStorage& storage, auto&& keys)
    {
        std::vector<std::optional<int>> result;
        result.emplace_back(200);
        co_return result;
    }

    friend auto tag_invoke(storage2::tag_t<bcos::storage2::readOne> /*unused*/, MyStorage& storage,
        auto&& key) -> task::Task<std::optional<int>>
    {
        co_return std::make_optional(100);
    }
};

BOOST_AUTO_TEST_CASE(passbyDefault)
{
    task::syncWait([]() -> task::Task<void> {
        MyStorage<int> myStorage;

        auto num = co_await storage2::readOne(myStorage, 1);

        BOOST_CHECK_EQUAL(*num, 100);
    }());
}
}  // namespace other1::other2

BOOST_AUTO_TEST_CASE(writeReadModifyRemove)
{
    task::syncWait([]() -> task::Task<void> {
        constexpr static int count = 100;
        MemoryStorage<std::tuple<std::string, std::string>, storage::Entry, ORDERED> storage;
        co_await storage2::writeSome(
            storage, ::ranges::views::iota(0, count) | ::ranges::views::transform([](auto num) {
                auto key = std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(num));
                storage::Entry entry;
                entry.set("Hello world!" + boost::lexical_cast<std::string>(num));
                return std::make_tuple(key, entry);
            }));

        auto values = co_await storage2::readSome(
            storage, ::ranges::views::iota(0, count) | ::ranges::views::transform([](int i) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(i));
            }));

        for (auto&& [i, value] : ::ranges::views::enumerate(values))
        {
            BOOST_REQUIRE(value);
            BOOST_CHECK_EQUAL(value->get(), "Hello world!" + boost::lexical_cast<std::string>(i));
        }

        // modify
        storage::Entry newEntry;
        newEntry.set("Hello map!");
        co_await storage2::writeOne(
            storage, std::tuple<std::string, std::string>("table", "key:5"), std::move(newEntry));

        auto result = co_await storage2::readOne(
            storage, std::tuple<std::string, std::string>("table", "key:5"));
        BOOST_REQUIRE(result);
        BOOST_CHECK_EQUAL(result->get(), "Hello map!");

        BOOST_CHECK_NO_THROW(co_await storage2::removeSome(
            storage, ::ranges::views::iota(10, 20) | ::ranges::views::transform([](int i) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(i));
            })));

        // Check if records had erased
        auto values2 = co_await storage2::readSome(
            storage, ::ranges::views::iota(0, count) | ::ranges::views::transform([](int i) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(i));
            }));

        for (auto&& [i, value] : ::ranges::views::enumerate(values2))
        {
            if (i >= 10 && i < 20)
            {
                BOOST_REQUIRE(!value);
            }
            else
            {
                BOOST_REQUIRE(value);
                if (i == 5)
                {
                    BOOST_CHECK_EQUAL(value->get(), "Hello map!");
                }
                else
                {
                    BOOST_CHECK_EQUAL(
                        value->get(), "Hello world!" + boost::lexical_cast<std::string>(i));
                }
            }
        }

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(lru)
{
    task::syncWait([]() -> task::Task<void> {
        MemoryStorage<int, storage::Entry, Attribute(ORDERED | LRU)> storage(1);
        storage.setMaxCapacity(1040);

        // write 10 100byte value
        storage::Entry entry;
        entry.set(std::string(100, 'a'));
        co_await storage2::writeSome(storage,
            ::ranges::views::zip(::ranges::views::iota(0, 10), ::ranges::repeat_view(entry)));

        // ensure 10 are useable
        auto values = co_await storage2::readSome(storage, ::ranges::views::iota(0, 10));
        for (auto&& [i, value] : ::ranges::views::enumerate(values))
        {
            BOOST_REQUIRE(value);
        }

        // ensure 0 is erased
        co_await storage2::writeOne(storage, 10, entry);
        auto notExists = co_await storage2::readOne(storage, 0);
        BOOST_REQUIRE(!notExists);

        // ensure another still exists
        auto values2 = co_await storage2::readSome(storage, ::ranges::views::iota(1, 11));
        for (auto&& value : values2)
        {
            BOOST_REQUIRE(value);
        }

        // ensure all
        auto range = co_await storage2::range(storage);
        size_t count = 0;
        while (co_await range.next())
        {
            ++count;
        }
        BOOST_CHECK_EQUAL(count, 10);
    }());
}

BOOST_AUTO_TEST_CASE(logicalDeletion)
{
    task::syncWait([]() -> task::Task<void> {
        MemoryStorage<int, storage::Entry, Attribute(ORDERED | LOGICAL_DELETION)> storage;

        // Write 100 items
        co_await storage2::writeSome(
            storage, ::ranges::views::iota(0, 100) | ::ranges::views::transform([](int num) {
                storage::Entry entry;
                entry.set(fmt::format("Item: {}", num));
                return std::make_tuple(num, entry);
            }));

        // Delete half of items
        co_await storage2::removeSome(
            storage, ::ranges::views::iota(0, 50) |
                         ::ranges::views::transform([](int num) { return num * 2; }));

        // Query and check if deleted items
        int i = 0;
        auto values = co_await storage2::range(storage);
        while (auto item = co_await values.next())
        {
            auto [key, value] = *item;
            if (i % 2 == 0)
            {
                BOOST_CHECK(std::holds_alternative<storage2::DELETED_TYPE>(value));
            }
            else
            {
                BOOST_CHECK_EQUAL(
                    std::get<storage::Entry>(value).get(), fmt::format("Item: {}", i));
            }
            ++i;
        }

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(range)
{
    task::syncWait([]() -> task::Task<void> {
        constexpr static int count = 100;

        MemoryStorage<std::tuple<std::string, std::string>, storage::Entry, ORDERED> storage;
        co_await storage2::writeSome(
            storage, ::ranges::views::iota(0, count) | ::ranges::views::transform([](auto num) {
                auto key = std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(num));
                storage::Entry entry;
                entry.set("Hello world!" + boost::lexical_cast<std::string>(num));
                return std::make_tuple(key, entry);
            }));

        auto readRange = co_await storage2::readSome(
            storage, ::ranges::views::iota(0, 10) | ::ranges::views::transform([](int i) {
                return std::tuple<std::string, std::string>(
                    "table", "key:" + boost::lexical_cast<std::string>(i));
            }));
        for (auto&& [value, num] : ::ranges::views::zip(readRange, ::ranges::iota_view<size_t>(0)))
        {
            BOOST_CHECK(value);
            BOOST_CHECK_EQUAL(value->get(), "Hello world!" + boost::lexical_cast<std::string>(num));
            BOOST_CHECK_LT(num, 10);
        }

        auto seekRange = co_await storage2::range(storage);
        size_t num = 0;
        while (auto kv = co_await seekRange.next())
        {
            BOOST_CHECK(kv);
            auto& [key, value] = *kv;
            const auto& [tableName, keyName] = key;
            BOOST_CHECK_LT(num, 100);
            ++num;
        }
        BOOST_CHECK_EQUAL(num, 100);

        MemoryStorage<int, int,
            bcos::storage2::memory_storage::Attribute(
                bcos::storage2::memory_storage::LOGICAL_DELETION |
                bcos::storage2::memory_storage::ORDERED)>
            intStorage;
        co_await storage2::writeSome(intStorage,
            ::ranges::views::zip(::ranges::views::iota(0, 10), ::ranges::repeat_view<int>(100)));
        auto start = 4;
        auto range3 = co_await storage2::range(intStorage, storage2::RANGE_SEEK, start);
        while (auto pair = co_await range3.next())
        {
            auto&& [key, value] = *pair;
            BOOST_CHECK_EQUAL(key, start++);
        }
        BOOST_CHECK_EQUAL(start, 10);
    }());
}

BOOST_AUTO_TEST_CASE(merge)
{
    task::syncWait([]() -> task::Task<void> {
        MemoryStorage<int, int, ORDERED> storage1;
        MemoryStorage<int, int, ORDERED> storage2;

        co_await storage2::writeSome(storage1,
            ::ranges::views::zip(::ranges::views::iota(0, 10), ::ranges::repeat_view<int>(100)));
        co_await storage2::writeSome(storage2,
            ::ranges::views::zip(::ranges::views::iota(9, 19), ::ranges::repeat_view<int>(200)));

        co_await storage2::merge(storage1, storage2);
        auto values = co_await storage2::range(storage1);

        int i = 0;
        while (auto tuple = co_await values.next())
        {
            auto [key, value] = *tuple;
            auto keyNum = key;
            auto valueNum = std::get<int>(value);

            BOOST_CHECK_EQUAL(keyNum, i);
            if (keyNum > 8)
            {
                BOOST_CHECK_EQUAL(valueNum, 200);
            }
            else
            {
                BOOST_CHECK_EQUAL(valueNum, 100);
            }
            ++i;
        }
        BOOST_CHECK_EQUAL(i, 19);

        MemoryStorage<int, int, ORDERED> storage3;
        MemoryStorage<int, int, ORDERED> storage4;
        MemoryStorage<int, int, ORDERED | CONCURRENT> storage5;

        co_await storage2::writeSome(storage3,
            ::ranges::views::zip(::ranges::views::iota(0, 10), ::ranges::repeat_view<int>(100)));
        co_await storage2::writeSome(storage4,
            ::ranges::views::zip(::ranges::views::iota(10, 20), ::ranges::repeat_view<int>(100)));
        co_await storage2::merge(storage5, storage3, storage4);

        auto values2 = co_await storage2::readSome(storage5, ::ranges::views::iota(0, 20));
        BOOST_CHECK_EQUAL(values2.size(), 20);
        auto expectValues2 = ::ranges::views::iota(0, 20) | ::ranges::views::transform([](int i) {
            return std::make_optional(100);
        }) | ::ranges::to<std::vector>();
        BOOST_CHECK(values2 == expectValues2);
    }());
}

BOOST_AUTO_TEST_CASE(directDelete)
{
    task::syncWait([]() -> task::Task<void> {
        MemoryStorage<int, int, bcos::storage2::memory_storage::LOGICAL_DELETION, std::hash<int>>
            storage;
        co_await storage2::writeSome(storage,
            ::ranges::views::zip(::ranges::views::iota(0, 10), ::ranges::repeat_view<int>(100)));

        auto range1 = co_await storage2::range(storage);
        int count1 = 0;
        while (co_await range1.next())
        {
            ++count1;
        }
        BOOST_CHECK_EQUAL(count1, 10);

        co_await storage2::removeOne(storage, 6, bcos::storage2::DIRECT);
        auto range2 = co_await storage2::range(storage);
        int count2 = 0;
        while (co_await range2.next())
        {
            ++count2;
        }
        BOOST_CHECK_EQUAL(count2, 9);
    }());
}

BOOST_AUTO_TEST_CASE(keyComp)
{
    executor_v1::StateKey key1("/tables/t_testV320", "type");
    executor_v1::StateKeyView key2("/tables/t_testV320", "type");

    BOOST_CHECK_EQUAL(key1, key2);
    BOOST_CHECK(!(key1 > key2));
    BOOST_CHECK(!(key1 < key2));

    BOOST_CHECK_EQUAL(key2, key1);
    BOOST_CHECK(!(key2 > key1));
    BOOST_CHECK(!(key2 < key1));

    auto hash1 = std::hash<decltype(key1)>{}(key1);
    auto hash2 = std::hash<decltype(key2)>{}(key2);
    BOOST_CHECK_EQUAL(hash1, hash2);

    constexpr static auto keys = std::to_array<std::string_view>(
        {"s_tables:/apps/130cfa5e32bcb8bfcffc146bbdcb8967ecc3e556_accessAuth",
            "/apps/130cfa5e32bcb8bfcffc146bbdcb8967ecc3e556_accessAuth:admin",
            "/apps/130cfa5e32bcb8bfcffc146bbdcb8967ecc3e556_accessAuth:status",
            "/apps/130cfa5e32bcb8bfcffc146bbdcb8967ecc3e556_accessAuth:method_auth_type",
            "/apps/130cfa5e32bcb8bfcffc146bbdcb8967ecc3e556_accessAuth:method_auth_white",
            "/apps/130cfa5e32bcb8bfcffc146bbdcb8967ecc3e556_accessAuth:method_auth_black",
            "s_tables:/apps/130cfa5e32bcb8bfcffc146bbdcb8967ecc3e556", "/tables:t_testV320",
            "s_tables:/tables/t_testV320",
            "s_tables:/apps/f051d3dc3139daa9eeb86a4e388b73cb024d5f5f",
            "s_code_binary:\216+wW_4\312\301\233\314G\024\367\271K!\223\023\364\245\004Ioi\216="
            "\027\355\205Ł",
            "/apps/f051d3dc3139daa9eeb86a4e388b73cb024d5f5f:codeHash",
            "s_contract_abi:\305\322F\001\206\367#<\222~}\262\334\307\003\300\345\000\266Sʂ';{"
            "\372\330\004]\205\244p",
            "/tables/t_testV320:type", "/tables/t_testV320:link_address",
            "s_tables:/apps/f051d3dc3139daa9eeb86a4e388b73cb024d5f5f_accessAuth",
            "/apps/f051d3dc3139daa9eeb86a4e388b73cb024d5f5f_accessAuth:admin",
            "/apps/f051d3dc3139daa9eeb86a4e388b73cb024d5f5f_accessAuth:status",
            "/apps/f051d3dc3139daa9eeb86a4e388b73cb024d5f5f_accessAuth:method_auth_type",
            "/apps/f051d3dc3139daa9eeb86a4e388b73cb024d5f5f_accessAuth:method_auth_white",
            "/apps/f051d3dc3139daa9eeb86a4e388b73cb024d5f5f_accessAuth:method_auth_black",
            "s_tables:u_/tables/t_testV320"});

    bcos::task::syncWait([&]() -> task::Task<void> {
        bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
            bcos::executor_v1::StateValue,
            bcos::storage2::memory_storage::ORDERED |
                bcos::storage2::memory_storage::LOGICAL_DELETION>
            storage;
        for (const auto& key : keys)
        {
            bcos::executor_v1::StateValue value;
            value.set("link");
            co_await storage2::writeOne(
                storage, bcos::executor_v1::StateKey(std::string{key}), std::move(value));
        }

        BOOST_CHECK(co_await storage2::existsOne(
            storage, bcos::executor_v1::StateKeyView("/tables/t_testV320", "type")));
        BOOST_CHECK(co_await storage2::existsOne(
            storage, bcos::executor_v1::StateKeyView("/tables/t_testV320", "link_address")));
        BOOST_CHECK(co_await storage2::existsOne(storage,
            bcos::executor_v1::StateKeyView(
                "/apps/f051d3dc3139daa9eeb86a4e388b73cb024d5f5f_accessAuth", "method_auth_white")));
        BOOST_CHECK(co_await storage2::existsOne(
            storage, bcos::executor_v1::StateKeyView(
                         "/apps/130cfa5e32bcb8bfcffc146bbdcb8967ecc3e556_accessAuth", "status")));
    }());
}

BOOST_AUTO_TEST_CASE(HeterogeneousLookup)
{
    using namespace std::string_literals;
    using namespace std::string_view_literals;

    task::syncWait([]() -> task::Task<void> {
        struct StringHasher
        {
            size_t operator()(const std::string& str) const noexcept
            {
                return std::hash<std::string>{}(str);
            }
            size_t operator()(std::string_view str) const noexcept
            {
                return std::hash<std::string_view>{}(str);
            }
        };

        MemoryStorage<std::string, int, ORDERED> storage1;
        co_await storage2::writeOne(storage1, "key1"s, 1);
        auto value1 = co_await storage2::readOne(storage1, "key1"sv);
        BOOST_REQUIRE(value1);
        BOOST_CHECK_EQUAL(*value1, 1);

        MemoryStorage<std::string, int, UNORDERED, StringHasher> storage2;
        co_await storage2::writeOne(storage2, "key1"s, 2);
        auto value2 = co_await storage2::readOne(storage2, "key1"sv);
        BOOST_REQUIRE(value2);
        BOOST_CHECK_EQUAL(*value2, 2);
    }());
}

BOOST_AUTO_TEST_CASE(concurrentRange)
{
    task::syncWait([]() -> task::Task<void> {
        constexpr static int count = 100;

        MemoryStorage<std::string, storage::Entry,
            Attribute(ORDERED | bcos::storage2::memory_storage::CONCURRENT), std::hash<std::string>>
            storage;

        co_await storage2::writeSome(
            storage, ::ranges::views::iota(0, count) | ::ranges::views::transform([](auto num) {
                auto key = boost::lexical_cast<std::string>(num);
                storage::Entry entry;
                entry.set("Hello world!" + boost::lexical_cast<std::string>(num));
                return std::make_tuple(key, entry);
            }));

        auto range = co_await storage2::range(storage);
        auto expect = count;
        while (auto value = co_await range.next())
        {
            --expect;
            auto&& [key, entry] = *value;
            auto index = boost::lexical_cast<int>(key);
            BOOST_CHECK_LT(index, count);
        }
        BOOST_CHECK_EQUAL(expect, 0);
    }());
}

BOOST_AUTO_TEST_CASE(dirtctReadOne)
{
    task::syncWait([]() -> task::Task<void> {
        MemoryStorage<int, int,
            bcos::storage2::memory_storage::ORDERED |
                bcos::storage2::memory_storage::LOGICAL_DELETION>
            storage;
        co_await storage2::writeSome(storage,
            ::ranges::views::zip(::ranges::views::iota(0, 10), ::ranges::repeat_view<int>(100)));
        co_await storage2::removeOne(storage, 6);

        auto value = co_await storage2::readOne(storage, 6);
        BOOST_CHECK(!value);
        auto value2 = storage.readOne(6);
        BOOST_CHECK(std::holds_alternative<bcos::storage2::DELETED_TYPE>(value2));

        auto value3 = storage.readOne(11);
        BOOST_CHECK(std::holds_alternative<bcos::storage2::NOT_EXISTS_TYPE>(value3));
        auto value4 = co_await storage2::readOne(storage, 11);
        BOOST_CHECK(!value4);

        auto value5 = co_await storage2::readOne(storage, 3);
        BOOST_CHECK(value5);
        auto value6 = storage.readOne(3);
        BOOST_CHECK(std::holds_alternative<int>(value6));

        MemoryStorage<int, int, bcos::storage2::memory_storage::ORDERED> storage_2;
        co_await storage2::writeSome(storage_2,
            ::ranges::views::zip(::ranges::views::iota(0, 10), ::ranges::repeat_view<int>(100)));
        co_await storage2::removeOne(storage_2, 6);

        auto value21 = co_await storage2::readOne(storage_2, 6);
        BOOST_CHECK(!value21);

        auto value22 = storage_2.readOne(6);
        BOOST_CHECK(std::holds_alternative<bcos::storage2::NOT_EXISTS_TYPE>(value22));

        auto value23 = storage_2.readOne(11);
        BOOST_CHECK(std::holds_alternative<bcos::storage2::NOT_EXISTS_TYPE>(value23));
        auto value24 = co_await storage2::readOne(storage_2, 11);
        BOOST_CHECK(!value24);
    }());
}

BOOST_AUTO_TEST_SUITE_END()