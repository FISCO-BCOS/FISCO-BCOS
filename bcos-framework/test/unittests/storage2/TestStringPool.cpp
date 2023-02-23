#include <bcos-framework/storage2/StringPool.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::storage2::string_pool;

class TestStringPoolFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestStringPool, TestStringPoolFixture)

BOOST_AUTO_TEST_CASE(addAndQuery)
{
    storage2::string_pool::FixedStringPool pool;
    auto id = makeStringID(pool, "hello world!");
    auto id2 = makeStringID(pool, "hello world!");

    BOOST_REQUIRE_EQUAL(id, id2);

    BOOST_REQUIRE_EQUAL(*id, "hello world!");
    BOOST_REQUIRE_EQUAL(*id2, "hello world!");

    auto id3 = makeStringID(pool, "hello world2!");
    BOOST_REQUIRE_NE(id3, id);
    BOOST_REQUIRE_NE(id3, id2);
    BOOST_REQUIRE_EQUAL(*id3, "hello world2!");
}

BOOST_AUTO_TEST_CASE(compare)
{
    storage2::string_pool::FixedStringPool pool;
    auto anyID = makeStringID(pool, "");

    StringID emptyID;
    BOOST_CHECK_LT(emptyID, anyID);
    BOOST_CHECK_GT(anyID, emptyID);

    StringID emptyID2;
    BOOST_CHECK_EQUAL(emptyID, emptyID2);

    BOOST_CHECK_THROW(*emptyID, EmptyStringIDError);
}

BOOST_AUTO_TEST_SUITE_END()