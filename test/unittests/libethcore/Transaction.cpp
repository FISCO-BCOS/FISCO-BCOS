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
#include <iostream>

#include <libconfig/GlobalConfigure.h>
#include <libdevcore/Assertions.h>
#include <libdevcore/CommonJS.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace dev;
using namespace dev::eth;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(TransactionTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testCreateTxByRLP)
{
    u256 value = u256(100);
    u256 gas = u256(100000000);
    u256 gasPrice = u256(0);
    Address dst = toAddress(KeyPair::create().pub());
    std::string str = "test transaction";
    bytes data(str.begin(), str.end());
    Transaction tx(value, gasPrice, gas, dst, data);
    KeyPair sigKeyPair = KeyPair::create();
    SignatureStruct sig = dev::sign(sigKeyPair.secret(), tx.sha3(WithoutSignature));
    /// update the signature of transaction
    tx.updateSignature(sig);
    /// test encode
    bytes encodeBytes;
    tx.encode(encodeBytes, eth::IncludeSignature::WithSignature);
    /// test decode
    Transaction decodeTx;
    decodeTx.decode(ref(encodeBytes));
    BOOST_CHECK(tx == decodeTx);
    BOOST_CHECK(decodeTx.sender() == tx.safeSender());
    BOOST_CHECK_NO_THROW(decodeTx.signature());
    BOOST_CHECK_NO_THROW(decodeTx.checkLowS());
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
