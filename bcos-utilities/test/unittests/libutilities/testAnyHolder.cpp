#include <bcos-utilities/AnyHolder.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::utilities;

struct TestAnyHolderFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestAnyHolder, TestAnyHolderFixture)

BOOST_AUTO_TEST_CASE(hold)
{
    int i = 100;
    std::string value = "Hello world!";

    AnyHolder<int> refHolder(i);
    BOOST_CHECK_EQUAL(std::addressof(refHolder.get()), std::addressof(i));

    AnyHolder valueHolder(std::move(value));
    BOOST_CHECK_NE(std::addressof(valueHolder.get()), std::addressof(value));
    BOOST_CHECK_EQUAL(valueHolder.get(), "Hello world!");
}

BOOST_AUTO_TEST_SUITE_END()