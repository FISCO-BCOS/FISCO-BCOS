#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include <boost/test/unit_test.hpp>

using namespace bcos::transaction_executor;
using namespace std::string_view_literals;

struct TestTransactionExecutorFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestTransactionExecutor, TestTransactionExecutorFixture)

BOOST_AUTO_TEST_CASE(stateKey)
{
    StateKey stateKey1("test state table1"sv, "test state key1"sv);
    StateKey stateKey2("test state table2"sv, "test state key2"sv);

    BOOST_CHECK_NE(stateKey1, stateKey2);

    StateKeyView view1("table", "key");

    std::stringstream ss;
    ss << view1;
    BOOST_CHECK_EQUAL(ss.str(), "table:key");
}

BOOST_AUTO_TEST_CASE(single_view)
{
    int i = 100;
    auto& ref = i;

    auto view = RANGES::views::single(ref);
    auto&& j = view[0];
    BOOST_CHECK_EQUAL(view[0], 100);
}

BOOST_AUTO_TEST_SUITE_END()