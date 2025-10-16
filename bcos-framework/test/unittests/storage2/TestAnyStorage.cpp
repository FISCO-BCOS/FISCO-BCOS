#include "bcos-framework/storage2/AnyStorage.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::storage2::memory_storage;

BOOST_AUTO_TEST_SUITE(TestAnyStorage)

BOOST_AUTO_TEST_CASE(basicReadWrite)
{
    task::syncWait([]() -> task::Task<void> {
        MemoryStorage<int, std::string, ORDERED> underlying;
        AnyStorage<int, std::string> any(underlying);

        co_await writeOne(any, 1, std::string{"hello"});
        co_await writeOne(any, 2, std::string{"world"});

        auto v1 = co_await readOne(any, 1);
        auto v2 = co_await readOne(any, 2);
        BOOST_REQUIRE(v1);
        BOOST_REQUIRE(v2);
        BOOST_CHECK_EQUAL(*v1, "hello");
        BOOST_CHECK_EQUAL(*v2, "world");

        auto vec = co_await readSome(any, std::vector<int>{1, 2, 3});
        BOOST_CHECK(vec[0]);
        BOOST_CHECK(vec[1]);
        BOOST_CHECK(!vec[2]);

        auto it = co_await range(any);
        int count = 0;
        while (auto item = co_await it.next())
        {
            ++count;
        }
        BOOST_CHECK_EQUAL(count, 2);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(writeSome_readSome_removeSome)
{
    task::syncWait([]() -> task::Task<void> {
        MemoryStorage<int, std::string, ORDERED> underlying;
        AnyStorage<int, std::string> any(underlying);

        // writeSome: (0..4) => "v<i>"
        constexpr int kCount = 5;
        std::vector<std::pair<int, std::string>> kvs;
        kvs.reserve(kCount);
        for (int i = 0; i < kCount; ++i)
        {
            kvs.emplace_back(i, std::string("v") + std::to_string(i));
        }
        co_await writeSome(any, kvs);

        // readSome with hits + misses
        auto values = co_await readSome(any, std::vector<int>{0, 1, 2, kCount});
        BOOST_REQUIRE(values.size() == 4);
        BOOST_CHECK(values[0] && *values[0] == "v0");
        BOOST_CHECK(values[1] && *values[1] == "v1");
        BOOST_CHECK(values[2] && *values[2] == "v2");
        BOOST_CHECK(!values[3]);

        // removeSome subset
        co_await removeSome(any, std::vector<int>{1, 3});
        auto values2 = co_await readSome(any, std::vector<int>{0, 1, 2, 3, 4});
        BOOST_REQUIRE(values2.size() == 5);
        BOOST_CHECK(values2[0] && *values2[0] == "v0");
        BOOST_CHECK(!values2[1]);
        BOOST_CHECK(values2[2] && *values2[2] == "v2");
        BOOST_CHECK(!values2[3]);
        BOOST_CHECK(values2[4] && *values2[4] == "v4");

        // range sanity (count present after remove)
        auto it = co_await range(any);
        int count = 0;
        while (auto item = co_await it.next())
        {
            auto& kv = *item;
            auto& storageValue = std::get<1>(kv);
            // Items may be logically deleted; count present values
            if (std::holds_alternative<storage2::NOT_EXISTS_TYPE>(storageValue) ||
                std::holds_alternative<storage2::DELETED_TYPE>(storageValue))
            {
                continue;
            }
            ++count;
        }
        BOOST_CHECK_EQUAL(count, 3); // keys 0,2,4 remain

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(merge_cross_types)
{
    task::syncWait([]() -> task::Task<void> {
        // Prepare two different memory storages with same Key/Value but different attributes
        MemoryStorage<int, std::string, UNORDERED> fromUnordered; // source unordered
        MemoryStorage<int, std::string, ORDERED> toOrdered;        // dest ordered

        // Wrap both with AnyStorage
        AnyStorage<int, std::string> anyFrom(fromUnordered);
        AnyStorage<int, std::string> anyTo(toOrdered);

        // from: 0..4 -> "a<i>" ; to: 3..7 -> "b<i>"
        std::vector<std::pair<int, std::string>> fromPairs;
        std::vector<std::pair<int, std::string>> toPairs;
        constexpr int kFromL = 0;
        constexpr int kFromR = 5;
        constexpr int kToL = 3;
        constexpr int kToR = 8;
        fromPairs.reserve(kFromR - kFromL);
        toPairs.reserve(kToR - kToL);
        for (int i = kFromL; i < kFromR; ++i)
        {
            fromPairs.emplace_back(i, std::string("a") + std::to_string(i));
        }
        for (int i = kToL; i < kToR; ++i)
        {
            toPairs.emplace_back(i, std::string("b") + std::to_string(i));
        }
        co_await writeSome(anyFrom, fromPairs);
        co_await writeSome(anyTo, toPairs);

        // Merge to <- from (source should overwrite overlaps 3,4)
        co_await merge(anyTo, anyFrom);

        constexpr int kCheckMax = 8;
        std::vector<int> checkKeys;
        checkKeys.reserve(kCheckMax);
        for (int i = 0; i < kCheckMax; ++i)
        {
            checkKeys.push_back(i);
        }
        auto check = co_await readSome(anyTo, checkKeys);
        // 0,1,2 newly inserted from source
        BOOST_CHECK(check[0] && *check[0] == "a0");
        BOOST_CHECK(check[1] && *check[1] == "a1");
        BOOST_CHECK(check[2] && *check[2] == "a2");
        // 3,4 overwritten by source
        BOOST_CHECK(check[3] && *check[3] == "a3");
        BOOST_CHECK(check[4] && *check[4] == "a4");
        // 5,6,7 remain from dest
        BOOST_CHECK(check[5] && *check[5] == "b5");
        BOOST_CHECK(check[6] && *check[6] == "b6");
        BOOST_CHECK(check[7] && *check[7] == "b7");

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(merge_with_logical_deletion)
{
    task::syncWait([]() -> task::Task<void> {
        // Source enables logical deletion so its range() emits DELETED_TYPE; destination plain ordered
        MemoryStorage<int, std::string, ORDERED> toPlain; // dest
        MemoryStorage<int, std::string, Attribute(LOGICAL_DELETION)> fromWithLD; // source

        AnyStorage<int, std::string> anyTo(toPlain);
        AnyStorage<int, std::string> anyFrom(fromWithLD);

        // Seed data
        co_await writeSome(anyTo, std::vector<std::pair<int, std::string>>{{1, "x1"}, {2, "x2"}, {3, "x3"}});
        co_await writeSome(anyFrom, std::vector<std::pair<int, std::string>>{{2, "y2"}});

    // Source expresses a deletion for key=1 via removeOne (kept as logical deletion in source)
        co_await removeOne(anyFrom, 1);

        // Merge: expect key=1 deleted in dest, key=2 overwritten to y2, key=3 untouched
        co_await merge(anyTo, anyFrom);

    auto res = co_await readSome(anyTo, std::vector<int>{1,2,3});
        BOOST_CHECK(!res[0]); // deleted
        BOOST_CHECK(res[1] && *res[1] == "y2");
        BOOST_CHECK(res[2] && *res[2] == "x3");

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
