#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include <boost/container/small_vector.hpp>
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

    stateKey1 == stateKey2;

    BOOST_CHECK_NE(stateKey1, stateKey2);
}

BOOST_AUTO_TEST_SUITE_END()