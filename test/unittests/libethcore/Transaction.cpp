/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief Unit tests for the Transaction
 * @file Transaction.cpp
 * @author: chaychen
 * @date 2018
 */

#include <libdevcore/Assertions.h>
#include <libdevcore/CommonJS.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace dev;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(Transaction, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testCreateTxByRLP)
{
    bytes rlpBytes = fromHex(
        "f8ac8401be1a7d80830f4240941dc8def0867ea7e3626e03acee3eb40ee17251c880b84494e78a100000000000"
        "000000000000003ca576d469d7aa0244071d27eb33c5629753593e000000000000000000000000000000000000"
        "00000000000000000000000013881ba0f44a5ce4a1d1d6c2e4385a7985cdf804cb10a7fb892e9c08ff6d62657c"
        "4da01ea01d4c2af5ce505f574a320563ea9ea55003903ca5d22140155b3c2c968df050948203ea");

    RLP rlpObj(rlpBytes);
    bytesConstRef d = rlpObj.data();
    eth::Transaction tx(d, eth::CheckTransaction::Everything);

    BOOST_CHECK(tx.sender() == tx.safeSender());
    BOOST_CHECK_NO_THROW(tx.signature());
    BOOST_CHECK_NO_THROW(tx.checkLowS());
    bytes s;
    BOOST_CHECK_NO_THROW(tx.encode(s, eth::IncludeSignature::WithSignature));
}

BOOST_AUTO_TEST_CASE(testCreateTxByTransactionSkeleton)
{
    bytes rlpBytes = fromHex(
        "f8ac8401be1a7d80830f4240941dc8def0867ea7e3626e03acee3eb40ee17251c880b84494e78a100000000000"
        "000000000000003ca576d469d7aa0244071d27eb33c5629753593e000000000000000000000000000000000000"
        "00000000000000000000000013881ba0f44a5ce4a1d1d6c2e4385a7985cdf804cb10a7fb892e9c08ff6d62657c"
        "4da01ea01d4c2af5ce505f574a320563ea9ea55003903ca5d22140155b3c2c968df050948203ea");

    RLP rlpObj(rlpBytes);
    bytesConstRef d = rlpObj.data();
    eth::Transaction tx(d, eth::CheckTransaction::Everything);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
