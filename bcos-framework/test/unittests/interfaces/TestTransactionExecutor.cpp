#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include <boost/container/small_vector.hpp>
#include <boost/test/unit_test.hpp>

using namespace std::string_view_literals;

struct TestTransactionExecutorFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestTransactionExecutor, TestTransactionExecutorFixture)

BOOST_AUTO_TEST_CASE(tableName)
{
    boost::container::small_vector<char, 32> test1;
    BOOST_CHECK_EQUAL(test1.capacity(), 32);

    bcos::transaction_executor::ContractTable tableName;
    BOOST_CHECK_EQUAL(tableName.capacity(), bcos::transaction_executor::CONTRACT_TABLE_LENGTH);

    auto PREFIX = "/apps/"sv;
    tableName.insert(tableName.end(), PREFIX.begin(), PREFIX.end());

    std::array<uint8_t, 20> bytes{};
    boost::algorithm::hex_lower(bytes.begin(), bytes.end(), std::back_inserter(tableName));

    BOOST_CHECK_EQUAL(tableName.capacity(), bcos::transaction_executor::CONTRACT_TABLE_LENGTH);
}

BOOST_AUTO_TEST_SUITE_END()