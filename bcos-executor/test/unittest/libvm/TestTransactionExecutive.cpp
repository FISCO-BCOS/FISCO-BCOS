#include "../../src/executive/TransactionExecutive.h"
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
class TransactionExecutiveFixture
{
public:
    TransactionExecutiveFixture() {}
};

BOOST_FIXTURE_TEST_SUITE(testTransactionExecutive, TransactionExecutiveFixture)

BOOST_AUTO_TEST_CASE(test)
{
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test