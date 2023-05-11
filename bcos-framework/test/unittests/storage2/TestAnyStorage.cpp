#include "bcos-framework/storage/Entry.h"
#include <bcos-framework/storage2/AnyStorage.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Wait.h>
#include <fmt/format.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2::any_storage;
using namespace bcos::storage2::memory_storage;

struct AnyStorageFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestMemoryStorage, AnyStorageFixture)

BOOST_AUTO_TEST_CASE(testBasicOp)
{
    task::syncWait([]() -> task::Task<void> {
        MemoryStorage<int, std::string, ORDERED> storage;
        AnyStorage<int, std::string,
            bcos::storage2::any_storage::Attribute(
                bcos::storage2::any_storage::Attribute::READABLE |
                bcos::storage2::any_storage::Attribute::WRITEABLE)>
            anyStorage(storage);

        co_await storage2::writeOne(anyStorage, 1, "hello");
        co_await storage2::writeOne(anyStorage, 2, "world");

        auto value = co_await storage2::readOne(anyStorage, 1);
        BOOST_REQUIRE(value);
        BOOST_CHECK_EQUAL(*value, "hello");

        auto value2 = co_await storage2::readOne(anyStorage, 2);
        BOOST_REQUIRE(value2);
        BOOST_CHECK_EQUAL(*value2, "world");
    }());
}

BOOST_AUTO_TEST_SUITE_END()