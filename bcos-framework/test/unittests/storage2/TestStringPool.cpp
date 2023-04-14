#include <bcos-framework/storage2/StringPool.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;

class TestStringPoolFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestStringPool, TestStringPoolFixture)

BOOST_AUTO_TEST_CASE(addAndQuery)
{
    storage2::string_pool::StringPool pool;
    auto id = pool.add("hello world!");
    auto id2 = pool.add("hello world!");

    BOOST_REQUIRE_EQUAL(id, id2);

    BOOST_REQUIRE_EQUAL(storage2::string_pool::query(id), "hello world!");
    BOOST_REQUIRE_EQUAL(storage2::string_pool::query(id2), "hello world!");

    auto id3 = pool.add("hello world2!");
    BOOST_REQUIRE_NE(id3, id);
    BOOST_REQUIRE_NE(id3, id2);
    BOOST_REQUIRE_EQUAL(storage2::string_pool::query(id3), "hello world2!");
}

BOOST_AUTO_TEST_SUITE_END()