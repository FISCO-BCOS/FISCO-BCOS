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
    /*bytes s;
    BOOST_CHECK_NO_THROW(tx.encode(s, eth::IncludeSignature::WithSignature));*/

    // RC1_VERSION encode and decode
    g_BCOSConfig.setVersion(RC1_VERSION);
    /// test encode
    bytes encodeBytesRC1;
    BOOST_CHECK_NO_THROW(tx.encode(encodeBytesRC1, eth::IncludeSignature::WithSignature));
/// test decode
#ifdef FISCO_GM
    bytes rlpBytes = fromHex(
        "f901309f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d14485174876e7ff8609"
        "184e729fff8204a294d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce00000000000000"
        "0000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292ff4aaa5797bf"
        "671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7f7395028658d0e01"
        "b86a37b840c7ca78e7ab80ee4be6d3936ba8e899d8fe12c12114502956ebe8c8629d36d88481dec9973574"
        "2ea523c88cf3becba1cc4375bc9e225143fe1e8e43abc8a7c493a0ba3ce8383b7c91528bede9cf890b4b1e"
        "9b99c1d8e56d6f8292c827470a606827a0ed511490a1666791b2bd7fc4f499eb5ff18fb97ba68ff9aee206"
        "8fd63b88e817");
#else
    bytes rlpBytes = fromHex(
        "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
        "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
        "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
        "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
        "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
        "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e");
#endif
    Transaction decodeTxRC2;
    BOOST_CHECK_NO_THROW(decodeTxRC2.decode(ref(rlpBytes)));
    g_BCOSConfig.setVersion(RC2_VERSION);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
